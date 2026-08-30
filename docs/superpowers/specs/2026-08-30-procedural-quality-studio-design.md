# Procedural Quality Studio Design

**Status:** Approved for implementation

## Goal

Make procedural quality observable, make thermo learning mode-aware, add a small reversible world-studio workflow, and add a fourth connector world without weakening the existing hard WFC guarantees.

## Product behavior

### Quality Observatory

- The normal bottom HUD continues to show the current solver and quality.
- Lowercase `l` opens a full-screen `EVOLUTION LAB` overlay; press `l` again to return.
- The overlay shows the active mode, seed, solver/sampler, learning state, quality total, all quality dimensions, thermo beta/confidence/round/observations, proposal outcomes, and a compact quality trend.
- The quality trend is bounded to the most recent 64 observations and is reset when a new grid is started.
- `--report FILE` writes a small JSON report after a headless solve, including the reproducibility identity, quality vector, solver, and thermo counters.

### Mode-aware intelligence

- Quality totals use explicit mode profiles rather than one universal weighting.
- Connector worlds emphasize hard validity and their intended topology:
  - `streets`: intersections, balanced connectivity, and frame-safe boundaries.
  - `neurons`: branching/hub structure and tile diversity.
  - `mycelium`: sparse connected growth and smooth density.
  - `delta`: flowing branch structure, coverage, and frame-safe boundaries.
- Thermo feedback includes the full quality vector and profile focus label.
- The dependency-free learner persists a bounded history of metric snapshots in the existing per-mode profile; all preference values remain clamped.

### World Studio

- Hovering a cell and pressing uppercase `P` pins/unpins its current tile.
- A pin first collapses the hovered cell transactionally if needed, then protects that singleton during later thermo/classic propagation.
- Pinned cells receive a small visual marker and the HUD reports the pin count.
- Right-click carving unpins the cell before reopening it; `u` still undoes the preceding sculpt operation.
- Pins are part of the world save/load payload, with a backwards-compatible reader for existing saves.
- `--report` includes pin count and can be used with the same seed/link to compare runs.

### Fourth mode: delta

- `delta` is a bounded, non-toroidal connector world with water/estuary colors, branching channels, confluences, and animated downstream glints.
- It uses the existing 14 connector tiles with a distinct prior that favors long channels and occasional three-way confluences.
- The mode is included in mode cycling, `--list-modes`, help, collage, gallery, audio tables, exports, and quality checks.

## Invariants

- C remains authoritative for domains, compatibility, propagation, rollback, and hard borders.
- Thermo may propose only tiles in the current domain; invalid patches and invalid complete configurations are rejected.
- Pins are ordinary singleton domains plus a separate bounded marker; they never bypass compatibility propagation.
- Learning is optional, bounded, mode/fingerprint isolated, atomically persisted, and disabled by `--no-learn`.
- Headless output remains deterministic for classic solver runs with the same mode, seed, dimensions, and arguments.
- No new runtime dependency is required.

## Verification

- Python unit tests cover metric-history validation, persistence, and the worker feedback contract.
- C/CLI checks cover the new report schema, pin metadata, mode list, delta solve, and deterministic quality output.
- Existing strict compilation, all-mode smoke solves, bridge/protocol tests, learning tests, quality checks, regression checks, and sanitizer fuzzing remain green.
- Interactive verification is performed in the real macOS Terminal.app session with `l`, `P`, `u`, `R`, mode cycling, and thermo learning.
