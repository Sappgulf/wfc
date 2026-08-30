# Quality Evolution Studio

## Goal

Make procedural quality an active part of generation. A mode should not only
solve valid local constraints; it should express its visual grammar, expose
where a result is weak, learn from measured quality, and make it inexpensive to
choose a stronger deterministic variant.

The existing WFC hard-constraint boundary remains authoritative. This feature
set adds a quality-directed soft layer around it:

1. quality profiles are sent to thermo as weights and tile priors;
2. deterministic macro skeletons guide network modes before local detail;
3. Evolution Lab ranks seed variants (and preserves current studio pins as
   hard constraints when present);
4. a repair heatmap and explanation identify the weakest cells;
5. a benchmark lab compares classic, ephemeral thermo, and learned thermo;
6. reports contain enough evidence to reproduce and compare a run.

## Non-goals

- No new external runtime dependency for classic mode, tests, or reports.
- No sidecar authority over compatibility, pins, propagation, or final maps.
- No unbounded profile history, candidate count, grid size, or JSON payload.
- No automatic network publication or mutation of a user's saved profile unless
  the user explicitly runs a learning-enabled thermo session.

## Quality objective

`QualityMetrics` remains the eight-dimensional vector:

`total, validity, boundary, coverage, diversity, smoothness, stability, topology`.

Each mode exposes a normalized `QualityProfile` containing the seven component
weights (total is derived) and a short focus name. The C side serializes those
weights and a bounded per-tile quality prior in the thermo `init` message. The
worker adds the prior to its candidate score, while C evaluates before/after
vectors and returns both the vector and per-dimension delta in feedback. The
learner stores the latest bounded vector history and uses the weighted objective
for reward; no metric can bypass hard validation.

The objective is deliberately mode-aware:

- streets favors clean borders, coverage, balanced degree, and smooth avenues;
- neurons favors branching topology and diverse junctions;
- mycelium favors sparse connected growth and organic smoothness;
- delta favors border-safe channel coverage, smooth flow, and confluences.

## Macro guidance

Network worlds receive a deterministic low-frequency field before WFC detail.
The field is not a forced tile map. It gives each cell a role and a bounded
soft preference, so normal propagation and retries remain safe.

Roles are generated from the seed and normalized cell coordinates:

- streets: arterial crossbars plus offset side streets and intersections;
- neurons: soma/hub points with branching rays and tapering terminals;
- mycelium: several radial sources with noisy tendrils and sparse knots;
- delta: source-to-mouth bands with tributary/confluence regions.

The role bias favors a degree class appropriate to the local role. It is
applied in entropy tie-breaking and tile choice, and is recalculated on resize
or reset. A `macro` label and guide coverage summary are shown in the HUD and
report so a result can be understood without inspecting the pixels.

## Evolution Lab

`--evolve N` runs a bounded tournament of deterministic seed variants. The base
seed and current dimensions/mode are fixed; variant seeds are derived by a
stable splitmix-style hash. Each candidate uses the same hard rules and current
studio pins, then receives a complete quality vector. The winner is the
highest weighted total, with validity and boundary used as tie-breakers.

The CLI prints a compact ranked summary and leaves the winning map active. The
interactive `E` key runs the same lab after pausing, then displays the winner,
score, focus, and a small top-candidate table. Candidate count is clamped to a
safe range. A report records the winner seed, candidate count, and scores.

## Repair heatmap

`Q` toggles a live local-quality overlay. Each cell receives a bounded score
from unresolved-domain pressure, hard-edge mismatch exposure, macro-role fit,
and local topology fit. The weakest cell is highlighted in the terminal and
reported with a plain-language reason such as `boundary`, `branch`, `coverage`,
or `entropy`. Heatmap rendering is purely diagnostic; it never changes domains.

The Observatory adds the hotspot coordinates, score, reason, macro label, and a
repair hint. Pinned cells remain visibly distinct from diagnostic markers.

## Benchmark and regression lab

`make quality-benchmark` invokes a standard-library Python harness. It runs the
four network modes through:

- classic;
- thermo with learning disabled;
- thermo with an isolated temporary learned profile.

The harness records solve success, quality dimensions, weighted total, and
wall-clock milliseconds as JSON plus a readable table. It uses small bounded
maps by default and accepts `--trials` for a larger local comparison. Existing
strict, all-mode, bridge, protocol, sanitizer, and fuzz gates remain intact.

## Reports and compatibility

The report schema increments to 2 and remains backward-readable by consumers
that only use the existing fields. New fields are additive:

```json
{
  "quality": {
    "profile_weights": {},
    "hotspot": {"x": 0, "y": 0, "score": 0.0, "reason": "entropy"}
  },
  "macro": {"name": "arterial-grid", "guided_cells": 0},
  "evolution": {"candidates": 0, "winner_seed": 0, "scores": []}
}
```

Classic output for a fixed mode, seed, size, and command line stays
deterministic. Optional profile paths used by tests are isolated and never
depend on a user's existing home profile.

## Acceptance criteria

- thermo proposals include and use quality weights and tile priors;
- feedback contains complete metric deltas and the learner persists bounded
  objective history;
- all four network modes expose a deterministic macro name and guided-cell
  count;
- `--evolve 3` is deterministic, reports a winner, and preserves validity;
- `Q` and `E` are visible in help and safe when no cell is hovered;
- reports contain hotspot, macro, and evolution data;
- `make quality-benchmark` covers classic/ephemeral/learned paths;
- all existing and new tests pass, including strict compilation, sanitizer,
  fuzz, visual inspection, and a real Terminal.app smoke test.
