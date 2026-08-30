#!/usr/bin/env python3
"""wfc_thermo.py - wave function collapse as an energy-based model.

The worker protocol reads JSONL commands on stdin (written by wfc.c
--solver thermo):
    w, h, ntiles, seed, torus
    unary[ntiles]          per-tile log-weight (unary energy)
    cdir[4][ntiles]        per-direction bitmask of compatible b tiles
    domains[n]             per-cell uint64 allowed-tile masks (optional)
    steps, chains, beta0, beta_max
    form                   "potts" (default) | "ising" (domain-wall p-bits)

The long-lived protocol is bidirectional:
    init -> ready
    sample -> stats, proposal | done
    feedback -> learn
    reset -> learn
    finish -> done
    stop -> exit

The sampler is intentionally optional.  If THRML/JAX is installed, the
one-shot compatibility API below remains available; the persistent worker
uses its dependency-free bounded proposal engine so incremental learning has
the same behavior in every environment.

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
import math
import os
import random
import re
import signal
import sys
import time

from wfc_learning import (
    CONTEXT_COUNT,
    load_profile,
    new_state,
    reset_state,
    save_profile,
    tile_fingerprint,
    update_state,
)

_T0 = time.monotonic()

MAX_TILES = 63  # C represents compatibility and domains as uint64_t masks.
MAX_CELLS = 100_000
MAX_DIM = 4096
MAX_STEPS = 10_000
MAX_CHAINS = 1_024
MAX_EDGE_BYTES = 256 * 1024 * 1024
MAX_CHAIN_STATES = 64 * 1024 * 1024
MAX_SCAN_STATES = 64 * 1024 * 1024
MAX_PROTOCOL_LINE = 8 * 1024 * 1024
MAX_FEEDBACK_EVENTS = 512
U64_MASK = (1 << 64) - 1


def _fatal(why):
    print(json.dumps({"t": "fatal", "why": why}))
    sys.stdout.flush()
    sys.exit(1)


_RUNTIME_ERROR = None
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
    _RUNTIME_ERROR = e

# if the C parent dies we must not die with a traceback mid-write
signal.signal(signal.SIGPIPE, signal.SIG_DFL)

INVALID = -1e10


def require_runtime():
    if _RUNTIME_ERROR is not None:
        raise RuntimeError("missing thrml/jax (pip install thrml jax): %s" % _RUNTIME_ERROR)


def jax_key(seed):
    """Represent the full protocol seed with JAX's uint32 key primitives."""
    seed &= U64_MASK
    key = jax.random.key(seed & 0xFFFFFFFF)
    return jax.random.fold_in(key, (seed >> 32) & 0xFFFFFFFF)


def emit(v):
    sys.stdout.write(json.dumps(v) + "\n")
    sys.stdout.flush()


def _read_protocol_line():
    """Bound one JSONL frame before handing it to the JSON decoder."""
    line = sys.stdin.readline(MAX_PROTOCOL_LINE + 1)
    if len(line) > MAX_PROTOCOL_LINE:
        raise ValueError("protocol line exceeds the supported limit of %d bytes" % MAX_PROTOCOL_LINE)
    return line


