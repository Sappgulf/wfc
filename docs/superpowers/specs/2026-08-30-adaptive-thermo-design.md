# Adaptive Thermo Learning and New World Modes

Date: 2026-08-30

## Status

Approved direction; implementation begins after this spec is reviewed.

## Problem

The classic solver owns the live WFC domain state in `wfc.c`. The current
thermodynamic path launches `wfc_thermo.py` once per map, gives it a complete
problem, and waits for one final configuration. The sidecar samples a fixed
energy landscape: it has no feedback channel, no persistent state, and no
useful intermediate proposals. The terminal therefore shows an animated
waiting state rather than a thermodynamic solve that visibly learns while it
generates.

The project also needs three additional worlds that exercise different
topologies: an urban street network, a neural network, and an organic fungal
network.

## Goals

1. Make thermo generate incrementally and adapt during one map.
2. Learn procedural quality automatically, without user labels or an external
   training service.
3. Keep hard WFC compatibility authoritative and preserve classic fallback.
4. Retain useful learned preferences per mode while keeping them bounded,
   resettable, schema-versioned, and safe across tile changes.
5. Add `streets`, `neurons`, and `mycelium` as first-class modes with distinct
   rendering, animation, and quality metrics.
6. Keep the classic path deterministic and keep the existing headless,
   sanitizer, export, and interactive contracts intact.
7. Keep interactive launches and user testing in macOS Terminal.app only.

## Non-goals

- Do not learn or mutate `cdir_` hard compatibility masks.
- Do not add a neural-network training dependency or a remote service.
- Do not make thermo required to build or run the classic solver.
- Do not combine thermo with twin, quad, or infinite-world modes in the first
  implementation.
- Do not redesign all existing renderers; new shared helpers are preferred,
  but changes remain scoped to generation, quality feedback, and the three
  new modes.

## Design principles

### Hard constraints stay in C

`wfc.c` remains the source of truth for domains, propagation, rollback, and
compatibility. A thermo proposal is advisory until C validates it against the
current domain and propagates it. Any invalid proposal is rejected without
damaging the live map.

The learned model can change only soft terms:

```text
E(state) = E_hard(state) + E_tile(state) + E_pair(state) + E_context(state)
```

`E_hard` remains the existing compatibility relation. Learned tile, pair, and
context terms are clamped and decayed so learning cannot overwhelm validity or
make a mode permanently collapse into one pattern.

### C owns the frame; Python owns the sampler

C continues to render every frame and handles all terminal interaction. The
Python sidecar maintains the THRML sampler, annealing state, learned soft
parameters, and candidate chain states. The bridge is a persistent, versioned
JSON-lines protocol rather than a one-shot stdin document.

### Quality is measured, not guessed

Every accepted patch receives a bounded reward from deterministic C-side
metrics. The reward combines general map quality with mode-specific structure:

- validity and number of accepted assignments,
- contradiction avoidance and rollback cost,
- tile-distribution diversity versus the mode target,
- spatial smoothness for field modes,
- connectedness, branch balance, and loop quality for network modes,
- a final completion score when the map is solved.

The reward is normalized against a running baseline before it reaches the
learner. This prevents a large map or a high step count from producing a larger
learning signal merely because it has more cells.

## Runtime architecture

### Persistent worker lifecycle

When thermo is enabled for a supported single world, C creates two pipes and
starts one `wfc_thermo.py` child:

- parent-to-child commands use one pipe,
- child-to-parent events use the existing nonblocking read pattern,
- the child is killed and reaped on mode changes, new maps, shutdown, timeout,
  or malformed output.

The worker is restarted when grid dimensions, tile count, mode schema, or
thermo form changes. It remains alive across sample rounds within one map so
JAX/THRML compilation and chain state can be reused.

### Protocol

Every message has `v: 1` and a short `t` discriminator. Messages are bounded
by the existing C line-buffer limit and validated before use.

Parent commands:

```json
{"v":1,"t":"init","mode":"neurons","w":80,"h":32,
 "ntiles":16,"seed":123,"torus":false,"smooth":false,
 "unary":[...],"cdir":[...],"domains":[...],"profile":{...}}
{"v":1,"t":"sample","budget":12,"beta_target":5.0}
{"v":1,"t":"feedback","reward":0.18,"accepted":9,
 "contradictions":1,"quality":0.62,"assignments":[...]}
{"v":1,"t":"finish","quality":0.81,"assignments":[...]}
{"v":1,"t":"stop"}
```

