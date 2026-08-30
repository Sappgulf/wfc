#!/usr/bin/env python3
"""wfc_thermo.py - wave function collapse as an energy-based model.

Reads a JSON spec on stdin (written by wfc.c --solver thermo):
    w, h, ntiles, seed, torus
    unary[ntiles]          per-tile log-weight (unary energy)
    cdir[4][ntiles]        per-direction bitmask of compatible b tiles
    domains[n]             per-cell uint64 allowed-tile masks (optional)
    steps, chains, beta0, beta_max
    form                   "potts" (default) | "ising" (domain-wall p-bits)

WFC is a pairwise Potts/EBM: cells are categorical nodes, cdir masks are
pairwise compatibility energies, tile weights are unary biases. THRML
samples it with checkerboard block Gibbs under a beta sweep; many chains
race, the first fully-valid configuration wins.

form = "ising" compiles the same objective into thermometer spins (the
p-bit representation a Z1-class Thermodynamic Sampling Unit would run),
reports the spin budget, and solves in the categorical representation.

Stdout (JSON lines):
  {"t":"meta",  "pbits": n}
  {"t":"done",  "valid": 1, "cfg":[...], "form":"potts"}
  {"t":"fatal", "why":"..."}   (wfc.c falls back to the classic solver)

Anything unrecognized on stdout is ignored by the C parent, so ALL
diagnostics go to stderr; stdout carries JSON lines only.
"""

import json
import signal
import sys
import time

_T0 = time.monotonic()


def _fatal(why):
    print(json.dumps({"t": "fatal", "why": why}))
    sys.stdout.flush()
    sys.exit(1)


try:
    import numpy as np
    import jax
    import jax.numpy as jnp

    from thrml.block_management import Block
    from thrml.block_sampling import BlockGibbsSpec, SamplingSchedule, sample_states
    from thrml.factor import FactorSamplingProgram
    from thrml.models.discrete_ebm import (
        CategoricalEBMFactor,
        CategoricalGibbsConditional,
    )
    from thrml.pgm import CategoricalNode
except Exception as e:  # pragma: no cover - environment dependent
    _fatal("missing thrml/jax (pip install thrml jax): %s" % e)

# if the C parent dies we must not die with a traceback mid-write
signal.signal(signal.SIGPIPE, signal.SIG_DFL)

INVALID = -1e10


def emit(v):
    sys.stdout.write(json.dumps(v) + "\n")
    sys.stdout.flush()


def load_spec():
    try:
        spec = json.load(sys.stdin)
    except Exception as e:
        _fatal("bad json spec: %s" % e)
    if not isinstance(spec, dict):
        _fatal("spec must be a json object")
    for key in ("w", "h", "ntiles", "cdir", "unary"):
        if key not in spec:
            _fatal("spec missing key: %s" % key)
    try:
        w, h, ntiles = int(spec["w"]), int(spec["h"]), int(spec["ntiles"])
    except (TypeError, ValueError):
        _fatal("w/h/ntiles must be integers")
    if w < 1 or h < 1 or ntiles < 1:
        _fatal("w/h/ntiles must be >= 1")
    spec["w"], spec["h"], spec["ntiles"] = w, h, ntiles
    n = w * h
    raw = spec["cdir"]
    if not isinstance(raw, list) or len(raw) < 4:
        _fatal("cdir must be a list of 4 rows")
    cdir_masks = []
    for d in range(4):
        row = raw[d]
        if isinstance(row, dict):
            row = [row.get(str(i), 0) for i in range(ntiles)]
        elif isinstance(row, list):
            if len(row) != ntiles:
                _fatal("cdir[%d] has wrong length" % d)
        else:
            row = [row] * ntiles
        try:
            cdir_masks.append([int(v) & ((1 << ntiles) - 1) for v in row])
        except (TypeError, ValueError):
            _fatal("cdir[%d] not integers" % d)
    spec["cdir_masks"] = cdir_masks
    unary = spec["unary"]
    if not isinstance(unary, list) or len(unary) != ntiles:
        _fatal("unary must have ntiles entries")
    try:
        unary = np.array([float(v) for v in unary], dtype=np.float64)
    except (TypeError, ValueError):
        _fatal("unary not numeric")
    if not np.all(np.isfinite(unary)):
        _fatal("unary has non-finite values")
    spec["unary"] = unary.tolist()
    dom = None
    if "domains" in spec:
        if len(spec["domains"]) != n:
            print("wfc_thermo: domains has wrong length, ignoring",
                  file=sys.stderr)
        else:
            try:
                dom = [int(v) & ((1 << ntiles) - 1) for v in spec["domains"]]
            except (TypeError, ValueError):
                _fatal("domains not integers")
            for i, m in enumerate(dom):
                if m == 0:
                    _fatal("cell %d has an empty domain (contradicted)" % i)
    spec["domain"] = dom
    spec["torus"] = bool(spec.get("torus", False))
    spec["steps"] = max(1, int(spec.get("steps", 180) or 1))
    spec["chains"] = max(1, int(spec.get("chains", 48) or 1))
    return spec