def validate_spec(spec):
    """Normalize one init/legacy spec without importing numeric runtimes."""
    if not isinstance(spec, dict):
        raise ValueError("spec must be a json object")
    for key in ("w", "h", "ntiles", "cdir", "unary"):
        if key not in spec:
            raise ValueError("spec missing key: %s" % key)
    if any(isinstance(spec[key], bool) or not isinstance(spec[key], int)
           for key in ("w", "h", "ntiles")):
        raise ValueError("w/h/ntiles must be integers")
    w, h, ntiles = spec["w"], spec["h"], spec["ntiles"]
    if w < 1 or h < 1 or ntiles < 1:
        raise ValueError("w/h/ntiles must be >= 1")
    if w > MAX_DIM or h > MAX_DIM:
        raise ValueError("w/h exceed the supported limit of %d" % MAX_DIM)
    if w > MAX_CELLS // h:
        raise ValueError("grid exceeds the supported limit of %d cells" % MAX_CELLS)
    if ntiles > MAX_TILES:
        raise ValueError("ntiles exceeds the uint64 mask limit of %d" % MAX_TILES)
    spec["w"], spec["h"], spec["ntiles"] = w, h, ntiles
    n = w * h
    if 2 * n * ntiles * ntiles > MAX_EDGE_BYTES // 4:
        raise ValueError("compatibility matrix exceeds the supported memory budget")
    raw = spec["cdir"]
    if not isinstance(raw, list) or len(raw) != 4:
        raise ValueError("cdir must be a list of exactly 4 rows")
    cdir_masks = []
    mask_limit = (1 << ntiles) - 1
    for d in range(4):
        row = raw[d]
        if isinstance(row, dict):
            row = [row.get(str(i), 0) for i in range(ntiles)]
        elif isinstance(row, list):
            if len(row) != ntiles:
                raise ValueError("cdir[%d] has wrong length" % d)
        else:
            row = [row] * ntiles
        try:
            values = []
            for v in row:
                if isinstance(v, bool) or not isinstance(v, int) or v < 0 or v > mask_limit:
                    raise ValueError
                values.append(v)
            cdir_masks.append(values)
        except (TypeError, ValueError):
            raise ValueError("cdir[%d] not integers" % d)
    spec["cdir_masks"] = cdir_masks
    unary = spec["unary"]
    if not isinstance(unary, list) or len(unary) != ntiles:
        raise ValueError("unary must have ntiles entries")
    try:
        unary = [float(v) for v in unary]
    except (TypeError, ValueError):
        raise ValueError("unary not numeric")
    if not all(math.isfinite(value) for value in unary):
        raise ValueError("unary has non-finite values")
    spec["unary"] = unary
    dom = None
    if "domains" in spec:
        if not isinstance(spec["domains"], list) or len(spec["domains"]) != n:
            raise ValueError("domains must have exactly w*h entries")
        try:
            dom = []
            for v in spec["domains"]:
                if isinstance(v, bool) or not isinstance(v, int) or v < 0 or v > mask_limit:
                    raise ValueError
                dom.append(v)
        except (TypeError, ValueError):
            raise ValueError("domains not integers")
        for i, m in enumerate(dom):
            if m == 0:
                raise ValueError("cell %d has an empty domain (contradicted)" % i)
    spec["domain"] = dom
    torus = spec.get("torus", False)
    if not isinstance(torus, bool):
        raise ValueError("torus must be boolean")
    spec["torus"] = torus
    for key, default, upper in (("steps", 180, MAX_STEPS), ("chains", 48, MAX_CHAINS)):
        value = spec.get(key, default)
        if isinstance(value, bool) or not isinstance(value, int) or value < 1 or value > upper:
            raise ValueError("%s must be an integer in the range 1..%d" % (key, upper))
        spec[key] = value
    if n > MAX_CHAIN_STATES // spec["chains"]:
        raise ValueError("initial chain state exceeds the supported memory budget")
    if n > MAX_SCAN_STATES // (spec["steps"] * spec["chains"]):
        raise ValueError("anneal output exceeds the supported memory budget")
    seed = spec.get("seed", 0)
    if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0 or seed >= (1 << 64):
        raise ValueError("seed must be an unsigned 64-bit integer")
    spec["seed"] = seed & U64_MASK
    form = spec.get("form", "potts")
    if form not in ("potts", "ising"):
        raise ValueError("form must be potts or ising")
    spec["form"] = form
    mode = spec.get("mode", "default")
    if not isinstance(mode, str) or len(mode) > 64:
        raise ValueError("mode must be a short string")
    spec["mode"] = mode
    learn = spec.get("learn", True)
    if not isinstance(learn, bool):
        raise ValueError("learn must be boolean")
    spec["learn"] = learn
    profile_dir = spec.get("profile_dir")
    if profile_dir is not None and (not isinstance(profile_dir, str) or len(profile_dir) > 4096):
        raise ValueError("profile_dir must be a path string")
    return spec


