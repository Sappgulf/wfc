# Quality Evolution Studio Implementation Plan

> Execute this plan inline. The user has approved all six upgrades and asked
> for a final macOS Terminal.app launch after verification.

**Goal:** make mode-aware quality an active generation objective, add
deterministic macro guidance, provide seed/pin evolution and repair heatmaps,
and ship a reproducible benchmark lab without weakening WFC hard guarantees.

**Architecture:** retain `wfc.c` as the authority for hard domains,
compatibility, propagation, pins, rendering, and reports. Extend the existing
thermo JSONL bridge with profile weights, tile priors, and metric deltas. Add
small C helpers for macro guidance, local hotspot scoring, and candidate
tournaments. Add one dependency-free Python benchmark harness and wire it into
Makefile quality gates.

**Verification:** use focused red/green tests first, then strict build, all-mode
smoke, deterministic regression, Python/protocol/bridge/quality/studio tests,
sanitizers, fuzzing, visual image inspection, and a real Terminal.app smoke
session. Commit and push only after fresh verification.

## Task 1: Lock the new contracts with failing tests

- [x] Extend `tests/test_quality_studio.py` for help, report schema 2, hotspot,
  macro, and deterministic `--evolve` output.
- [x] Add `tests/test_quality_benchmark.py` for the benchmark table and the
  three solver labels.
- [x] Extend `tests/test_protocol_contract.py` with quality weights/tile-prior
  validation and metric-delta feedback.
- [x] Extend `tests/test_wfc_learning.py` for bounded objective history and
  metric-delta reward behavior.
- [x] Extend `tests/test_wfc_studio.c` with macro guidance and local hotspot
  invariants where static inclusion makes that practical.
- [x] Run the focused tests and record the expected red failures before
  implementation.

## Task 2: Make thermo quality-directed

- [x] Add a normalized profile-weight serializer and bounded per-tile prior in
  `wfc.c`.
- [x] Add strict validation for optional `quality_weights`, `quality_priors`,
  and `metrics_delta` in the Python worker.
- [x] Include the prior in `_candidate_options` and include metric deltas in
  feedback/reward updates while keeping C validation authoritative.
- [x] Persist a bounded objective history in `wfc_learning.py` with finite,
  normalized values and reset/profile compatibility.
- [x] Add bridge/protocol assertions showing the worker both receives and uses
  the objective contract.

## Task 3: Add deterministic macro skeleton guidance

- [x] Allocate/reset/resize a bounded per-cell macro-role buffer.
- [x] Implement stable role fields for streets, neurons, mycelium, and delta.
- [x] Add role-aware soft tile scoring to classic and thermo choices without
  making a role a hard constraint.
- [x] Expose macro name and guided-cell count in HUD, Observatory, and reports.
- [x] Verify every network mode solves across dimensions smaller than the
  macro wavelength and that classic determinism is preserved.

## Task 4: Add Evolution Lab

- [x] Add `--evolve N` parsing, safe clamping, and incompatibility checks for
  multi-world/infinite/interactive-only paths.
- [x] Implement deterministic candidate seed derivation, pin replay, quality
  ranking, winner restoration, and compact CLI output.
- [x] Add interactive `E` overlay with winner and top-candidate scores; keep it
  reversible and paused while displayed.
- [x] Include candidate count/winner/scores in the report.
- [x] Test the same invocation twice byte-for-byte and test pinned candidates
  remain valid.

## Task 5: Add repair heatmap and explanations

- [x] Implement local quality scoring using domain entropy, hard-edge exposure,
  macro fit, and network degree fit.
- [x] Track the weakest cell/reason deterministically and render `Q` heatmap
  markers without overriding pin markers.
- [x] Add hotspot data and a repair hint to Observatory and JSON reports.
- [x] Verify `Q` is safe before hover and during partial/finished solves.

## Task 6: Benchmark/regression lab and documentation

- [x] Add `tests/quality_benchmark.py` with bounded subprocess execution,
  isolated learned profiles, JSON output, readable table, and failure exit.
- [x] Add `make quality-benchmark` and include the harness in syntax checks.
- [x] Update README controls, report schema, thermo learning, macro guidance,
  Evolution Lab, heatmap, and benchmark instructions.
- [x] Update the design/spec plan checkboxes and capture verification evidence.

## Task 7: Full verification and delivery

- [x] Run focused red/green tests, then the complete Makefile suite.
- [x] Generate reports and gallery assets; inspect representative street,
  neuron, mycelium, delta, heatmap, and evolution outputs.
- [x] Launch the latest binary only in macOS Terminal.app, exercise `l`, `Q`,
  `E`, `T`, `P`, and `u` using the computer-control path, and leave it open.
- [x] Review `git diff`, commit the implementation, push the current branch,
  verify remote SHA, and report the exact test/launch status.

## Verification evidence

- Focused, complete, protocol, bridge, learning, quality, studio, regression,
  sanitizer, fuzz, and benchmark gates pass on the final implementation.
- `--evolve 3` is byte-for-byte deterministic and reports a winner plus scores.
- Fresh streets, neurons, mycelium, and evolved-delta PNG/report pairs were
  generated and visually inspected.
- Terminal.app smoke confirmed `Q` heatmap, `l` Observatory, `E` Evolution Lab,
  `P` hover guard, `T` thermo launch, and live objective counters; the latest
  build remains running in Terminal.app for manual testing.