def build_edges(W_, H_, torus):
    """Each undirected lattice edge once: (src, dst, dir) with dir C-style
    (0=north, 1=east). Torus wraps the borders. Degenerate dims (H_==1 or
    W_==1 with torus) produce no self-loops: a factor needs distinct nodes."""
    edges = []
    for y in range(H_):
        for x in range(W_):
            i = y * W_ + x
            if torus and H_ > 1:
                edges.append((i, ((y - 1) % H_) * W_ + x, 0))
            elif y > 0:
                edges.append((i, (y - 1) * W_ + x, 0))
            if torus and W_ > 1 and x == W_ - 1:
                edges.append((i, y * W_, 1))
            elif x < W_ - 1:
                edges.append((i, y * W_ + x + 1, 1))
    return edges


def edge_weights(edges, cdir_masks, ntiles):
    """(E, K, K): +1 for compatible (src, dst) pairs, -5 for violations."""
    Wm = np.full((len(edges), ntiles, ntiles), -5.0, dtype=np.float32)
    for e, (s, t, d) in enumerate(edges):
        for ca in range(ntiles):
            m = cdir_masks[d][ca]
            for cb in range(ntiles):
                if (m >> cb) & 1:
                    Wm[e, ca, cb] = 1.0
    return Wm


def cell_unary(unary, domain, ntiles, n, pref, scale=2.0):
    """Unary energy from a per-cell style preference (like a classic WFC
    rollout: draw one tile per cell from the weight distribution, then let
    the pairwise energy fix the inconsistencies). Preference breaks ties;
    pairwise compat rules violations. `pref[i]` is the preferred tile."""
    U = np.zeros((n, ntiles), dtype=np.float64)
    if domain is not None:
        dom = np.array(domain, dtype=np.uint64)
        alive = ((dom[:, None] >> np.arange(ntiles, dtype=np.uint64)[None, :]) & 1).astype(bool)
        U[~alive] = INVALID
    U[np.arange(n), np.asarray(pref, dtype=np.int64)] = 1.0
    return U * scale