Child events:

```json
{"v":1,"t":"ready","schema":1}
{"v":1,"t":"stats","beta":2.4,"energy":-91.0,"bad":4,
 "confidence":0.58,"reward":0.0}
{"v":1,"t":"proposal","patch":[{"i":42,"tile":7,"p":0.94},...],
 "beta":3.1,"energy":-104.2,"bad":1}
{"v":1,"t":"learn","tile_bias":[...],"pair_bias":[...],
 "context_bias":[...]}
{"v":1,"t":"done","valid":1,"quality":0.81,"cfg":[...]}
{"v":1,"t":"fatal","why":"..."}
```

The exact pair and context payload shape is fixed in the implementation plan;
the protocol must use flat arrays so C can validate lengths before applying
anything. Diagnostics remain on stderr.

### Incremental generation loop

The interactive thermo branch becomes a round-based loop:

1. C sends the current domains and base/learned soft terms on `init`.
2. The worker performs a bounded number of annealing sweeps.
3. The worker emits statistics and a patch containing only high-confidence
   assignments from chain agreement or energy margin.
4. C copies the live domains, attempts the patch in confidence order, calls
   `propagate_from` for each accepted assignment, and restores the copy if a
   proposal contradicts.
5. C computes local reward and sends `feedback` with accepted assignments and
   quality metrics.
6. The worker updates its soft parameters and returns the next proposal.
7. When all domains are singleton, C sends `finish`, runs post-passes, and
   stores the learned profile.

If a round emits no safe patch, C continues rendering the entropy view while
the worker samples another round. A fixed round budget and the existing
240-second timeout prevent a stalled sidecar from blocking the application.
If the sidecar fails, C disables thermo for the current attempt and resumes
the classic solver exactly as it does today.

## Learning model

### Three learning layers

1. **Tile memory:** a per-tile logit captures whether a tile tends to appear in
   high-quality accepted solutions. It is applied as an additive unary term.
2. **Pair memory:** a per-direction compatible `(source tile, destination tile)`
   bias captures useful local transitions. Invalid pairs retain the hard
   penalty and cannot become valid through learning.
3. **Context memory:** compact features capture local structure without a
   large neural model: tile degree, band bucket, boundary/interior position,
   and neighboring domain-width bucket. Context terms are shared within a mode
   and updated from accepted patches.

Each update uses a clipped temporal-difference error:

```text
delta = clamp(reward - running_baseline, -1, 1)
parameter = clamp(decay * parameter + learning_rate * delta * feature, -2.5, 2.5)
```

The rate decreases mildly with observation count and never reaches zero during
an active session. A profile starts from zero learned bias, so the base tile
weights remain the initial personality of every mode.

### Adaptive annealing

The worker adjusts the next beta target from recent signals:

- raise beta when bad-edge count falls and confidence rises,
- hold or slightly lower beta when energy plateaus,
- reheat when bad-edge count increases or patch acceptance collapses,
- retain the best chain state and one diverse elite state for each round.

Beta and learning-rate changes are bounded by the request and are reported in
`stats` for the HUD and tests.

### Profile persistence

Profiles live outside `~/.wfcrc` in a versioned directory such as
`~/.wfc-thermo/`. Each mode profile records:

- schema version and mode name,
- tile count and a tile-structure fingerprint,
- observation count and baseline,
- tile, pair, and context biases,
- a bounded quality history.

Profiles are loaded only when the fingerprint matches. Writes use a temporary
file plus rename and are best-effort; a corrupt or incompatible profile is
ignored with a visible note. `--no-learn` disables both updates and disk I/O,
and an interactive reset control clears the active session profile.

## Quality scoring

The scorer is deterministic and must not depend on wall-clock time or random
state. It exposes a normalized `[0,1]` score and component values for debug
output/HUD.

Shared terms:

- `validity`: all singleton cells and compatible edges,
- `coverage`: accepted cells relative to the current frontier,
- `diversity`: distribution entropy relative to the mode's target,
- `smoothness`: neighboring band deltas for field modes,
- `stability`: reward for patches that remain valid after subsequent rounds.

Network modes add:

- connected component coverage,
- branch/junction balance,
- endpoint and loop targets,
- boundary contact penalties where appropriate.

The final score is recorded in `done` and used as the strongest, but not sole,
reward signal. A map that is valid but visually monotonous should improve
less than a valid map with healthy diversity.

## New modes

The mode count becomes 24. All three modes use connector-oriented tiles so
they exercise the learned pair/context terms while keeping the tile budget
under `MAXT`.

### `streets`

An urban night street plan using empty blocks, straight roads, curves,
T-junctions, and rare four-way intersections. Base weights favor connected
roads and varied block sizes. The renderer draws asphalt, sidewalks, lane
markings, windows, traffic lights, and moving headlights. The quality scorer
favors connected road coverage, a controlled junction rate, and a few loops
without turning the whole map into a grid.

### `neurons`

A living neural tissue field using sparse dendrites, branches, synapses, and
rare soma-like junction variants. The renderer draws warm axons, glowing cell
bodies, synaptic sparks, and pulses traveling along connected traces. The
quality scorer favors connected coverage, branching without excessive hubs,
and a healthy mix of endpoints and junctions.

### `mycelium`

An organic fungal network using sparse filaments, branch clusters, and rare
fruiting-body variants. The renderer uses earthy colors, translucent threads,
spore motes, and slow growth pulses. The scorer favors multiple connected
regions, organic branch-length variation, and nonuniform density.

Mode-specific construction stays compatible with the existing `Tile` edge
representation. Flags identify renderer-only variants; compatibility remains
determined by edge values.

## User experience

- `T` toggles classic/thermo as today; the status line identifies adaptive
  learning when thermo is active.
- Add a reset-learning key and document it in help.
- Add a compact thermo HUD showing solver, beta, energy, bad edges, confidence,
  reward, quality, and profile observations without obscuring the world.
- Add `--no-learn` and a profile path/reset option suitable for headless tests.
- The entropy view distinguishes classic domain entropy from thermo proposal
  confidence.
- Existing keyboard, mouse, save, GIF, raytrace, iso, zen, and fallback flows
  remain available for supported combinations.

## Error handling and security

- Validate protocol version, message type, numeric finiteness, array lengths,
  tile IDs, cell IDs, confidence range, and bias limits before use.
- Use explicit pipe close/reap paths and retain the existing timeout.
- Never pass learned strings to a shell. Profile paths are constructed from a
  validated mode name and use private permissions.
- Treat all sidecar output as untrusted input; malformed lines are discarded or
  fail the current thermo attempt without corrupting C state.
- Keep all compatibility and domain checks in C before committing a proposal.

## Verification

The implementation must add or update tests for:

1. Protocol handshake, partial messages, malformed messages, and timeout.
2. Patch rollback when one proposed assignment contradicts another.
3. Hard compatibility invariance after many learning updates.
4. Bias clamping, decay, profile fingerprint mismatch, and corrupt profile.
5. Deterministic classic and `--no-learn` runs for fixed seeds.
6. Adaptive thermo produces at least one intermediate proposal on a supported
   test grid when THRML is available.
7. All 24 modes solve in classic mode across seed sweeps.
8. New mode renderers, GIF/PNG exports, and terminal paths under sanitizers.
9. Headless fallback when Python/THRML is unavailable.
10. A bounded benchmark comparing classic, thermo, and adaptive thermo memory.

The existing `make test regression python-check fuzz strict asan` gates remain
required. Interactive verification is run through a real macOS Terminal.app
window only.

## Implementation order

1. Extract shared quality/scoring and profile data structures without changing
   classic behavior.
2. Add the versioned persistent sidecar protocol and C patch transaction path.
3. Add the tile/pair/context learner and adaptive annealing.
4. Add the three modes, renderers, animation clocks, docs, and HUD.
5. Add focused regression/sidecar tests, run the complete verification suite,
   inspect the diff, commit, and push the finished implementation.

## Acceptance criteria

The work is complete when a Terminal.app run of adaptive thermo visibly accepts
multiple safe proposals during one map, reports changing quality/confidence
metrics, updates a bounded per-mode profile, and still produces a valid map;
all 24 modes remain usable; classic and no-learning determinism hold; the full
verification suite passes; and the final implementation is committed and
pushed without including unrelated pre-existing changes.