def load_spec():
    try:
        spec = json.load(sys.stdin)
        return validate_spec(spec)
    except SystemExit:
        raise
    except Exception as e:
        _fatal("bad json spec: %s" % e)


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
    require_runtime()
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
    rng_c = np.random.default_rng((seed + 31) & U64_MASK)
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

    keys0 = jax.random.split(jax_key(seed), steps)
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
        seed2 = (seed + 1000 * (retry + 1)) & U64_MASK
        pref = draw_preferences(unary, domain, ntiles, n,
                                np.random.default_rng((seed2 + 555) & U64_MASK), W_, H_,
                                spec.get("smooth", False))
        U_basis = jnp.array(cell_unary(unary, domain, ntiles, n, pref), dtype=jnp.float32)
        rng_c = np.random.default_rng((seed2 + 31) & U64_MASK)
        nxt = np.zeros((chains, n), dtype=np.uint8)
        for c in range(chains):
            nxt[c] = [int(rng_c.choice(allowed_ids(i))) for i in range(n)]
        cfgs2 = np.asarray(anneal(jnp.array(nxt), jax.random.split(jax_key(seed2), steps)))
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


def _safe_profile_name(mode):
    """Keep user-visible mode names from becoming path components."""
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(mode)).strip("._")
    return name or "default"


def _domain_values(raw, n, ntiles):
    """Validate a protocol domain vector without depending on NumPy."""
    if not isinstance(raw, list) or len(raw) != n:
        raise ValueError("domains must have exactly w*h entries")
    limit = (1 << ntiles) - 1
    values = []
    for i, value in enumerate(raw):
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0 or value > limit:
            raise ValueError("domain %d is empty or outside the tile mask" % i)
        values.append(value)
    return values


def _singleton_tile(mask):
    if mask <= 0 or mask & (mask - 1):
        return -1
    return mask.bit_length() - 1


