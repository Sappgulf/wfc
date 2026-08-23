# wfc — wave function collapse, animated in your terminal

Procedural worlds grown live in your terminal. One file of C, one dependency (`-lz`), one `make`.

## Build & run

```sh
make
./wfc
```

## The eleven worlds

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

Press `m` to cycle, `z` for the all-worlds sheet, `r` for the raytraced
heightfield view of any solved world.

## Keys

`space` new · `m` mode · `y` theme · `z` sheet · `[` / `]` density ·
`+` / `-` speed · `I` slow-mo · `p` pause · `e` entropy view ·
`g` gif · `c` auto-cycle · `s` save png · `a` audio · `o` share link ·
`v` clipboard shot · `k` CRT · `f` drift cam · `W`/`L` world save/load ·
`u` undo · `h` help · `q` quit

Mouse: left-click seed, right-click carve, drag paint, hover inspect.

## Flags

```
--mode M     circuit|terrain|truchet|fire|waves|dungeon|maze|galaxy|city|aurora|matrix
--w/--h N    grid cells (auto-fit by default)
--seed N|w   numeric or word seed
--speed N    collapse steps/sec
--infinite   ever-growing world
--twin/--quad 2 or 4 worlds at once
--gfx/--no-gfx  iTerm2/WezTerm/kitty/ghostty pixel rendering
--gif/--save/--zoom N  exports
--gallery out.html  all-mode web showcase
--collage out.png    3x3 mosaic of all worlds
--bench      performance table
--pan        camera drift
--daycycle   terrain dawn/dusk
--sound      synth sfx
--no-bloom / --no-weather  kill switches
--once       exit after one map
```

Environmental memory: `~/.wfcrc` remembers mode/theme/speed/density/audio/CRT.

## The raymarcher

`r` toggles a live ray-marched view of the solved grid: the collapsed map
becomes a heightfield with soft shadows, ambient occlusion, fresnel,
horizon fog, and a sun-disk — rendered per-dot into braille at ~26fps.
Albedo comes from the active mode's palette (ray-traced fires, nebulae, seas).

## Test discipline

- 11/11 modes solve first-try across seed sweeps
- AddressSanitizer + UBSan clean on solver, exports, all interactive paths
- Dirty-diff rendering: only changed cells repaint, frames are
  synchronized-output wrapped
- `--bench` prints a per-mode performance table

~3,500 lines of C. `cc -O2 -std=c11 -o wfc wfc.c -lz` if you hate make.
