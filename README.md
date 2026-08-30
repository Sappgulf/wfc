# wfc — wave function collapse, animated in your terminal

Procedural worlds grown live in your terminal. One file of C, one dependency (`-lz`), one `make`.

## Build & run

```sh
make
./wfc
```

## The twenty-four worlds

| mode      | design                                        |
|-----------|-----------------------------------------------|
| circuit   | rainbow circuit boards, signals racing traces |
| terrain   | hillshaded biomes + carved rivers + day cycle |
| truchet   | woven arcs with a travelling light pulse      |
| fire      | living flames with rising embers              |
| waves     | rolling ocean, crest lines, moonpath          |
| dungeon   | torch-lit catacombs with breathing warmth     |
| maze      | labyrinth with wall pulses                    |
| galaxy    | nebulae with shooting stars                   |
| city      | night skylines with beacons + rain            |
| aurora    | drifting green curtains over stars            |
| matrix    | digital rain with white-hot glyph heads       |
| pipes     | water pressure networks, pulses racing runs   |
| mondrian  | painted plazas split by charcoal rules        |
| koi       | pond bands, koi gliding between lily pads     |
| lava      | crusting basalt over a molten breath          |
| sakura    | spring night, blossom petals drifting down    |
| geode     | crystal cavern facets with wandering glints   |
| lantern   | festival sky, lanterns rising past the stars  |
| dunes     | heat shimmer under a fixed blazing sun        |
| reef      | caustic water, coral, bubbles, a fish school  |
| stained   | jewel-glass panes, lead lines, roaming light  |
| streets   | procedural city streets, lanes, signals, and intersections |
| neurons   | branching brain-like dendrites with traveling action pulses |
| mycelium  | living root networks, knots, and drifting spore light |

Press `m` to cycle, `z` for the all-worlds sheet, `r` for the raytraced
heightfield view, `i` for the isometric relief view of any solved world.

## Keys

`space` new · `m` mode · `y` theme · `z` sheet · `[` / `]` density ·
`+` / `-` speed · `I` slow-mo · `p` pause · `e` entropy view ·
`g` gif · `c` auto-cycle · `s` save png · `a` audio · `o` share link ·
`v` clipboard shot · `k` CRT · `f` drift cam · `W`/`L` world save/load ·
`u` undo · `i` iso view · `,` / `.` scrub the collapse in time ·
`n` zen mode · `T` toggle the thermodynamic solver · `R` reset thermo learning ·
`w a s d` hero crawl
through solved dungeons (light all the torches) · `h` help · `q` quit

Mouse: left-click seed, right-click carve, drag paint, hover inspect.

## Flags

```
--mode M     circuit|terrain|truchet|fire|waves|dungeon|maze|galaxy|city|aurora|matrix|pipes|mondrian|koi|lava|sakura|geode|lantern|dunes|reef|stained|streets|neurons|mycelium
--w/--h N    grid cells (auto-fit by default)
--seed N|w   numeric or word seed
--speed N    collapse steps/sec
--infinite   ever-growing world
--twin/--quad 2 or 4 worlds at once
--gfx/--no-gfx  iTerm2/WezTerm/kitty/ghostty pixel rendering
--gif/--save/--zoom N  exports
--gallery out.html  all-mode web showcase
--collage out.png    mosaic of all worlds
--bench      performance table
--pan        camera drift
--daycycle   terrain dawn/dusk
--sound      synth sfx + per-world ambient drones
--solver     classic | thermo[=potts|ising]
--no-learn   disable persistent thermo preferences
--thermo-profile DIR  store thermo profiles in DIR
--reset-learning     clear the active thermo profile before sampling
--theme N    color theme 0-7 (same as y)
--list-modes print all mode names and exit
--zen        worlds dissolve into each other — endless, no restarts
--no-bloom / --no-weather  kill switches
--once       exit after one map
```

Headless runs are intentionally bounded: `--twin`, `--quad`, and
`--infinite` require an interactive terminal. Numeric options are validated
and rejected with exit code 2 when malformed or out of range; `--seed 0` is a
valid deterministic seed. Image exports are capped at 64 million pixels and
return a nonzero exit status if the destination cannot be written.

