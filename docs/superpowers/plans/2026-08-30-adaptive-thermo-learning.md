
# Adaptive Thermo Learning and New World Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn thermo into a persistent, quality-driven online learner that proposes safe incremental WFC assignments while adding streets, neurons, mycelium, and delta worlds.

**Architecture:** Keep hard domains, compatibility, propagation, rollback, and rendering authoritative in wfc.c. Add a persistent JSON-lines worker protocol to wfc_thermo.py; it samples fixed-shape THRML chains in rounds, receives deterministic quality feedback, and updates bounded per-mode tile, pair, and context biases. Add a pure standard-library wfc_learning.py module for profile math and persistence, then integrate the learned terms into the existing C/Python bridge without changing classic behavior.

**Tech Stack:** C11, zlib, POSIX pipes/processes/signals, Python 3 standard library, NumPy/JAX/THRML when thermo is enabled, Make, shell regression tests, AddressSanitizer, UBSan.

**Spec:** docs/superpowers/specs/2026-08-30-adaptive-thermo-design.md

## Global Constraints

- wfc.c remains the source of truth for domains, propagation, rollback, and compatibility.
- Do not learn or mutate cdir_ hard compatibility masks.
- Do not add a neural-network training dependency or a remote service.
- Do not make thermo required to build or run the classic solver.
- Do not combine thermo with twin, quad, or infinite-world modes in the first implementation.
- Keep the classic path deterministic and keep the existing headless, sanitizer, export, and interactive contracts intact.
- Profiles are versioned, fingerprinted per mode, bounded, resettable, and best-effort to load/save.
- Interactive launches and user testing use macOS Terminal.app only.
- Preserve the existing uncommitted audit changes; stage only files belonging to the implementation commits.

## File Map

- Modify wfc.c: append the three mode names, build their tiles, render them, calculate deterministic quality metrics, manage the persistent child process, validate incremental patches, expose learning controls/HUD, and preserve classic fallback.
- Modify wfc_thermo.py: turn the one-shot solver into a persistent JSONL worker, maintain THRML chain state, emit round proposals/statistics, apply adaptive annealing, and call the pure learner.
- Create wfc_learning.py: dependency-light learner state, feature updates, profile fingerprinting, validation, atomic persistence, and reset behavior.
- Modify Makefile: add pure learner/protocol checks and include the new mode and adaptive fallback checks in the existing gates.
- Modify README.md: document the three worlds, adaptive thermo, learning controls, profile location/override, deterministic no-learning mode, and Terminal.app test command.
- Create tests/test_wfc_learning.py: standard-library unit tests for update math and profile persistence.
- Create tests/fake_thermo.py: deterministic JSONL sidecar used to exercise the C bridge without NumPy/JAX/THRML.
- Create tests/test_protocol_contract.py: subprocess-level tests for the fake worker handshake, patch, feedback, malformed command, and stop behavior.

## Interfaces Locked Before Implementation

### Pure learner module

wfc_learning.py exposes these functions using JSON-compatible dictionaries and
lists so the sidecar can pass values directly to JAX and the tests can run with
Python's standard library alone:

~~~python
PROFILE_VERSION = 1

def tile_fingerprint(mode, tiles, cdir):
    """Return a stable 16-hex schema fingerprint."""

def new_state(ntiles, pair_count, context_count=8):
    """Return a zeroed learner state with baseline and observation count."""

def update_state(state, reward, tile_events, pair_events, context_events,
                 learning_rate=0.06, decay=0.995):
    """Mutate and return a bounded state; all biases remain in [-2.5, 2.5]."""

def load_profile(path, mode, fingerprint, ntiles, pair_count,
                 context_count=8):
    """Load a matching profile or return a fresh state on any invalid input."""

def save_profile(path, mode, fingerprint, state):
    """Write a validated profile through a private temporary file and rename."""

def reset_state(state):
    """Return a fresh zeroed state preserving the expected array dimensions."""
~~~

### Sidecar protocol

Every message has v: 1 and a short t discriminator. Parent commands are:

~~~json
{"v":1,"t":"init","mode":"neurons","w":20,"h":12,"ntiles":16,
 "seed":7,"torus":false,"smooth":false,"unary":[...],"cdir":[...],
 "domains":[...],"learn":true,"profile_dir":"/Users/.../.wfc-thermo",
 "reset_profile":false}
{"v":1,"t":"sample","domains":[...],"budget":12,"beta_target":5.0}
{"v":1,"t":"feedback","reward":0.18,"quality":0.62,"accepted":9,
 "rejected":1,"contradictions":1,"tile_events":[...],"pair_events":[...],
 "context_events":[...],"final":false}
{"v":1,"t":"finish","quality":0.81,"assignments":[...]}
{"v":1,"t":"reset"}
{"v":1,"t":"stop"}
~~~

