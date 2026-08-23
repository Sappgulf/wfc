# wfc — wave function collapse, in your terminal

Procedural worlds grown live in your terminal. One file of C, zero deps beyond zlib.

## Build

```sh
make                 # cc -O2 -std=c11 -o wfc wfc.c -lz
./wfc
```

## Modes

| mode      | design                                  |
|-----------|-----------------------------------------|
| circuit   | rainbow circuit boards (braille pixels) |
| terrain   | hillshaded biomes + carved rivers        |
| truchet   | two-color woven arcs                    |
| fire      | living flames                           |
| waves     | rolling ocean with foam                 |
| dungeon   | torch-lit catacombs                     |
| maze      | braided labyrinth                       |
| galaxy    | nebula + twinkling stars                |
| city      | night skylines with rain                |

## Keys

`space` new · `m` mode · `y` theme · `z` all-worlds sheet · `[ / ]` density · `+/-` speed ·
`p` pause · `g` gif · `c` auto-cycle · `s` save png · `a` audio · `o` share link ·
`v` clipboard shot · `k` CRT · `f` drift cam · `W/L` world save/load · `u` undo · `h` help · `q` quit

Mouse: left-click seed, right-click carve, drag paint, hover inspect.

## Flags

```
--mode M     circuit|terrain|truchet|fire|waves|dungeon|maze|galaxy|city
--w/--h N    grid cells (auto-fit by default)
--seed N|w   numeric or word seed
--speed N    collapse steps/sec
--infinite   ever-growing world (toroidal modes)
--twin/--quad 2 or 4 worlds at once
--gfx/--no-gfx  iTerm2/kitty pixel rendering
--gif/--save/--zoom N  exports
--gallery out.html  all-mode web showcase
--bench      performance table
--pan        camera drift
--sound      synth sfx
--daycycle   terrain dawn/dusk
--once       exit after one map
```

Environmental memory: ~/.wfcrc remembers mode/theme/speed/density/audio/CRT.