def draw_preferences(weights, domain, ntiles, n, rng, W_=0, H_=0, smooth=False):
    """Per-cell tile preference. In smooth/field modes (terrain, fire,
    waves, ...) the draws are spatially correlated: a coarse noise field
    is bilinearly upsampled and the nearest band wins, so preference
    *regions* emerge — the EBM analogue of the classic solver's blobby
    texture. Connector modes use independent weighted draws."""
    w = np.array(weights, dtype=np.float64)
    w = np.maximum(w, 1e-9)
    if smooth and W_ > 0 and H_ > 0:
        gw, gh = max(1, W_ // 5), max(1, H_ // 5)
        noise = rng.random((gh + 1, gw + 1))
        fy = np.arange(H_) * gh / H_
        fx = np.arange(W_) * gw / W_
        y0, x0 = fy.astype(np.int64), fx.astype(np.int64)
        y1, x1 = np.minimum(y0 + 1, gh), np.minimum(x0 + 1, gw)
        ty, tx = fy - y0, fx - x0
        top = noise[y0][:, x0] * (1 - tx)[None, :] + noise[y0][:, x1] * tx[None, :]
        bot = noise[y1][:, x0] * (1 - tx)[None, :] + noise[y1][:, x1] * tx[None, :]
        v = top * (1 - ty)[:, None] + bot * ty[:, None]
        pref = np.clip(np.rint(v * (ntiles - 1)), 0, ntiles - 1).astype(np.int64).reshape(-1)
        if domain is not None:
            dom = np.array(domain, dtype=np.uint64)
            ok = ((dom >> pref.astype(np.uint64)) & 1).astype(bool)
            for i in np.nonzero(~ok)[0]:  # rare: pull to the nearest open tile
                pref[i] = _nearest_allowed(int(dom[i]), int(pref[i]), ntiles)
        return pref
    if domain is None:
        return rng.choice(ntiles, size=n, p=w / w.sum()).astype(np.int64)
    dom = np.array(domain, dtype=np.uint64)
    bits = np.arange(ntiles, dtype=np.uint64)
    alive = ((dom[:, None] >> bits[None, :]) & 1).astype(bool)
    p = np.where(alive, w[None, :], 0.0)
    p /= p.sum(axis=1, keepdims=True)
    u = rng.random(n)
    return (p.cumsum(axis=1) < u[:, None]).sum(axis=1).astype(np.int64)


def _nearest_allowed(mask, band, ntiles):
    if not mask:
        return band
    best, bd = band, 1 << 30
    for t in range(ntiles):
        if (mask >> t) & 1:
            d = abs(t - band)
            if d < bd:
                bd, best = d, t
    return best


# ---------------- Potts solve (categorical nodes) ----------------

def potts_solve(spec, emit_meta=True):
    W_, H_, ntiles = spec["w"], spec["h"], spec["ntiles"]
    seed = spec["seed"]
    steps = spec["steps"]
    chains = spec["chains"]
    beta0 = float(spec.get("beta0", 0.2))
    beta_max = float(spec.get("beta_max", 8.0 if ntiles <= 16 else 14.0))
    if not (0.0 < beta0 < beta_max):  # geomspace needs a positive ordered sweep
        if beta_max <= 0.0:
            beta_max = 8.0 if ntiles <= 16 else 14.0
        beta0 = max(1e-3, beta_max / 40.0)
    cdir_masks = spec["cdir_masks"]
    domain = spec["domain"]
    unary = np.array(spec["unary"], dtype=np.float64)

    n = W_ * H_
    edges = build_edges(W_, H_, spec["torus"])
    Wm = edge_weights(edges, cdir_masks, ntiles)

    nodes = [CategoricalNode() for _ in range(n)]
    A = [nodes[s] for (s, t, d) in edges]
    B = [nodes[t] for (s, t, d) in edges]
    ev_idx = jnp.array([y * W_ + x for y in range(H_) for x in range(W_) if (x + y) % 2 == 0], dtype=jnp.int32)
    od_idx = jnp.array([y * W_ + x for y in range(H_) for x in range(W_) if (x + y) % 2 == 1], dtype=jnp.int32)
    ev = [nodes[int(i)] for i in ev_idx]
    od = [nodes[int(i)] for i in od_idx]

    def allowed_ids(i):
        m = domain[i] if domain is not None else ((1 << ntiles) - 1)
        return np.array([t for t in range(ntiles) if (m >> t) & 1])

    # one texture draw (classic-WFC-style rollout) shared by all chains;
    # chains diverge via their own random inits.
    Wm_np = np.asarray(Wm)          # hoisted: was deep-copied per candidate
    Esrc_np = np.array([s for (s, t, d) in edges], dtype=np.intp)
    Edst_np = np.array([t for (s, t, d) in edges], dtype=np.intp)

    def bad_of(cfg):
        cfg = np.asarray(cfg, dtype=np.intp)
        w = Wm_np[np.arange(len(edges)), cfg[Esrc_np], cfg[Edst_np]]
        return int((w <= 0).sum())

    pref = draw_preferences(unary, domain, ntiles, n,
                              np.random.default_rng(seed), W_, H_,
                              spec.get("smooth", False))
    U = cell_unary(unary, domain, ntiles, n, pref)
    U_basis = jnp.array(U, dtype=jnp.float32)

    init = np.zeros((chains, n), dtype=np.uint8)
    rng_c = np.random.default_rng(seed + 31)
    for c in range(chains):
        init[c] = [int(rng_c.choice(allowed_ids(i))) for i in range(n)]
    init = jnp.array(init)

    Wm_basis = jnp.array(Wm, dtype=jnp.float32)
    SCHED = SamplingSchedule(4, 1, 1)  # 4 warmup sweeps per beta rung

    def step_batch(state, ti):
        key, beta = ti
        bet = beta.astype(jnp.float32)
        prog = FactorSamplingProgram(
            BlockGibbsSpec([Block(ev), Block(od)], []),
            [CategoricalGibbsConditional(ntiles), CategoricalGibbsConditional(ntiles)],
            [CategoricalEBMFactor([Block(A), Block(B)], Wm_basis * bet),
             CategoricalEBMFactor([Block(nodes)], U_basis * bet)],
            [])
        blocks = [state[:, ev_idx], state[:, od_idx]]
        ckeys = jax.random.split(key, chains)
        new = jax.vmap(
            lambda k, b: sample_states(k, prog, SCHED, b, [], [Block(nodes)])[0][0]
        )(ckeys, blocks)
        return new, new

    betas = jnp.array(np.geomspace(beta0, beta_max, num=steps), dtype=jnp.float32)

    @jax.jit
    def anneal(init, keys):
        _, outs = jax.lax.scan(step_batch, init, (keys, betas))
        return outs  # (steps, chains, n)

    keys0 = jax.random.split(jax.random.key(seed), steps)
    # the domain-wall compile budget for THIS grid (validated per attempt:
    # thermometer spins = sum over cells of (allowed-states - 1))
    if domain is not None:
        pbits = sum(bin(domain[i]).count("1") - 1 for i in range(n))
    else:
        pbits = n * (ntiles - 1)
    if emit_meta:
        emit({"t": "meta", "pbits": int(pbits),
              "note": "domain-wall compile: %d thermometer pbits (Z1-class budget)"
                      % int(pbits)})
    # scan every step, not just the final: valid configurations often
    # appear mid-anneal before a marginal edge freezes incorrectly
    all_steps = np.asarray(anneal(init, keys0))  # (steps, chains, n)

    best = None
    best_bad = 1 << 30
    for si in range(0, all_steps.shape[0], 2):
        for c in range(all_steps.shape[1]):
            cfg = all_steps[si, c]
            bad = bad_of(cfg)
            if bad == 0:
                emit({"t": "done", "valid": 1, "cfg": cfg.astype(int).tolist(), "form": "potts"})
                return cfg
            if bad < best_bad:
                best_bad = bad
                best = cfg

    # hot re-anneals with fresh keys AND fresh textures (the classic
    # solver's contradiction-retry analog: each restart faces a new
    # entropy landscape). The C parent SIGKILLs us at 240s, so stop
    # launching new anneals past half the budget — return the best-so-far.
    for retry in range(4 if ntiles <= 16 else 8):
        if time.monotonic() - _T0 > 120.0:
            break
        seed2 = seed + 1000 * (retry + 1)
        pref = draw_preferences(unary, domain, ntiles, n,
                                np.random.default_rng(seed2 + 555), W_, H_,
                                spec.get("smooth", False))
        U_basis = jnp.array(cell_unary(unary, domain, ntiles, n, pref), dtype=jnp.float32)
        rng_c = np.random.default_rng(seed2 + 31)
        nxt = np.zeros((chains, n), dtype=np.uint8)
        for c in range(chains):
            nxt[c] = [int(rng_c.choice(allowed_ids(i))) for i in range(n)]
        cfgs2 = np.asarray(anneal(jnp.array(nxt), jax.random.split(jax.random.key(seed2), steps)))
        for si in range(0, cfgs2.shape[0], 2):
            for c in range(cfgs2.shape[1]):
                cfg = cfgs2[si, c]
                bad = bad_of(cfg)
                if bad == 0:
                    emit({"t": "done", "valid": 1, "cfg": cfg.astype(int).tolist(), "form": "potts"})
                    return cfg
                if bad < best_bad:
                    best_bad = bad
                    best = cfg

    emit({"t": "done", "valid": 0, "bad": int(best_bad),
          "cfg": best.astype(int).tolist(), "form": "potts"})


# ---------------- domain-wall Ising (Z1 p-bit form) ----------------

def spin_of_cell(cell, ntiles):
    return cell * (ntiles - 1)


def compile_dwc(unary_per_cell, edges, Wm, ntiles, W_, H_):
    """Potts -> domain-wall Ising: thermometer spins per cell.

    Faithful port of the codon walkthrough's compile_dwc, extended from a
    chain to a lattice: every cell has up to four partners (N/E/S/W edges,
    each contributing its own energy-coupled second differences).

    Reference only — not exercised by the solve path (ising_solve reports
    the budget analytically and solves in the categorical representation);
    kept as the blueprint for a native spin-domain solve.
    """
    K = ntiles
    n = W_ * H_
    pos_of_spin = np.array([c for c in range(n) for _ in range(K - 1)])
    spin_pos_index = np.array([j for c in range(n) for j in range(K - 1)])
    n_spins = len(pos_of_spin)
    off = [spin_of_cell(c, ntiles) for c in range(n)]

    bias_base = np.zeros(n_spins, dtype=np.float32)
    first_minus_last = np.zeros(n_spins, dtype=np.float32)
    for s in range(n_spins):
        c, j = pos_of_spin[s], spin_pos_index[s]
        # unary first difference (like field modes: |dh|<=1 too weak; use
        # direct weights scaled by beta later)
        bias_base[s] = (unary_per_cell[c, j + 1] - unary_per_cell[c, j]) / 2
        if j == 0:
            first_minus_last[s] += 1.0
        if j == K - 2:
            first_minus_last[s] -= 1.0

    constraint_edges = [(off[c] + j, off[c] + j + 1)
                        for c in range(n) for j in range(K - 2)]

    inter_edges, inter_weights = [], []
    for e, (s, t, d) in enumerate(edges):
        Wd = Wm[e]
        s0, t0 = off[s], off[t]
        for i in range(K - 1):
            # src spin i couples to dst via second differences
            jj = jnp.arange(K - 1)
            bias_base[s0 + i] += (Wd[i + 1, 0] - Wd[i, 0] +
                                  Wd[i + 1, K - 1] - Wd[i, K - 1]) / 4.0
            bias_base[t0 + i] += (Wd[0, i + 1] - Wd[0, i] +
                                  Wd[K - 1, i + 1] - Wd[K - 1, i]) / 4.0
            for j in range(K - 1):
                w = (Wd[i + 1, j + 1] - Wd[i, j + 1] - Wd[i + 1, j] + Wd[i, j]) / 4.0
                if abs(w) > 1e-6:
                    inter_edges.append((s0 + i, t0 + j))
                    inter_weights.append(w)

    colors = {}
    for s in range(n_spins):
        colors.setdefault((pos_of_spin[s] % 2, spin_pos_index[s] % 2), []).append(s)

    return {
        "n_spins": n_spins, "bias": jnp.array(bias_base),
        "fml": jnp.array(first_minus_last), "constraint": constraint_edges,
        "inter": inter_edges, "inter_w": jnp.array(inter_weights, dtype=jnp.float32),
        "colors": colors, "off": off, "K": K, "n": n,
    }


def ising_solve(spec):
    """Report the Z1 p-bit budget for this problem — the domain-wall
    thermometer compile of the Potts lattice is sum over cells of
    (allowed-states - 1) spins — then solve in the categorical
    representation. THRML is the sampler both ways; the structural budget
    is what a thermodynamic device would need to run this exact model."""
    W_, H_, ntiles = spec["w"], spec["h"], spec["ntiles"]
    domain = spec["domain"]

    n = W_ * H_
    if domain is not None:
        n_spins = sum(bin(domain[i]).count("1") - 1 for i in range(n))
    else:
        n_spins = n * (ntiles - 1)
    emit({"t": "meta", "pbits": int(n_spins),
          "note": "domain-wall compile: %d thermometer pbits (Z1-class budget)"
                  % int(n_spins)})

    # solve in the Potts (hardware-parallel) representation; the meta
    # above is the one budget report, so suppress potts_solve's duplicate
    potts_solve(spec, emit_meta=False)


def main():
    try:
        spec = load_spec()
        if spec.get("form", "potts") == "ising":
            ising_solve(spec)
        else:
            potts_solve(spec)
    except BrokenPipeError:
        sys.exit(1)
    except SystemExit:
        raise
    except BaseException as e:  # noqa: BLE001 - the C parent needs one JSON line
        # stdout stays clean of tracebacks; details go to stderr (the
        # parent redirects those to /tmp/wfc_thermo_err.log)
        print("wfc_thermo: fatal: %r" % e, file=sys.stderr)
        try:
            emit({"t": "fatal", "why": "%s: %s" % (type(e).__name__, e)})
        except Exception:
            pass
        sys.exit(1)


if __name__ == "__main__":
    main()