class ThermoSession:
    """Long-lived, C-controlled thermodynamic proposal session.

    The C side owns hard domains and propagation.  This object only proposes
    soft assignments and updates bounded preferences from C's measured reward.
    Keeping those responsibilities separate makes a rejected proposal cheap:
    the C transaction rolls back while the learner receives the result.
    """

    def __init__(self):
        self.spec = None
        self.edges = []
        self.state = None
        self.learn = False
        self.profile_path = None
        self.rng = random.Random(0)
        self.round = 0
        self.beta_scale = 1.0
        self.pending = None
        self.sampler = "python"

    def _identity(self, spec):
        mode = spec.get("mode", "default")
        tiles = [{"index": i, "weight": spec["unary"][i]}
                 for i in range(spec["ntiles"])]
        fingerprint = tile_fingerprint(mode, tiles, spec["cdir_masks"])
        pair_count = 4 * spec["ntiles"] * spec["ntiles"]
        profile_dir = (spec.get("profile_dir") or
                       os.environ.get("WFC_THERMO_PROFILE") or
                       os.path.expanduser("~/.wfc-thermo"))
        profile_name = "%s-%s.json" % (_safe_profile_name(mode), fingerprint)
        self.profile_path = os.path.join(profile_dir, profile_name)
        return mode, fingerprint, pair_count

    def init(self, command):
        if command.get("v") != 1:
            raise ValueError("unsupported protocol version")
        spec = validate_spec(dict(command))
        self.spec = spec
        self.edges = build_edges(spec["w"], spec["h"], spec["torus"])
        mode, fingerprint, pair_count = self._identity(spec)
        self.learn = bool(spec.get("learn", True))
        if self.learn:
            self.state = load_profile(
                self.profile_path,
                mode,
                fingerprint,
                spec["ntiles"],
                pair_count,
                CONTEXT_COUNT,
            )
        else:
            self.state = new_state(spec["ntiles"], pair_count, CONTEXT_COUNT)
            self.state["mode"] = mode
            self.state["fingerprint"] = fingerprint
        self.rng = random.Random((spec["seed"] ^ 0x9E3779B97F4A7C15) & U64_MASK)
        self.round = 0
        self.beta_scale = 1.0
        self.pending = None
        # The persistent path is deliberately the bounded Python proposal
        # engine.  THRML remains available for the legacy one-shot API, but
        # claiming it here would misdescribe the incremental worker.
        self.sampler = "python"
        domain = spec.get("domain")
        pbits = (sum(mask.bit_count() - 1 for mask in domain)
                 if domain is not None else spec["w"] * spec["h"] * (spec["ntiles"] - 1))
        emit({
            "v": 1,
            "t": "ready",
            "schema": 1,
            "sampler": self.sampler,
            "observations": self.state["observations"],
            "pbits": int(pbits),
        })

    def _neighbors(self, index):
        w, h = self.spec["w"], self.spec["h"]
        x, y = index % w, index // w
        for direction in range(4):
            nx, ny = x, y
            if direction == 0:
                if h == 1:
                    continue
                ny = (y - 1) % h if self.spec["torus"] else y - 1
            elif direction == 1:
                if w == 1:
                    continue
                nx = (x + 1) % w if self.spec["torus"] else x + 1
            elif direction == 2:
                if h == 1:
                    continue
                ny = (y + 1) % h if self.spec["torus"] else y + 1
            else:
                if w == 1:
                    continue
                nx = (x - 1) % w if self.spec["torus"] else x - 1
            if 0 <= nx < w and 0 <= ny < h:
                yield ny * w + nx, direction

    def _context_index(self, domains, index):
        neighbors = list(self._neighbors(index))
        boundary = len(neighbors) < 4
        unresolved = sum(_singleton_tile(domains[n]) < 0 for n, _ in neighbors)
        return min(CONTEXT_COUNT - 1, (4 if boundary else 0) + min(3, unresolved))

    def _pair_index(self, direction, source, target):
        ntiles = self.spec["ntiles"]
        return (direction * ntiles + source) * ntiles + target

    def _pair_term(self, direction, source, neighbor_mask):
        compatible = self.spec["cdir_masks"][direction][source] & neighbor_mask
        if not compatible:
            return None
        total = 0.0
        count = 0
        while compatible:
            bit = compatible & -compatible
            target = bit.bit_length() - 1
            total += self.state["pair_bias"][self._pair_index(direction, source, target)]
            count += 1
            compatible ^= bit
        return total / max(1, count)

    def _candidate_options(self, domains, index):
        ntiles = self.spec["ntiles"]
        context = self._context_index(domains, index)
        options = []
        for tile in range(ntiles):
            if not domains[index] & (1 << tile):
                continue
            pair_score = 0.0
            support = 0
            safe = True
            for neighbor, direction in self._neighbors(index):
                term = self._pair_term(direction, tile, domains[neighbor])
                if term is None:
                    safe = False
                    break
                pair_score += term
                support += (self.spec["cdir_masks"][direction][tile] & domains[neighbor]).bit_count()
            if not safe:
                continue
            weight = max(float(self.spec["unary"][tile]), 1e-9)
            score = (math.log(weight) + self.state["tile_bias"][tile] +
                     0.34 * pair_score + 0.18 * self.state["context_bias"][context] +
                     0.035 * math.log1p(support))
            options.append((tile, score, context))
        return options

    def _choice(self, options, beta):
        if not options:
            return None, 0.0
        scaled = [max(-60.0, min(60.0, beta * item[1])) for item in options]
        peak = max(scaled)
        weights = [math.exp(value - peak) for value in scaled]
        total = sum(weights)
        pick = self.rng.random() * total
        for item, weight in zip(options, weights):
            pick -= weight
            if pick <= 0.0:
                return item, weight / total
        return options[-1], weights[-1] / total

    def _partial_bad(self, domains):
        bad = 0
        for source, target, direction in self.edges:
            a = _singleton_tile(domains[source])
            b = _singleton_tile(domains[target])
            if a >= 0 and b >= 0 and not (self.spec["cdir_masks"][direction][a] & (1 << b)):
                bad += 1
        return bad

    def _complete_config(self, domains):
        cfg = [_singleton_tile(mask) for mask in domains]
        if any(tile < 0 for tile in cfg):
            return cfg, -1
        bad = 0
        for source, target, direction in self.edges:
            if not (self.spec["cdir_masks"][direction][cfg[source]] & (1 << cfg[target])):
                bad += 1
        return cfg, bad

    def _energy(self, domains):
        cfg, _ = self._complete_config(domains)
        energy = 0.0
        for index, tile in enumerate(cfg):
            if tile >= 0:
                energy += math.log(max(float(self.spec["unary"][tile]), 1e-9))
        for source, target, direction in self.edges:
            a, b = cfg[source], cfg[target]
            if a >= 0 and b >= 0:
                energy += 1.0 if self.spec["cdir_masks"][direction][a] & (1 << b) else -5.0
        return energy

    def _proposal(self, domains, budget, beta):
        unresolved = [i for i, mask in enumerate(domains) if _singleton_tile(mask) < 0]
        if not unresolved:
            return None
        # WFC's minimum-entropy ordering remains the strongest structural
        # heuristic; the thermal draw only chooses among the best frontier.
        ranked = sorted(
            unresolved,
            key=lambda i: (domains[i].bit_count(),
                           -sum(_singleton_tile(nmask) >= 0
                                for n, _ in self._neighbors(i)
                                for nmask in (domains[n],)), i),
        )
        scan = min(len(ranked), max(1, budget))
        candidates = []
        for index in ranked[:scan]:
            options = self._candidate_options(domains, index)
            if options:
                candidates.append((index, options))
        if not candidates:
            for index in ranked[scan:]:
                options = self._candidate_options(domains, index)
                if options:
                    candidates.append((index, options))
                    break
        if not candidates:
            return None
        # Prefer the most constrained candidate but retain a small thermal
        # chance of exploring the next frontier cell.
        cell_weights = [math.exp(-0.45 * (len(options) - 1)) for _, options in candidates]
        total = sum(cell_weights)
        pick = self.rng.random() * total
        chosen_index, chosen_options = candidates[-1]
        for item, weight in zip(candidates, cell_weights):
            pick -= weight
            if pick <= 0.0:
                chosen_index, chosen_options = item
                break
        choice, confidence = self._choice(chosen_options, beta)
        if choice is None:
            return None
        tile, score, context = choice
        return {
            "i": chosen_index,
            "tile": tile,
            "p": round(float(confidence), 6),
            "score": round(float(score), 6),
            "context": context,
        }

    def _save(self):
        if self.learn and self.profile_path:
            save_profile(
                self.profile_path,
                self.state["mode"],
                self.state["fingerprint"],
                self.state,
            )

    @staticmethod
    def _feedback_events(command, key):
        events = command.get(key, [])
        if events is None:
            return []
        if not isinstance(events, list) or len(events) > MAX_FEEDBACK_EVENTS:
            raise ValueError("%s must contain at most %d events" % (key, MAX_FEEDBACK_EVENTS))
        return events

    def _done(self, domains):
        cfg, bad = self._complete_config(domains)
        if bad < 0:
            return False
        valid = int(bad == 0)
        emit({
            "v": 1,
            "t": "done",
            "valid": valid,
            "bad": int(bad),
            "quality": 1.0 if valid else max(0.0, 1.0 - 0.05 * bad),
            "cfg": cfg,
            "form": self.spec["form"],
            "sampler": self.sampler,
        })
        return True

    def sample(self, command):
        if self.spec is None:
            raise ValueError("sample received before init")
        domains = _domain_values(command.get("domains"),
                                 self.spec["w"] * self.spec["h"],
                                 self.spec["ntiles"])
        if self._done(domains):
            return
        raw_budget = command.get("budget", 12)
        if isinstance(raw_budget, bool) or not isinstance(raw_budget, int) or raw_budget < 1:
            raise ValueError("budget must be a positive integer")
        budget = min(raw_budget, 256)
        raw_beta = command.get("beta_target", 2.0)
        beta_target = float(raw_beta)
        if not math.isfinite(beta_target) or beta_target <= 0.0:
            raise ValueError("beta_target must be positive and finite")
        beta = max(0.05, min(24.0, beta_target * self.beta_scale))
        self.round += 1
        proposal = self._proposal(domains, budget, beta)
        if proposal is None:
            raise ValueError("no locally safe proposal; classic solver should take over")
        bad = self._partial_bad(domains)
        emit({
            "v": 1,
            "t": "stats",
            "round": self.round,
            "beta": round(beta, 6),
            "energy": round(self._energy(domains), 6),
            "bad": bad,
            "confidence": proposal["p"],
            "reward": self.state["quality_history"][-1] if self.state["quality_history"] else 0.0,
            "observations": self.state["observations"],
            "sampler": self.sampler,
        })
        self.pending = proposal
        emit({
            "v": 1,
            "t": "proposal",
            "round": self.round,
            "patch": [proposal],
            "beta": round(beta, 6),
            "energy": round(self._energy(domains), 6),
            "bad": bad,
        })

    def feedback(self, command):
        if self.spec is None:
            raise ValueError("feedback received before init")
        reward = float(command.get("reward", 0.0))
        if not math.isfinite(reward):
            raise ValueError("feedback reward is not finite")
        accepted = int(command.get("accepted", 0))
        rejected = int(command.get("rejected", 0))
        contradictions = int(command.get("contradictions", 0))
        if contradictions > 0 or rejected > accepted:
            self.beta_scale = max(0.72, self.beta_scale * 0.94)
        elif accepted > 0 and reward >= 0.0:
            self.beta_scale = min(1.35, self.beta_scale * 1.018)
        if self.learn:
            update_state(
                self.state,
                reward,
                self._feedback_events(command, "tile_events"),
                self._feedback_events(command, "pair_events"),
                self._feedback_events(command, "context_events"),
            )
            self._save()
        emit({
            "v": 1,
            "t": "learn",
            "observations": self.state["observations"],
            "baseline": round(self.state["baseline"], 6),
            "tile_bias": [round(float(value), 6) for value in self.state["tile_bias"]],
            "pair_bias": [round(float(value), 6) for value in self.state["pair_bias"]],
            "context_bias": [round(float(value), 6) for value in self.state["context_bias"]],
            "beta_scale": round(self.beta_scale, 6),
        })
        self.pending = None

    def reset(self):
        if self.state is None:
            raise ValueError("reset received before init")
        self.state = reset_state(self.state)
        self.beta_scale = 1.0
        self.pending = None
        self._save()
        emit({
            "v": 1,
            "t": "learn",
            "observations": 0,
            "baseline": 0.0,
            "tile_bias": list(self.state["tile_bias"]),
            "pair_bias": list(self.state["pair_bias"]),
            "context_bias": list(self.state["context_bias"]),
            "beta_scale": 1.0,
            "reset": True,
        })

    def finish(self, command):
        if self.spec is None:
            raise ValueError("finish received before init")
        raw = command.get("domains")
        if raw is not None:
            domains = _domain_values(raw, self.spec["w"] * self.spec["h"], self.spec["ntiles"])
        else:
            cfg = command.get("cfg", command.get("assignments"))
            if not isinstance(cfg, list) or len(cfg) != self.spec["w"] * self.spec["h"]:
                raise ValueError("finish needs domains or a complete cfg")
            domains = []
            for tile in cfg:
                if isinstance(tile, bool) or not isinstance(tile, int) or tile < 0 or tile >= self.spec["ntiles"]:
                    raise ValueError("finish cfg contains an invalid tile")
                domains.append(1 << tile)
        self._done(domains)