**Zen mode** (`n`, or `--zen`): when a world finishes it lingers as a ghost
while the next collapse seeds itself on top — the frontier visibly re-weaves
the old world into the new one, scattering away cell by cell with a slow
radial sweep, while the whole field drifts like a slow orbit. No hard
restarts, ever. The ghost fade self-paces to the collapse time. Combine
with `c` (auto-cycle) for an endless morphing slideshow. The preference is
remembered in `~/.wfcrc`.

**Zen GIF loops**: `--gif out.gif --zen` (or `g` then watch, then `q`)
records *across* the morphs and writes one seamless loop when you quit —
the dissolve itself is in the gif.

Environmental memory: `~/.wfcrc` remembers mode/theme/speed/density/audio/CRT.

## The thermodynamic solver

`--solver thermo` (or `T` live) re-runs the *same* WFC problem as a
pairwise energy-based model, sampled with **Extropic's THRML**:

- cells become `CategoricalNode`s, the cdir compatibility table becomes a
  pairwise `CategoricalEBMFactor` energy (+1 compatible, −5 violating),
  tile weights become per-cell unary style preferences,
- block Gibbs with checkerboard coloring runs under an inverse-temperature
  sweep (`beta` 0.2 → 8), many chains in parallel (`vmap` + `lax.scan`),
  and the first fully-valid state wins — the classic solver's retry loop
  becomes hot re-anneals at fresh keys,
- `thermo=ising` reports the **Z1 p-bit budget**: the domain-wall
  thermometer compile of the same model — `cells × (K−1)` spins for an
  unconstrained grid, or the sum of `(allowed states − 1)` — that a
  Thermodynamic Sampling Unit would run natively.

The sidecar is a long-lived JSONL worker: C sends `init`, bounded `sample`
rounds, and measured `feedback`; the worker returns `ready`, `stats`,
incremental `proposal` patches, and `learn` updates. C transactionally applies
each patch through the authoritative propagator and rolls it back on a
contradiction. Tile, compatible-pair, and boundary-context preferences are
bounded, fingerprinted per mode, and atomically persisted in
`~/.wfc-thermo` (or `--thermo-profile DIR`). `--no-learn` keeps a run
ephemeral. The worker validates dimensions, masks, domains, and sampling
budgets before sampling; malformed requests emit a structured `fatal` line
and the C parent falls back to the classic solver.

THRML/JAX is optional. If installed, the legacy one-shot compatibility API can
use it; the persistent worker also has a dependency-free bounded sampler, so
`--solver thermo` remains usable without a Python package install. To enable
the accelerated sampler, install `thrml` and `jax` (see [thrml docs](https://docs.thrml.ai)),
then either run `wfc` from the repo root (it finds `wfc_thermo.py` there), or
point at your venv:

```sh
WFC_PYTHON=/path/to/venv/bin/python ./wfc --solver thermo
```

If the worker itself cannot launch or returns a fatal protocol error, the
solver notes "thermo failed" and falls back to classic automatically — the
project still builds with zero dependencies.

## The raymarcher

`r` toggles a live ray-marched view of the solved grid: the collapsed map
becomes a heightfield with soft shadows, ambient occlusion, fresnel,
horizon fog, and a sun-disk — rendered per-dot into braille at ~26fps.
Albedo comes from the active mode's palette (ray-traced fires, nebulae, seas).

## Test discipline

- 24/24 modes solve first-try across seed sweeps
- AddressSanitizer + UBSan clean on solver, exports, all interactive paths
  (`make asan`, then `./wfc_asan --mode <m> --once --save out.png`; pty via
  `script -q /dev/null ./wfc_asan ...` for the interactive paths)
- Zero warnings under `-Wall -Wextra -Wpedantic -Wshadow` (`make strict`)
- Dirty-diff rendering: only changed cells repaint, frames are
  synchronized-output wrapped
- `--bench` prints a per-mode performance table

~5,800 lines of C, ~900 lines of Python. `cc -O2 -std=c11 -o wfc wfc.c -lz`
if you hate make.
