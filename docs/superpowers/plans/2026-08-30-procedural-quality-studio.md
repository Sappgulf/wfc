# Procedural Quality Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ship an observable, mode-aware procedural quality studio with pinned constraints, reproducible reports, and a distinct `delta` world.

**Architecture:** Keep the C WFC grid as the only authority for hard constraints. Add a bounded C quality snapshot/trend layer and a JSON report writer, send a mode-aware quality vector through the existing thermo feedback protocol, and extend the existing bounded learner with validated metric history. Add pins as singleton domains plus a parallel marker array, then build `delta` on the established connector tiles with its own render and quality profile.

**Tech Stack:** C11, zlib, Python 3 standard library, unittest, Makefile, ANSI terminal rendering, existing JSONL thermo bridge.

**Spec:** `docs/superpowers/specs/2026-08-30-procedural-quality-studio-design.md`

## Global Constraints

- C remains authoritative for domains, compatibility, propagation, rollback, and hard borders.
- Thermo proposals are soft assignments only and must stay within the current domains.
- Learning remains dependency-free, bounded, mode/fingerprint isolated, atomically persisted, and disabled by `--no-learn`.
- No new runtime dependency is required.
- Preserve existing deterministic classic solves and backwards-compatible world saves.
- Use the actual macOS Terminal.app for interactive launch verification.

---

### Task 1: Lock the contracts with failing tests

**Files:**
- Create: `tests/test_quality_studio.py`
- Modify: `tests/test_wfc_learning.py`
- Modify: `tests/test_protocol_contract.py`
- Modify: `Makefile`

**Interfaces:**
- Tests expect `--list-modes` to include `delta` and `--report FILE` to produce a JSON object with `mode`, `seed`, `dimensions`, `quality`, `thermo`, and `studio` fields.
- Tests expect the real worker `learn` event to echo a bounded `metrics` snapshot and persist `metrics_history`.

- [x] Write tests for the new CLI report/mode contracts and the real worker metric-history contract.
- [x] Run the focused tests and confirm they fail because the new CLI/report fields and worker metrics are absent.
- [x] Add the focused test target to `Makefile` without weakening existing checks.
- [x] Run the focused tests again to confirm the failures are still feature failures rather than test errors.

### Task 2: Add the quality observatory and report model

**Files:**
- Modify: `wfc.c` around quality globals, `quality_measure`, HUD rendering, key handling, and headless completion.
- Modify: `README.md`

**Interfaces:**
- Add bounded quality snapshot storage and a `quality_record(QualityMetrics)` helper.
- Add `render_observatory(void)` and lowercase `l` handling.
- Add `--report FILE` parsing and a validated JSON writer that emits the current mode, seed, dimensions, quality vector, solver state, thermo counters, and pin count.

- [x] Implement the smallest metric snapshot/trend storage needed by the failing CLI/report tests.
- [x] Run the focused CLI tests and confirm the report schema passes.
- [x] Implement the interactive overlay and HUD pin/thermo fields.
- [x] Run strict compilation and the focused CLI tests again.

### Task 3: Make quality and learning mode-aware

**Files:**
- Modify: `wfc.c` quality profile selection and thermo feedback serialization.
- Modify: `wfc_learning.py`
- Modify: `wfc_thermo.py`
- Modify: `tests/test_wfc_learning.py`
- Modify: `tests/test_protocol_contract.py`

**Interfaces:**
- Add explicit quality profile labels/weights for `streets`, `neurons`, `mycelium`, `delta`, and the generic fallback.
- Extend feedback with `quality_focus` and a finite `metrics` object.
- Extend learner state with a maximum-64 `metrics_history` list and validate every stored metric as finite and in range.
- Worker `learn` emits `metrics` and `metrics_history` length while retaining all existing fields.

- [x] Add failing learner tests for metric snapshots, bounds, persistence, and worker echo.
- [x] Run the tests to observe the expected failures.
- [x] Implement validated metric history and pass the C quality vector through the worker.
- [x] Run learning/protocol tests and full Python syntax checks.
- [x] Refactor only after the tests are green, preserving the protocol’s old fields.

### Task 4: Add pinned world-studio constraints

**Files:**
- Modify: `wfc.c` grid allocation/reset, sculpt handling, rendering, world save/load, report output, and cleanup.
- Modify: `tests/test_quality_studio.py`
- Modify: `README.md`

**Interfaces:**
- Add `studio_pin_` and `studio_tile_` arrays sized with the grid.
- Add `studio_pin_cell(int x, int y)` and `studio_unpin_cell(int x, int y)` with transactional propagation and user notes.
- Uppercase `P` toggles the hovered cell; right-click unpins before carving.
- World save version remains readable: old five-word headers load with zero pins; new saves append pin count and pin records.

- [x] Add a CLI/report test for zero pins and a source/runtime contract test for the new pin control.
- [x] Run the focused test and confirm it fails before production changes.
- [x] Implement pin state, marker rendering, reset/cleanup, and backwards-compatible save/load.
- [x] Run focused tests, strict compilation, and an interactive pin/undo smoke test.

### Task 5: Add and polish the `delta` world

**Files:**
- Modify: `wfc.c` mode list, builders, setup, animation, palettes, terminal/image rendering, audio tables, help, and mode-specific post-processing.
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `tests/test_quality_studio.py`

**Interfaces:**
- Add `delta` as the 25th mode without renumbering existing modes.
- Reuse connector tiles through `build_connector_world`, but select distinct weights and non-toroidal boundary masks.
- Add `network_color`/image branches for estuary blues and animated downstream glints.
- Add delta-specific topology weights and ensure collage/gallery/ambient arrays include a valid entry.

- [x] Add the delta list/solve/quality tests and run them to confirm they fail before implementation.
- [x] Implement the mode across every mode-indexed surface.
- [x] Run all-mode smoke, collage, quality, strict, and sanitizer checks.
- [x] Inspect the delta output visually in Terminal.app and adjust only evidence-backed rendering issues.

### Task 6: Final verification and delivery

**Files:**
- Modify: `Makefile` only if a verification command needs to be wired.
- Modify: `README.md` for final controls/report documentation.

- [x] Run `make strict`, `make test`, `make regression`, `make python-check`, `make learning-check`, `make protocol-check`, `make bridge-check`, `make quality-check`, `make studio-check`, and `make fuzz` fresh on the final tree.
- [x] Run the report, delta, thermo, pin, reset, and no-learn checks independently and inspect exit codes/output.
- [x] Run a security review of the final diff and resolve any actionable finding.
- [ ] Commit the verified work on `codex/adaptive-thermo` and push it to `origin`.
- [ ] Launch a fresh latest build only through macOS Terminal.app and leave it open for testing.
- [ ] Report exact verification evidence, commit/push state, Terminal.app launch, and any remaining limitations.