def run_worker(first_command=None):
    session = ThermoSession()
    def handle(command):
        if not isinstance(command, dict):
            raise ValueError("command must be an object")
        kind = command.get("t")
        if kind == "init":
            session.init(command)
        elif kind == "sample":
            session.sample(command)
        elif kind == "feedback":
            session.feedback(command)
        elif kind == "reset":
            session.reset()
        elif kind == "finish":
            session.finish(command)
        elif kind == "stop":
            return True
        else:
            raise ValueError("unknown command")
        return False

    lines = []
    if first_command is not None:
        lines.append(first_command)
    for first in lines:
        try:
            command = json.loads(first) if isinstance(first, str) else first
            if handle(command):
                return 0
        except BrokenPipeError:
            return 1
        except Exception as error:  # noqa: BLE001 - one JSON error at the process boundary
            emit({"v": 1, "t": "fatal", "why": str(error)})
            return 1
    while True:
        try:
            line = _read_protocol_line()
            if not line:
                break
            command = json.loads(line)
            if handle(command):
                return 0
        except BrokenPipeError:
            return 1
        except Exception as error:  # noqa: BLE001 - one JSON error at the process boundary
            emit({"v": 1, "t": "fatal", "why": str(error)})
            return 1
    return 0


def main():
    try:
        first_line = _read_protocol_line()
        if not first_line:
            _fatal("missing json spec")
        try:
            first = json.loads(first_line)
        except Exception as e:
            _fatal("bad json spec: %s" % e)
        if isinstance(first, dict) and first.get("t") == "init":
            return run_worker(first)
        spec = validate_spec(first)
        if spec.get("form", "potts") == "ising":
            ising_solve(spec)
        else:
            potts_solve(spec)
        return 0
    except BrokenPipeError:
        return 1
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
        return 1


if __name__ == "__main__":
    sys.exit(main())