Child events are:

~~~json
{"v":1,"t":"ready","schema":1}
{"v":1,"t":"stats","beta":2.4,"energy":-91.0,"bad":4,
 "confidence":0.58,"reward":0.0,"observations":12}
{"v":1,"t":"proposal","patch":[{"i":42,"tile":7,"p":0.94}],
 "beta":3.1,"energy":-104.2,"bad":1}
{"v":1,"t":"learn","tile_bias":[...],"pair_bias":[...],
 "context_bias":[...],"observations":13}
{"v":1,"t":"done","valid":1,"quality":0.81,"cfg":[...]}
{"v":1,"t":"fatal","why":"..."}
~~~

Flat arrays are validated for exact lengths. pair_bias is flattened as
direction * ntiles * ntiles + source * ntiles + destination; context_bias has
eight entries for degree buckets 0..4, boundary/interior, and field/network mode
class. A proposal patch is sorted by descending confidence and contains no more
than 128 assignments.

---

### Task 1: Establish the learner module and its pure test contract

**Files:**
- Create: wfc_learning.py
- Create: tests/test_wfc_learning.py
- Modify: Makefile

**Interfaces:**
- Produces the learner functions listed above.
- Consumes only Python standard-library modules: hashlib, json, math, os, and
  tempfile/explicit sibling path handling.
- Later sidecar tasks consume state dictionaries with keys version, mode,
  fingerprint, observations, baseline, tile_bias, pair_bias, context_bias, and
  quality_history.

- [ ] **Step 1: Write the failing learner tests**

Add tests that assert the exact update and persistence contract:

~~~python
class LearnerTests(unittest.TestCase):
    def test_positive_reward_increases_observed_features(self):
        state = new_state(4, 16)
        update_state(state, 1.0, [{"tile": 2, "value": 1.0}],
                     [{"index": 5, "value": 1.0}],
                     [{"index": 3, "value": 1.0}])
        self.assertGreater(state["tile_bias"][2], 0.0)
        self.assertGreater(state["pair_bias"][5], 0.0)
        self.assertGreater(state["context_bias"][3], 0.0)

    def test_negative_reward_decreases_features_and_updates_baseline(self):
        state = new_state(2, 8)
        update_state(state, -1.0, [{"tile": 1, "value": 1.0}], [], [])
        self.assertLess(state["tile_bias"][1], 0.0)
        self.assertLess(state["baseline"], 0.0)
        self.assertEqual(state["observations"], 1)

    def test_biases_are_clamped_and_decay_is_applied(self):
        state = new_state(1, 1)
        for _ in range(200):
            update_state(state, 1.0, [{"tile": 0, "value": 100.0}], [], [])
        self.assertLessEqual(state["tile_bias"][0], 2.5)
        state["tile_bias"][0] = 2.5
        update_state(state, 0.0, [], [], [], decay=0.5)
        self.assertLess(state["tile_bias"][0], 2.5)

    def test_profile_fingerprint_mismatch_returns_fresh_state(self):
        path = self.tmp_path / "profile.json"
        save_profile(str(path), "streets", "aaaa", new_state(4, 64))
        loaded = load_profile(str(path), "streets", "bbbb", 4, 64)
        self.assertEqual(loaded["observations"], 0)
        self.assertEqual(loaded["tile_bias"], [0.0] * 4)

    def test_corrupt_profile_is_ignored(self):
        path = self.tmp_path / "profile.json"
        path.write_text("{not json", encoding="utf-8")
        loaded = load_profile(str(path), "neurons", "abcd", 16, 1024)
        self.assertEqual(loaded["observations"], 0)

    def test_save_is_atomic_and_validated(self):
        path = self.tmp_path / "profile.json"
        state = new_state(3, 12)
        save_profile(str(path), "mycelium", "cafe", state)
        payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["version"], PROFILE_VERSION)
        self.assertEqual(payload["fingerprint"], "cafe")
        self.assertFalse((self.tmp_path / "profile.json.tmp").exists())
~~~

- [ ] **Step 2: Run the focused tests and verify they fail**

Run:

~~~bash
python3 -m unittest tests.test_wfc_learning -v
~~~

Expected: import/function failures because wfc_learning.py does not exist.

- [ ] **Step 3: Write minimal implementation**

Implement the update formula exactly as specified:

~~~python
delta = max(-1.0, min(1.0, float(reward) - state["baseline"]))
state["baseline"] = max(-1.0, min(1.0,
    state["baseline"] * 0.95 + float(reward) * 0.05))
for item in events:
    index = int(item["index"])
    feature = max(-1.0, min(1.0, float(item.get("value", 1.0))))
    values[index] = max(-2.5, min(2.5,
        values[index] * decay + learning_rate * delta * feature))
~~~

Use strict dimension checks, reject non-finite numbers, canonicalize the mode
and fingerprint into the JSON payload, create the profile directory with mode
0700, write a sibling temporary file with mode 0600, flush and fsync when
available, then replace the destination with os.replace.

- [ ] **Step 4: Run the focused tests and verify they pass**

Run the same unittest command. Expected: all six tests pass.

- [ ] **Step 5: Add the pure learner Make target**

Add this target without changing the existing test, regression, or fuzz behavior:

~~~make
learning-check:
	@python3 -m unittest tests.test_wfc_learning -v
	@echo "learning: profile/update contract OK"
~~~

Run make learning-check and confirm a zero exit status.

### Task 2: Add deterministic C-side quality metrics

**Files:**
- Modify: wfc.c near the WFC core and quality-related globals
- Modify: Makefile

**Interfaces:**
- Produces:
  typedef struct QualityMetrics { double total, validity, coverage,
  diversity, smoothness, stability, topology; } QualityMetrics;
  static QualityMetrics quality_measure(bool final_map);
  static double quality_reward(QualityMetrics before, QualityMetrics after,
  int accepted, int rejected);
- Consumes current dom_, tiles_, cdir_, W_, H_, and mode metadata.
- Later C thermo code calls these functions after each accepted patch and once
  at completion.

- [ ] **Step 1: Add a debug-only regression expectation**

Extend the headless regression recipe with a deterministic debug run:

~~~make
debug_quality=$$(WFC_DEBUG=1 ./wfc --mode streets --seed 7 --w 8 --h 6 --once 2>&1); \
echo "$$debug_quality" | grep -q 'quality=';
~~~

Run make regression and verify it fails because no quality line exists.

- [ ] **Step 2: Implement the shared metric structure and safe partial-map scan**

For every cell, treat an empty domain as invalid, a singleton as decided, and
a multi-state domain as unresolved. Compute:

~~~c
coverage = (double)decided / (double)(W_ * H_);
validity = empty_cells == 0 ? 1.0 : 0.0;
~~~

For singleton neighbor pairs, count compatibility through cdir_; never call
__builtin_ctzll unless the mask is nonzero. Compute normalized distribution
entropy over tile IDs and band smoothness from neighboring singleton edge
values. Use fixed constants and no wall-clock/random input.

- [ ] **Step 3: Add network topology and mode-aware aggregation**

For connector modes, derive degree from the four edge values and score branch,
endpoint, component, and loop balance. Keep the scan bounded by W_ * H_ and
reuse comp_ only after the map is complete; partial scoring uses degree and
local compatibility only. Aggregate with these fixed weights:

~~~c
total = 0.30 * validity + 0.18 * coverage + 0.16 * diversity +
        0.16 * smoothness + 0.20 * topology;
~~~

Clamp every public metric to [0, 1].

- [ ] **Step 4: Implement normalized patch reward and debug output**

Use:

~~~c
double improvement = after.total - before.total;
double acceptance = accepted / (double)(accepted + rejected + 1);
double penalty = rejected > 0 ? 0.08 * rejected : 0.0;
return clampd(2.0 * improvement + 0.20 * acceptance - penalty, -1.0, 1.0);
~~~

Print one WFC_DEBUG line containing quality, validity, coverage, diversity,
smoothness, and topology after headless completion. Do not print it on normal
stdout.

- [ ] **Step 5: Run C validation**

Run:

~~~bash
make strict
make regression
~~~

Expected: both pass, with the new debug quality assertion included.

### Task 3: Convert the Python sidecar into a persistent worker

**Files:**
- Modify: wfc_thermo.py
- Modify: wfc_learning.py only where the sidecar contract needs a pure helper
- Create: tests/test_protocol_contract.py
- Create: tests/fake_thermo.py

**Interfaces:**
- Produces:
  def worker_loop(input_stream=sys.stdin, output_stream=sys.stdout):
  class ThermoSession with init(command), sample(command), feedback(command),
  reset(), and finish(command) methods.
- Consumes the protocol messages locked above.
- C bridge task consumes ready, stats, proposal, learn, done, and fatal events.

- [ ] **Step 1: Make dependency loading lazy and test the worker with a fake**

Change module import behavior so importing protocol helpers does not call
sys.exit when THRML is absent. Store the import exception in RUNTIME_ERROR;
main() emits fatal only when a sampling command requires the runtime. The fake
worker uses only standard library JSON parsing.

Write the first contract test:

~~~python
def test_fake_worker_handshake_and_stop(self):
    proc = subprocess.Popen(
        [sys.executable, "tests/fake_thermo.py"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True)
    proc.stdin.write(json.dumps({"v": 1, "t": "init", "mode": "streets",
        "w": 4, "h": 3, "ntiles": 4, "seed": 1, "domains": [15] * 12,
        "cdir": [[[15] * 4] * 4] * 4, "unary": [1] * 4}) + "\n")
    proc.stdin.flush()
    self.assertEqual(json.loads(proc.stdout.readline())["t"], "ready")
    proc.stdin.write('{"v":1,"t":"stop"}\n')
    proc.stdin.flush()
    proc.wait(timeout=2)
    self.assertEqual(proc.returncode, 0)
~~~

- [ ] **Step 2: Run the contract test and verify it fails**

Run:

~~~bash
python3 -m unittest tests.test_protocol_contract -v
~~~

Expected: worker script missing and/or no ready event.

- [ ] **Step 3: Implement ThermoSession initialization and profile loading**

On init, validate dimensions, tile count, masks, domains, seed, profile
directory, and exact array lengths. Build edges and fixed compatibility weights
once. Calculate a fingerprint from mode/tile edge metadata/cdir, load the
matching profile with load_profile, and emit:

~~~python
emit({"v": 1, "t": "ready", "schema": 1})
~~~

Do not construct a JAX program until the first sample command; this keeps the
handshake testable and makes a malformed command fail cleanly.

- [ ] **Step 4: Implement round sampling and proposal extraction**

Refactor the existing potts_solve body into reusable session methods:

~~~python
def sample(self, command):
    """Run bounded sweeps, update chain state, emit stats and a patch."""
~~~

Keep chain arrays shaped (chains, cells) across rounds. Encode current domain
masks as invalid unary states, project/reseed a chain cell if its old tile is no
longer allowed, and reuse the compiled factor graph when dimensions and tile
count are unchanged. Do not build all_steps; inspect each round's final and
elite states directly.

For each cell, compute majority tile confidence across chains. Emit only cells
with confidence >= 0.72, cap at 128 entries, and sort descending by confidence.
Emit stats before proposal even when the patch is empty.

- [ ] **Step 5: Implement feedback, adaptive beta, and worker commands**

On feedback, call update_state with the event arrays, update the retained elite
states, and choose the next beta using these bounds:

~~~python
if bad < self.last_bad and confidence >= self.last_confidence:
    self.beta_target = min(self.beta_max, self.beta_target * 1.12)
elif bad > self.last_bad or accepted == 0:
    self.beta_target = max(self.beta0, self.beta_target * 0.82)
~~~

Handle reset, finish, and stop. finish saves the profile only when learning is
enabled and emits a valid done event. Any exception emits one fatal line and
exits nonzero; diagnostics go to stderr.

- [ ] **Step 6: Implement the deterministic fake worker**

The fake worker must implement the same command/event schema without optional
dependencies. On each sample, choose the first unresolved cell and the first
candidate tile that remains locally compatible with every currently allowed
neighbor, emit a one-cell proposal with confidence 1.0, and emit done once all
cells are singleton. On feedback, increment observations; on malformed JSON or
unknown commands, emit fatal and exit 1.

- [ ] **Step 7: Run Python protocol tests and syntax checks**

Run:

~~~bash
python3 -m unittest tests.test_protocol_contract -v
python3 -m py_compile wfc_learning.py wfc_thermo.py tests/fake_thermo.py
~~~

Expected: all protocol tests pass.

### Task 4: Add transactional C patch application and the bidirectional bridge

**Files:**
- Modify: wfc.c in the thermo bridge and propagation core
- Modify: Makefile

**Interfaces:**
- Produces:
  typedef struct DomainTxn { int *idx; uint64_t *old; size_t len, cap;
  uint32_t *marks; uint32_t generation; } DomainTxn;
  static bool propagate_txn(DomainTxn *txn, int start);
  static void txn_rollback(DomainTxn *txn);
  static int thermo_apply_patch(const char *line, int *accepted,
  int *rejected);
  static bool thermo_send_command(const char *line);
- Consumes the persistent worker protocol from Task 3.
- Later interactive code consumes thermo_apply_patch and quality feedback.

- [ ] **Step 1: Add a regression fixture for patch rollback**

Add a fake-worker scenario that sends two patch entries: the first is locally
safe and the second contradicts it. The expected headless run must finish with a
valid map and report one accepted and one rejected proposal in WFC_DEBUG.

Run the new fixture before implementation and verify it fails because the C
bridge still expects one done line and has no transaction type.

- [ ] **Step 2: Implement DomainTxn recording and rollback**

Record a cell's original mask once per transaction before the first write. Use a
generation-mark array sized to W_ * H_ so propagation updates do not duplicate
records. txn_rollback restores masks in reverse order and resets the length;
successful transactions discard the record list without changing domains.

- [ ] **Step 3: Implement transactional propagation**

Copy the existing neighbor logic into propagate_txn, but replace direct writes
with:

~~~c
if (nd != dom_[n]) {
    if (!txn_record(txn, n, dom_[n])) return false;
    dom_[n] = nd;
    stk_[sp++] = n;
}
~~~

Return false on empty masks. Leave existing propagate_from unchanged for classic
paths and mouse sculpting.

- [ ] **Step 4: Implement strict patch parsing and application**

Parse only proposal.patch entries, validate integer cell/tile fields and finite
confidence in [0,1], reject out-of-range or disallowed assignments, and attempt
entries in emitted order. For each entry, start a fresh transaction, set the
singleton, run propagate_txn, commit on success, and rollback on failure. Update
g_decided after the patch. Never accept a tile not present in the current domain.

- [ ] **Step 5: Replace one-way child setup with two pipes**

Add thermo_in_fd_ and thermo_in_ state. thermo_launch creates parent-write and
child-read pipes plus the existing child-write/parent-read pipe, forks, and
executes the configured Python interpreter. The child redirects stdin/stdout,
keeps stderr at /tmp/wfc_thermo_err.log, and enters worker_loop.

Set the parent output FD nonblocking and the input stream line-buffered. Close
both ends on every fork failure, fdopen failure, timeout, mode change, stop,
and successful completion. Reap the child before clearing profile/spec state.

- [ ] **Step 6: Implement command sending and event parsing**

Add bounded JSON-line construction for init, sample, feedback, finish, reset,
and stop. Send the full current domains in init and every sample command so C
remains authoritative. Extend thermo_poll to parse ready, stats, proposal,
learn, done, and fatal; unknown events are ignored.

If a line exceeds the 32 MiB buffer, if a numeric field is malformed, or if the
child closes unexpectedly, call thermo_kill, set the fallback note, and return
the existing failure status.

- [ ] **Step 7: Run bridge and sanitizer checks**

Run:

~~~bash
make wfc
WFC_PYTHON=python3 WFC_THERMO_PY=tests/fake_thermo.py \
  WFC_DEBUG=1 ./wfc --solver thermo --mode streets --seed 7 --w 8 --h 6 --once
make asan
WFC_PYTHON=python3 WFC_THERMO_PY=tests/fake_thermo.py \
  ASAN_OPTIONS=detect_leaks=0 ./wfc_asan --solver thermo --mode streets \
  --seed 7 --w 8 --h 6 --once >/dev/null
~~~

Expected: the fake worker performs multiple incremental proposals, the map is
valid, and ASan/UBSan report no errors.

### Task 5: Integrate online quality feedback and adaptive thermo into the live loop

**Files:**
- Modify: wfc.c in thermo state, main interactive loop, HUD, and cleanup
- Modify: wfc_thermo.py for event arrays and final scoring
- Modify: Makefile

**Interfaces:**
- Produces thermo_feedback_round, thermo_status, and a round-based interactive
  path that accepts safe proposals while rendering.
- Consumes quality_measure, quality_reward, thermo_apply_patch, and worker
  commands/events.

- [ ] **Step 1: Add adaptive state and CLI flags**

Add C state for g_thermo_learn (default true), g_thermo_profile_dir[512],
g_thermo_reset_profile, thermo_ready_, thermo_beta_, thermo_energy_,
thermo_confidence_, thermo_reward_, thermo_quality_, thermo_observations_, and
thermo_round_. Parse:

~~~text
--no-learn             disable updates and profile disk I/O
--thermo-profile DIR   override the per-mode profile directory
--reset-learning       start this mode with a zeroed profile
~~~

Reject a missing profile path, an empty path, or a path longer than 480 bytes
with exit code 2. Keep classic runs dependency-free.

- [ ] **Step 2: Send initial state and start round sampling**

When a single-world interactive thermo map starts, launch the worker, send init,
and wait for ready while rendering the unresolved grid. Once ready, send sample
with budget 12 and the current beta target. Do not call the old one-shot
thermo_poll path.

- [ ] **Step 3: Apply proposals and send feedback**

When a proposal event arrives:

~~~c
QualityMetrics before = quality_measure(false);
int accepted = 0, rejected = 0;
thermo_apply_patch(line, &accepted, &rejected);
QualityMetrics after = quality_measure(false);
double reward = quality_reward(before, after, accepted, rejected);
thermo_send_feedback(reward, after, accepted, rejected);
~~~

Include tile/pair/context event arrays for accepted assignments. Update the HUD
state, increment the visual step counter, and request the next sample using the
current domains. A proposal with no accepted cells still produces feedback and
another round, subject to the timeout and round cap.

- [ ] **Step 4: Finish maps and preserve fallback**

When dom_ is fully singleton, compute the final quality score, send finish, wait
for done, run existing component/river post-passes, and retain the learned
profile. If thermo emits fatal, times out, or cannot create a round, kill the
child and resume the classic solver for the current attempt. Do not leave
half-applied domains on fallback.

- [ ] **Step 5: Add the thermo HUD and controls**

Add R to reset the active learner through the worker and clear local metrics.
Show a compact line in the terminal renderer only when thermo is active:

~~~text
THERMO adaptive  beta 3.10  energy -104.2  bad 1  conf 94%  q 0.62  r +.18  learn 13
~~~

In entropy view, use thermo confidence for cells with proposals and domain
entropy for all other cells. Document the control in render_help.

- [ ] **Step 6: Test the live path through Terminal.app**

Build and launch the adaptive fake-worker path in a real macOS Terminal window:

~~~bash
make
osascript -e 'tell application "Terminal" to activate' \
  -e 'tell application "Terminal" to do script "cd /Users/austinbeatty/Downloads/0x && WFC_PYTHON=python3 WFC_THERMO_PY=tests/fake_thermo.py ./wfc --solver thermo --mode streets --w 24 --h 12"'
~~~

Verify visually that multiple proposals are accepted during one map, the HUD
changes over time, R resets observations, q exits cleanly, and the terminal
returns to the shell. Do not launch this interactive path through a Codex PTY.

### Task 6: Add the four connector worlds and their quality metadata

**Files:**
- Modify: wfc.c mode list, tile builders, setup metadata, animation clock,
  boundary initialization, and quality classification
- Modify: README.md mode table and flags once rendering is complete

**Interfaces:**
- Produces build_streets, build_neurons, build_mycelium, build_delta,
  mode_is_network, and mode_name behavior with mode indices appended after the
  original 21 modes.
- Consumes the existing Tile edge/flag representation and build_compat.

- [ ] **Step 1: Add mode names without renumbering existing worlds**

Change the mode count to 25 and append exactly:

~~~c
"streets", "neurons", "mycelium", "delta"
~~~

Do not reorder the original entries; this preserves existing saved mode indices
and remembered configuration behavior.

- [ ] **Step 2: Add shared connector tile helpers**

Add a helper that creates all 16 four-edge masks with a supplied weight and
renderer flag. Define degree as the count of nonzero edges. Compatibility remains
exact edge equality through build_compat(false).

- [ ] **Step 3: Implement street tiles and boundaries**

build_streets uses all 16 masks with these base weights by degree/mask class:

~~~c
degree 0: 30.0
degree 1: 1.4
degree 2 straight: 6.0
degree 2 corner: 4.5
degree 3: 2.2
degree 4: 0.7
~~~

Mark degree-3/4 tiles as intersections. Set non-torus mode behavior and remove
outward-facing road connectors at the four grid boundaries during grid_reset.

- [ ] **Step 4: Implement neuron tiles and metadata**

build_neurons uses sparse degree-1/2 filaments, moderate degree-3 branches,
and rare degree-4 soma hubs. Set renderer flags for degree-3/4 hubs and a small
subset of degree-1 synapse endpoints. Use non-torus boundaries and a softer
edge-density penalty in quality scoring.

- [ ] **Step 5: Implement mycelium tiles and metadata**

build_mycelium favors degree-1/2 organic filaments, degree-3 branch clusters,
and rare degree-0 fruiting/spore cells. Give branch and fruiting variants flags
without changing edge compatibility. Use non-torus boundaries and reward
multiple components with varied branch lengths.

- [ ] **Step 6: Add mode setup and animation entries**

Dispatch the builders in setup_mode, classify all three as network modes, add
  animation epochs at 140 ms for streets, 100 ms for neurons, 180 ms for
  mycelium, and 145 ms for delta, and ensure g_torus, g_smooth, g_bulk_idx,
  and hero state are initialized deterministically.

- [ ] **Step 7: Run classic mode coverage**

Run:

~~~bash
make test
for m in streets neurons mycelium delta; do
  ./wfc --mode "$m" --seed 7 --w 40 --h 20 --once >/dev/null
done
~~~

Expected: all 25 modes solve headlessly and the four new modes return status 0.

### Task 7: Add renderers, image exports, and quality-specific visuals

**Files:**
- Modify: wfc.c paint_cell, terminal art helpers, img_px, and shared network
  animation helpers
- Modify: README.md

**Interfaces:**
- Produces network_mask, network_degree, network_color, and renderer branches
  for streets, neurons, mycelium, and delta in both terminal and raster paths.
- Consumes tile flags, anim_epoch, g_seed, g_hover_x/y, and the existing PNG/GIF
  pipeline.

- [ ] **Step 1: Add shared network geometry helpers**

Implement one geometry helper that maps the four connector bits to an 8x8
pixel mask with a two-pixel trunk, endpoint caps, and a four-pixel center. Use
the same mask for braille and raster rendering so terminal and exported images
agree structurally.

- [ ] **Step 2: Implement streets visuals**

Render asphalt in dark blue-gray, sidewalks/buildings in alternating blocks,
yellow/white lane markings along connectors, intersection paint, windows, and
slow deterministic headlight pulses derived from g_seed, cell index, and
anim_epoch. Keep rendering free of random-state mutation.

- [ ] **Step 3: Implement neurons visuals**

Render a deep violet/black background, amber/teal axons, brighter flagged soma
hubs, endpoint synapses, and pulses moving from a hash-derived phase along each
connector. Avoid unbounded brightness accumulation; compute color from the
current epoch only.

- [ ] **Step 4: Implement mycelium visuals**

Render an earthy background, moss/amber filaments, pale fruiting nodes, and
slow spore motes from deterministic hash positions. Keep the geometry readable
at both normal and zoomed terminal scales.

- [ ] **Step 5: Exercise exports and views**

Run:

~~~bash
for m in streets neurons mycelium delta; do
  ./wfc --mode "$m" --seed 9 --w 24 --h 12 --once --save "/tmp/$m.png" >/dev/null
  test -s "/tmp/$m.png"
done
./wfc --gallery /tmp/wfc-24-gallery.html >/dev/null
test -s /tmp/wfc-24-gallery.html
./wfc --collage /tmp/wfc-24-collage.png >/dev/null
test -s /tmp/wfc-24-collage.png
~~~

- [ ] **Step 6: Run sanitizer export checks**

Run the new modes through wfc_asan with PNG/GIF export and inspect stderr for
ASan/UBSan reports. Expected: zero sanitizer diagnostics and nonempty files.

### Task 8: Wire CLI, persistence, documentation, and regression targets

**Files:**
- Modify: wfc.c CLI help/config/cleanup
- Modify: README.md
- Modify: Makefile
- Modify: .gitignore only if a new local test artifact is generated inside the repository

**Interfaces:**
- Produces documented --no-learn, --thermo-profile DIR, and --reset-learning
  behavior plus deterministic profile controls.
- Consumes the persistent worker/profile contract.

- [ ] **Step 1: Add and validate CLI help**

Add the flags to both the parser and help text. Reject a missing profile path, an
empty path, or a path longer than 480 bytes with exit code 2. Keep the default
profile directory as ~/.wfc-thermo inside Python unless overridden.

- [ ] **Step 2: Preserve deterministic classic/no-learning behavior**

Add regression commands:

~~~make
a=$$(./wfc --mode streets --seed 0 --w 8 --h 6 --once --no-learn); \
b=$$(./wfc --mode streets --seed 0 --w 8 --h 6 --once --no-learn); \
test "$$a" = "$$b";
~~~

Also run classic --solver classic with the same seed and verify no profile file is
created and no Python process is launched.

- [ ] **Step 3: Document adaptive behavior and controls**

Update the mode table, flags, thermo section, HUD example, profile location,
profile fingerprint/reset semantics, fallback behavior, and the exact Terminal.app
test command. State that cdir is never learned and --no-learn restores a
reproducible fixed model.

- [ ] **Step 4: Add Make targets for all focused gates**

Add:

~~~make
protocol-check: wfc
	@python3 -m unittest tests.test_protocol_contract -v
	@WFC_PYTHON=python3 WFC_THERMO_PY=tests/fake_thermo.py \
		./wfc --solver thermo --mode streets --seed 7 --w 8 --h 6 --once >/tmp/wfc-protocol.out
	@grep -q '^OK mode=streets ' /tmp/wfc-protocol.out
	@echo "protocol: persistent bridge + patch contract OK"

adaptive-check: learning-check protocol-check
	@echo "adaptive thermo: focused gates OK"
~~~

Add both targets to .PHONY and keep the existing make test regression
python-check fuzz strict asan targets intact.

- [ ] **Step 5: Run focused CLI/documentation checks**

Run:

~~~bash
make learning-check
make protocol-check
./wfc --help | grep -E -- '--no-learn|--thermo-profile|--reset-learning'
./wfc --list-modes | tail -4 | grep -E 'streets|neurons|mycelium|delta'
~~~

### Task 9: Full verification, review, commit, push, and Terminal.app handoff

**Files:**
- Modify only files required by failed verification or final documentation
- Do not stage unrelated pre-existing changes

**Interfaces:**
- Consumes every implementation task's tests and produces the finished binary
  and committed/pushed implementation.

- [ ] **Step 1: Inspect the final worktree and diff boundaries**

Run:

~~~bash
git status --short
git diff --stat
git diff --check
git diff -- Makefile README.md wfc.c wfc_thermo.py wfc_learning.py tests docs/superpowers/plans
~~~

Confirm the earlier audit changes remain included intentionally, the design and
plan commits are present, and no generated images/logs/profile files are staged.

- [ ] **Step 2: Run the complete automated suite**

Run:

~~~bash
make test regression python-check learning-check protocol-check adaptive-check
make strict
make asan
make fuzz
~~~

Expected: all 25 modes, gallery/collage, deterministic seed/argument/export
regressions, Python syntax, learner tests, fake protocol, adaptive patch path,
strict compile, sanitizer build, and 25 random sanitizer combinations pass.

- [ ] **Step 3: Run additional thermo and profile checks when dependencies exist**

If python3 -c 'import numpy,jax,thrml' succeeds, run:

~~~bash
WFC_PYTHON=python3 ./wfc --solver thermo --mode streets --seed 7 --w 12 --h 8 --once
WFC_PYTHON=python3 ./wfc --solver thermo --mode neurons --seed 11 --w 12 --h 8 --once
WFC_PYTHON=python3 ./wfc --solver thermo --mode mycelium --seed 13 --w 12 --h 8 --once
~~~

Confirm the sidecar emits intermediate proposals, final maps are valid, and a
profile is created only when learning is enabled. Run each command once with
--no-learn and confirm the output is byte-for-byte reproducible.

- [ ] **Step 4: Launch the final build only through macOS Terminal.app**

Use the actual computer terminal:

~~~bash
osascript -e 'tell application "Terminal" to activate' \
  -e 'tell application "Terminal" to do script "cd /Users/austinbeatty/Downloads/0x && ./wfc --solver thermo --mode streets --w 32 --h 16"'
~~~

Test m across streets, neurons, mycelium, and delta, T to toggle thermo, R to reset
learning, e for confidence/entropy, s for PNG, g for GIF, and q to quit. Verify
Terminal.app returns to a shell and the process does not remain orphaned.

- [ ] **Step 5: Commit implementation changes in reviewable commits**

Use separate commits for the pure learner/profile, persistent bridge/quality
loop, and new modes/UX:

~~~bash
git add wfc_learning.py tests/test_wfc_learning.py
git commit -m "feat: add bounded thermo learning profiles"

git add wfc.c wfc_thermo.py tests/fake_thermo.py tests/test_protocol_contract.py Makefile
git commit -m "feat: stream adaptive thermo proposals"

git add wfc.c README.md Makefile
git commit -m "feat: add streets neurons and mycelium worlds"
~~~

If the existing audit changes are still unstaged, do not add them to a commit
unless the commit's diff intentionally contains their related changes. Keep the
design and plan commits intact.

- [ ] **Step 6: Push the finished branch and report exact verification**

Inspect remotes and branch first:

~~~bash
git remote -v
git branch --show-current
~~~

Push the current branch only when a configured remote is present:

~~~bash
git push origin "$(git branch --show-current)"
~~~

Report the pushed branch, final commit IDs, the Terminal.app launch command, and
any skipped real-THRML test with the reason.

## Plan Self-Review

- **Spec coverage:** hard/soft separation is covered by Tasks 1, 3, and 4;
  persistent protocol and incremental proposals by Tasks 3–5; three learning
  layers by Tasks 1, 3, and 5; adaptive annealing by Task 3; profiles by Task 1
  and Task 8; scoring by Task 2; all four modes by Tasks 6–7; HUD/controls by
  Tasks 5 and 8; error handling by Tasks 3–4; verification by Task 9.
- **Completeness scan:** every step contains concrete files, interfaces, commands,
  expected outcomes, or exact implementation rules; no unresolved work markers
  remain.
- **Type consistency:** C patch/protocol names are defined before their use;
  Python learner keys and protocol event names match across Tasks 1, 3, 4, and
  5; profile dimensions use the same ntiles, pair_count, and eight-entry
  context_count contract throughout.
- **Scope:** the work is staged into pure learner, quality, bridge, live loop,
  modes, rendering, and verification units. Each unit has a focused test cycle
  and preserves the classic fallback.
