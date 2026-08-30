/* wfc.c — Wave Function Collapse, animated live in your terminal.
 *
 * Watch a grid of superposed cells collapse into:
 *   circuit : glowing toroidal circuit boards (braille-dot pixel art)
 *   terrain : smooth elevation islands - beaches, forests, snowy peaks
 *   truchet : two-color woven loops (color-matched truchet arcs)
 *   pipes   : water-carrying pressure networks with flowing pulses
 *   mondrian: painted plazas split by charcoal rules
 *   koi     : a pond where koi drift between lily pads
 *   lava    : crusting basalt over a breathing molten field
 *   streets : procedural city streets with signals and crossings
 *   neurons : branching dendrites with live action potentials
 *   mycelium: organic root networks and drifting spores
 *   delta   : branching river mouths, estuaries, and tidal glints
 *
 * Views: r raymarched heightfield, i isometric relief, z all-worlds sheet.
 * Extras: gif/png export, terminal pixel rendering (iTerm/kitty/WezTerm),
 * WASD crawler in solved dungeons, and ',' '.' collapse time-scrubbing.
 *
 * build:  cc -O2 -std=c11 -Wall -Wextra -o wfc wfc.c -lz
 * run:    ./wfc
 * keys:   space new map | m mode | +/- speed | p pause | s save PNG | q quit
 *
 * Piped stdout runs one headless solve and prints stats (for testing).
 */

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#define MAXT 64
#define MAXW 4
#define NDIR 4
#define OPPOSITE(d) (((d) + 2) & 3)

/* ---------------- config ---------------- */
#define NMODES 25
static const char *MODES[NMODES] = {"circuit", "terrain", "truchet", "fire", "waves", "dungeon", "maze", "galaxy", "city", "aurora", "matrix", "pipes", "mondrian", "koi", "lava", "sakura", "geode", "lantern", "dunes", "reef", "stained", "streets", "neurons", "mycelium", "delta"};
static int g_mode_idx = 0;
static int g_user_w = 999, g_user_h = 999;
static uint64_t g_seed = 0;
static bool g_seed_set = false;
static long g_speed = 1600;
static bool g_once = false;
static char g_save_path[512] = {0};
static bool g_save_auto = false;
static double g_delay_ms;
static int g_render_every;
static int g_decided = 0;
static double g_quality_live = -1.0;
static bool g_daycycle = false;
static bool g_twin = false;
static bool g_quad = false;
static int g_nworlds = 1;
static bool g_is_tty = false;
static uint64_t *domB_, *domC_, *domD_;
static int *stkB_, *stkC_, *stkD_;
static uint64_t rsB_, rsC_, rsD_;
static int g_theme = 0;
static double g_bias = 0.5;
static bool g_crt = false;
static bool g_pan = false;
static bool g_bench = false;
static bool g_inf = false;
static bool g_no_bloom = false;
static bool g_no_weather = false;
static bool g_rt = false;   /* raytraced view of the solved grid */
static bool g_iso = false;  /* isometric relief view of the solved grid */
/* crawler (dungeon): WASD hero with a torch */
static bool g_hero_on = false;
static int g_hx = 0, g_hy = 0;
static int g_loot = 0, g_loot_tot = 0;
static char g_collage_path[512] = {0};
static char g_report_path[512] = {0};
static int g_zoom = 1;
static int g_evolve_count = 0;
static bool g_heatmap = false;
static bool g_evolve_view = false;
static bool g_evolve_was_paused = false;
#define EVOLUTION_MAX 8
static int g_evolution_n = 0;
static uint64_t g_evolution_seeds[EVOLUTION_MAX];
static double g_evolution_scores[EVOLUTION_MAX];
static uint64_t g_evolution_winner_seed = 0;
static int g_fit_w = 80, g_fit_h = 24;   /* terminal viewport in cells */
static int g_vx = 0, g_vy = 0;
static double now_ms(void);
static void quality_trace_clear(void);
static void thermo_json_string(FILE *f, const char *s);
static void macro_build(void);
static double quality_clamp(double v);
static double quality_signed_clamp(double v);
static bool evolution_run(int requested);
static int g_inf_ax = 0, g_inf_ay = 0;
static uint32_t *prev_sig_ = NULL;
static size_t prev_sig_cap_ = 0;
static bool full_repaint_ = true;
static double last_draw_ms_ = -1000;
static uint8_t *studio_pin_ = NULL;
static uint8_t *studio_tile_ = NULL;
static int studio_pin_count_ = 0;
#define QUALITY_TRACE_N 64
static double g_quality_trace_[QUALITY_TRACE_N];
static int g_quality_trace_len_ = 0, g_quality_trace_pos_ = 0;
static double g_quality_validity_live = -1.0;
static double g_quality_boundary_live = -1.0;
static double g_quality_coverage_live = -1.0;
static double g_quality_diversity_live = -1.0;
static double g_quality_smoothness_live = -1.0;
static double g_quality_stability_live = -1.0;
static double g_quality_topology_live = -1.0;

/* per-mode animation clock: coarse buckets so idle animations repaint
 * at sane rates instead of mutating every pixel every frame */
static uint32_t anim_epoch(void) {
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "fire")) return (uint32_t)(now_ms() / 90);
    if (!strcmp(m, "waves")) return (uint32_t)(now_ms() / 160);
    if (!strcmp(m, "galaxy")) return (uint32_t)(now_ms() / 100);
    if (!strcmp(m, "city")) return (uint32_t)(now_ms() / 400);
    if (!strcmp(m, "terrain")) return (uint32_t)(now_ms() / 260); /* clouds + rivers */
    if (!strcmp(m, "circuit")) return (uint32_t)(now_ms() / 140); /* signal pulses */
    if (!strcmp(m, "truchet")) return (uint32_t)(now_ms() / 500); /* strand glow */
    if (!strcmp(m, "dungeon")) return (uint32_t)(now_ms() / 220); /* depth + fireflies */
    if (!strcmp(m, "maze")) return (uint32_t)(now_ms() / 360);    /* wall breathing */
    if (!strcmp(m, "matrix")) return (uint32_t)(now_ms() / 140);  /* rain refresh */
    if (!strcmp(m, "pipes")) return (uint32_t)(now_ms() / 180);   /* water flow */
    if (!strcmp(m, "mondrian")) return (uint32_t)(now_ms() / 400);
    if (!strcmp(m, "koi")) return (uint32_t)(now_ms() / 250);     /* koi drift */
    if (!strcmp(m, "lava")) return (uint32_t)(now_ms() / 130);    /* crust crawl */
    if (!strcmp(m, "sakura")) return (uint32_t)(now_ms() / 120);  /* petal fall */
    if (!strcmp(m, "geode")) return (uint32_t)(now_ms() / 160);   /* glint wander */
    if (!strcmp(m, "lantern")) return (uint32_t)(now_ms() / 220); /* lantern rise */
    if (!strcmp(m, "dunes")) return (uint32_t)(now_ms() / 200);   /* heat shimmer */
    if (!strcmp(m, "reef")) return (uint32_t)(now_ms() / 160);    /* caustics + fish */
    if (!strcmp(m, "stained")) return (uint32_t)(now_ms() / 300); /* light sweep */
    if (!strcmp(m, "streets")) return (uint32_t)(now_ms() / 170);  /* traffic signals */
    if (!strcmp(m, "neurons")) return (uint32_t)(now_ms() / 95);   /* action potentials */
    if (!strcmp(m, "mycelium")) return (uint32_t)(now_ms() / 260); /* spore drift */
    if (!strcmp(m, "delta")) return (uint32_t)(now_ms() / 145); /* tidal glints */
    return 0;
}
static int g_bulk_idx = -1;
static bool g_cycle = false;
static bool g_help = false;
static bool g_observe = false;
static bool g_observe_was_paused = false;
static volatile sig_atomic_t g_winch = 0;

static int W_, H_;
#define IDX(x, y) ((y) * W_ + (x))

#define MAX_SPEED 200000L
#define MAX_EXPORT_PIXELS ((size_t)64 * 1024 * 1024)

/* ---------------- rng ---------------- */
static uint64_t rs_;
static uint64_t rnd(void) {
    uint64_t x = rs_ += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
static double rndf(void) { return (double)(rnd() >> 11) / 9007199254740992.0; }
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

/* ---------------- tiles ---------------- */
typedef struct { uint8_t e[NDIR]; uint8_t flag; double weight, wbase, lw; } Tile;
static Tile tiles_[MAXT];
static int ntiles_;
static uint64_t cdir_[NDIR][MAXT];
static bool g_torus = true;
static bool g_smooth = false;   /* field modes: |band diff| <= 1 compat */

static void build_circuit(void) {
    const int m[] = {0, 10, 5, 3, 6, 12, 9, 7, 14, 4, 8, 1, 2, 15};
    const double w[] = {42, 10, 10, 6, 6, 6, 6, 2, 2, 1, 1, 1, 1, 1};
    for (int i = 0; i < 14; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        if (m[i] & 1) t->e[0] = 1;
        if (m[i] & 2) t->e[1] = 1;
        if (m[i] & 4) t->e[2] = 1;
        if (m[i] & 8) t->e[3] = 1;
        t->weight = t->wbase = w[i];
    }
}
/* pipes: same connector lattice as circuit, but the network carries water:
 * long straight runs dominate, elbows 8-way, pressure joins stay rare */
static void build_pipes(void) {
    const int m[] = {0, 10, 5, 3, 6, 12, 9, 7, 14, 4, 8, 1, 2, 15};
    const double w[] = {26, 16, 16, 8, 8, 8, 8, 4, 3, 1, 1, 1, 1, 1};
    for (int i = 0; i < 14; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        if (m[i] & 1) t->e[0] = 1;
        if (m[i] & 2) t->e[1] = 1;
        if (m[i] & 4) t->e[2] = 1;
        if (m[i] & 8) t->e[3] = 1;
        t->weight = t->wbase = w[i];
    }
}
/* The three adaptive worlds share exact connector compatibility but use
 * different priors.  This keeps the hard domain graph identical while making
 * each visual language statistically distinct for the thermo learner. */
static void build_connector_world(const double weights[14]) {
    const int masks[] = {0, 10, 5, 3, 6, 12, 9, 7, 14, 4, 8, 1, 2, 15};
    for (int i = 0; i < 14; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        for (int d = 0; d < NDIR; d++)
            t->e[d] = (uint8_t)((masks[i] >> d) & 1);
        t->weight = t->wbase = weights[i];
    }
}
/* streets: broad avenues, corners, crossings, and occasional dead ends */
static void build_streets(void) {
    static const double weights[14] = {32, 18, 18, 9, 9, 9, 9, 5, 5, 1.6, 1.6, 1.2, 1.2, 0.7};
    build_connector_world(weights);
}
/* neurons: branching dendrites with a bright population of junctions */
static void build_neurons(void) {
    static const double weights[14] = {4, 12, 12, 10, 10, 10, 10, 8, 8, 1.4, 1.4, 2.2, 2.2, 3.0};
    build_connector_world(weights);
}
/* mycelium: sparse roots, knots, and a few spore-like termini */
static void build_mycelium(void) {
    static const double weights[14] = {24, 10, 10, 7, 7, 7, 7, 3.8, 3.8, 2.4, 2.4, 2.0, 2.0, 0.45};
    build_connector_world(weights);
}
/* delta: long channels dominate, with occasional confluences and sandbars */
static void build_delta(void) {
    static const double weights[14] = {18, 16, 16, 10, 10, 10, 10, 9, 7, 1.8, 1.8, 1.0, 1.0, 0.9};
    build_connector_world(weights);
}
/* mondrian: five painted plains + rare black slab; blockers are drawn as
 * thick charcoal lines wherever two bands meet */
static void build_mondrian(void) {
    const double w[] = {14, 17, 12, 8, 4, 2};
    for (int el = 0; el < 6; el++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        uint8_t v = (uint8_t)(el << 4);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = v;
        t->weight = t->wbase = w[el];
    }
}
/* koi: quietly deep-to-shallow pond bands; fish are rendered, not solved */
static void build_koi(void) {
    const double w[] = {6, 9, 11, 10, 8, 6, 4, 3};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
static float koi_x_[8], koi_y_[8], koi_a_[8];
static int n_koi_ = 0;
static void koi_seed(void) {
    n_koi_ = W_ * H_ / 160 + 2;
    if (n_koi_ > 8) n_koi_ = 8;
    for (int i = 0; i < n_koi_; i++) {
        koi_x_[i] = rndf() * W_;
        koi_y_[i] = rndf() * H_;
        koi_a_[i] = rndf() * 6.2832;
    }
}
/* lava: heat bands 0..7, molten at the bottom of the field, crust on top */
static void build_lava(void) {
    const double w[] = {8, 7, 6, 5, 4, 3, 3, 2};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* terrain: elevation 0..7 (hi nibble) x moisture 0..4 (lo nibble) = 40 tiles */
static void build_terrain(void) {
    const double ew[] = {10,10,8,10,11,9,5,3};
    const double mw[] = {0.8, 1.0, 1.1, 1.0, 0.9};
    for (int m = 0; m < 5; m++)
        for (int el = 0; el < 8; el++) {
            Tile *t = &tiles_[ntiles_++];
            memset(t, 0, sizeof *t);
            uint8_t v = (uint8_t)((el << 4) | m);
            t->e[0] = t->e[1] = t->e[2] = t->e[3] = v;
            t->weight = t->wbase = ew[el] * mw[m];
        }
}
/* rotation r connects sides r,(r+1)&3 with arc color 1(amber)/2(sky).
 * single-side stubs let strands terminate; empty keeps the weave sparse. */
static void build_truchet(void) {
    Tile *e = &tiles_[ntiles_++];
    memset(e, 0, sizeof *e);
    e->weight = e->wbase = 11.0;
    for (int rot = 0; rot < 4; rot++)
        for (int col = 1; col <= 2; col++) {
            Tile *t = &tiles_[ntiles_++]; /* stub */
            memset(t, 0, sizeof *t);
            t->e[rot] = (uint8_t)col;
            t->weight = t->wbase = 1.0;
        }
    for (int rot = 0; rot < 4; rot++)
        for (int col = 1; col <= 2; col++) {
            Tile *t = &tiles_[ntiles_++]; /* arc */
            memset(t, 0, sizeof *t);
            t->e[rot] = (uint8_t)col;
            t->e[(rot + 1) & 3] = (uint8_t)col;
            t->weight = 2.0;
        }
}
static void apply_bias(void) {
    for (int i = 0; i < ntiles_; i++) {
        if (tiles_[i].wbase <= 0) {
            tiles_[i].wbase = tiles_[i].weight > 0 ? tiles_[i].weight : 1.0;
            if (tiles_[i].weight <= 0) tiles_[i].weight = tiles_[i].wbase;
        }
        if (tiles_[i].weight < 1e-9) tiles_[i].weight = 1e-9;
        tiles_[i].lw = log2(tiles_[i].weight);
    }
    if (!strcmp(MODES[g_mode_idx], "circuit") || !strcmp(MODES[g_mode_idx], "truchet") ||
        !strcmp(MODES[g_mode_idx], "pipes") || !strcmp(MODES[g_mode_idx], "streets") ||
        !strcmp(MODES[g_mode_idx], "neurons") || !strcmp(MODES[g_mode_idx], "mycelium") ||
        !strcmp(MODES[g_mode_idx], "delta")) {
        g_bulk_idx = 0;
        for (int i = 0; i < ntiles_; i++)
            tiles_[i].weight = tiles_[i].wbase *
                (i == 0 ? pow(0.22, (g_bias - 0.5) * 4) : pow(2.2, (g_bias - 0.5) * 2));
        return;
    }
    if (!strcmp(MODES[g_mode_idx], "dungeon") || !strcmp(MODES[g_mode_idx], "maze")) {
        /* tilt rock/solid vs carved: scale pure-bulk tile and wall/passage weights */
        for (int i = 0; i < ntiles_; i++) {
            bool bulk = true;
            for (int d = 0; d < NDIR; d++) {
                uint8_t v = tiles_[i].e[d];
                if ((!strcmp(MODES[g_mode_idx], "dungeon") && v != 0) ||
                    (!strcmp(MODES[g_mode_idx], "maze") && v != 0)) { bulk = false; break; }
            }
            tiles_[i].weight = tiles_[i].wbase *
                (bulk ? pow(0.22, (g_bias - 0.5) * 4) : pow(2.2, (g_bias - 0.5) * 2));
        }
        return;
    }
    /* field modes: tilt along band order */
    int center = (ntiles_ - 1) / 2;
    for (int i = 0; i < ntiles_; i++) {
        double rel = (double)(i - center) / 4.0;
        tiles_[i].weight = tiles_[i].wbase * pow(2.2, rel * (g_bias - 0.5) * 4);
    }
}
static void build_compat(bool smooth) {
    for (int d = 0; d < NDIR; d++)
        for (int a = 0; a < ntiles_; a++) {
            uint64_t ok = 0;
            for (int b = 0; b < ntiles_; b++) {
                int ea = tiles_[a].e[d], eb = tiles_[b].e[OPPOSITE(d)];
                bool good;
                if (smooth)
                    good = abs((ea >> 4) - (eb >> 4)) <= 1 && abs((ea & 15) - (eb & 15)) <= 1;
                else
                    good = ea == eb;
                if (good) ok |= 1ULL << b;
            }
            cdir_[d][a] = ok;
        }
}
/* waves: wave-height bands 0..7 on a torus, animated crests */
static void build_waves(void) {
    const double w[] = {6, 9, 12, 10, 7, 4, 2, 1};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* maze: bit per edge = passage opening. dense braided labyrinth */
static void build_maze(void) {
    for (int m = 0; m < 16; m++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        for (int d = 0; d < NDIR; d++) t->e[d] = (uint8_t)((m >> d) & 1);
        int opens = __builtin_popcount((unsigned)m);
        bool straight = opens == 2 && ((m & 5) == 5 || (m & 10) == 10);
        t->weight = t->wbase = straight ? 1.5 : opens == 1 ? 0.7 : opens == 3 ? 0.9 : 1.0;
    }
}
/* galaxy: nebula density bands 0..7 on a torus; stars twinkle in render */
static void build_galaxy(void) {
    const double w[] = {10, 9, 8, 7, 6, 4, 3, 2};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* sakura: spring-night sky bands 0..7, petals drift down in render */
static void build_sakura(void) {
    const double w[] = {3, 5, 7, 9, 11, 12, 10, 8};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* geode: mineral richness bands 0..7 on a torus; facets glint in render */
static void build_geode(void) {
    const double w[] = {2, 3, 4, 5, 6, 8, 10, 12};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* lantern festival: night-glow bands, brightest near the horizon */
static void build_lantern(void) {
    const double w[] = {14, 11, 8, 6, 5, 4, 3, 2};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* dunes: sky bands up top, ridge shadow below */
static void build_dunes(void) {
    const double w[] = {10, 9, 8, 7, 6, 5, 4, 3};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* reef: water depth bands, sunlit surface over the deep */
static void build_reef(void) {
    const double w[] = {4, 6, 8, 10, 11, 10, 8, 6};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* stained: jewel panes; lead gathers where panes meet */
static void build_stained(void) {
    const double w[] = {9, 9, 9, 9, 9, 9, 9, 6};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* city: altitude bands 0..7 - sky above, glowing streets below */
static void build_city(void) {
    const double w[] = {12, 10, 8, 6, 5, 5, 4, 3};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* aurora: curtain-intensity bands, bright at the top of the sky */
static void build_aurora(void) {
    const double w[] = {3, 5, 8, 11, 13, 9, 4, 2};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* matrix: rain-intensity bands; deep columns pour harder */
static void build_matrix(void) {
    const double w[] = {4, 7, 10, 12, 11, 8, 5, 3};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
/* dungeon: edges 0=rock,1=wall-face,2=floor. rock may never touch floor
 * directly - a wall band must sit between them. torch variants on 1-wall tiles */
static void add_dtile(const int e[4], double wt, bool torch) {
    if (ntiles_ >= MAXT) return;
    Tile *t = &tiles_[ntiles_++];
    memset(t, 0, sizeof *t);
    for (int d = 0; d < NDIR; d++) t->e[d] = (uint8_t)e[d];
    t->flag = torch ? 1 : 0;
    t->weight = t->wbase = wt;
}
static void build_dungeon(void) {
    for (int m = 0; m < 81; m++) {
        int e[4], v = m;
        for (int d = 0; d < NDIR; d++) { e[d] = v % 3; v /= 3; }
        bool ok = true;
        for (int d = 0; d < NDIR; d++) {
            int a = e[d], b = e[(d + 1) & 3];
            if ((a == 0 && b == 2) || (a == 2 && b == 0)) ok = false;
        }
        if (!ok) continue;
        int r = 0, f = 0, wc = 0;
        for (int d = 0; d < NDIR; d++) {
            if (e[d] == 0) r++; else if (e[d] == 1) wc++; else f++;
        }
        double wt;
        if (r == 4) wt = 28;
        else if (f == 4) wt = 34;
        else wt = wc == 1 ? 9 : wc == 2 ? 6 : wc == 3 ? 3 : 2;
        bool torchable = wc == 1 || (wc == 2 && f == 2);
        add_dtile(e, wt, false);
        if (torchable && ntiles_ < MAXT - 8) add_dtile(e, wt * 0.18, true);
    }
}
/* fire: temperature bands 0..7, smooth gradient, blackbody palette */
static void build_fire(void) {
    const double w[] = {1,2,2,3,4,5,6,8};
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = w[i];
    }
}
static void set_title(const char *mode);
static void setup_mode(int idx) {
    g_mode_idx = ((idx % NMODES) + NMODES) % NMODES;
    ntiles_ = 0;
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "terrain")) { build_terrain(); build_compat(true); }
    else if (!strcmp(m, "truchet")) { build_truchet(); build_compat(false); }
    else if (!strcmp(m, "fire")) { build_fire(); build_compat(true); }
    else if (!strcmp(m, "waves")) { build_waves(); build_compat(true); }
    else if (!strcmp(m, "dungeon")) { build_dungeon(); build_compat(false); }
    else if (!strcmp(m, "maze")) { build_maze(); build_compat(false); }
    else if (!strcmp(m, "galaxy")) { build_galaxy(); build_compat(true); }
    else if (!strcmp(m, "city")) { build_city(); build_compat(true); }
    else if (!strcmp(m, "aurora")) { build_aurora(); build_compat(true); }
    else if (!strcmp(m, "matrix")) { build_matrix(); build_compat(true); }
    else if (!strcmp(m, "pipes")) { build_pipes(); build_compat(false); }
    else if (!strcmp(m, "mondrian")) { build_mondrian(); build_compat(true); }
    else if (!strcmp(m, "koi")) { build_koi(); build_compat(true); }
    else if (!strcmp(m, "lava")) { build_lava(); build_compat(true); }
    else if (!strcmp(m, "sakura")) { build_sakura(); build_compat(true); }
    else if (!strcmp(m, "geode")) { build_geode(); build_compat(true); }
    else if (!strcmp(m, "lantern")) { build_lantern(); build_compat(true); }
    else if (!strcmp(m, "dunes")) { build_dunes(); build_compat(true); }
    else if (!strcmp(m, "reef")) { build_reef(); build_compat(true); }
    else if (!strcmp(m, "stained")) { build_stained(); build_compat(true); }
    else if (!strcmp(m, "streets")) { build_streets(); build_compat(false); }
    else if (!strcmp(m, "neurons")) { build_neurons(); build_compat(false); }
    else if (!strcmp(m, "mycelium")) { build_mycelium(); build_compat(false); }
    else if (!strcmp(m, "delta")) { build_delta(); build_compat(false); }
    else { build_circuit(); build_compat(false); }
    g_torus = strcmp(MODES[g_mode_idx], "truchet") != 0;
    /* fire keeps torus off too */
    if (!strcmp(MODES[g_mode_idx], "fire") || !strcmp(MODES[g_mode_idx], "city") ||
        !strcmp(MODES[g_mode_idx], "aurora") || !strcmp(MODES[g_mode_idx], "lava") ||
        !strcmp(MODES[g_mode_idx], "sakura") || !strcmp(MODES[g_mode_idx], "lantern") ||
        !strcmp(MODES[g_mode_idx], "dunes") || !strcmp(MODES[g_mode_idx], "reef") ||
        !strcmp(MODES[g_mode_idx], "streets") || !strcmp(MODES[g_mode_idx], "delta"))
        g_torus = false;
    g_smooth = !strcmp(MODES[g_mode_idx], "terrain") ||
               !strcmp(MODES[g_mode_idx], "fire") ||
               !strcmp(MODES[g_mode_idx], "waves") ||
               !strcmp(MODES[g_mode_idx], "galaxy") ||
               !strcmp(MODES[g_mode_idx], "city") ||
               !strcmp(MODES[g_mode_idx], "aurora") ||
               !strcmp(MODES[g_mode_idx], "matrix") ||
               !strcmp(MODES[g_mode_idx], "mondrian") ||
               !strcmp(MODES[g_mode_idx], "koi") ||
               !strcmp(MODES[g_mode_idx], "lava") ||
               !strcmp(MODES[g_mode_idx], "sakura") ||
               !strcmp(MODES[g_mode_idx], "geode") ||
               !strcmp(MODES[g_mode_idx], "lantern") ||
        !strcmp(MODES[g_mode_idx], "dunes") ||
        !strcmp(MODES[g_mode_idx], "reef") ||
               !strcmp(MODES[g_mode_idx], "stained");
    g_hero_on = false;
    apply_bias();
    if (g_is_tty) set_title(MODES[g_mode_idx]);
}

/* ---------------- colors fwd ---------------- */
typedef struct { uint8_t r, g, b; } RGB;
static bool g_sheet_opened = false;
static RGB hsv(double h, double s, double v);
static void set_title(const char *mode) {
    printf("\x1b]2;wfc \xe2\x80\x94 %s\x07", mode);
    fflush(stdout);
}

/* ---------------- wfc core ---------------- */
static uint64_t *dom_;
static int *stk_;

static int *comp_;           /* circuit only: connected-trace component id */
static RGB *comp_col_;
static int n_comp_;
static bool g_comp_ready;
static uint8_t *river_;      /* terrain only: cell is on a river */
static int *river_rank_;     /* carve order for animated reveal */
static int n_river_;
static int g_river_show;     /* how many river cells are revealed */
static uint8_t *macro_role_; /* low-frequency design guidance for network modes */
static int macro_guided_count_;

static void *slots_dom[MAXW];
static int *slots_stk[MAXW];
static uint64_t slots_rs[MAXW];
static int cur_slot = 0;
/* Keep one canonical pointer per world.  dom_ and stk_ are aliases for the
 * currently selected world; they must not be used to rebuild this table when
 * switching, or a switch away from world 0 would lose its pointer. */
static void world_sync(void) {
    slots_dom[0] = dom_; slots_stk[0] = stk_; slots_rs[0] = rs_;
    if (g_twin || g_quad) { slots_dom[1] = domB_; slots_stk[1] = stkB_; slots_rs[1] = rsB_; }
    if (g_quad) {
        slots_dom[2] = domC_; slots_stk[2] = stkC_; slots_rs[2] = rsC_;
        slots_dom[3] = domD_; slots_stk[3] = stkD_; slots_rs[3] = rsD_;
    }
}

extern void click_bufs_invalidate(void);
static void hist_clear(void);
static int hist_stride_ = 1, hist_cnt_ = 0;
static void free_world_buffers(void) {
    uint64_t *doms[MAXW] = {dom_, domB_, domC_, domD_};
    int *stks[MAXW] = {stk_, stkB_, stkC_, stkD_};
    for (int i = 0; i < MAXW; i++) {
        bool seen_dom = false, seen_stk = false;
        for (int j = 0; j < i; j++) {
            if (doms[j] == doms[i]) seen_dom = true;
            if (stks[j] == stks[i]) seen_stk = true;
        }
        if (doms[i] && !seen_dom) free(doms[i]);
        if (stks[i] && !seen_stk) free(stks[i]);
    }
    dom_ = domB_ = domC_ = domD_ = NULL;
    stk_ = stkB_ = stkC_ = stkD_ = NULL;
    for (int i = 0; i < MAXW; i++) {
        slots_dom[i] = NULL;
        slots_stk[i] = NULL;
        slots_rs[i] = 0;
    }
}
static void grid_alloc(int w, int h) {
    free_world_buffers();
    free(river_); free(river_rank_);
    free(comp_); free(comp_col_);
    free(macro_role_);
    free(studio_pin_); free(studio_tile_);
    studio_pin_ = NULL; studio_tile_ = NULL; studio_pin_count_ = 0;
    macro_role_ = NULL; macro_guided_count_ = 0;
    dom_ = malloc(sizeof(uint64_t) * (size_t)w * h);
    stk_ = malloc(sizeof(int) * (size_t)w * h * MAXT);
    river_ = malloc((size_t)w * h);
    river_rank_ = malloc(sizeof(int) * (size_t)w * h);
    comp_ = malloc(sizeof(int) * (size_t)w * h);
    comp_col_ = malloc(sizeof(RGB) * (size_t)w * h);
    macro_role_ = calloc((size_t)w * h, 1);
    studio_pin_ = calloc((size_t)w * h, 1);
    studio_tile_ = malloc((size_t)w * h);
    free(domB_); free(stkB_); free(domC_); free(stkC_); free(domD_); free(stkD_);
    domB_ = malloc(sizeof(uint64_t) * (size_t)w * h);
    stkB_ = malloc(sizeof(int) * (size_t)w * h * MAXT);
    domC_ = malloc(sizeof(uint64_t) * (size_t)w * h);
    stkC_ = malloc(sizeof(int) * (size_t)w * h * MAXT);
    domD_ = malloc(sizeof(uint64_t) * (size_t)w * h);
    stkD_ = malloc(sizeof(int) * (size_t)w * h * MAXT);
    if (!dom_ || !stk_ || !river_ || !river_rank_ || !comp_ || !comp_col_ ||
        !macro_role_ ||
        !studio_pin_ || !studio_tile_ ||
        !domB_ || !stkB_ || !domC_ || !stkC_ || !domD_ || !stkD_) {
        perror("malloc");
        free_world_buffers();
        free(river_); free(river_rank_); free(comp_); free(comp_col_);
        free(macro_role_);
        free(studio_pin_); free(studio_tile_);
        river_ = NULL; river_rank_ = NULL; comp_ = NULL; comp_col_ = NULL;
        macro_role_ = NULL;
        studio_pin_ = NULL; studio_tile_ = NULL;
        exit(1);
    }
    memset(studio_tile_, 0, (size_t)w * h);
    rsB_ = g_seed ^ 0xA24BAED4963EE407ULL;
    rsC_ = g_seed ^ 0x9FB21C651E98DF25ULL;
    rsD_ = g_seed ^ 0xC13FA9A902A6328FULL;
    cur_slot = 0;
    world_sync();
    click_bufs_invalidate();
}

static void load_world(int w) {
    if (w < 0 || w >= MAXW) return;
    if ((w == 1 && !g_twin && !g_quad) || (w >= 2 && !g_quad)) return;
    if (w == cur_slot) return;
    if (!slots_dom[w]) return;
    slots_rs[cur_slot] = rs_;
    cur_slot = w;
    dom_ = slots_dom[w]; stk_ = slots_stk[w]; rs_ = slots_rs[w];
}
static int pc64(uint64_t x);
static bool propagate_from(int start);
/* expand the canvas by unlocking a fresh ring around a finished world */
static bool world_grow(void) {
    if (g_nworlds != 1) return false;
    if (getenv("WFC_DEBUG")) { FILE *df=fopen("/tmp/wfc_dbg.log","a"); if(df){fprintf(df,"[grow at %dx%d ok=%d]\n",W_,H_,1); fclose(df);} }
    if (W_ > 300 || H_ > 180) return false;
    int gw = W_ + 10, gh = H_ + 7;
    uint64_t *nd = malloc(sizeof(uint64_t) * (size_t)gw * gh);
    int *ns = malloc(sizeof(int) * (size_t)gw * gh * MAXT);
    uint8_t *nriv = calloc((size_t)gw * gh, 1);
    int *nrr = malloc(sizeof(int) * (size_t)gw * gh);
    uint8_t *npin = calloc((size_t)gw * gh, 1);
    uint8_t *ntile = calloc((size_t)gw * gh, 1);
    uint8_t *nmacro = calloc((size_t)gw * gh, 1);
    if (!nd || !ns || !nriv || !nrr || !npin || !ntile || !nmacro) {
        free(nd); free(ns); free(nriv); free(nrr); free(npin); free(ntile); free(nmacro); return false;
    }
    int *nc = malloc(sizeof(int) * (size_t)gw * gh);
    RGB *ncc = malloc(sizeof(RGB) * (size_t)gw * gh);
    if (!nc || !ncc) {
        free(nc); free(ncc); free(nd); free(ns); free(nriv); free(nrr); free(npin); free(ntile); free(nmacro);
        return false;
    }
    uint64_t full = ((uint64_t)1 << ntiles_) - 1;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) { nd[(size_t)y * gw + x] = full; nrr[(size_t)y * gw + x] = -1; }
    int ox = (gw - W_) / 2, oy = (gh - H_) / 2;
    for (int y = 0; y < H_; y++)
        for (int x = 0; x < W_; x++) {
            uint64_t v = dom_[IDX(x, y)];
            nd[(size_t)(y + oy) * gw + (x + ox)] = v;
            npin[(size_t)(y + oy) * gw + (x + ox)] = studio_pin_[IDX(x, y)];
            ntile[(size_t)(y + oy) * gw + (x + ox)] = studio_tile_[IDX(x, y)];
            nmacro[(size_t)(y + oy) * gw + (x + ox)] = macro_role_[IDX(x, y)];
            if (river_rank_[IDX(x, y)] >= 0 && g_river_show > 0)
                nriv[(size_t)(y + oy) * gw + (x + ox)] = 1,
                nrr[(size_t)(y + oy) * gw + (x + ox)] = river_rank_[IDX(x, y)];
        }
    free(dom_); free(stk_); free(river_); free(river_rank_);
    free(comp_); free(comp_col_);
    free(macro_role_);
    free(studio_pin_); free(studio_tile_);
    comp_ = nc; comp_col_ = ncc;
    dom_ = nd; stk_ = ns; river_ = nriv; river_rank_ = nrr;
    studio_pin_ = npin; studio_tile_ = ntile; macro_role_ = nmacro;
    int old_w = W_, old_h = H_;
    W_ = gw; H_ = gh;
    macro_build();
    g_inf_ax += ox;
    g_inf_ay += oy;
    full_repaint_ = true;
    hist_clear();
    click_bufs_invalidate(); /* undo snapshots are sized to the old canvas */
    n_river_ = 0;
    studio_pin_count_ = 0;
    for (int i = 0; i < gw * gh; i++) studio_pin_count_ += studio_pin_[i] != 0;
    macro_guided_count_ = 0;
    for (int i = 0; i < gw * gh; i++) macro_guided_count_ += macro_role_[i] != 0;
    world_sync();
    /* pre-constrain the new ring from the solved bedrock border so the
     * solver doesn't immediately contradict at the seam */
    {
        int pushed = 0;
        for (int y = oy - 1; y <= oy + old_h && pushed < 512; y++)
            for (int x = ox - 1; x <= ox + old_w && pushed < 512; x++) {
                if (y < 0 || x < 0 || y >= gh || x >= gw) continue;
                bool on_edge = (x == ox - 1 || x == ox + old_w ||
                                y == oy - 1 || y == oy + old_h);
                if (!on_edge) continue;
                if (pc64(dom_[IDX(x, y)]) == 1) {
                    propagate_from(IDX(x, y));
                    pushed++;
                }
            }
        (void)pushed;
    }
    g_decided = 0; /* frontier ring is fresh; recount on next step */
    return true;
}

/* contradiction repair that preserves decided cells */
static uint64_t grid_cell_mask(int x, int y);
static void grid_soft_reset(void) {
    for (int i = 0; i < W_ * H_; i++)
        if (pc64(dom_[i]) != 1)
            dom_[i] = grid_cell_mask(i % W_, i / W_);
}
static bool bounded_connector_mode(void) {
    const char *m = MODES[g_mode_idx];
    return !g_torus && (!strcmp(m, "truchet") || !strcmp(m, "streets") ||
                        !strcmp(m, "neurons") || !strcmp(m, "mycelium") ||
                        !strcmp(m, "delta"));
}
static uint64_t grid_cell_mask(int x, int y) {
    uint64_t full = ((uint64_t)1 << ntiles_) - 1;
    uint64_t m = full;
    if (bounded_connector_mode()) { /* connector strokes must stay in frame */
        for (int t = 0; t < ntiles_; t++) {
            if (y == 0 && tiles_[t].e[0]) m &= ~(1ULL << t);
            if (y == H_ - 1 && tiles_[t].e[2]) m &= ~(1ULL << t);
            if (x == 0 && tiles_[t].e[3]) m &= ~(1ULL << t);
            if (x == W_ - 1 && tiles_[t].e[1]) m &= ~(1ULL << t);
        }
    }
    bool fire_grad = !strcmp(MODES[g_mode_idx], "fire") || !strcmp(MODES[g_mode_idx], "city")
                     || !strcmp(MODES[g_mode_idx], "aurora") || !strcmp(MODES[g_mode_idx], "lava")
                     || !strcmp(MODES[g_mode_idx], "sakura") || !strcmp(MODES[g_mode_idx], "lantern")
                     || !strcmp(MODES[g_mode_idx], "dunes") || !strcmp(MODES[g_mode_idx], "reef");
    if (fire_grad) {
        int tb = (int)(7.0 * (H_ - 1 - y) / (H_ > 1 ? H_ - 1 : 1));
        if (!strcmp(MODES[g_mode_idx], "aurora")) tb = 7 - tb;
        if (!strcmp(MODES[g_mode_idx], "lava") || !strcmp(MODES[g_mode_idx], "lantern")) tb = 7 - tb;
        int lo = tb - 2 < 0 ? 0 : tb - 2, hi = tb + 2 > 7 ? 7 : tb + 2;
        for (int t = 0; t < ntiles_; t++) {
            int b = tiles_[t].e[0] >> 4;
            if (b < lo || b > hi) m &= ~(1ULL << t);
        }
    }
    return m;
}
static void grid_reset(void) {
    hist_clear();
    hist_stride_ = W_ * H_ > 8000 ? 4 : 1;
    for (int y = 0; y < H_; y++)
        for (int x = 0; x < W_; x++) {
            dom_[IDX(x, y)] = grid_cell_mask(x, y);
        }
    if (!strcmp(MODES[g_mode_idx], "koi")) koi_seed();
    memset(river_, 0, (size_t)W_ * H_);
    for (int i = 0; i < W_ * H_; i++) river_rank_[i] = -1;
    n_river_ = 0;
    g_river_show = 0;
    full_repaint_ = true;
    for (int i = 0; i < W_ * H_; i++) comp_[i] = -1;
    n_comp_ = 0;
    g_comp_ready = false;
    memset(studio_pin_, 0, (size_t)W_ * H_);
    memset(studio_tile_, 0, (size_t)W_ * H_);
    studio_pin_count_ = 0;
    macro_build();
    quality_trace_clear();
}

/* flood-fill connected traces; each loop gets its own golden-angle hue */
static void label_components(void) {
    n_comp_ = 0;
    for (int i = 0; i < W_ * H_; i++) comp_[i] = -1;
    for (int i = 0; i < W_ * H_; i++) {
        if (pc64(dom_[i]) != 1) {
            g_comp_ready = false;
            return;
        }
    }
    for (int s = 0; s < W_ * H_; s++) {
        if (comp_[s] >= 0) continue;
        int qh = 0, qt = 0;
        stk_[qt++] = s;
        comp_[s] = n_comp_;
        while (qh < qt) {
            int c = stk_[qh++];
            int cx = c % W_, cy = c / W_;
            int ta = __builtin_ctzll(dom_[c]);
            for (int d = 0; d < NDIR; d++) {
                if (!tiles_[ta].e[d]) continue;
                int nx = (cx + (d == 1) - (d == 3) + W_) % W_;
                int ny = (cy + (d == 2) - (d == 0) + H_) % H_;
                int n = IDX(nx, ny);
                if (comp_[n] >= 0) continue;
                int tb = __builtin_ctzll(dom_[n]);
                if (!tiles_[tb].e[OPPOSITE(d)]) continue;
                comp_[n] = n_comp_;
                stk_[qt++] = n;
            }
        }
        n_comp_++;
    }
    for (int k = 0; k < n_comp_; k++)
        comp_col_[k] = hsv(fmod(k * 137.508, 360.0), 0.78, 0.95);
    g_comp_ready = true;
}

static int pc64(uint64_t x); /* forward */

/* ---------------- rivers (terrain post-pass) ---------------- */
static int elev_at(int i) {
    if (pc64(dom_[i]) != 1) return -1;
    return tiles_[__builtin_ctzll(dom_[i])].e[0] >> 4;
}

static void rivers_clear(void) {
    memset(river_, 0, (size_t)W_ * H_);
    for (int i = 0; i < W_ * H_; i++) river_rank_[i] = -1;
    n_river_ = 0;
    g_river_show = 0;
}

/* greedy downhill walks from highlands; commit only paths that reach water */
static void carve_rivers(void) {
    int target = W_ * H_ / 220 + 3;
    int path[8192];
    int tries = 0;
    while (n_river_ < target && tries < target * 40) {
        tries++;
        int start = (int)(rnd() % (uint64_t)(W_ * H_));
        if (elev_at(start) < 4 || river_[start]) continue;
        /* rank array doubles as visited marker for this walk */
        int sp = 0, cur = start, guard = 8 * (W_ + H_) + 16;
        static int walkmark_seq = 1000000000;
        walkmark_seq++;
        while (guard-- > 0) {
            int e = elev_at(cur);
            if (e <= 2) break; /* reached the sea */
            if (river_rank_[cur] == walkmark_seq) break; /* looped onto self */
            river_rank_[cur] = walkmark_seq;
            if (sp < (int)(sizeof path / sizeof path[0])) path[sp++] = cur;
            int best = -1, bestscore = 1 << 30;
            for (int d = 0; d < NDIR; d++) {
                int nx = cur % W_, ny = cur / W_;
                if (d == 0) ny = (ny + H_ - 1) % H_;
                else if (d == 1) nx = (nx + 1) % W_;
                else if (d == 2) ny = (ny + 1) % H_;
                else nx = (nx + W_ - 1) % W_;
                int n = IDX(nx, ny);
                if (river_rank_[n] == walkmark_seq) continue;
                int en = elev_at(n);
                if (en < 0) continue;
                int score = en * 4 + (int)(rndf() * 3) - (river_[n] ? 6 : 0);
                if (score < bestscore) { bestscore = score; best = n; }
            }
            if (best < 0) break;
            cur = best;
            if (river_[cur] && sp > 0) break; /* joined an existing river */
        }
        /* commit only if we ended on water or joined a river */
        if ((elev_at(cur) <= 2 || river_[cur]) && sp >= 2) {
            for (int k = 0; k < sp; k++) {
                int c = path[k];
                if (!river_[c]) { river_[c] = 1; river_rank_[c] = n_river_++; }
            }
        } else {
            for (int k = 0; k < sp; k++) river_rank_[path[k]] = -1;
        }
    }
}
static int pc64(uint64_t x) { return __builtin_popcountll(x); }

enum {
    MACRO_NONE = 0,
    MACRO_ARTERIAL = 1,
    MACRO_INTERSECTION = 2,
    MACRO_SOMA = 3,
    MACRO_BRANCH = 4,
    MACRO_TERMINAL = 5,
    MACRO_SOURCE = 6,
    MACRO_CHANNEL = 7,
    MACRO_CONFLUENCE = 8,
};

static bool macro_network_mode(void) {
    const char *m = MODES[g_mode_idx];
    return !strcmp(m, "streets") || !strcmp(m, "neurons") ||
           !strcmp(m, "mycelium") || !strcmp(m, "delta");
}

static const char *macro_name(void) {
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "streets")) return "arterial-grid";
    if (!strcmp(m, "neurons")) return "soma-branches";
    if (!strcmp(m, "mycelium")) return "spore-tendrils";
    if (!strcmp(m, "delta")) return "delta-channel";
    return "none";
}

static int macro_role_at(int x, int y) {
    if (!macro_network_mode() || W_ <= 0 || H_ <= 0) return MACRO_NONE;
    const char *m = MODES[g_mode_idx];
    int dx = x - W_ / 2, dy = y - H_ / 2;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    uint32_t salt = (uint32_t)(g_seed ^ (g_seed >> 32));
    uint32_t h = hash3((uint32_t)x, (uint32_t)y, salt ^ 0xA51C3E17u);
    if (!strcmp(m, "streets")) {
        int wx = W_ > 12 ? 1 : 0, wy = H_ > 10 ? 1 : 0;
        if (adx <= wx && ady <= wy) return MACRO_INTERSECTION;
        if (adx <= wx || ady <= wy) return MACRO_ARTERIAL;
        int side = 5 + (int)(salt % 3);
        if (((x + y * 2 + (int)(salt % (uint32_t)side)) % side) == 0)
            return MACRO_ARTERIAL;
    } else if (!strcmp(m, "neurons")) {
        if (adx <= 1 && ady <= 1) return MACRO_SOMA;
        if (adx > 1 && ady > 1 && abs(adx - ady) <= 1) return MACRO_BRANCH;
        if ((h % 17) < 2) return MACRO_BRANCH;
        if (x == 0 || y == 0 || x == W_ - 1 || y == H_ - 1)
            return MACRO_TERMINAL;
    } else if (!strcmp(m, "mycelium")) {
        int sx = W_ / 3, sy = H_ / 3;
        int sx2 = (2 * W_) / 3, sy2 = (2 * H_) / 3;
        int d1 = abs(x - sx) + abs(y - sy);
        int d2 = abs(x - sx2) + abs(y - sy2);
        if (d1 <= 1 || d2 <= 1) return MACRO_SOURCE;
        if ((h % 11) < 4 || abs((int)(h % 7) - 3) <= 1)
            return MACRO_BRANCH;
        if (h % 23 == 0) return MACRO_TERMINAL;
    } else if (!strcmp(m, "delta")) {
        int channel_y = H_ / 2 + (int)(h % 3) - 1;
        if (x == 0 && ady <= 1) return MACRO_SOURCE;
        if (x == W_ - 1 && abs(y - H_ / 2) <= 1) return MACRO_CONFLUENCE;
        if (abs(y - channel_y) == 0) return MACRO_CHANNEL;
        if (x > W_ / 5 && (abs(y - (H_ / 2 - 2)) <= 1 ||
                           (h % 13) < 3)) return MACRO_BRANCH;
    }
    return MACRO_NONE;
}

static void macro_build(void) {
    macro_guided_count_ = 0;
    if (!macro_role_ || W_ <= 0 || H_ <= 0) return;
    memset(macro_role_, MACRO_NONE, (size_t)W_ * H_);
    if (!macro_network_mode()) return;
    for (int y = 0; y < H_; y++)
        for (int x = 0; x < W_; x++) {
            uint8_t role = (uint8_t)macro_role_at(x, y);
            macro_role_[IDX(x, y)] = role;
            macro_guided_count_ += role != MACRO_NONE;
        }
}

static int macro_guided_cells(void) { return macro_guided_count_; }

static int macro_target_degree(int role) {
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "streets"))
        return role == MACRO_INTERSECTION ? 4 : 2;
    if (!strcmp(m, "neurons"))
        return role == MACRO_SOMA ? 3 : role == MACRO_TERMINAL ? 1 : 2;
    if (!strcmp(m, "mycelium"))
        return role == MACRO_SOURCE ? 1 : role == MACRO_TERMINAL ? 1 : 2;
    if (!strcmp(m, "delta"))
        return role == MACRO_SOURCE ? 1 : role == MACRO_CONFLUENCE ? 3 :
               role == MACRO_BRANCH ? 3 : 2;
    return 2;
}

static double macro_tile_fit(int cell, int tile) {
    if (!macro_role_ || cell < 0 || cell >= W_ * H_ ||
        tile < 0 || tile >= ntiles_ || macro_role_[cell] == MACRO_NONE)
        return 0.5;
    int degree = 0;
    for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
    double fit = 1.0 - fabs((double)degree - macro_target_degree(macro_role_[cell])) / 4.0;
    return quality_clamp(fit);
}

static double macro_tile_bonus(int cell, int tile) {
    return cell >= 0 ? quality_signed_clamp((macro_tile_fit(cell, tile) - 0.5) * 1.6) : 0.0;
}

static double quality_tile_prior(int tile) {
    if (!macro_network_mode() || tile < 0 || tile >= ntiles_) return 0.0;
    const char *m = MODES[g_mode_idx];
    double ideal = !strcmp(m, "streets") ? 2.2 : !strcmp(m, "neurons") ? 2.0 :
                   !strcmp(m, "mycelium") ? 1.7 : 1.9;
    int degree = 0;
    for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
    return quality_signed_clamp((1.0 - fabs(degree - ideal) / 3.5) * 2.0 - 1.0);
}

static int weighted_pick_at(uint64_t m, int cell) {
    double tot = 0, acc = 0;
    for (uint64_t mm = m; mm; mm &= mm - 1) {
        int tile = __builtin_ctzll(mm);
        double weight = tiles_[tile].weight;
        if (cell >= 0) weight *= exp(0.52 * macro_tile_bonus(cell, tile));
        tot += weight;
    }
    double r = rndf() * tot;
    for (uint64_t mm = m; mm; mm &= mm - 1) {
        int tile = __builtin_ctzll(mm);
        double weight = tiles_[tile].weight;
        if (cell >= 0) weight *= exp(0.52 * macro_tile_bonus(cell, tile));
        acc += weight;
        if (r <= acc) return tile;
    }
    return __builtin_ctzll(m);
}

/* propagate constraints outward from cell; false on contradiction */
static bool propagate_from(int start) {
    int sp = 0;
    stk_[sp++] = start;
    while (sp) {
        int c = stk_[--sp];
        int cx = c % W_, cy = c / W_;
        for (int d = 0; d < NDIR; d++) {
            int nx = cx, ny = cy;
            if (d == 0) ny = g_torus ? (cy + H_ - 1) % H_ : cy - 1;
            else if (d == 1) nx = g_torus ? (cx + 1) % W_ : cx + 1;
            else if (d == 2) ny = g_torus ? (cy + 1) % H_ : cy + 1;
            else nx = g_torus ? (cx + W_ - 1) % W_ : cx - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
            int n = IDX(nx, ny);
            uint64_t allow = 0, dm = dom_[c];
            while (dm) { allow |= cdir_[d][__builtin_ctzll(dm)]; dm &= dm - 1; }
            uint64_t nd = dom_[n] & allow;
            if (!nd) return false;
            if (nd != dom_[n]) { dom_[n] = nd; stk_[sp++] = n; }
        }
    }
    return true;
}

/* collapse lowest-entropy cell, propagate constraints outward.
 * returns 1 done, 0 progressed, -1 contradiction (caller restarts cheaply). */
static int wfc_step(void) {
    int best = -1, ties = 0;
    double be = 1e9;
    g_decided = 0;
    for (int i = 0; i < W_ * H_; i++) {
        int k = pc64(dom_[i]);
        if (k == 0) return -1;
        if (k == 1) g_decided++;
        if (k > 1) {
            double sw = 0, slw = 0;
            uint64_t m = dom_[i];
            while (m) {
                int b = __builtin_ctzll(m); m &= m - 1;
                sw += tiles_[b].weight;
                slw += tiles_[b].weight * tiles_[b].lw;
            }
            double e = sw > 0 ? log2(sw) - slw / sw : 0;
            if (macro_role_ && macro_role_[i] != MACRO_NONE) e -= 0.08;
            e += rndf() * 1e-6;
            if (e < be * (1 - 1e-9)) { be = e; best = i; ties = 1; }
            else if (e < be * (1 + 1e-9)) { ties++; if (rndf() * ties < 1.0) best = i; }
        }
    }
    if (best < 0) return 1;

    dom_[best] = 1ULL << weighted_pick_at(dom_[best], best);
    return propagate_from(best) ? 0 : -1;
}

/* ---------------- deterministic quality feedback ---------------- */
typedef struct {
    double total;
    double validity;
    double boundary;
    double coverage;
    double diversity;
    double smoothness;
    double stability;
    double topology;
} QualityMetrics;

typedef struct {
    const char *focus;
    double validity, boundary, coverage, diversity;
    double smoothness, stability, topology;
} QualityProfile;

static QualityProfile quality_profile(void) {
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "streets"))
        return (QualityProfile){"streets", 0.30, 0.18, 0.14, 0.08, 0.08, 0.06, 0.16};
    if (!strcmp(m, "neurons"))
        return (QualityProfile){"neurons", 0.24, 0.05, 0.12, 0.15, 0.08, 0.06, 0.30};
    if (!strcmp(m, "mycelium"))
        return (QualityProfile){"mycelium", 0.24, 0.04, 0.16, 0.14, 0.12, 0.06, 0.24};
    if (!strcmp(m, "delta"))
        return (QualityProfile){"delta", 0.27, 0.12, 0.14, 0.08, 0.10, 0.05, 0.24};
    return (QualityProfile){"balanced", 0.30, 0.03, 0.18, 0.16, 0.16, 0.05, 0.12};
}

static double quality_clamp(double v) {
    if (!isfinite(v)) return 0.0;
    return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
}

static double quality_signed_clamp(double v) {
    if (!isfinite(v)) return 0.0;
    return v < -1.0 ? -1.0 : v > 1.0 ? 1.0 : v;
}

static bool quality_network_mode(void) {
    const char *m = MODES[g_mode_idx];
    return !strcmp(m, "circuit") || !strcmp(m, "truchet") ||
           !strcmp(m, "pipes") || !strcmp(m, "dungeon") ||
           !strcmp(m, "maze") || !strcmp(m, "streets") ||
           !strcmp(m, "neurons") || !strcmp(m, "mycelium") ||
           !strcmp(m, "delta");
}

static double macro_coherence(void) {
    if (!macro_role_ || macro_guided_count_ <= 0) return 1.0;
    double sum = 0.0;
    int seen = 0;
    for (int i = 0; i < W_ * H_; i++) {
        if (macro_role_[i] == MACRO_NONE) continue;
        uint64_t domain = dom_[i];
        if (pc64(domain) != 1) {
            sum += 0.5;
            seen++;
            continue;
        }
        sum += macro_tile_fit(i, __builtin_ctzll(domain));
        seen++;
    }
    return seen ? quality_clamp(sum / seen) : 0.5;
}

typedef struct {
    int x, y;
    double score;
    const char *reason;
} QualityHotspot;

static double quality_local_cell_score(int x, int y, const char **reason) {
    if (reason) *reason = "balanced";
    if (x < 0 || y < 0 || x >= W_ || y >= H_ || !dom_) {
        if (reason) *reason = "validity";
        return 0.0;
    }
    uint64_t domain = dom_[IDX(x, y)];
    if (!domain) {
        if (reason) *reason = "validity";
        return 0.0;
    }
    int choices = pc64(domain);
    if (choices > 1) {
        if (reason) *reason = "entropy";
        return quality_clamp(1.0 / (1.0 + 0.32 * (choices - 1)));
    }
    int tile = __builtin_ctzll(domain);
    double score = 1.0;
    int known = 0, good = 0, boundary_bad = 0;
    for (int d = 0; d < NDIR; d++) {
        int nx = x, ny = y;
        if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
        else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
        else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
        else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
        if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) {
            if (bounded_connector_mode() && tiles_[tile].e[d]) boundary_bad++;
            continue;
        }
        uint64_t neighbor = dom_[IDX(nx, ny)];
        if (pc64(neighbor) != 1) continue;
        known++;
        int other = __builtin_ctzll(neighbor);
        if ((cdir_[d][tile] >> other) & 1ULL) good++;
    }
    if (boundary_bad) {
        if (reason) *reason = "boundary";
        score *= 0.18;
    }
    if (known) {
        double edge_score = (double)good / known;
        score *= 0.55 + 0.45 * edge_score;
        if (!good && reason) *reason = "validity";
    }
    if (macro_role_ && macro_role_[IDX(x, y)] != MACRO_NONE) {
        double fit = macro_tile_fit(IDX(x, y), tile);
        score = 0.62 * score + 0.38 * fit;
        if (fit < 0.52 && reason) *reason = "branch";
    }
    if (quality_network_mode() && tiles_[tile].e[0] == 0 &&
        tiles_[tile].e[1] == 0 && tiles_[tile].e[2] == 0 && tiles_[tile].e[3] == 0 &&
        macro_role_ && macro_role_[IDX(x, y)] != MACRO_NONE) {
        score *= 0.45;
        if (reason) *reason = "coverage";
    }
    if (boundary_bad == 0 && good == known &&
        (!macro_role_ || macro_role_[IDX(x, y)] == MACRO_NONE || score >= 0.72)) {
        if (reason) *reason = "balanced";
    }
    return quality_clamp(score);
}

static QualityHotspot quality_hotspot(void) {
    QualityHotspot best = {0, 0, 1.0, "waiting"};
    if (W_ <= 0 || H_ <= 0 || !dom_) return best;
    for (int y = 0; y < H_; y++) {
        for (int x = 0; x < W_; x++) {
            const char *reason = "balanced";
            double score = quality_local_cell_score(x, y, &reason);
            if (score < best.score) {
                best.x = x;
                best.y = y;
                best.score = score;
                best.reason = reason;
            }
        }
    }
    return best;
}

static QualityMetrics quality_measure(bool final_map) {
    QualityMetrics q = {0};
    if (W_ <= 0 || H_ <= 0 || ntiles_ <= 0) return q;
    int cells = W_ * H_;
    int hist[MAXT] = {0};
    int decided = 0, empty = 0, active = 0;
    double degree_sum = 0.0;
    for (int i = 0; i < cells; i++) {
        uint64_t mask = dom_[i];
        int k = pc64(mask);
        if (!mask) { empty++; continue; }
        if (k != 1) continue;
        int tile = __builtin_ctzll(mask);
        hist[tile]++;
        decided++;
        int degree = 0;
        for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
        if (degree > 0) active++;
        degree_sum += degree;
    }

    int edge_known = 0, edge_good = 0;
    double smooth_sum = 0.0;
    for (int y = 0; y < H_; y++) {
        for (int x = 0; x < W_; x++) {
            int i = IDX(x, y);
            uint64_t a = dom_[i];
            if (pc64(a) != 1) continue;
            int ta = __builtin_ctzll(a);
            for (int d = 0; d < 2; d++) { /* count each pair once */
                int nx = x + (d == 1 ? 1 : 0);
                int ny = y + (d == 0 ? -1 : 0);
                if (g_torus) {
                    nx = (nx + W_) % W_;
                    ny = (ny + H_) % H_;
                } else if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
                int j = IDX(nx, ny);
                uint64_t b = dom_[j];
                if (pc64(b) != 1) continue;
                int tb = __builtin_ctzll(b);
                edge_known++;
                if ((cdir_[d][ta] >> tb) & 1ULL) edge_good++;
                int ba = tiles_[ta].e[0] >> 4;
                int bb = tiles_[tb].e[0] >> 4;
                int diff = abs(ba - bb);
                smooth_sum += 1.0 - (diff > 7 ? 7 : diff) / 7.0;
            }
        }
    }

    q.coverage = quality_clamp((double)decided / (double)cells);
    q.validity = empty ? 0.0 : edge_known ? quality_clamp((double)edge_good / edge_known) : 1.0;
    q.boundary = 1.0;
    if (bounded_connector_mode()) {
        int boundary_known = 0, boundary_good = 0;
        for (int y = 0; y < H_; y++) {
            for (int x = 0; x < W_; x++) {
                uint64_t mask = dom_[IDX(x, y)];
                if (pc64(mask) != 1) continue;
                int tile = __builtin_ctzll(mask);
                if (y == 0) { boundary_known++; boundary_good += !tiles_[tile].e[0]; }
                if (y == H_ - 1) { boundary_known++; boundary_good += !tiles_[tile].e[2]; }
                if (x == 0) { boundary_known++; boundary_good += !tiles_[tile].e[3]; }
                if (x == W_ - 1) { boundary_known++; boundary_good += !tiles_[tile].e[1]; }
            }
        }
        q.boundary = boundary_known ? quality_clamp((double)boundary_good / boundary_known) : 1.0;
        q.validity = quality_clamp(q.validity * q.boundary);
    }
    if (decided > 0) {
        double entropy = 0.0;
        for (int t = 0; t < ntiles_; t++) {
            if (!hist[t]) continue;
            double p = (double)hist[t] / decided;
            entropy -= p * log2(p);
        }
        double max_entropy = log2(ntiles_ < decided ? ntiles_ : decided);
        q.diversity = max_entropy > 0.0 ? quality_clamp(entropy / max_entropy) : 1.0;
    }
    q.smoothness = edge_known ? quality_clamp(smooth_sum / edge_known) : 1.0;
    q.stability = empty ? 0.0 : (final_map && decided == cells ? 1.0 : 0.8);

    if (!quality_network_mode()) {
        q.topology = 1.0;
    } else if (!active) {
        q.topology = decided ? 0.15 : 0.0;
    } else {
        const char *m = MODES[g_mode_idx];
        double ideal = !strcmp(m, "streets") ? 2.1 :
                       !strcmp(m, "neurons") ? 1.8 :
                       !strcmp(m, "mycelium") ? 1.6 :
                       !strcmp(m, "delta") ? 1.9 : 1.8;
        double balance = quality_clamp(1.0 - fabs(degree_sum / active - ideal) / 3.0);
        double active_ratio = quality_clamp((double)active / (double)(decided ? decided : 1));
        q.topology = quality_clamp(0.42 * balance + 0.34 * active_ratio +
                                   0.24 * macro_coherence());
    }
    QualityProfile profile = quality_profile();
    q.total = quality_clamp(profile.validity * q.validity +
                            profile.boundary * q.boundary +
                            profile.coverage * q.coverage +
                            profile.diversity * q.diversity +
                            profile.smoothness * q.smoothness +
                            profile.stability * q.stability +
                            profile.topology * q.topology);
    return q;
}

static void quality_trace_clear(void) {
    g_quality_live = -1.0;
    g_quality_validity_live = -1.0;
    g_quality_boundary_live = -1.0;
    g_quality_coverage_live = -1.0;
    g_quality_diversity_live = -1.0;
    g_quality_smoothness_live = -1.0;
    g_quality_stability_live = -1.0;
    g_quality_topology_live = -1.0;
    g_quality_trace_len_ = 0;
    g_quality_trace_pos_ = 0;
    memset(g_quality_trace_, 0, sizeof g_quality_trace_);
}

static void quality_record(QualityMetrics q) {
    g_quality_live = quality_clamp(q.total);
    g_quality_validity_live = quality_clamp(q.validity);
    g_quality_boundary_live = quality_clamp(q.boundary);
    g_quality_coverage_live = quality_clamp(q.coverage);
    g_quality_diversity_live = quality_clamp(q.diversity);
    g_quality_smoothness_live = quality_clamp(q.smoothness);
    g_quality_stability_live = quality_clamp(q.stability);
    g_quality_topology_live = quality_clamp(q.topology);
    g_quality_trace_[g_quality_trace_pos_] = g_quality_live;
    g_quality_trace_pos_ = (g_quality_trace_pos_ + 1) % QUALITY_TRACE_N;
    if (g_quality_trace_len_ < QUALITY_TRACE_N) g_quality_trace_len_++;
}

static void quality_json(FILE *f, QualityMetrics q) {
    QualityProfile profile = quality_profile();
    QualityHotspot hotspot = quality_hotspot();
    fprintf(f, "{\"focus\":\"%s\",\"total\":%.9g,\"validity\":%.9g,"
               "\"boundary\":%.9g,\"coverage\":%.9g,\"diversity\":%.9g,"
               "\"smoothness\":%.9g,\"stability\":%.9g,\"topology\":%.9g,"
               "\"profile_weights\":{\"validity\":%.9g,\"boundary\":%.9g,"
               "\"coverage\":%.9g,\"diversity\":%.9g,\"smoothness\":%.9g,"
               "\"stability\":%.9g,\"topology\":%.9g},\"hotspot\":{"
               "\"x\":%d,\"y\":%d,\"score\":%.9g,\"reason\":",
            profile.focus, quality_clamp(q.total), quality_clamp(q.validity),
            quality_clamp(q.boundary), quality_clamp(q.coverage),
            quality_clamp(q.diversity), quality_clamp(q.smoothness),
            quality_clamp(q.stability), quality_clamp(q.topology),
            profile.validity, profile.boundary, profile.coverage, profile.diversity,
            profile.smoothness, profile.stability, profile.topology,
            hotspot.x, hotspot.y, quality_clamp(hotspot.score));
    thermo_json_string(f, hotspot.reason);
    fputc('}', f);
    fputc('}', f);
}

static void quality_delta_json(FILE *f, QualityMetrics before, QualityMetrics after) {
    fprintf(f, "{\"total\":%.9g,\"validity\":%.9g,\"boundary\":%.9g,"
               "\"coverage\":%.9g,\"diversity\":%.9g,\"smoothness\":%.9g,"
               "\"stability\":%.9g,\"topology\":%.9g,\"focus\":",
            quality_signed_clamp(after.total - before.total),
            quality_signed_clamp(after.validity - before.validity),
            quality_signed_clamp(after.boundary - before.boundary),
            quality_signed_clamp(after.coverage - before.coverage),
            quality_signed_clamp(after.diversity - before.diversity),
            quality_signed_clamp(after.smoothness - before.smoothness),
            quality_signed_clamp(after.stability - before.stability),
            quality_signed_clamp(after.topology - before.topology));
    thermo_json_string(f, quality_profile().focus);
    fputc('}', f);
}

static double quality_reward(QualityMetrics before, QualityMetrics after,
                             int accepted, int rejected) {
    if (accepted < 0) accepted = 0;
    if (rejected < 0) rejected = 0;
    QualityProfile profile = quality_profile();
    double improvement = profile.validity * (after.validity - before.validity) +
                         profile.boundary * (after.boundary - before.boundary) +
                         profile.coverage * (after.coverage - before.coverage) +
                         profile.diversity * (after.diversity - before.diversity) +
                         profile.smoothness * (after.smoothness - before.smoothness) +
                         profile.stability * (after.stability - before.stability) +
                         profile.topology * (after.topology - before.topology);
    improvement = 0.75 * improvement + 0.25 * (after.total - before.total);
    double acceptance = accepted / (double)(accepted + rejected + 1);
    double penalty = rejected > 0 ? 0.08 * rejected : 0.0;
    return quality_signed_clamp(2.0 * improvement + 0.20 * acceptance - penalty);
}

/* ---------------- colors ---------------- */
static RGB hsv(double h, double s, double v) {
    h = fmod(h, 360.0); if (h < 0) h += 360;
    double c = v * s, x = c * (1 - fabs(fmod(h / 60, 2) - 1)), m = v - c;
    double r_, g_, b_;
    if (h < 60)       { r_ = c; g_ = x; b_ = 0; }
    else if (h < 120) { r_ = x; g_ = c; b_ = 0; }
    else if (h < 180) { r_ = 0; g_ = c; b_ = x; }
    else if (h < 240) { r_ = 0; g_ = x; b_ = c; }
    else if (h < 300) { r_ = x; g_ = 0; b_ = c; }
    else              { r_ = c; g_ = 0; b_ = x; }
    RGB o = {(uint8_t)((r_ + m) * 255), (uint8_t)((g_ + m) * 255), (uint8_t)((b_ + m) * 255)};
    return o;
}
static RGB lerp(RGB a, RGB b, double t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    RGB o = {(uint8_t)(a.r + (b.r - a.r) * t),
             (uint8_t)(a.g + (b.g - a.g) * t),
             (uint8_t)(a.b + (b.b - a.b) * t)};
    return o;
}
static RGB scalec(RGB c, double f) {
    RGB o = {(uint8_t)(c.r * f > 255 ? 255 : c.r * f < 0 ? 0 : c.r * f),
             (uint8_t)(c.g * f > 255 ? 255 : c.g * f < 0 ? 0 : c.g * f),
             (uint8_t)(c.b * f > 255 ? 255 : c.b * f < 0 ? 0 : c.b * f)};
    return o;
}
static RGB quality_heat_tint(RGB base, double score) {
    score = quality_clamp(score);
    RGB tint = score < 0.45 ? (RGB){255, 64, 76} :
               score < 0.75 ? (RGB){255, 190, 64} : (RGB){82, 214, 190};
    double amount = (1.0 - score) * 0.72;
    return lerp(base, tint, amount);
}
static const RGB C_AMBER = {245, 158, 11}, C_SKY = {56, 189, 248}, C_BG = {11, 14, 20};

/* blackbody ramp for fire mode */
static const RGB FIREPAL[8] = {
    {11, 10, 8}, {38, 18, 10}, {85, 22, 8}, {140, 42, 9},
    {196, 74, 12}, {232, 125, 19}, {245, 180, 49}, {252, 233, 160},
};
static const RGB GALPAL[8] = {
    {3, 3, 9}, {9, 6, 22}, {17, 11, 40}, {30, 16, 64},
    {50, 24, 94}, {76, 36, 126}, {110, 58, 158}, {150, 92, 188},
};
static const RGB CITYPAL[8] = {
    {4, 5, 12}, {7, 8, 17}, {12, 13, 24}, {20, 21, 34},
    {30, 31, 44}, {44, 44, 56}, {62, 60, 68}, {82, 78, 76},
};
static const RGB WINDOW_AMBER = {255, 190, 90};
static const RGB STAR_WARM = {255, 222, 190}, STAR_COOL = {225, 238, 255};
static const RGB AURPAL[8] = {
    {4, 4, 10}, {8, 14, 16}, {12, 40, 22}, {20, 78, 26},
    {44, 122, 34}, {96, 150, 80}, {170, 130, 110}, {236, 120, 160},
};
static const RGB MAZEPAL[2] = {{96, 88, 74}, {13, 12, 18}};
static const RGB DUNPAL[4] = {
    {26, 23, 34},   /* floor */
    {58, 52, 42},   /* stone */
    {112, 99, 76},  /* wall */
    {245, 158, 11}, /* torch */
};
static const RGB WAVEPAL[8] = {
    {5, 12, 30}, {8, 22, 54}, {12, 38, 88}, {18, 60, 118},
    {26, 90, 148}, {40, 122, 168}, {78, 156, 186}, {228, 242, 242},
};
/* koi pond: still, dark-to-lilypad water */
static const RGB KOIPAL[8] = {
    {10, 22, 44}, {16, 34, 64}, {24, 48, 88}, {36, 66, 116},
    {52, 90, 142}, {74, 118, 166}, {104, 148, 186}, {148, 190, 214},
};
/* lava: darkest crust -> white-hot melt */
static const RGB LAVAPAL[8] = {
    {14, 10, 8}, {34, 12, 8}, {76, 20, 9}, {128, 40, 10},
    {184, 72, 14}, {224, 122, 28}, {244, 180, 64}, {255, 238, 186},
};
/* mondrian uprights: painted plains + charcoal slab */
static const RGB MONDPAL[6] = {
    {233, 224, 198}, {240, 201, 52}, {32, 74, 164},
    {221, 44, 46}, {220, 214, 202}, {44, 38, 36},
};
/* sakura: plum night rising into pale rose */
static const RGB SKYPAL[8] = {
    {24, 14, 34}, {44, 22, 52}, {74, 34, 70}, {112, 52, 92},
    {152, 76, 110}, {194, 108, 128}, {226, 148, 148}, {246, 190, 172},
};
/* geode: deep violet veins opening into glacial teal */
static const RGB GEOPAL[8] = {
    {10, 8, 24}, {20, 14, 44}, {34, 22, 66}, {50, 34, 92},
    {62, 66, 128}, {58, 108, 152}, {70, 158, 172}, {96, 222, 208},
};
/* lantern festival: indigo night, warm smog breathing at the horizon */
static const RGB LNTPAL[8] = {
    {6, 8, 22}, {9, 12, 32}, {13, 16, 40}, {19, 20, 46},
    {28, 24, 50}, {40, 29, 52}, {56, 35, 52}, {78, 44, 52},
};
/* dunes: sun-bleached sky burning down into dark umber sand */
static const RGB DUNEPAL[8] = {
    {58, 32, 16}, {92, 50, 20}, {134, 78, 28}, {176, 112, 40},
    {206, 142, 60}, {226, 172, 90}, {240, 200, 132}, {248, 222, 172},
};
/* reef: sunlit aqua surface fading over the deep */
static const RGB REFPAL[8] = {
    {4, 18, 34}, {6, 34, 56}, {8, 54, 78}, {10, 76, 100},
    {14, 100, 122}, {22, 128, 144}, {48, 160, 166}, {110, 198, 196},
};
/* stained glass: jewel panes behind dark lead */
static const RGB STAINPAL[8] = {
    {148, 26, 32}, {188, 92, 18}, {196, 158, 24}, {38, 118, 52},
    {24, 84, 148}, {66, 44, 138}, {148, 40, 112}, {208, 176, 148},
};
static const RGB STREETPAL[8] = {
    {28, 34, 44}, {44, 52, 64}, {66, 74, 84}, {96, 102, 108},
    {134, 136, 132}, {176, 164, 126}, {228, 196, 112}, {248, 232, 170},
};
static const RGB NEURONPAL[8] = {
    {28, 12, 58}, {54, 18, 94}, {92, 28, 132}, {144, 38, 160},
    {196, 52, 168}, {232, 86, 180}, {246, 150, 210}, {255, 218, 238},
};
static const RGB MYCELIUMPAL[8] = {
    {12, 34, 28}, {16, 58, 38}, {22, 84, 48}, {34, 112, 56},
    {54, 140, 66}, {96, 166, 78}, {168, 184, 108}, {224, 214, 150},
};
static const RGB DELTAPAL[8] = {
    {3, 22, 34}, {4, 42, 58}, {6, 68, 82}, {8, 96, 104},
    {16, 126, 126}, {38, 158, 142}, {104, 190, 156}, {210, 224, 180},
};
static RGB network_bg(const char *mode) {
    if (!strcmp(mode, "streets")) return (RGB){7, 10, 16};
    if (!strcmp(mode, "neurons")) return (RGB){6, 3, 16};
    if (!strcmp(mode, "delta")) return (RGB){3, 19, 28};
    return (RGB){4, 16, 12};
}
static RGB network_color(const char *mode, int tile, int cx, int cy, double pulse) {
    int degree = 0;
    for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
    uint32_t h = hash3((uint32_t)(cx * 17 + tile), (uint32_t)(cy * 29 + degree), 731);
    if (!strcmp(mode, "streets")) {
        double signal = 0.80 + 0.20 * sin(now_ms() * 0.0017 + cx * 0.8 + cy * 0.33);
        int band = degree >= 3 ? 5 + (int)(h % 3) : degree + 1 + (int)(h % 2);
        if (band > 7) band = 7;
        RGB c = STREETPAL[band];
        if (degree >= 3) {
            /* Intersections read as lit crossings instead of generic circuit nodes. */
            c = lerp(c, STREETPAL[7], 0.30);
            signal *= 0.88 + 0.12 * sin(now_ms() * 0.003 + cx * 0.4 + cy * 0.6);
        } else if (degree == 1 && h % 37 == 5) {
            /* A sparse red marker gives dead ends a traffic-signal language. */
            c = lerp(c, (RGB){244, 86, 72}, 0.72);
        }
        return scalec(c, pulse * signal);
    }
    if (!strcmp(mode, "neurons")) {
        int band = (int)((h + (uint32_t)(now_ms() / 95)) % 8);
        RGB c = NEURONPAL[band];
        double action = 0.72 + 0.28 * sin(now_ms() * 0.006 + cx * 0.91 + cy * 1.37);
        if (degree >= 3) c = lerp(c, (RGB){255, 150, 230}, 0.22);
        return scalec(c, pulse * action);
    }
    if (!strcmp(mode, "delta")) {
        double tide = 0.84 + 0.16 * sin(now_ms() * 0.0018 + cx * 0.47 - cy * 0.22);
        int band = degree >= 3 ? 5 + (int)(h % 3) : degree == 2 ? 2 + (int)(h % 4) : 1 + (int)(h % 3);
        if (band > 7) band = 7;
        RGB c = DELTAPAL[band];
        if (degree >= 3) c = lerp(c, DELTAPAL[7], 0.35);
        if (degree == 1 && h % 19 == 3) c = lerp(c, (RGB){224, 198, 126}, 0.58);
        return scalec(c, pulse * tide);
    }
    int band = degree + (h % 3 == 0 ? 1 : 0);
    if (band > 7) band = 7;
    double breathe = 0.82 + 0.16 * sin(now_ms() * 0.0012 + cx * 0.23 + cy * 0.41);
    return scalec(MYCELIUMPAL[band], pulse * breathe);
}
static double hillshade(int cx, int cy, int tile) {
    int e0 = tiles_[tile].e[0] >> 4;
    int g = 0;
    int wx = (cx - 1 + W_) % W_;
    if (pc64(dom_[IDX(wx, cy)]) == 1)
        g += e0 - (tiles_[__builtin_ctzll(dom_[IDX(wx, cy)])].e[0] >> 4);
    int ny = (cy - 1 + H_) % H_;
    if (pc64(dom_[IDX(cx, ny)]) == 1)
        g += e0 - (tiles_[__builtin_ctzll(dom_[IDX(cx, ny)])].e[0] >> 4);
    double f = 1.0 + g * 0.16;
    return f > 1.35 ? 1.35 : f < 0.7 ? 0.7 : f;
}
static const RGB TRUCHET_DUOS[4][2] = {
    {{245, 158, 11}, {56, 189, 248}},
    {{232, 121, 249}, {163, 230, 53}},
    {{251, 113, 133}, {34, 211, 238}},
    {{250, 204, 21}, {167, 139, 250}},
};
static RGB duo_color(int colv, double pulse) {
    int ti = (g_theme & 7) >= 4 ? 0 : (g_theme & 3);
    return scalec(TRUCHET_DUOS[ti][colv - 1], pulse);
}

/* biome matrix [elevation 0..7][moisture 0..4]: deep ocean -> snowy peaks */
static RGB BIOMES_SEASONAL[3][8][5] = {
    { /* temperate (default) */
      {{14,36,72},{14,38,74},{15,40,76},{16,42,78},{17,44,80}},
      {{22,74,128},{22,78,132},{24,82,136},{26,86,140},{28,90,144}},
      {{52,132,168},{54,138,172},{56,144,176},{60,150,180},{64,156,184}},
      {{230,214,156},{208,198,140},{120,170,80},{96,160,70},{70,142,62}},
      {{182,178,112},{152,168,92},{72,132,58},{52,116,50},{38,102,46}},
      {{162,158,108},{120,148,86},{48,106,52},{40,88,54},{32,76,50}},
      {{138,126,108},{128,124,114},{114,116,110},{152,154,152},{142,146,146}},
      {{236,240,244},{234,239,243},{232,238,242},{230,237,241},{228,236,240}},
    },
    { /* arctic */
      {{10,18,40},{11,20,44},{12,22,48},{13,24,52},{14,26,56}},
      {{24,52,92},{26,56,98},{28,60,104},{30,64,110},{32,68,116}},
      {{90,140,180},{100,150,188},{110,160,196},{120,170,204},{130,180,212}},
      {{200,214,224},{210,222,230},{220,230,236},{228,236,240},{236,242,246}},
      {{168,186,200},{182,198,210},{196,210,220},{210,222,230},{222,232,238}},
      {{140,158,176},{154,172,190},{168,186,202},{182,198,212},{194,210,222}},
      {{120,134,152},{136,150,166},{152,166,180},{168,182,194},{184,198,208}},
      {{240,246,250},{238,244,249},{236,242,248},{234,240,247},{232,238,246}},
    },
    { /* autumn */
      {{12,30,60},{13,32,63},{14,34,66},{15,36,69},{16,38,72}},
      {{22,70,120},{24,74,125},{26,78,130},{28,82,135},{30,86,140}},
      {{50,126,162},{53,133,167},{56,140,172},{60,147,177},{64,153,181}},
      {{224,206,148},{216,178,110},{206,148,84},{196,124,70},{186,108,62}},
      {{198,142,66},{192,120,52},{184,102,46},{174,88,44},{164,78,46}},
      {{156,110,52},{146,94,48},{134,82,48},{122,74,50},{112,68,52}},
      {{130,118,100},{122,112,104},{114,108,110},{144,138,138},{136,132,136}},
      {{240,238,236},{238,235,232},{236,232,228},{234,229,226},{232,227,225}},
    },
};
static const RGB (*BIOMES)[5] = BIOMES_SEASONAL[0];
static RGB terrain_tint(RGB c) {
    if (!g_daycycle) return c;
    /* 3-minute day: warm dawn, neutral noon, orange dusk, blue night */
    double tod = fmod(now_ms() * 0.000093, 1.0);   /* full cycle ~3 min */
    double a = 2 * M_PI * tod;
    double r_tint = 0.10 * sin(a) - 0.06 * sin(2 * a);
    double b_tint = -0.12 * sin(a);
    double dim = 0.72 + 0.28 * (0.5 + 0.5 * sin(a));  /* darker nights */
    double rr = (c.r + r_tint * 255.0) * dim;
    double gg = c.g * dim;
    double bb = (c.b + b_tint * 255.0) * dim;
    if (rr < 0) rr = 0; if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; if (bb > 255) bb = 255;
    RGB o = { (uint8_t)rr, (uint8_t)gg, (uint8_t)bb };
    return o;
}
static RGB biome_color(int tile) {
    uint8_t v = tiles_[tile].e[0];
    return BIOMES[v >> 4][v & 15];
}
static RGB trace_color(int cx, int cy, double pulse) {
    bool circ = !strcmp(MODES[g_mode_idx], "circuit");
    bool pipes = !strcmp(MODES[g_mode_idx], "pipes");
    if (g_comp_ready && (circ || pipes)) {
        int i = IDX(cx, cy);
        int tt = pc64(dom_[i]) == 1 ? (int)__builtin_ctzll(dom_[i]) : -1;
        bool conn = tt >= 0 &&
                    (tiles_[tt].e[0] || tiles_[tt].e[1] || tiles_[tt].e[2] || tiles_[tt].e[3]);
        if (conn && comp_[i] >= 0 && comp_[i] < n_comp_) {
            RGB c = comp_col_[comp_[i]];
            if (pipes) /* shift into the water family: teals, seafoam, deep blues */
                c = hsv(fmod(comp_[i] * 137.508 + 155, 360.0), 0.55, 0.88);
            return scalec(c, pulse);
        }
        return scalec(circ ? (RGB){88, 94, 110} : (RGB){48, 66, 84}, pulse);
    }
    double hue;
    int th = (g_theme & 7) >= 4 ? 0 : (g_theme & 3);
    switch (th) {
        case 1: hue = 185 + 35 * sin((cx + cy) * 0.09); break; /* ocean shimmer */
        case 2: hue = fmod(340 + 50.0 * cy / (H_ > 1 ? H_ : 1), 360); break; /* sunset rows */
        case 3: return scalec(hsv(187, 0.55, 0.9), pulse);     /* mono ice */
        default: hue = fmod(cx * 6.0 + cy * 10.0, 360.0); break; /* rainbow diag */
    }
    return scalec(hsv(hue, 0.82, 0.95), pulse);
}

/* ---------------- growable byte buffer ---------------- */
typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void buf_init(Buf *x) { x->b = NULL; x->n = x->cap = 0; }
static void buf_put(Buf *x, const void *p, size_t n) {
    if (!n) return;
    if (n > SIZE_MAX - x->n) {
        fputs("buffer too large\n", stderr);
        exit(1);
    }
    size_t need = x->n + n;
    if (need > x->cap) {
        size_t cap = x->cap ? x->cap : 4096;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        uint8_t *nb = realloc(x->b, cap);
        if (!nb) { perror("realloc"); exit(1); }
        x->b = nb;
        x->cap = cap;
    }
    memcpy(x->b + x->n, p, n);
    x->n += n;
}
static void buf_u8(Buf *x, int v) { uint8_t c = (uint8_t)v; buf_put(x, &c, 1); }
static void buf_u16(Buf *x, int v) {
    uint8_t c[2] = {(uint8_t)(v & 255), (uint8_t)((v >> 8) & 255)};
    buf_put(x, c, 2);
}
static void buf_free(Buf *x) { free(x->b); x->b = NULL; x->n = x->cap = 0; }

/* tear-free frame presentation (ignored by terminals without support) */
static void frame_begin(void) { fputs("\x1b[?2026h", stdout); }
static void frame_end(void) { fputs("\x1b[?2026l", stdout); fflush(stdout); }

/* ---------------- framebuffer ---------------- */
static char *fb_; static size_t fblen_, fbcap_;
static void fb_reset(void) { fblen_ = 0; }
static void fb_reserve(size_t n) {
    if (n > SIZE_MAX - fblen_) {
        fputs("framebuffer too large\n", stderr);
        exit(1);
    }
    size_t need = fblen_ + n;
    if (need > fbcap_) {
        size_t cap = fbcap_ ? fbcap_ : (size_t)1 << 20;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        fb_ = realloc(fb_, cap);
        if (!fb_) { perror("realloc"); exit(1); }
        fbcap_ = cap;
    }
}
static void fb_puts(const char *s) {
    size_t n = strlen(s); fb_reserve(n);
    memcpy(fb_ + fblen_, s, n); fblen_ += n;
}
static int g_rowdim = 100;
static RGB crt(RGB c) {
    if (!g_crt || g_rowdim >= 100) return c;
    return scalec(c, 0.72 + 0.28 * (g_rowdim / 100.0));
}
static void fb_fg(RGB c) {
    c = crt(c);
    fb_reserve(32);
    fblen_ += snprintf(fb_ + fblen_, 32, "\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b);
}
static void fb_bg(RGB c) {
    c = crt(c);
    fb_reserve(32);
    fblen_ += snprintf(fb_ + fblen_, 32, "\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b);
}
static void fb_braille(uint8_t bits) {
    if (!bits) { fb_puts(" "); return; }
    uint32_t cp = 0x2800u + bits;
    fb_reserve(4);
    fb_[fblen_++] = (char)(0xE0 | (cp >> 12));
    fb_[fblen_++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    fb_[fblen_++] = (char)(0x80 | (cp & 0x3F));
}
static void fb_half(RGB top, RGB bot) {
    fb_fg(top); fb_bg(bot); fb_puts("\xe2\x96\x80\xe2\x96\x80");
}
static void buf_puts(Buf *x, const char *s) { buf_put(x, s, strlen(s)); }

/* ---------------- pixel art (8x8 px per cell) ---------------- */
static void art_circuit(int tile, bool px[8][8]) {
    memset(px, 0, 64);
    px[3][3] = px[3][4] = px[4][3] = px[4][4] = true;
    if (tiles_[tile].e[0]) px[3][0] = px[4][0] = px[3][1] = px[4][1] = px[3][2] = px[4][2] = true;
    if (tiles_[tile].e[2]) px[3][5] = px[4][5] = px[3][6] = px[4][6] = px[3][7] = px[4][7] = true;
    if (tiles_[tile].e[3]) px[0][3] = px[0][4] = px[1][3] = px[1][4] = px[2][3] = px[2][4] = true;
    if (tiles_[tile].e[1]) px[5][3] = px[5][4] = px[6][3] = px[6][4] = px[7][3] = px[7][4] = true;
}
static void art_truchet(int tile, bool px[8][8]) {
    memset(px, 0, 64);
    int c1 = -1, c2 = -1;
    for (int r = 0; r < 4; r++)
        if (tiles_[tile].e[r]) { if (c1 < 0) c1 = r; else c2 = r; }
    if (c2 < 0) { /* stub: endpoint disc + stem */
        double mx = c1 == 0 ? 3.5 : c1 == 1 ? 5.5 : c1 == 2 ? 3.5 : 2.0;
        double my = c1 == 0 ? 2.0 : c1 == 1 ? 3.5 : c1 == 2 ? 5.5 : 3.5;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                double dx = x + .5 - mx, dy = y + .5 - my;
                if (dx * dx + dy * dy <= 3.1) px[x][y] = true;
            }
        if (c1 == 0)      px[3][0] = px[4][0] = px[3][1] = px[4][1] = true;
        else if (c1 == 1) px[7][3] = px[7][4] = px[6][3] = px[6][4] = true;
        else if (c1 == 2) px[3][7] = px[4][7] = px[3][6] = px[4][6] = true;
        else              px[0][3] = px[0][4] = px[1][3] = px[1][4] = true;
        return;
    }
    double ccx = (c1 == 0 || c1 == 1) ? 8 : 0;
    double ccy = (c1 == 0 || c1 == 3) ? 0 : 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            double dx = x + 0.5 - ccx, dy = y + 0.5 - ccy;
            double dd = sqrt(dx * dx + dy * dy);
            if (dd >= 3.4 && dd <= 4.6) px[x][y] = true;
        }
}
/* maze: cls 0=floor(path), 1=wall */
static void art_maze(int tile, uint8_t cls[8][8]) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) cls[x][y] = 1;
    int e[4];
    for (int d = 0; d < NDIR; d++) e[d] = tiles_[tile].e[d];
    /* center junction always open when any passage exists */
    bool any = e[0] || e[1] || e[2] || e[3];
    if (any)
        for (int y = 3; y <= 4; y++)
            for (int x = 3; x <= 4; x++) cls[x][y] = 0;
    if (e[0]) { cls[3][0] = cls[4][0] = cls[3][1] = cls[4][1] = 0; }
    if (e[2]) { cls[3][7] = cls[4][7] = cls[3][6] = cls[4][6] = 0; }
    if (e[3]) { cls[0][3] = cls[0][4] = cls[1][3] = cls[1][4] = 0; }
    if (e[1]) { cls[7][3] = cls[7][4] = cls[6][3] = cls[6][4] = 0; }
}
/* classes: 0 floor, 1 stone, 2 wall, 3 torch */
static void art_dungeon(int tile, uint8_t cls[8][8]) {
    int e0 = tiles_[tile].e[0], e1 = tiles_[tile].e[1], e2 = tiles_[tile].e[2], e3 = tiles_[tile].e[3];
    bool has_floor = e0 == 2 || e1 == 2 || e2 == 2 || e3 == 2;
    uint8_t base = has_floor ? 0 : 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) cls[x][y] = base;
    int e[4] = {e0, e1, e2, e3};
    for (int d = 0; d < NDIR; d++) {
        if (e[d] != 1) continue;
        if (d == 0) for (int x = 0; x < 8; x++) cls[x][0] = cls[x][1] = 2;
        if (d == 2) for (int x = 0; x < 8; x++) cls[x][7] = cls[x][6] = 2;
        if (d == 3) for (int y = 0; y < 8; y++) cls[0][y] = cls[1][y] = 2;
        if (d == 1) for (int y = 0; y < 8; y++) cls[7][y] = cls[6][y] = 2;
    }
    if (tiles_[tile].flag) {
        int d = 0;
        while (e[d] != 1) d++;
        if (d == 0) { cls[3][3] = cls[4][3] = cls[3][2] = cls[4][2] = 3; }
        else if (d == 2) { cls[3][4] = cls[4][4] = cls[3][5] = cls[4][5] = 3; }
        else if (d == 3) { cls[2][3] = cls[2][4] = cls[3][3] = cls[3][4] = 3; }
        else { cls[5][3] = cls[5][4] = cls[4][3] = cls[4][4] = 3; }
    }
}
static int truchet_col(int tile) {
    for (int r = 0; r < 4; r++)
        if (tiles_[tile].e[r]) return tiles_[tile].e[r];
    return 1;
}

static const uint8_t BRAILLE_BIT[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};

/* ---------------- transient status note ---------------- */
static char g_note[128];
static double g_note_until = 0;
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
/* window title: show the live world (called on mode changes only) */
static void set_note(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_note, sizeof g_note, fmt, ap);
    va_end(ap);
    g_note_until = now_ms() + 2200;
}

/* ---------------- help overlay ---------------- */
static void render_help(void) {
    static const char *lines[] = {
        "  W A V E   F U N C T I O N   C O L L A P S E  ",
        "",
        "  modes: circuit terrain truchet fire waves dungeon",
        "  maze galaxy city aurora matrix pipes mondrian",
        "  koi lava sakura geode lantern dunes",
        "  reef stained streets neurons mycelium delta",
        "",
        "  space   new map              m     next mode",
        "  y       color theme          c     auto-cycle modes",
        "  +/-     collapse speed       p     pause",
        "  g       record gif           s     save image",
        "  h       this help            z     all-worlds sheet\n",
        "  r       raytrace view        e     entropy view\n",
        "  i       isometric view       , .    scrub time\n",
        "  T       thermo solver        R     reset thermo learning\n",
        "  l       quality observatory   P     pin/unpin hovered cell\n",
        "  Q       quality heatmap      E     evolve/rank seed variants\n",
        "  wasd    hero walk\n",
        "  n       zen: worlds morph    q     quit",
        "",
        "  mouse: left-click force-collapse a cell,",
        "         right-click carve it back open",
        "",
        "  press any key to return",
    };
    char st[256];
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    int row = 3;
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++, row++) {
        snprintf(st, sizeof st, "\x1b[%d;6H", row);
        fb_puts(st);
        fb_fg((RGB){148, 200, 255});
        fb_puts(lines[i]);
    }
    fb_puts("\x1b[0m");
    fwrite(fb_, 1, fblen_, stdout);
    fflush(stdout);
}

/* ---------------- render ---------------- */
static void render_status(int vh, int ch);
static bool g_paused = false;
static bool g_slowmo = false;
static bool g_entropy_view = false;
static int g_hover_k = -1, g_hover_x = -1, g_hover_y = -1;
/* returns true + color if an adjacent cell is a light source */
static bool glow_neighbor(int wx, int wy, RGB *out) {
    const char *mode = MODES[g_mode_idx];
    const bool m_circuit = !strcmp(mode, "circuit"), m_terrain = !strcmp(mode, "terrain"), m_truchet = !strcmp(mode, "truchet"), m_dungeon = !strcmp(mode, "dungeon"), m_city = !strcmp(mode, "city"), m_pipes = !strcmp(mode, "pipes");
    static const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int i = 0; i < 8; i++) {
        int nx = wx + DX[i], ny = wy + DY[i];
        if (g_torus || (m_terrain)) {
            nx = (nx + W_) % W_;
            ny = (ny + H_) % H_;
        } else if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
        if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
        uint64_t d = dom_[IDX(nx, ny)];
        if (pc64(d) != 1) continue;
        int t = __builtin_ctzll(d);
        if (m_circuit || m_pipes || !strcmp(mode, "streets") || !strcmp(mode, "delta")) {
            /* connector facing us? */
            int dx = -DX[i], dy = -DY[i];
            int side = dy == -1 ? 0 : dx == 1 ? 1 : dy == 1 ? 2 : 3;
            if (tiles_[t].e[side]) {
                *out = trace_color(wx, wy, 0.30);
                return true;
            }
        } else if (m_truchet) {
            for (int r = 0; r < NDIR; r++) {
                if (tiles_[t].e[r]) {
                    *out = scalec(duo_color(truchet_col(t), 0.32), 1);
                    return true;
                }
            }
        } else if (m_dungeon && tiles_[t].flag) {
            *out = scalec((RGB){245, 158, 11}, 0.35);
            return true;
        } else if (m_city) {
            int band = tiles_[t].e[0] >> 4;
            if (band >= 3 && (hash3((uint32_t)nx, (uint32_t)ny, 77) % 100) < 16) {
                *out = scalec((RGB){255, 190, 90}, 0.22);
                return true;
            }
        }
    }
    return false;
}

/* ordered dithering: 8x8 Bayer matrix for smooth halftone gradients */
static const uint8_t BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};
/* fractional level into nearest two palette bands (sub-level 0..99) */
static int dither_band(int band, int wx, int wy, int px, int py) {
    uint8_t t = BAYER8[(wy * 8 + py) & 7][(wx * 8 + px) & 7];
    /* per-cell sub-level from hash so cells don't all dither identically */
    uint32_t frac = hash3((uint32_t)wx, (uint32_t)wy, 7) % 100;
    int b = band + (frac > (uint32_t)t * 100 / 64 ? 1 : 0);
    return b < 0 ? 0 : b > 7 ? 7 : b; /* palettes are 8 entries */
}
static int elev_of_cell(int x, int y);
static int clampb(int b) { return b < 0 ? 0 : b > 7 ? 7 : b; }
static int near_torch(int wx, int wy) {
    static const int DX3[4] = {1, -1, 0, 0};
    static const int DY3[4] = {0, 0, 1, -1};
    int torches = 0;
    for (int i = 0; i < 4; i++) {
        int nx = (wx + DX3[i] + W_) % W_, ny = (wy + DY3[i] + H_) % H_;
        uint64_t d = dom_[IDX(nx, ny)];
        if (pc64(d) == 1 && tiles_[__builtin_ctzll(d)].flag) torches++;
    }
    return torches;
}
static int elev_surround(int wx, int wy) {
    int s = 0;
    static const int DX2[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY2[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (int i = 0; i < 8; i++) {
        int nx = (wx + DX2[i] + W_) % W_, ny = (wy + DY2[i] + H_) % H_;
        uint64_t d = dom_[IDX(nx, ny)];
        if (pc64(d) == 1) {
            int t = __builtin_ctzll(d);
            bool floorish = tiles_[t].e[0] == 2 || tiles_[t].e[1] == 2 ||
                            tiles_[t].e[2] == 2 || tiles_[t].e[3] == 2;
            if (!floorish) s++;
        }
    }
    return s;
}
static int px_check_pad(int chi, int sub) {
    return (chi == 1 || chi == 2) && (sub == 1);
}

/* ---------------- zen mode: worlds dissolve into each other ---------------- */
static bool g_zen = false;      /* no hard restarts: the next world morphs in */
static RGB *ghost_ = NULL;      /* previous world's per-cell colors */
static int ghost_w_ = 0, ghost_h_ = 0;
static long g_ghost_t0 = 0;
static long g_ghost_ms = 3800;  /* set per transition: scaled to collapse time */
static RGB img_px(int cx, int cy, int ix, int iy, int art);
static double ghost_alpha(void) {
    if (!g_zen || g_ghost_t0 == 0) return 0;
    double a = 1.0 - (now_ms() - g_ghost_t0) / (double)g_ghost_ms;
    if (a <= 0) return 0;
    if (a > 1) a = 1;
    return a * a * (3 - 2 * a); /* smoothstep: gentle landing, gentle exit */
}
/* per-cell dissolve: four choreographies, picked per transition —
 * scatter+radial sweep, a left-to-right curtain, an iris from the
 * center, or a diagonal pass — all softened by a little scatter */
static int g_ghost_style = 0;
static double ghost_cell_alpha(int x, int y) {
    double a = ghost_alpha();
    if (a <= 0) return 0;
    uint32_t h = hash3((uint32_t)x, (uint32_t)y, 4242);
    double dx = (x - W_ * 0.5) / (W_ * 0.5 + 0.5);
    double dy = (y - H_ * 0.5) / (H_ * 0.5 + 0.5);
    double rad = sqrt(dx * dx + dy * dy) * 0.7071; /* 0 center .. ~1 corner */
    double stag = (h % 1000) / 1000.0 * 0.18;
    double off;
    switch (g_ghost_style) {
        case 1: off = stag + (x / (double)W_) * 0.50; break;                 /* curtain */
        case 2: off = rad * 0.52; break;                                     /* iris */
        case 3: off = stag + ((x + y * 2) % (W_ + H_)) /
                        (double)(W_ + H_) * 0.50; break;                     /* diagonal */
        default: off = stag * 1.4 + rad * 0.30; break;                       /* scatter */
    }
    double t = (a - off) / 0.45;
    return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t);
}
static bool ghosting(void) {
    return g_zen && !g_entropy_view && ghost_w_ == W_ && ghost_h_ == H_ &&
           ghost_alpha() > 0.004;
}
static void zen_capture(void) {
    if (ghost_w_ != W_ || ghost_h_ != H_ || !ghost_) {
        free(ghost_);
        ghost_ = malloc(sizeof(RGB) * (size_t)W_ * H_);
        if (!ghost_) { ghost_w_ = ghost_h_ = 0; return; }
    }
    ghost_w_ = W_; ghost_h_ = H_;
    int art = strcmp(MODES[g_mode_idx], "terrain") ? 8 : 16;
    for (int y = 0; y < H_; y++)
        for (int x = 0; x < W_; x++)
            ghost_[IDX(x, y)] = img_px(x, y, art / 2, art / 2, art);
    /* the ghost should outlive the incoming collapse: scale its fade to
     * how long this grid takes to re-weave at the current pace */
    g_ghost_ms = (long)((double)W_ * H_ / (g_speed > 0 ? g_speed : 3500) * 1000 * 0.85);
    if (g_ghost_ms < 2000) g_ghost_ms = 2000;
    if (g_ghost_ms > 9000) g_ghost_ms = 9000;
    g_ghost_style = (int)(rnd() % 4); /* a new choreography every morph */
    g_ghost_t0 = now_ms();
}

static void paint_cell(int wx, int wy, int sub, double pulse) {
    const char *mode = MODES[g_mode_idx];
    const bool m_circuit = !strcmp(mode, "circuit"), m_terrain = !strcmp(mode, "terrain"), m_truchet = !strcmp(mode, "truchet"), m_fire = !strcmp(mode, "fire"), m_waves = !strcmp(mode, "waves"), m_dungeon = !strcmp(mode, "dungeon"), m_maze = !strcmp(mode, "maze"), m_galaxy = !strcmp(mode, "galaxy"), m_city = !strcmp(mode, "city"), m_aurora = !strcmp(mode, "aurora"), m_matrix = !strcmp(mode, "matrix"), m_pipes = !strcmp(mode, "pipes"), m_mondrian = !strcmp(mode, "mondrian"), m_koi = !strcmp(mode, "koi"), m_lava = !strcmp(mode, "lava"), m_sakura = !strcmp(mode, "sakura"), m_geode = !strcmp(mode, "geode"), m_lantern = !strcmp(mode, "lantern"), m_dunes = !strcmp(mode, "dunes"), m_reef = !strcmp(mode, "reef"), m_stained = !strcmp(mode, "stained"), m_streets = !strcmp(mode, "streets"), m_neurons = !strcmp(mode, "neurons"), m_mycelium = !strcmp(mode, "mycelium"), m_delta = !strcmp(mode, "delta");
    bool braille = !m_terrain;
            if (g_entropy_view && pc64(dom_[IDX(wx, wy)]) > 1) {
                int k2 = pc64(dom_[IDX(wx, wy)]);
                float frac2 = 1.0f - k2 / (float)ntiles_;
                RGB heat = hsv(50 + 300 * frac2, 0.85, 0.85);
                fb_fg(heat);
                fb_bg(heat);
                fb_puts(" ");
                fb_fg((RGB){1, 1, 1});
                return;
            }
            int cy = wy;
            {
                int cx = wx;
                uint64_t d = dom_[IDX(wx, cy)];
                int k = pc64(d);
                if (braille) {
                    for (int chi = 0; chi < 4; chi++) {
                        uint8_t bits = 0;
                        RGB col = {0, 0, 0};
                        if (k == 1) {
                            int t = __builtin_ctzll(d);
                            if (m_dungeon || m_maze) {
                                uint8_t cls[8][8];
                                bool maz = m_maze;
                                int ncls = maz ? 2 : 4;
                                (void)ncls;
                                if (maz) art_maze(t, cls); else art_dungeon(t, cls);
                                const RGB *pal = maz ? MAZEPAL : DUNPAL;
                                /* fireflies drift through dark floors */
                                if (!maz && !g_no_weather && k == 1) {
                                    double ffx = cx + sin(now_ms() * 0.00033 + (wx * 13 + wy * 7) * 1.7);
                                    double ffy = wy + cos(now_ms() * 0.00041 + (wx * 5 + wy * 17) * 1.3);
                                    int fxc = (int)ffx, fyc = (int)ffy;
                                    if ((fxc == wx || fxc == (wx + 1) % W_) &&
                                        (fyc == wy || fyc == (wy + 1) % H_) &&
                                        cls[4][4] == 0) {
                                        double blink = 0.5 + 0.5 * sin(now_ms() * 0.004 + (wx * 31 + wy * 7) * 2.3);
                                        if (blink > 0.25) {
                                            bits = BRAILLE_BIT[(wx + wy) & 3][(wx >> 2) & 1];
                                            fb_fg(scalec((RGB){210, 180, 60}, pulse * blink));
                                            fb_braille(bits);
                                            continue;
                                        }
                                    }
                                }
                                int counts[4] = {0, 0, 0, 0};
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (cls[chi * 2 + xx][sub * 4 + yy])
                                            counts[cls[chi * 2 + xx][sub * 4 + yy]]++;
                                int pickc = -1;
                                for (int c2 = ncls - 1; c2 >= 0; c2--)
                                    if (counts[c2]) { pickc = c2; break; }
                                if (pickc < 0) pickc = 1;
                                if (!maz && counts[3]) pickc = 3;
                                else if (!maz) {
                                    /* prefer wall over stone over floor for mixed chars */
                                    if (counts[2]) pickc = 2;
                                    else if (counts[1]) pickc = 1;
                                    else if (counts[0]) pickc = 0;
                                }
                                bits = 0;
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (cls[chi * 2 + xx][sub * 4 + yy] == pickc)
                                            bits |= BRAILLE_BIT[yy][xx];
                                uint32_t j = hash3((uint32_t)(cx * 16 + chi * 2),
                                                   (uint32_t)(cy * 16 + sub * 4), 5) % 1000;
                                col = scalec(pal[pickc], pulse * (0.9 + j * 0.0002));
                                if (maz) {
                                    /* wall breathing: subtle warm pulse along borders */
                                    double br2 = 1.0 + 0.12 * sin(now_ms() * 0.0016 +
                                                                  (wx * 7 + wy * 13) * 0.8);
                                    col = scalec(col, br2);
                                }
                                if (!maz) {
                                    /* enclosing rock darkens the chamber */
                                    int surround = (elev_surround(wx, wy)) ;
                                    col = scalec(col, 1.0 - surround * 0.16);
                                    /* torch warmth breathes: slow heat wave */
                                    int torchn = near_torch(wx, wy);
                                    if (torchn) {
                                        double warm = 0.5 + 0.5 * sin(now_ms() * 0.0009 + wx * 1.7 + wy * 2.1);
                                        col = lerp(col, (RGB){220, 150, 60}, torchn * 0.16 * warm);
                                    }
                                    /* crawler torchlight: world falls to darkness away from hero */
                                    if (g_hero_on && g_nworlds == 1) {
                                        double _dx = wx - g_hx, _dy = wy - g_hy;
                                        double _distsq = _dx * _dx + _dy * _dy;
                                        double light = 1.0 - 0.115 * sqrt(_distsq);
                                        if (light < 0.10) light = 0.10;
                                        col = scalec(col, light);
                                        if (wx == g_hx && wy == g_hy) {
                                            bits = BRAILLE_BIT[0][1] | BRAILLE_BIT[1][0] |
                                                   BRAILLE_BIT[1][1] | BRAILLE_BIT[2][1];
                                            col = scalec((RGB){255, 236, 170}, pulse);
                                        }
                                    }
                                }
                            } else if (m_city) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t h2c = hash3((uint32_t)(cx * 23 + chi),
                                                     (uint32_t)(cy * 19 + sub * 3), 77);
                                bits = (uint8_t)((h2c ^ (h2c >> 4)) | 0x80u);
                                col = scalec(CITYPAL[dither_band(band, wx, wy, chi * 2, sub * 8)], pulse);
                                if (band <= 1 && h2c % 1000 < 14) {
                                    /* stars over the skyline */
                                    bits = BRAILLE_BIT[(h2c >> 2) & 3][(h2c >> 4) & 1];
                                    double ph2 = now_ms() * 0.005 + (h2c % 628) * 0.01;
                                    col = scalec(STAR_COOL, pulse * (0.4 + 0.6 * (0.5 + 0.5 * sin(ph2))));
                                } else if (band >= 3 && h2c % 100 < 16) {
                                    /* lit windows, slow flicker */
                                    double ph2 = now_ms() * 0.0011 + (h2c % 97) * 0.11;
                                    if (sin(ph2) > -0.55)
                                        col = lerp(CITYPAL[band], WINDOW_AMBER,
                                                   0.55 + 0.45 * sin(ph2 + 1.2));
                                } else if (band >= 6 && h2c % 211 == 14) {
                                    /* red beacon blinking on tallest towers */
                                    double bl2 = sin(now_ms() * 0.008 + (h2c % 61));
                                    if (bl2 > 0)
                                        col = lerp(CITYPAL[band], (RGB){255, 60, 50},
                                                   0.4 + 0.6 * bl2);
                                }
                            } else if (m_streets || m_neurons || m_mycelium || m_delta) {
                                bool netpx[8][8];
                                art_circuit(t, netpx);
                                bits = 0;
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (netpx[chi * 2 + xx][sub * 4 + yy])
                                            bits |= BRAILLE_BIT[yy][xx];
                                col = network_color(mode, t, wx, wy, pulse);
                                if ((m_streets || m_delta) && !tiles_[t].e[0] && !tiles_[t].e[1] &&
                                    !tiles_[t].e[2] && !tiles_[t].e[3])
                                    col = scalec(network_bg(mode), pulse);
                            } else if (m_aurora) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t h3 = hash3((uint32_t)(cx * 31 + chi), (uint32_t)cy, 123);
                                /* drifting shimmer: vertical ripple per column */
                                double drift = sin(cy * 0.37 - now_ms() * 0.0012 + cx * 0.21);
                                int tb = dither_band(clampb(band + (int)round(drift * (band > 2 ? 1.1 : 0.35))),
                                                     wx, wy, chi * 2, sub * 8);
                                bits = (uint8_t)((h3 ^ (h3 >> 3)) | 0x40u);
                                uint32_t twk = hash3((uint32_t)cx * 7 + chi, (uint32_t)cy * 3, 555);
                                double tw = 0.55 + 0.45 * sin(now_ms() * 0.003 + (twk % 628) * 0.01);
                                col = scalec(AURPAL[tb], pulse * tw);
                                /* wandering fabric folds */
                                double fold = sin(wx * 0.6 + now_ms() * 0.0009 + wy * 0.3);
                                if (fold > 0.72 && band > 1)
                                    col = scalec(col, 1.22);
                                /* stars above the curtain */
                                if (band <= 1 && h3 % 173 == 9) {
                                    bits = BRAILLE_BIT[(h3 >> 2) & 3][(h3 >> 4) & 1];
                                    col = scalec((RGB){235, 240, 255}, pulse * (0.5 + 0.5 * sin(now_ms() * 0.004 + (h3 % 628))));
                                }
                            } else if (m_matrix) {
                                int band = tiles_[t].e[0] >> 4;
                                int dband = dither_band(band + 1, wx, wy, chi * 2, sub * 8);
                                double speed = 3.0 + dband * 0.7;
                                int height = H_ * 2;
                                int tnow = (int)(now_ms() / 34);
                                int head = ((wx * 131 + tnow * (int)speed) % height + height) % height;
                                int row = cy * 2 + sub;
                                /* distance above falling head */
                                int dist = (row - head + height) % height;
                                if (row == head) {
                                    bits = 0xFF;
                                    col = scalec((RGB){0, 255, 96}, pulse);
                                    /* occasionally a white-hot glyph head */
                                    if (hash3((uint32_t)wx, (uint32_t)(tnow / 40), 771) % 13 == 0)
                                        col = scalec((RGB){240, 255, 245}, pulse);
                                } else if (dist > 0 && dist < band + 3) {
                                    bits = (uint8_t)(hash3((uint32_t)wx, (uint32_t)row, tnow) &
                                                     (0xAAu << ((row & 1) * 2)));
                                    double fade = 1.0 - (double)dist / (band + 3.0);
                                    col = scalec((RGB){0, 255, 96}, pulse * fade * 0.9);
                                } else {
                                    bits = 0;
                                }
                                if (!bits) {
                                    /* faint background shimmer in active columns */
                                    uint32_t bgp = hash3((uint32_t)wx, (uint32_t)row, 999);
                                    if ((bgp % 100) < (uint32_t)band * 2u) bits = 0x05;
                                    if (bits) col = scalec((RGB){0, 90, 40}, pulse * 0.6);
                                }
                            } else if (m_galaxy) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t h = hash3((uint32_t)(cx * 31 + chi),
                                                   (uint32_t)(cy * 17 + sub * 3 + chi), 99);
                                uint32_t tw = hash3((uint32_t)(cx * 13 + chi),
                                                    (uint32_t)(cy * 7 + sub), 555);
                                if (h % 1000 < 10 + (unsigned)band * 5) {
                                    /* a star: single dot, twinkling */
                                    bits = BRAILLE_BIT[(tw >> 2) & 3][(tw >> 4) & 1];
                                    double ph = ((now_ms() * 0.006) + (double)(tw % 628) * 0.01);
                                    double br = 0.45 + 0.55 * (0.5 + 0.5 * sin(ph));
                                    col = scalec((tw & 1) ? STAR_WARM : STAR_COOL,
                                                 pulse * br);
                                } else {
                                    /* dithered nebula gas */
                                    bits = (uint8_t)(h ^ (h >> 7));
                                    int tb = dither_band(clampb(band + (int)(tw % 3) - 1),
                                                         wx, wy, chi * 2, sub * 8);
                                    col = scalec(GALPAL[tb], pulse);
                                }
                            } else if (m_koi) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t hk = hash3((uint32_t)(cx * 23 + chi),
                                                    (uint32_t)(cy * 17 + sub * 5), 424);
                                bits = 0xFF;
                                /* slow caustic shimmer */
                                double caus = sin(cx * 0.9 + now_ms() * 0.0008 + sub * 0.8)
                                            + sin(cy * 1.1 - now_ms() * 0.0006 + cx * 0.4);
                                int tb = dither_band(clampb(band + (caus > 1.5 ? 0 : caus < -1.5 ? 1 : 0)),
                                                     wx, wy, chi * 2, sub * 8);
                                col = scalec(KOIPAL[tb], pulse * (0.92 + 0.08 * caus));
                                /* lily pads on shallows */
                                if (band <= 2 && hk % 97 < 3) {
                                    bits = 0xCC >> ((wx + wy) & 1);
                                    col = scalec((RGB){36, 104, 54}, pulse);
                                }
                                /* koi glide through, one fish per few cells */
                                double tsec = now_ms() * 0.001;
                                for (int f = 0; f < n_koi_; f++) {
                                    float fx = fmodf(koi_x_[f] + cosf(koi_a_[f]) * 0.55f * (float)tsec, (float)W_);
                                    float fy = fmodf(koi_y_[f] + sinf(koi_a_[f]) * 0.55f * (float)tsec, (float)H_);
                                    if (fx < 0) fx += W_;
                                    if (fy < 0) fy += H_;
                                    int cxk = (int)fx, cyk = (int)fy;
                                    if (cxk != wx || cyk != wy) continue;
                                    float dirx = cosf(koi_a_[f]), diry = sinf(koi_a_[f]);
                                    float px0 = (fx - cxk) * 8.0f, py0 = (fy - cyk) * 8.0f;
                                    for (int seg = 0; seg < 3; seg++) {
                                        float dxp = px0 - dirx * seg * 2.6f, dyp = py0 - diry * seg * 2.6f;
                                        int bx = (int)dxp, by = (int)dyp;
                                        if (bx < 0 || bx > 7 || by < 0 || by > 7) continue;
                                        if (bx / 2 != chi || by / 4 != sub) continue;
                                        bits |= BRAILLE_BIT[by & 3][bx & 1];
                                        RGB fc = seg == 0 ? (RGB){240, 240, 226}
                                               : seg == 1 ? (RGB){235, 118, 42}
                                               : (RGB){200, 84, 30};
                                        if (hash3((uint32_t)f, seg, 9) % 3 == 0)
                                            fc = (RGB){168, 52, 28};
                                        col = scalec(fc, pulse);
                                    }
                                }
                            } else if (m_sakura || m_geode || m_lantern) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t h = hash3((uint32_t)(cx * 31 + chi),
                                                   (uint32_t)(cy * 27 + sub * 5), 21);
                                double tsec = now_ms() * 0.001;
                                if (m_sakura) {
                                    /* dusk gradient with drifting blossom petals */
                                    int tb = dither_band(clampb(band + (int)(h % 3) - 1),
                                                         wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 6)) | 0x81);
                                    col = scalec(SKYPAL[tb], pulse);
                                    uint32_t lane = hash3((uint32_t)cx, (uint32_t)cy, 88);
                                    double px = (lane % 97) / 97.0 * W_ +
                                                sin(tsec * 0.9 + lane) * 1.6;
                                    double py = fmod((lane % 53) / 53.0 * H_ +
                                                     tsec * (1.1 + (lane % 7) * 0.22) *
                                                     (H_ / 24.0), (double)H_);
                                    int pxc = ((int)px % W_ + W_) % W_, pyc = (int)py % H_;
                                    if (pxc == wx && pyc == cy) {
                                        double fxp = px - floor(px), fyp = py - floor(py);
                                        if ((chi == (fxp >= 0.5 ? 1 : 0)) &&
                                            (sub == (fyp >= 0.5 ? 1 : 0))) {
                                            uint32_t pp = hash3((uint32_t)(wx * 7 + chi),
                                                                (uint32_t)(cy * 5), 404);
                                            bits = BRAILLE_BIT[pp & 3][(pp >> 2) & 1];
                                            col = scalec((RGB){255, 214, 228},
                                                         pulse * (0.75 + 0.25 * sin(pp + tsec * 2.0)));
                                        }
                                    }
                                } else if (m_geode) {
                                    /* faceted planes: 2x2 blocks share a
                                     * shade, lit like crystal faces */
                                    uint32_t facet = hash3((uint32_t)(cx / 2),
                                                           (uint32_t)(cy / 2), band * 17 + 3);
                                    int tb = clampb(band + (int)(facet % 3) - 1);
                                    double shade = (facet % 5) == 0 ? 0.72 :
                                                   (facet % 5) == 1 ? 1.18 :
                                                   (facet % 5) == 2 ? 0.92 : 1.0;
                                    bits = (uint8_t)((h ^ (h >> 5)) | 0x18);
                                    if (bits == 0x18 || bits == 0x98) bits = 0x3C;
                                    col = scalec(GEOPAL[tb], pulse * shade);
                                    /* glints: rare white sparks that wander */
                                    uint32_t gl = hash3((uint32_t)(cx * 13 + chi),
                                                        (uint32_t)(cy * 11 + sub),
                                                        (uint32_t)(now_ms() / 240));
                                    if (gl % 151 == 42) {
                                        bits = BRAILLE_BIT[gl & 3][(gl >> 2) & 1];
                                        col = scalec((RGB){214, 250, 245},
                                                     pulse * (0.6 + 0.4 * sin(gl + now_ms() * 0.005)));
                                    }
                                } else {
                                    /* indigo night; stars high, lanterns rising */
                                    int tb = dither_band(band, wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 4)) & 0x77);
                                    col = scalec(LNTPAL[tb], pulse);
                                    if (band >= 6 && h % 173 < 3) {
                                        double ph2 = now_ms() * 0.004 + (h % 628) * 0.01;
                                        bits = BRAILLE_BIT[(h >> 2) & 3][(h >> 4) & 1];
                                        col = scalec((RGB){200, 214, 255},
                                                     pulse * (0.35 + 0.45 * (0.5 + 0.5 * sin(ph2))));
                                    }
                                    uint32_t ln = hash3((uint32_t)wx, (uint32_t)cy, 512);
                                    double rise = (ln % 89) / 89.0 * H_ +
                                                  tsec * (0.55 + (ln % 5) * 0.11) * (H_ / 26.0);
                                    int lx = ((int)(wx + sin(tsec * 0.8 + ln) * 1.2) % W_ + W_) % W_;
                                    int ly = (H_ - 1 - ((int)rise % H_) + H_) % H_;
                                    int dxl = lx - wx, dyl = ly - cy;
                                    if (dxl > W_ / 2) dxl -= W_;
                                    if (dxl < -W_ / 2) dxl += W_;
                                    if (dyl > H_ / 2) dyl -= H_;
                                    if (dyl < -H_ / 2) dyl += H_;
                                    int ad = (dxl < 0 ? -dxl : dxl) + (dyl < 0 ? -dyl : dyl);
                                    if (ad == 0) {
                                        bits = 0xFF;
                                        col = scalec((RGB){255, 186, 100},
                                                     pulse * (0.85 + 0.15 * sin(tsec * 3 + ln)));
                                    } else if (ad == 1) {
                                        col = lerp(col, (RGB){126, 72, 42}, 0.38);
                                    } else if (ad == 2) {
                                        col = lerp(col, (RGB){72, 46, 42}, 0.18);
                                    }
                                }
                            } else if (m_dunes || m_reef || m_stained) {
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t h = hash3((uint32_t)(cx * 29 + chi),
                                                   (uint32_t)(cy * 23 + sub * 3), 63);
                                double tsec = now_ms() * 0.001;
                                if (m_dunes) {
                                    /* heat-shimmered sand under a fixed blazing sun */
                                    int sh = (int)round(sin(cx * 0.8 + tsec * 2.2 +
                                                            sin(cy * 0.9) * 1.4));
                                    int tb = dither_band(clampb(band + sh),
                                                         wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 5)) | 0x2A);
                                    col = scalec(DUNEPAL[tb], pulse);
                                    int sx = W_ * 3 / 4, sy = H_ / 5;
                                    int dxs = sx - wx, dys = sy - cy;
                                    if (dxs > W_ / 2) dxs -= W_;
                                    if (dxs < -W_ / 2) dxs += W_;
                                    int ad = (dxs < 0 ? -dxs : dxs) + (dys < 0 ? -dys : dys);
                                    if (ad <= 1) {
                                        bits = 0xFF;
                                        col = scalec((RGB){255, 252, 236}, pulse);
                                    } else if (ad <= 3) {
                                        col = lerp(col, (RGB){255, 236, 180},
                                                   0.30 - (ad - 2) * 0.08);
                                    }
                                } else if (m_reef) {
                                    /* caustic water, coral on the floor, rising bubbles */
                                    double caus = sin(cx * 0.7 + now_ms() * 0.0011 +
                                                      sin(cy * 1.1) * 1.2)
                                                + sin(cy * 1.3 - now_ms() * 0.0007);
                                    int tb = dither_band(clampb(band + (caus > 1.4 ? 1 :
                                                                 caus < -1.4 ? -1 : 0)),
                                                         wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 4)) | 0x54);
                                    col = scalec(REFPAL[tb], pulse * (0.9 + 0.1 * caus));
                                    if (band <= 2 && h % 53 < 4) {
                                        /* coral polyps: warm specks on the dark floor */
                                        bits = BRAILLE_BIT[h & 3][(h >> 2) & 1];
                                        col = scalec((h & 4) ? (RGB){236, 96, 84} :
                                                               (RGB){244, 148, 66}, pulse);
                                    }
                                    uint32_t bub = hash3((uint32_t)wx, 7, 5150);
                                    if (bub % 11 < 2) {
                                        double brise = (bub % 71) / 71.0 * H_ +
                                                       tsec * (0.9 + (bub % 5) * 0.2) * (H_ / 20.0);
                                        if (cy == (H_ - 1 - ((int)brise % H_) + H_) % H_) {
                                            bits = BRAILLE_BIT[(bub >> 2) & 3][(bub >> 4) & 1];
                                            col = scalec((RGB){190, 232, 240}, pulse * 0.85);
                                        }
                                    }
                                    /* a small school crossing the reef */
                                    for (int fk = 0; fk < 4; fk++) {
                                        double fxp = fmod(fk * (W_ / 4.0) +
                                                          tsec * (1.3 + fk * 0.17), (double)W_);
                                        if (fxp < 0) fxp += W_;
                                        double fyp = fmod(H_ * (0.3 + 0.14 * fk) +
                                                          sin(tsec * 1.2 + fk * 2.1) * 1.8,
                                                          (double)H_);
                                        if (fyp < 0) fyp += H_;
                                        if ((int)fxp == wx && (int)fyp == cy &&
                                            (chi == ((fxp - floor(fxp)) >= 0.5 ? 1 : 0)) &&
                                            (sub == ((fyp - floor(fyp)) >= 0.5 ? 1 : 0))) {
                                            bits = 0xFF;
                                            col = scalec((fk & 1) ? (RGB){255, 196, 92} :
                                                                    (RGB){255, 238, 150}, pulse);
                                        }
                                    }
                                } else {
                                    /* jewel panes with thin lead on the
                                     * edges where panes meet, a slow
                                     * light sweeping across the glass */
                                    RGB pane = STAINPAL[band];
                                    double ang = atan2(cy - H_ / 2.0, cx - W_ / 2.0);
                                    double sweep = 0.82 + 0.3 * (0.5 + 0.5 * sin(ang * 2.0 +
                                                                                   now_ms() * 0.0006));
                                    col = scalec(pane, pulse * sweep);
                                    int bn = elev_of_cell(wx, (cy + H_ - 1) % H_);
                                    int bs = elev_of_cell(wx, (cy + 1) % H_);
                                    int bw2 = elev_of_cell((wx + W_ - 1) % W_, cy);
                                    int be = elev_of_cell((wx + 1) % W_, cy);
                                    uint8_t cls[8][8];
                                    memset(cls, 0, sizeof cls);
                                    if (bn != band) for (int kk = 0; kk < 8; kk++) { cls[kk][0] = 1; cls[kk][1] = 1; }
                                    if (bs != band) for (int kk = 0; kk < 8; kk++) { cls[kk][6] = 1; cls[kk][7] = 1; }
                                    if (bw2 != band) for (int kk = 0; kk < 8; kk++) { cls[0][kk] = 1; cls[1][kk] = 1; }
                                    if (be != band) for (int kk = 0; kk < 8; kk++) { cls[6][kk] = 1; cls[7][kk] = 1; }
                                    if (cls[chi * 2 + 0][sub * 4] || cls[chi * 2 + 1][sub * 4] ||
                                        cls[chi * 2 + 0][sub * 4 + 1] || cls[chi * 2 + 1][sub * 4 + 1] ||
                                        cls[chi * 2 + 0][sub * 4 + 2] || cls[chi * 2 + 1][sub * 4 + 2] ||
                                        cls[chi * 2 + 0][sub * 4 + 3] || cls[chi * 2 + 1][sub * 4 + 3]) {
                                        bits = 0;
                                        for (int yy = 0; yy < 4; yy++)
                                            for (int xx = 0; xx < 2; xx++)
                                                if (cls[chi * 2 + xx][sub * 4 + yy])
                                                    bits |= BRAILLE_BIT[yy][xx];
                                        col = scalec((RGB){26, 22, 28}, pulse);
                                    } else {
                                        bits = (uint8_t)((h & 0x66) | 0x81);
                                    }
                                }
                            } else if (m_fire || m_waves || m_lava) {
                                int is_fire = m_fire;
                                int is_lava = m_lava;
                                int band = tiles_[t].e[0] >> 4;
                                uint32_t salt = (uint32_t)(now_ms() / (is_lava ? 130 : (is_fire ? 90 : 160)));
                                int off = (int)(hash3((uint32_t)(cx * 37 + chi),
                                                      (uint32_t)(cy * 29 + sub * 5 + chi), salt) % 3) - 1;
                                int tb = clampb(dither_band(clampb(band + off), wx, wy,
                                             chi * 2, sub * 8));
                                bits = 0xFF;
                                uint32_t sp = hash3((uint32_t)(cx * 53 + chi),
                                                    (uint32_t)(cy * 41 + sub), salt + 7);
                                if (is_fire || is_lava) {
                                    if (is_lava && tb <= 1) {
                                        /* hardening crust: scabby holes in the black plate */
                                        bits = 0xFF;
                                        if (sp % 4 < 2) bits &= (uint8_t)~BRAILLE_BIT[sp & 3][(sp >> 2) & 1];
                                        if (sp % 17 == 3) bits = 0x00;
                                    } else {
                                        if (sp % 29 < 8) bits &= (uint8_t)~BRAILLE_BIT[sp & 3][(sp >> 2) & 1];
                                    }
                                    col = scalec(is_lava ? LAVAPAL[tb] : FIREPAL[tb], pulse);
                                    if (is_lava && tb >= 4) {
                                        /* molten breathing */
                                        col = scalec(col,
                                                     pulse * (1.0 + 0.13 *
                                                     sin(now_ms() * 0.0028 + wx * 2.1 + wy * 3.3)));
                                    }
                                    /* rising embers: bright sparks drifting upward */
                                    uint32_t em = hash3((uint32_t)(wx * 19 + chi),
                                                        (uint32_t)((cy * 31 - (int)(now_ms() / (is_lava ? 150 : 110))) & 2047), 909);
                                    if (em % 41 == 7) {
                                        bits |= BRAILLE_BIT[em & 3][(em >> 2) & 1];
                                        col = scalec(is_lava ? (RGB){255, 158, 40} : (RGB){255, 220, 120}, pulse);
                                    }
                                    /* smoke wisps above hot cores */
                                    if (is_fire && band <= 2 && em % 23 == 11)
                                        col = lerp(col, (RGB){60, 50, 55}, 0.5);
                                } else {
                                    /* rolling ocean swell: coherent traveling phase */
                                    double ph = cx * 0.55 - now_ms() * 0.0045
                                              + sin(cy * 1.31) * 1.9;
                                    int swell = (int)round(sin(ph + cy * 0.7) * 1.8);
                                    int sb = band + swell;
                                    if (sb < 0) sb = 0; if (sb > 7) sb = 7;
                                    if (sb >= 5 && sp % 4 < 2) {
                                        /* foam flecks on crests */
                                        bits = (uint8_t)((sp * 2654435761u) | BRAILLE_BIT[(sp >> 1) & 3][(sp >> 3) & 1]);
                                        col = scalec(WAVEPAL[7], pulse);
                                    } else {
                                        bits = (uint8_t)((0xFF & ~(0x24u << ((sp >> 3) & 3))) | 0x81);
                                        col = scalec(WAVEPAL[sb], pulse);
                                    }
                                }
                            } else if (m_mondrian) {
                                int band = tiles_[t].e[0] >> 4;
                                /* charcoal rule where bands meet; flat paint inside */
                                uint8_t cls[8][8];
                                memset(cls, 0, sizeof cls);
                                int bn = elev_of_cell(wx, (cy + H_ - 1) % H_);
                                int bs = elev_of_cell(wx, (cy + 1) % H_);
                                int bw = elev_of_cell((wx + W_ - 1) % W_, cy);
                                int be = elev_of_cell((wx + 1) % W_, cy);
                                if (bn != band) for (int kk = 0; kk < 8; kk++) cls[kk][0] = cls[kk][1] = 1;
                                if (bs != band) for (int kk = 0; kk < 8; kk++) cls[kk][6] = cls[kk][7] = 1;
                                if (bw != band) for (int kk = 0; kk < 8; kk++) cls[0][kk] = cls[1][kk] = 1;
                                if (be != band) for (int kk = 0; kk < 8; kk++) cls[6][kk] = cls[7][kk] = 1;
                                RGB fill = MONDPAL[band];
                                uint32_t jit = hash3((uint32_t)wx, (uint32_t)cy, 3) % 1000;
                                fill = scalec(fill, 0.97 + jit * 0.00004);
                                int linects = 0;
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (cls[chi * 2 + xx][sub * 4 + yy]) linects++;
                                col = linects >= 3 ? (RGB){16, 13, 12} : fill;
                                bits = 0;
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (cls[chi * 2 + xx][sub * 4 + yy])
                                            bits |= BRAILLE_BIT[yy][xx];
                                if (!bits) bits = 0xFF;
                                col = scalec(col, pulse);
                            } else if (m_streets || m_neurons || m_mycelium || m_delta) {
                                bool netpx[8][8];
                                art_circuit(t, netpx);
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (netpx[chi * 2 + xx][sub * 4 + yy])
                                            bits |= BRAILLE_BIT[yy][xx];
                                col = network_color(mode, t, wx, cy, pulse);
                            } else {
                                bool px[8][8];
                                if (m_circuit || m_pipes) art_circuit(t, px);
                                else art_truchet(t, px);
                                for (int yy = 0; yy < 4; yy++)
                                    for (int xx = 0; xx < 2; xx++)
                                        if (px[chi * 2 + xx][sub * 4 + yy])
                                            bits |= BRAILLE_BIT[yy][xx];
                                col = m_truchet
                                          ? duo_color(truchet_col(t), pulse)
                                          : trace_color(wx, cy, pulse);
                                if (m_truchet) {
                                    /* light pulse travels along the strand: fake via
                                     * per-cell phase so the whole weave feels electric */
                                    double ph = now_ms() * 0.0007 + (wx * 0.6 + wy * 0.9) * 2.2;
                                    double pl = 0.5 + 0.5 * sin(ph);
                                    col = scalec(col, 0.85 + 0.35 * pl * (px_check_pad(chi, sub) ? 1.4 : 0.6));
                                }
                                /* signal pulse rides the trace network; water courses through pipes */
                                if (m_circuit || m_pipes) {
                                    bool ispipe = m_pipes;
                                    int sig = (int)(now_ms() / (ispipe ? 240 : 130) +
                                                    wx * (ispipe ? 2 : 3) + cy * (ispipe ? 3 : 5));
                                    if (((sig % (ispipe ? 23 : 17)) < 3) && (tiles_[t].e[0] || tiles_[t].e[1] ||
                                                                              tiles_[t].e[2] || tiles_[t].e[3]))
                                        col = scalec(ispipe ? (RGB){190, 235, 255} : (RGB){255, 255, 210}, pulse);
                                }
                            }
                        } else {
                            double frac = (double)k / ntiles_;
                            double dens = pow(frac, 1.7) * 0.38 + 0.015;
                            const RGB daa = {17, 14, 34}, dbb = {110, 110, 220};
                            /* mode-aware shimmer so the uncollapsed world stays in-character */
                            const char *shm = MODES[g_mode_idx];
                            RGB da = daa, db = dbb;
                            if (!strcmp(shm, "galaxy")) { da = (RGB){8, 10, 28}; db = (RGB){60, 80, 140}; }
                            else if (!strcmp(shm, "matrix")) { da = (RGB){4, 20, 8}; db = (RGB){0, 120, 60}; }
                            else if (!strcmp(shm, "fire")) { da = (RGB){30, 12, 4}; db = (RGB){190, 70, 20}; }
                            else if (!strcmp(shm, "waves")) { da = (RGB){6, 16, 30}; db = (RGB){50, 110, 160}; }
                            else if (!strcmp(shm, "city")) { da = (RGB){8, 9, 18}; db = (RGB){70, 80, 130}; }
                            else if (!strcmp(shm, "dungeon")) { da = (RGB){14, 12, 20}; db = (RGB){90, 70, 130}; }
                            else if (!strcmp(shm, "aurora")) { da = (RGB){6, 12, 12}; db = (RGB){40, 110, 70}; }
                            else if (!strcmp(shm, "truchet")) { da = (RGB){20, 16, 24}; db = (RGB){130, 100, 180}; }
                            else if (!strcmp(shm, "maze")) { da = (RGB){16, 14, 20}; db = (RGB){100, 110, 130}; }
                            else if (!strcmp(shm, "streets")) { da = (RGB){7, 10, 16}; db = (RGB){104, 92, 70}; }
                            else if (!strcmp(shm, "neurons")) { da = (RGB){6, 3, 16}; db = (RGB){148, 42, 156}; }
                            else if (!strcmp(shm, "mycelium")) { da = (RGB){4, 16, 12}; db = (RGB){74, 142, 78}; }
                            else if (!strcmp(shm, "delta")) { da = (RGB){3, 19, 28}; db = (RGB){38, 136, 132}; }
                            for (int yy = 0; yy < 4; yy++)
                                for (int xx = 0; xx < 2; xx++)
                                    if ((hash3((uint32_t)cx, (uint32_t)cy,
                                               (uint32_t)((sub * 4 + yy) * 8 + chi * 2 + xx)) % 1000) <
                                        dens * 1000)
                                        bits |= BRAILLE_BIT[yy][xx];
                            col = lerp(db, da, frac);
                            /* zen: the old world lingers here, scattering
                             * away cell by cell as the collapse decides */
                            if (ghosting()) {
                                double ga = ghost_cell_alpha(wx, cy);
                                if (ga > 0.004) {
                                    RGB gc = ghost_[IDX(wx, cy)];
                                    double tw = 0.8 + 0.2 * sin(now_ms() * 0.0011 +
                                                                (wx * 7 + cy * 13) * 0.7);
                                    col = lerp(col, scalec(gc, ga * tw), ga);
                                }
                            }
                        }
                        if (!bits && k == 1) {
                            RGB gc;
                            if (glow_neighbor(wx, wy, &gc)) {
                                bits = BRAILLE_BIT[(wx + wy) & 3][(wx >> 1) & 1];
                                fb_fg(gc);
                            }
                        }
                        if (g_heatmap && !(studio_pin_ && studio_pin_[IDX(wx, cy)]))
                            col = quality_heat_tint(col, quality_local_cell_score(wx, cy, NULL));
                        if (bits) fb_fg(col);
                        fb_braille(bits);
                        if (studio_pin_ && studio_pin_[IDX(wx, cy)] && chi == 0 && sub == 0) {
                            fb_fg((RGB){255, 220, 100});
                            fb_braille(0xFF);
                        }
                    }
                } else {
                    if (k == 1) {
                        RGB base = terrain_tint(biome_color(__builtin_ctzll(d)));
                        double sh = hillshade(wx, cy, __builtin_ctzll(d));
                        /* contour hatching where neighbors differ in band */
                        if (m_terrain) {
                            int bself = tiles_[__builtin_ctzll(d)].e[0] >> 4;
                            int bw_ = elev_of_cell((wx - 1 + W_) % W_, cy);
                            int bn_ = elev_of_cell(wx, (cy - 1 + H_) % H_);
                            if ((bw_ != bself || bn_ != bself) && bself >= 1 && bself <= 6) {
                                if ((hash3((uint32_t)wx, (uint32_t)cy, 17) % 3) == 0)
                                    sh *= 0.82;
                            }
                        }
                        /* drifting cloud shadows */
                        double cl = sin(cx * 0.31 + now_ms() * 0.00045)
                                  + sin(cy * 0.21 - now_ms() * 0.00030 + cx * 0.13);
                        sh *= 1.0 + 0.09 * cl;
                        /* dither ocean bands for smooth coastlines */
                        if (biome_color(__builtin_ctzll(d)).b >
                            biome_color(__builtin_ctzll(d)).r + 20) {
                            int elev = tiles_[__builtin_ctzll(d)].e[0] >> 4;
                            if (elev < 3) {
                                double dj = 0.85 + (BAYER8[(wx * 8) & 7][(wy * 8) & 7] / 64.0) * 0.25;
                                sh *= dj;
                            }
                        }
                        int rrk = river_rank_[IDX(wx, cy)];
                        if (river_[IDX(wx, cy)] && rrk >= 0 && rrk < g_river_show) {
                            base = lerp(base, (RGB){36, 96, 150}, 0.72);
                            /* pulse traveling downstream (rank increases seaward) */
                            long ph2 = (rrk * 9 - (long)now_ms() / 150) % 700;
                            if (ph2 < 60) base = lerp(base, (RGB){150, 200, 235}, 0.6);
                            /* rapids where steep drop: rank gap vs west/north neighbor */
                            for (int d2 = 0; d2 < 2; d2++) {
                                int ax = wx - (d2 == 0), ay = cy - (d2 == 1);
                                if (!g_torus && (ax < 0 || ay < 0)) continue;
                                ax = (ax + W_) % W_; ay = (ay + H_) % H_;
                                int nk = river_rank_[IDX(ax, ay)];
                                if (nk >= 0 && nk < g_river_show &&
                                    rrk - nk > 4) {
                                    if ((hash3((uint32_t)wx, (uint32_t)cy,
                                               (uint32_t)(now_ms() / 170)) % 5) < 3)
                                        base = lerp(base, (RGB){235, 245, 250}, 0.8);
                                    break;
                                }
                            }
                        }
                        uint32_t h1 = hash3((uint32_t)cx, (uint32_t)cy, 7) % 1000;
                        uint32_t h2 = hash3((uint32_t)cx, (uint32_t)cy, 13) % 1000;
                        if (studio_pin_ && studio_pin_[IDX(wx, cy)]) {
                            base = (RGB){255, 220, 100};
                            sh = 1.0;
                        }
                        if (g_heatmap && !(studio_pin_ && studio_pin_[IDX(wx, cy)]))
                            base = quality_heat_tint(base, quality_local_cell_score(wx, cy, NULL));
                        fb_half(scalec(base, (0.92 + h1 * 0.00016) * pulse * sh),
                                scalec(base, (0.92 + h2 * 0.00016) * pulse * sh));
                    } else {
                        double frac = (double)k / ntiles_;
                        const RGB deep = {16, 24, 42}, lite = {88, 96, 130};
                        fb_bg(lerp(lite, deep, frac)); fb_puts("  ");
                    }
                }
    }
    fb_puts("\x1b[0m");
}

static void render_twin_frame(long stepsA, long stepsB, double pulse) {
    (void)stepsA; (void)stepsB;
    const char *mode = MODES[g_mode_idx];
    bool braille = strcmp(mode, "terrain") != 0;
    fb_reset();
    fb_puts("\x1b[H");
    for (int cy = 0; cy < H_; cy++) {
        for (int sub = 0; sub < (braille ? 2 : 1); sub++) {
            g_rowdim = g_crt ? ((cy * 2 + sub) % 2 ? 55 : 100) : 100;
            load_world(0);
            for (int cx = 0; cx < W_; cx++) paint_cell(cx, cy, sub, pulse);
            load_world(1);
            for (int cx = 0; cx < W_; cx++) paint_cell(cx, cy, sub, pulse);
            load_world(0);
            fb_puts("\x1b[0m\n");
        }
    }
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

static void render_quad_frame(long steps, double pulse) {
    (void)steps;
    const char *mode = MODES[g_mode_idx];
    bool braille = strcmp(mode, "terrain") != 0;
    int sh = H_ * 2; /* screen rows */
    fb_reset();
    fb_puts("\x1b[H");
    for (int sy = 0; sy < sh; sy++) {
        for (int sub = 0; sub < (braille ? 2 : 1); sub++) {
            g_rowdim = g_crt ? ((sy * 2 + sub) % 2 ? 55 : 100) : 100;
            for (int sx = 0; sx < W_ * 2; sx++) {
                int qrow = sy >= H_, qcol = sx >= W_;
                load_world(qrow * 2 + qcol);
                paint_cell(sx - qcol * W_, sy - qrow * H_, sub, pulse);
            }
            load_world(0);
            fb_puts("\x1b[0m\n");
        }
    }
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

/* gfx twin: sample both worlds across the raster */
#define DOM_AT(x, y) (g_twin ? ((x) < W_ ? dom_[IDX(x, y)] : domB_[IDX((x) - W_, y)]) \
                            : dom_[IDX(((x) + g_vx + W_) % W_, y)])

/* ---------------- the raymarcher ----------------
 * collapses the solved WFC grid into a real heightfield and
 * ray-marches it: soft shadows, ambient occlusion, fresnel,
 * horizon fog, sun-disk glow. rendered per-dot into braille.
 * -------------------------------------------------- */
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)) + 1e-6f; return vmul(a, 1.0f / l); }

static int elev_of_cell(int x, int y) {
    uint64_t d = dom_[IDX(x, y)];
    if (pc64(d) != 1) return 3;
    return tiles_[__builtin_ctzll(d)].e[0] >> 4;
}
static float height_at(float x, float y) {
    float fx = fmodf(x, (float)W_); if (fx < 0) fx += W_;
    float fy = fmodf(y, (float)H_); if (fy < 0) fy += H_;
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = (x0 + 1) % W_, y1 = (y0 + 1) % H_;
    float tx = fx - x0, ty = fy - y0;
    float h00 = elev_of_cell(x0, y0) * 0.55f, h10 = elev_of_cell(x1, y0) * 0.55f;
    float h01 = elev_of_cell(x0, y1) * 0.55f, h11 = elev_of_cell(x1, y1) * 0.55f;
    float a = h00 + (h10 - h00) * tx;
    float b = h01 + (h11 - h01) * tx;
    return a + (b - a) * ty;
}
static RGB rt_albedo(int band) {
    const char *m = MODES[g_mode_idx];
    if (!strcmp(m, "terrain")) {
        static const RGB t2[8] = {
            {18, 40, 68}, {28, 76, 116}, {50, 120, 150}, {150, 146, 104},
            {90, 138, 58}, {46, 100, 48}, {104, 96, 82}, {232, 236, 238},
        };
        return t2[band];
    }
    if (!strcmp(m, "fire")) return FIREPAL[band];
    if (!strcmp(m, "lava")) return LAVAPAL[band];
    if (!strcmp(m, "koi")) return KOIPAL[band];
    if (!strcmp(m, "sakura")) return SKYPAL[band];
    if (!strcmp(m, "geode")) return GEOPAL[band];
    if (!strcmp(m, "lantern")) return LNTPAL[band];
    if (!strcmp(m, "dunes")) return DUNEPAL[band];
    if (!strcmp(m, "reef")) return REFPAL[band];
    if (!strcmp(m, "stained")) return STAINPAL[band];
    if (!strcmp(m, "mondrian")) return MONDPAL[band < 6 ? band : 5];
    if (!strcmp(m, "galaxy")) {
        static const RGB g2[8] = {
            {10, 12, 30}, {16, 14, 40}, {24, 18, 56}, {40, 28, 80},
            {60, 40, 110}, {84, 56, 140}, {120, 80, 170}, {170, 120, 200},
        };
        return g2[band];
    }
    if (!strcmp(m, "waves")) return WAVEPAL[band];
    if (!strcmp(m, "aurora")) return AURPAL[band];
    return (RGB){(uint8_t)(40 + band * 26), (uint8_t)(48 + band * 18), (uint8_t)(52 + band * 12)};
}
static V3 rt_sun = {-0.55f, 0.62f, 0.42f};
static float rt_shadow(float x, float y, float hgt) {
    float occl = 0;
    for (int i = 1; i <= 5; i++) {
        float t = 0.18f * i;
        if (height_at(x + rt_sun.x * t, y + rt_sun.z * t) > hgt + rt_sun.y * t + 0.12f)
            occl += 0.2f;
    }
    return 1.0f - occl;
}
/* full shade of one ray; returns lit-flags + color */
static uint8_t rt_ray(V3 ro, V3 rd, float *r, float *g, float *b) {
    float t = 0.6f;
    for (int s = 1; s < 70; s++) {
        /* growing steps: near surface first */
        t += 0.30f + 0.05f * s;
        float px = ro.x + rd.x * t, py = ro.z + rd.z * t;
        float sgn = (ro.y + rd.y * t) - height_at(px, py);
        if (sgn < 0) {
            float lo = t - 0.30f - 0.05f * (s - 1), hi = t;
            for (int k = 0; k < 4; k++) {
                float mid = (lo + hi) * 0.5f;
                float dm = (ro.y + rd.y * mid) - height_at(ro.x + rd.x * mid, ro.z + rd.z * mid);
                if (dm < 0) hi = mid; else lo = mid;
            }
            t = (lo + hi) * 0.5f;
            px = ro.x + rd.x * t; py = ro.z + rd.z * t;
            float hh = height_at(px, py);
            int band = (int)(hh / 0.55f + 0.5f);
            if (band < 0) band = 0; if (band > 7) band = 7;
            float e = 0.35f;
            V3 n = vnorm(v3(height_at(px - e, py) - height_at(px + e, py),
                            2 * e,
                            height_at(px, py - e) - height_at(px, py + e)));
            float lam = fmaxf(vdot(n, rt_sun), 0.0f);
            float wrap = (lam + 0.3f) / 1.3f;
            float sh = rt_shadow(px, py, hh);
            RGB alb = rt_albedo(band);
            float fres = powf(1.0f - fmaxf(vdot(n, vmul(rd, -1.0f)), 0.0f), 3.0f);
            float litv = wrap * sh * (1.0f - 0.30f * (1.0f - sh));
            *r = alb.r * (litv * 1.08f) + fres * 34.0f + 4.0f;
            *g = alb.g * (litv * 1.06f) + fres * 30.0f + 4.0f;
            *b = alb.b * (litv * 1.00f) + fres * 28.0f + 6.0f;
            return BRAILLE_BIT[0][0] | 0x3E; /* generous lit marks re-set by caller */
        }
    }
    /* sky */
    float hgtv = fmaxf(rd.y, 0.0f);
    float t2 = powf(hgtv, 0.4f);
    float sun = fmaxf(vdot(rd, rt_sun), 0.0f);
    *r = 12 + (92 - 12) * t2 + powf(sun, 32.0f) * 250;
    *g = 14 + (30 - 14) * t2 + powf(sun, 32.0f) * 150;
    *b = 26 + (60 - 26) * t2 + powf(sun, 16.0f) * 120;
    return 0x00;
}

static void render_rt_frame(double tms) {
    int pw = W_ * 16, ph = H_ * 16;
    if (pw > 576) pw = 576;
    if (ph > 300) ph = 300;
    pw &= ~1; /* even for 2-dot chars */
    float worldscale = 4.5f;
    V3 center = v3(W_ * worldscale * 0.5f, -0.4f, H_ * worldscale * 0.5f);
    double ang = tms * 0.00024;
    V3 off = v3(cosf((float)ang) * 4.6f,
                3.4f + 1.4f * sinf((float)(tms * 0.000087)),
                sinf((float)ang) * 4.6f);
    V3 ro = v3(center.x + off.x, center.y + off.y, center.z + off.z);
    V3 fwd = vnorm(vsub(center, ro));
    V3 right = vnorm(v3(fwd.z, 0.0f, -fwd.x)); /* cross(up,fwd) */
    V3 up2 = vnorm(v3(
        fwd.y * -fwd.x,
        fwd.x * fwd.x + fwd.z * fwd.z,
        fwd.y * -fwd.z
    ));
    float fov = 0.85f;
    float aspect = (float)pw / ph;

    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    for (int cy = 0; cy < ph / 4; cy++) {
        for (int cx = 0; cx < pw / 2; cx++) {
            float acc_r = 0, acc_g = 0, acc_b = 0;
            uint8_t bits = 0;
            for (int dy = 0; dy < 4; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    float ndx = ((cx * 2 + dx) / (float)pw - 0.5f) * 2 * fov * aspect;
                    float ndy = (0.5f - (cy * 4 + dy) / (float)ph) * 2 * fov;
                    V3 rd = vnorm(vadd(vadd(vmul(right, ndx), vmul(up2, ndy)), fwd));
                    float r, g2, b2;
                    uint8_t hit = rt_ray(ro, rd, &r, &g2, &b2);
                    acc_r += r; acc_g += g2; acc_b += b2;
                    if (hit) bits |= BRAILLE_BIT[dy][dx];
                }
            }
            float fr = acc_r / 8, fg2 = acc_g / 8, fb2 = acc_b / 8;
            /* gamma + saturation lift for CRT-grade punch */
            float lum = 0.2126f * fr + 0.7152f * fg2 + 0.0722f * fb2;
            fr = lum + (fr - lum) * 1.35f;
            fg2 = lum + (fg2 - lum) * 1.35f;
            fb2 = lum + (fb2 - lum) * 1.35f;
            float gamm = 1.0f / 2.2f;
            fr = powf(fmaxf(fr, 0) / 255.0f, gamm) * 255.0f;
            fg2 = powf(fmaxf(fg2, 0) / 255.0f, gamm) * 255.0f;
            fb2 = powf(fmaxf(fb2, 0) / 255.0f, gamm) * 255.0f;
            RGB c = {(uint8_t)(fr > 255 ? 255 : fr),
                     (uint8_t)(fg2 > 255 ? 255 : fg2),
                     (uint8_t)(fb2 > 255 ? 255 : fb2)};
            fb_fg(c);
            fb_braille(bits);
        }
        fb_puts("\x1b[0m\n");
    }
    fb_puts("\x1b[0m");
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

/* ---------------- the iso view ----------------
 * 45-degree relief: the solved grid is laid out on a diamond lattice,
 * one braille pad per cell (denser = taller). back-to-front so later
 * cells sit in front. height reads as brightness + column density.
 * -------------------------------------------------- */
static const uint8_t ISO_DENS[8] = {0x0A, 0x12, 0x2A, 0x46, 0x9E, 0xEE, 0xFE, 0xFF};
static void render_iso_frame(void) {
    if (g_nworlds > 1) return;
    int cols = 120, rows = 40;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 10 && ws.ws_row > 9) {
        cols = ws.ws_col;
        rows = ws.ws_row - 1;
    }
    int step = 1, nx = W_, ny = H_;
    while (step < 64) {
        nx = (W_ + step - 1) / step;
        ny = (H_ + step - 1) / step;
        if (2 * (nx + ny - 1) + 2 <= cols && nx + ny - 1 <= rows) break;
        step *= 2;
    }
    fb_reserve(1 << 20);
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    int cofs = (cols - (2 * (nx + ny - 1) + 1)) / 2;
    if (cofs < 0) cofs = 0;
    char pos[32];
    for (int s = 0; s <= nx + ny - 2; s++) {
        for (int i = 0; i < nx; i++) {
            int j = s - i;
            if (j < 0 || j >= ny) continue;
            int x = i * step, y = j * step;
            uint64_t d = dom_[IDX(x, y)];
            int band = pc64(d) == 1 ? (tiles_[__builtin_ctzll(d)].e[0] >> 4) : -1;
            RGB top = band >= 0 ? rt_albedo(band) : (RGB){34, 38, 64};
            double br = band >= 0 ? 0.42 + 0.082 * band : 0.5;
            top = scalec(top, br);
            int c = (i - j) + (ny - 1);
            snprintf(pos, sizeof pos, "\x1b[%d;%dH", s + 2, 2 * c + 1 + cofs);
            fb_puts(pos);
            fb_fg(top);
            fb_braille(band >= 0 ? ISO_DENS[band] : 0x09);
        }
    }
    fb_puts("\x1b[0m");
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

static void render_frame(long steps, int attempts, double pulse) {
    (void)steps; (void)attempts;
    const char *mode = MODES[g_mode_idx];
    bool braille = strcmp(mode, "terrain") != 0;
    int cw = braille ? 4 : 2, ch = braille ? 2 : 1;
    int vw = g_inf ? (W_ < g_fit_w ? W_ : g_fit_w) : W_;
    int vh = g_inf ? (H_ < g_fit_h ? H_ : g_fit_h) : H_;
    if (g_inf) {
        g_vx = g_inf_ax - vw / 2;
        g_vy = g_inf_ay - vh / 2;
        if (g_vx < 0) g_vx = 0;
        if (g_vy < 0) g_vy = 0;
        if (g_vx + vw > W_) g_vx = W_ - vw;
        if (g_vy + vh > H_) g_vy = H_ - vh;
    } else if (g_zen && !g_pan && !g_hero_on) {
        /* zen: the whole world slowly breathes across the screen (toroidal) */
        double t = now_ms() * 0.001;
        g_vx = (int)(W_ * 0.5 + W_ * 0.17 * sin(t * 0.19)) % W_;
        g_vy = (int)(H_ * 0.5 + H_ * 0.13 * cos(t * 0.11)) % H_;
    }

    /* dirty-cell state */
    size_t nsig = (size_t)W_ * H_ * 2;
    if (prev_sig_cap_ < nsig) {
        uint32_t *next_sig = realloc(prev_sig_, sizeof(uint32_t) * nsig);
        if (!next_sig) {
            set_note("render buffer unavailable");
            return;
        }
        prev_sig_ = next_sig;
        memset(prev_sig_, 0, sizeof(uint32_t) * nsig);
        prev_sig_cap_ = nsig;
        full_repaint_ = true;
    }
    uint32_t epoch = anim_epoch();
    uint32_t pq = (uint32_t)(pulse * 63.0);

    fb_reset();
    if (full_repaint_ && !ghosting()) fb_puts("\x1b[H\x1b[2J"); /* zen: no hard clear, dissolve */
    if (g_slowmo || (g_paused && !g_entropy_view) || g_entropy_view) {
        if (g_slowmo) { fb_fg((RGB){255, 190, 60}); fb_puts("\x1b[1;1HSLOW-MO\x1b[0m"); }
        else if (g_entropy_view) { fb_fg((RGB){255, 120, 80}); fb_puts("\x1b[1;1HENTROPY\x1b[0m"); }
        else { fb_fg((RGB){120, 200, 255}); fb_puts("\x1b[1;1HPAUSED\x1b[0m"); }
    }
    if (g_hero_on) {
        char sb[80];
        fb_bg((RGB){14, 12, 18});
        fb_fg((RGB){255, 190, 60});
        fb_puts("\x1b[1;1H ");
        fb_puts("crawler ");
        fb_fg((RGB){150, 255, 170});
        fb_puts("torches ");
        fb_fg((RGB){255, 226, 130});
        snprintf(sb, sizeof sb, "%d/%d \xe2\x80\x94 wasd ", g_loot, g_loot_tot > 0 ? g_loot_tot : 0);
        fb_puts(sb);
        fb_bg((RGB){0, 0, 0});
    }
    for (int cy = 0; cy < vh; cy++) {
        for (int sub = 0; sub < (braille ? 2 : 1); sub++) {
            g_rowdim = g_crt ? (((cy + g_vy) * 2 + sub) % 2 ? 55 : 100) : 100;
            for (int cx = 0; cx < vw; cx++) {
                int wxi = (cx + g_vx % W_ + W_) % W_; /* toroidal: --pan/zen drift wrap */
                int wyi = (cy + g_vy % H_ + H_) % H_;
                uint64_t dv = dom_[IDX(wxi, wyi)];
                uint32_t gq = ghosting() ? (uint32_t)(ghost_alpha() * 24.0) : 0;
                uint32_t sig = (uint32_t)(dv * 2654435761u >> 32)
                             ^ hash3((uint32_t)wxi, (uint32_t)wyi,
                                     epoch + (pq << 20) + (gq << 27));
                size_t si = ((size_t)wyi * W_ + wxi) * 2 + sub;
                if (!full_repaint_ && prev_sig_[si] == sig) continue;
                prev_sig_[si] = sig;
                char pos[24];
                snprintf(pos, sizeof pos, "\x1b[%d;%dH", cy * ch + sub + 1, cx * cw + 1);
                fb_puts(pos);
                paint_cell(wxi, wyi, sub, pulse);
            }
        }
    }
    full_repaint_ = false;
    render_status(vh, ch);
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

/* ---------------- image sampling (shared by BMP + GIF export) ---------------- */
static RGB img_px(int cx, int cy, int ix, int iy, int art) {
    const char *mode = MODES[g_mode_idx];
    const bool m_circuit = !strcmp(mode, "circuit"), m_terrain = !strcmp(mode, "terrain"), m_fire = !strcmp(mode, "fire"), m_waves = !strcmp(mode, "waves"), m_dungeon = !strcmp(mode, "dungeon"), m_maze = !strcmp(mode, "maze"), m_galaxy = !strcmp(mode, "galaxy"), m_city = !strcmp(mode, "city"), m_aurora = !strcmp(mode, "aurora"), m_matrix = !strcmp(mode, "matrix"), m_pipes = !strcmp(mode, "pipes"), m_mondrian = !strcmp(mode, "mondrian"), m_koi = !strcmp(mode, "koi"), m_lava = !strcmp(mode, "lava"), m_sakura = !strcmp(mode, "sakura"), m_geode = !strcmp(mode, "geode"), m_lantern = !strcmp(mode, "lantern"), m_dunes = !strcmp(mode, "dunes"), m_reef = !strcmp(mode, "reef"), m_stained = !strcmp(mode, "stained"), m_streets = !strcmp(mode, "streets"), m_neurons = !strcmp(mode, "neurons"), m_mycelium = !strcmp(mode, "mycelium"), m_delta = !strcmp(mode, "delta");
    uint64_t d = dom_[IDX(cx, cy)];
    if (pc64(d) != 1) {
        if (ghosting()) {
            double ga = ghost_cell_alpha(cx, cy);
            if (ga > 0.004)
                return lerp(C_BG, scalec(ghost_[IDX(cx, cy)], 0.35 + 0.65 * ga), ga);
        }
        return C_BG;
    }
    int t = __builtin_ctzll(d);
    if (studio_pin_ && studio_pin_[IDX(cx, cy)] &&
        (ix == 0 || iy == 0 || ix == art - 1 || iy == art - 1))
        return (RGB){255, 220, 100};
    if (m_terrain) {
        RGB base = terrain_tint(biome_color(t));
        int rrk = river_rank_[IDX(cx, cy)];
        if (river_[IDX(cx, cy)] && rrk >= 0 && rrk < g_river_show) {
            base = lerp(base, (RGB){36, 96, 150}, 0.72);
            long ph2 = (rrk * 9 - (long)now_ms() / 150) % 700;
            if (ph2 < 60) base = lerp(base, (RGB){150, 200, 235}, 0.6);
        }
        double shade = hillshade(cx, cy, t);
        double cl = sin(cx * 0.31 + now_ms() * 0.00045)
                  + sin(cy * 0.21 - now_ms() * 0.00030 + cx * 0.13);
        shade *= 1.0 + 0.09 * cl;
        return scalec(base,
                      shade * (0.92 + (hash3((uint32_t)(cx * 31 + ix), (uint32_t)(cy * 31 + iy), 3) % 1000) * 0.00016));
    }
    if (m_waves) {
        int band = tiles_[t].e[0] >> 4;
        double ph = cx * 0.55 - now_ms() * 0.0045 + sin(cy * 1.31) * 1.9;
        int swell = (int)round(sin(ph + cy * 0.7) * 1.8);
        int tb = band + swell;
        if (tb < 0) tb = 0; if (tb > 7) tb = 7;
        uint32_t h = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 11);
        if (tb >= 6 && h % 7 < 3) return WAVEPAL[7];
        return WAVEPAL[tb];
    }
    if (m_maze) {
        uint8_t cls[8][8];
        art_maze(t, cls);
        return MAZEPAL[cls[ix][iy]];
    }
    if (m_galaxy) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t h = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 99);
        if (h % 1000 < 14 + (unsigned)band * 6)
            return (h & 1) ? STAR_WARM : STAR_COOL;
        int off = (int)((h >> 10) % 3) - 1;
        int tb = band + off;
        if (tb < 0) tb = 0; if (tb > 7) tb = 7;
        return GALPAL[tb];
    }
    if (m_matrix) {
        int band = tiles_[t].e[0] >> 4;
        static const RGB MG[8] = {
            {2, 10, 6}, {3, 26, 12}, {4, 48, 20}, {6, 84, 32},
            {10, 128, 46}, {24, 180, 60}, {80, 230, 96}, {168, 255, 160},
        };
        return MG[band];
    }
    if (m_aurora) {
        int band = tiles_[t].e[0] >> 4;
        double drift = sin(cy * 0.37 - now_ms() * 0.0012 + cx * 0.21);
        int tb = band + (int)round(drift * (band > 2 ? 1.1 : 0.35));
        if (tb < 0) tb = 0; if (tb > 7) tb = 7;
        return AURPAL[tb];
    }
    if (m_city) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t h2c = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 77);
        if (band <= 2 && hash3((uint32_t)(cx * 7 + ix * 3), (uint32_t)(cy * 5 - ix * 4), 404) % 12 < 4)
            return (RGB){96, 116, 156};
        RGB c = CITYPAL[band];
        if (band >= 3 && h2c % 100 < 18) c = lerp(c, WINDOW_AMBER, 0.75);
        else if (band <= 1 && h2c % 1000 < 16) c = STAR_COOL;
        return c;
    }
    if (m_dungeon) {
        uint8_t cls[8][8];
        art_dungeon(t, cls);
        RGB c = DUNPAL[cls[ix][iy]];
        uint32_t j = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 5) % 1000;
        return scalec(c, 0.92 + j * 0.00016);
    }
    if (m_fire) {
        int band = tiles_[t].e[0] >> 4;
        int off = (int)(hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 7) % 3) - 1;
        int tb = band + off;
        if (tb < 0) tb = 0; if (tb > 7) tb = 7;
        return FIREPAL[tb];
    }
    if (m_lava) {
        int band = tiles_[t].e[0] >> 4;
        int off = (int)(hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 7) % 3) - 1;
        int tb = band + off;
        if (tb < 0) tb = 0; if (tb > 7) tb = 7;
        return LAVAPAL[tb];
    }
    if (m_koi) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t hk = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 424);
        int tb = dither_band(band, cx, cy, ix, iy);
        if (band <= 2 && hk % 97 < 3) return (RGB){36, 104, 54};
        return KOIPAL[tb];
    }
    if (m_sakura) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t h = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 31);
        int tb = dither_band(clampb(band + (int)(h % 3) - 1), cx, cy, ix, iy);
        RGB c = SKYPAL[tb];
        double tsec = now_ms() * 0.001;
        uint32_t lane = hash3((uint32_t)cx, (uint32_t)cy, 88);
        double px = (lane % 97) / 97.0 * W_ + sin(tsec * 0.9 + lane) * 1.6;
        double py = fmod((lane % 53) / 53.0 * H_ +
                         tsec * (1.1 + (lane % 7) * 0.22) * (H_ / 24.0), (double)H_);
        int pxc = ((int)px % W_ + W_) % W_, pyc = (int)py % H_;
        if (pxc == cx && pyc == cy) {
            double ddx = (px - floor(px)) * art - ix, ddy = (py - floor(py)) * art - iy;
            double d2 = ddx * ddx + ddy * ddy;
            if (d2 < 2.0)
                c = lerp(c, (RGB){255, 214, 228}, 1.0 - d2 / 2.0);
        }
        return c;
    }
    if (m_geode) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t facet = hash3((uint32_t)(cx / 2), (uint32_t)(cy / 2), band * 17 + 3);
        int tb = clampb(band + (int)(facet % 3) - 1);
        double shade = (facet % 5) == 0 ? 0.72 :
                       (facet % 5) == 1 ? 1.18 :
                       (facet % 5) == 2 ? 0.92 : 1.0;
        RGB c = GEOPAL[tb];
        uint32_t gl = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy),
                            (uint32_t)(now_ms() / 240));
        if (gl % 151 == 42) c = (RGB){214, 250, 245};
        return scalec(c, shade * (0.9 + 0.1 * (gl % 100) / 100.0));
    }
    if (m_lantern) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t h = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 21);
        RGB c = LNTPAL[dither_band(band, cx, cy, ix, iy)];
        if (band >= 6 && h % 173 < 3) c = (RGB){200, 214, 255};
        double tsec = now_ms() * 0.001;
        uint32_t ln = hash3((uint32_t)cx, (uint32_t)cy, 512);
        double rise = (ln % 89) / 89.0 * H_ +
                      tsec * (0.55 + (ln % 5) * 0.11) * (H_ / 26.0);
        int lx = ((int)(cx + sin(tsec * 0.8 + ln) * 1.2) % W_ + W_) % W_;
        int ly = (H_ - 1 - ((int)rise % H_) + H_) % H_;
        int dxl = lx - cx, dyl = ly - cy;
        if (dxl > W_ / 2) dxl -= W_;
        if (dxl < -W_ / 2) dxl += W_;
        if (dyl > H_ / 2) dyl -= H_;
        if (dyl < -H_ / 2) dyl += H_;
        int ad = (dxl < 0 ? -dxl : dxl) + (dyl < 0 ? -dyl : dyl);
        if (ad == 0) c = (RGB){255, 186, 100};
        else if (ad == 1) c = lerp(c, (RGB){126, 72, 42}, 0.38);
        else if (ad == 2) c = lerp(c, (RGB){72, 46, 42}, 0.18);
        return c;
    }
    if (m_dunes) {
        int band = tiles_[t].e[0] >> 4;
        double tsec = now_ms() * 0.001;
        int sh = (int)round(sin(cx * 0.8 + tsec * 2.2 + sin(cy * 0.9) * 1.4));
        RGB c = DUNEPAL[dither_band(clampb(band + sh), cx, cy, ix, iy)];
        int sx = W_ * 3 / 4, sy = H_ / 5;
        int dxs = sx - cx, dys = sy - cy;
        if (dxs > W_ / 2) dxs -= W_;
        if (dxs < -W_ / 2) dxs += W_;
        int ad = (dxs < 0 ? -dxs : dxs) + (dys < 0 ? -dys : dys);
        if (ad <= 1) c = (RGB){255, 252, 236};
        else if (ad <= 3) c = lerp(c, (RGB){255, 236, 180}, 0.30 - (ad - 2) * 0.08);
        return c;
    }
    if (m_reef) {
        int band = tiles_[t].e[0] >> 4;
        double tsec = now_ms() * 0.001;
        double caus = sin(cx * 0.7 + now_ms() * 0.0011 + sin(cy * 1.1) * 1.2)
                    + sin(cy * 1.3 - now_ms() * 0.0007);
        RGB c = REFPAL[dither_band(clampb(band + (caus > 1.4 ? 1 : caus < -1.4 ? -1 : 0)),
                                   cx, cy, ix, iy)];
        uint32_t h = hash3((uint32_t)(cx * 16 + ix), (uint32_t)(cy * 16 + iy), 63);
        if (band <= 2 && h % 53 < 4)
            c = (h & 4) ? (RGB){236, 96, 84} : (RGB){244, 148, 66};
        uint32_t bub = hash3((uint32_t)cx, 7, 5150);
        if (bub % 11 < 2) {
            double brise = (bub % 71) / 71.0 * H_ +
                           tsec * (0.9 + (bub % 5) * 0.2) * (H_ / 20.0);
            if (cy == (H_ - 1 - ((int)brise % H_) + H_) % H_ &&
                ix == (bub >> 2) % art && iy == (bub >> 4) % art)
                c = (RGB){190, 232, 240};
        }
        return c;
    }
    if (m_stained) {
        int band = tiles_[t].e[0] >> 4;
        int bn = elev_of_cell(cx, (cy + H_ - 1) % H_);
        int bs = elev_of_cell(cx, (cy + 1) % H_);
        int bw = elev_of_cell((cx + W_ - 1) % W_, cy);
        int be = elev_of_cell((cx + 1) % W_, cy);
        bool lead = (bn != band && iy < 2) || (bs != band && iy > 5) ||
                    (bw != band && ix < 2) || (be != band && ix > 5);
        if (lead) return (RGB){26, 22, 28};
        double ang = atan2(cy - H_ / 2.0, cx - W_ / 2.0);
        double sweep = 0.82 + 0.3 * (0.5 + 0.5 * sin(ang * 2.0 + now_ms() * 0.0006));
        return scalec(STAINPAL[band], sweep);
    }
    if (m_mondrian) {
        int band = tiles_[t].e[0] >> 4;
        int bn = elev_of_cell(cx, (cy + H_ - 1) % H_);
        int bs = elev_of_cell(cx, (cy + 1) % H_);
        int bw = elev_of_cell((cx + W_ - 1) % W_, cy);
        int be = elev_of_cell((cx + 1) % W_, cy);
        bool line = (bn != band && (iy == 0 || iy == 1)) ||
                    (bs != band && (iy == 6 || iy == 7)) ||
                    (bw != band && (ix == 0 || ix == 1)) ||
                    (be != band && (ix == 6 || ix == 7));
        return line ? (RGB){16, 13, 12} : MONDPAL[band];
    }
    bool px[8][8];
    if (m_streets || m_neurons || m_mycelium || m_delta) {
        art_circuit(t, px);
        if (!px[ix][iy]) return network_bg(mode);
        return network_color(mode, t, cx, cy, 1.0);
    }
    if (m_circuit || m_pipes) art_circuit(t, px); else art_truchet(t, px);
    if (!px[ix][iy]) return C_BG;
    if (m_pipes) {
        /* water family: teal to deep azure, always from the same family */
        double hh = 152 + 42 * (0.5 + 0.5 * sin(cx * 0.13 + cy * 0.05));
        return hsv(hh, 0.55, 0.88);
    }
    return m_circuit ? trace_color(cx, cy, 1.0)
                                    : (truchet_col(t) == 1 ? C_AMBER : C_SKY);
}

/* ---------------- bmp export ---------------- */
static bool image_dimensions(int art, int f, int *pw, int *ph) {
    if (!pw || !ph || art <= 0 || f <= 0 || W_ <= 0 || H_ <= 0) return false;
    uint64_t cell = (uint64_t)art * (uint64_t)f;
    uint64_t w = (uint64_t)W_ * cell, h = (uint64_t)H_ * cell;
    if (w > (uint64_t)INT_MAX || h > (uint64_t)INT_MAX ||
        (h != 0 && w > UINT64_MAX / h) || w * h > (uint64_t)MAX_EXPORT_PIXELS)
        return false;
    *pw = (int)w;
    *ph = (int)h;
    return true;
}

static bool save_bmp(const char *path) {
    const char *mode = MODES[g_mode_idx];
    int art = strcmp(mode, "terrain") ? 8 : 16;
    int f = (strcmp(mode, "terrain") ? 6 : 4) * g_zoom;
    int pw, ph;
    if (!image_dimensions(art, f, &pw, &ph)) {
        set_note("bmp too large (reduce --zoom or grid size)");
        return false;
    }
    size_t rowb = ((size_t)pw * 3 + 3) & ~(size_t)3;
    uint8_t hdr[54] = {0};
    uint64_t tot = (uint64_t)rowb * (uint64_t)ph;
    if (tot > 0xFFFFFFFFull - 54) { /* BMP sizes are 32-bit */
        set_note("bmp too large (%llux%llu)", (unsigned long long)pw, (unsigned long long)ph);
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) { set_note("save failed: %s", strerror(errno)); return false; }
    uint32_t imgsz = (uint32_t)tot;
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (uint8_t)((54 + imgsz));
    hdr[3] = (uint8_t)((54 + imgsz) >> 8);
    hdr[4] = (uint8_t)((54 + imgsz) >> 16);
    hdr[5] = (uint8_t)((54 + imgsz) >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (uint8_t)pw; hdr[19] = (uint8_t)(pw >> 8);
    hdr[20] = (uint8_t)(pw >> 16); hdr[21] = (uint8_t)(pw >> 24);
    hdr[22] = (uint8_t)ph; hdr[23] = (uint8_t)(ph >> 8);
    hdr[24] = (uint8_t)(ph >> 16); hdr[25] = (uint8_t)(ph >> 24);
    hdr[26] = 1;
    hdr[27] = 0;
    hdr[28] = 24;
    hdr[29] = 0;
    bool ok = fwrite(hdr, 1, 54, fp) == 54;
    uint8_t *row = calloc(rowb, 1);
    if (!row) {
        fclose(fp);
        set_note("save failed: out of memory");
        return false;
    }
    for (int y = ph - 1; y >= 0 && ok; y--) {
        int cy = y / (art * f), iy = (y / f) % art;
        for (int x = 0; x < pw; x++) {
            int cx = x / (art * f), ix = (x / f) % art;
            RGB c = img_px(cx, cy, ix, iy, art);
            size_t xi = (size_t)x * 3;
            row[xi] = c.b; row[xi + 1] = c.g; row[xi + 2] = c.r;
        }
        if (fwrite(row, 1, rowb, fp) != rowb) ok = false;
    }
    free(row);
    if (fclose(fp) != 0) ok = false;
    if (!ok) set_note("save failed (disk?)");
    return ok;
}

/* cheap additive bloom: bright pixels bleed light */
static void apply_bloom(uint8_t *rgb, int w, int h) {
    if (g_no_bloom) return;
    int bw = w / 4 > 1 ? w / 4 : 1, bh = h / 4 > 1 ? h / 4 : 1;
    float *m = calloc((size_t)bw * bh, sizeof(float));
    float *tmp = malloc(sizeof(float) * (size_t)bw * bh);
    if (!m || !tmp) { free(m); free(tmp); return; }
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int sx = x * 4 + 2 < w ? x * 4 + 2 : w - 1;
            int sy = y * 4 + 2 < h ? y * 4 + 2 : h - 1;
            const uint8_t *p = rgb + ((size_t)sy * w + sx) * 3;
            float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            m[(size_t)y * bw + x] = lum > 130 ? (lum - 130) / 125.0f : 0;
        }
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                float s = 0;
                for (int k = -2; k <= 2; k++) {
                    int xx = x + k; if (xx < 0) xx = 0; if (xx >= bw) xx = bw - 1;
                    s += m[(size_t)y * bw + xx];
                }
                tmp[(size_t)y * bw + x] = s / 5;
            }
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                float s = 0;
                for (int k = -2; k <= 2; k++) {
                    int yy = y + k; if (yy < 0) yy = 0; if (yy >= bh) yy = bh - 1;
                    s += tmp[(size_t)yy * bw + x];
                }
                m[(size_t)y * bw + x] = s / 5;
            }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int x0 = x / 4, y0 = y / 4;
            if (x0 >= bw) x0 = bw - 1;
            if (y0 >= bh) y0 = bh - 1;
            float b = m[(size_t)y0 * bw + x0] * 0.55f;
            if (b <= 0.01f) continue;
            uint8_t *p = rgb + ((size_t)y * w + x) * 3;
            int r = p[0] + (int)(b * p[0]), g = p[1] + (int)(b * p[1]), bl = p[2] + (int)(b * p[2]);
            p[0] = r > 255 ? 255 : (uint8_t)r;
            p[1] = g > 255 ? 255 : (uint8_t)g;
            p[2] = bl > 255 ? 255 : (uint8_t)bl;
        }
    free(m); free(tmp);
}

/* shared top-down RGB raster for exports */
static uint8_t *raster_rgb(int art, int f, int *ow, int *oh) {
    int pw, ph;
    if (!image_dimensions(art, f, &pw, &ph)) {
        if (ow) *ow = 0;
        if (oh) *oh = 0;
        return NULL;
    }
    size_t pixels = (size_t)pw * (size_t)ph;
    uint8_t *raw = malloc(pixels * 3);
    if (!raw) { perror("malloc"); exit(1); }
    for (int y = 0; y < ph; y++) {
        int cy = y / (art * f), iy = (y / f) % art;
        for (int x = 0; x < pw; x++) {
            int cx = x / (art * f), ix = (x / f) % art;
            RGB c = img_px(cx, cy, ix, iy, art);
            size_t i = ((size_t)y * pw + x) * 3;
            raw[i] = c.r; raw[i + 1] = c.g; raw[i + 2] = c.b;
        }
    }
    apply_bloom(raw, pw, ph);
    *ow = pw; *oh = ph;
    return raw;
}

/* png export via zlib */
static uint32_t crc_tab[256];
static uint32_t crc32_png(uint32_t crc, const uint8_t *p, size_t n) {
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        crc = crc_tab[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
static void png_crc_init(void) {
    static bool ready = false;
    if (ready) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
    ready = true;
}
static void png_chunk_buf(Buf *o, const char *type, const uint8_t *data, uint32_t len);

/* encode an RGB buffer as PNG in memory */
static Buf png_bytes(const uint8_t *rgb, int pw, int ph) {
    png_crc_init();
    if (!rgb || pw <= 0 || ph <= 0 ||
        (uint64_t)(unsigned)pw * (uint64_t)(unsigned)ph > (uint64_t)MAX_EXPORT_PIXELS) {
        fputs("invalid PNG dimensions\n", stderr);
        exit(1);
    }
    size_t rowlen = (size_t)pw * 3 + 1;
    if ((size_t)ph > SIZE_MAX / rowlen) {
        fputs("PNG too large\n", stderr);
        exit(1);
    }
    size_t rawlen = rowlen * (size_t)ph;
    if (rawlen > (size_t)ULONG_MAX) {
        fputs("PNG input too large\n", stderr);
        exit(1);
    }
    uint8_t *raw = malloc(rawlen);
    uLongf clen = compressBound((uLong)rawlen);
    uint8_t *comp = malloc(clen);
    if (!raw || !comp) { perror("malloc"); exit(1); }
    for (int y = 0; y < ph; y++) {
        raw[(size_t)y * rowlen] = 0;
        memcpy(raw + (size_t)y * rowlen + 1, rgb + (size_t)y * pw * 3, (size_t)pw * 3);
    }
    if (compress2(comp, &clen, raw, (uLong)rawlen, 6) != Z_OK || clen > UINT32_MAX) {
        free(raw); free(comp);
        fputs("PNG compression failed\n", stderr);
        exit(1);
    }

    Buf o;
    buf_init(&o);
    buf_put(&o, "\x89PNG\r\n\x1a\n", 8);
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(pw >> 24); ihdr[1] = (uint8_t)(pw >> 16);
    ihdr[2] = (uint8_t)(pw >> 8); ihdr[3] = (uint8_t)pw;
    ihdr[4] = (uint8_t)(ph >> 24); ihdr[5] = (uint8_t)(ph >> 16);
    ihdr[6] = (uint8_t)(ph >> 8); ihdr[7] = (uint8_t)ph;
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk_buf(&o, "IHDR", ihdr, 13);
    png_chunk_buf(&o, "IDAT", comp, (uint32_t)clen);
    png_chunk_buf(&o, "IEND", NULL, 0);
    free(raw); free(comp);
    return o;
}
static void png_chunk_buf(Buf *o, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hb[8];
    hb[0] = (uint8_t)(len >> 24); hb[1] = (uint8_t)(len >> 16);
    hb[2] = (uint8_t)(len >> 8); hb[3] = (uint8_t)len;
    memcpy(hb + 4, type, 4);
    buf_put(o, hb, 8);
    if (len && data) buf_put(o, data, len);
    uint32_t crc = crc32_png(0, (const uint8_t *)type, 4);
    if (len && data) crc = crc32_png(crc, data, len);
    uint8_t cb[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    buf_put(o, cb, 4);
}
static bool save_png(const char *path) {
    const char *mode = MODES[g_mode_idx];
    int art = strcmp(mode, "terrain") ? 8 : 16;
    int f = (strcmp(mode, "terrain") ? 6 : 4) * g_zoom;
    int pw, ph;
    uint8_t *rgb = raster_rgb(art, f, &pw, &ph);
    if (!rgb) {
        set_note("png too large (reduce --zoom or grid size)");
        return false;
    }
    Buf o = png_bytes(rgb, pw, ph);
    free(rgb);
    FILE *fp = fopen(path, "wb");
    if (!fp) { set_note("save failed: %s", strerror(errno)); buf_free(&o); return false; }
    bool ok = fwrite(o.b, 1, o.n, fp) == o.n;
    if (fclose(fp) != 0) ok = false;
    if (!ok) set_note("save failed (disk?)");
    buf_free(&o);
    return ok;
}

/* ---------------- web gallery export ---------------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_append(Buf *out, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) |
                     (i + 2 < n ? d[i + 2] : 0);
        buf_u8(out, B64[(v >> 18) & 63]);
        buf_u8(out, B64[(v >> 12) & 63]);
        buf_u8(out, i + 1 < n ? B64[(v >> 6) & 63] : '=');
        buf_u8(out, i + 2 < n ? B64[v & 63] : '=');
    }
}

/* solve one small map of the given mode/seed with all post-passes */
static bool gallery_solve(int mode_idx, uint64_t seed, int w, int h) {
    setup_mode(mode_idx);
    g_seed = seed;
    rs_ = g_seed ^ 0xD1B54A32D192ED03ULL;
    W_ = w; H_ = h;
    grid_alloc(W_, H_);
    bool solved = false;
    for (int tries = 0; tries < 1000; tries++) {
        grid_reset();
        bool done = false;
        while (!done) {
            int r = wfc_step();
            if (r == 1) done = true;
            else if (r == -1) break;
        }
        if (done) { solved = true; break; }
    }
    if (solved && !strcmp(MODES[g_mode_idx], "terrain")) {
        carve_rivers(); g_river_show = n_river_;
    }
    if (solved && (!strcmp(MODES[g_mode_idx], "circuit") || !strcmp(MODES[g_mode_idx], "pipes")))
        label_components();
    return solved;
}
/* solve every mode tiny; cache one color per cell */
static RGB sheet_cell[NMODES][48][24];
static int sheet_w[NMODES], sheet_h[NMODES];
static void sheet_scan(void) {
    /* Snapshot the live world so preview generation is observational. */
    int ow = W_, oh = H_, saved_mode = g_mode_idx;
    size_t cells = (ow > 0 && oh > 0) ? (size_t)ow * (size_t)oh : 0;
    if (!cells || !dom_ || !river_ || !river_rank_ || !comp_ || !comp_col_) {
        set_note("sheet unavailable");
        return;
    }
    uint64_t *odom = malloc(sizeof(uint64_t) * cells);
    uint8_t *oriver = malloc(cells);
    int *orank = malloc(sizeof(int) * cells);
    int *ocomp = malloc(sizeof(int) * cells);
    RGB *ocomp_col = malloc(sizeof(RGB) * cells);
    if (!odom || !oriver || !orank || !ocomp || !ocomp_col) {
        free(odom); free(oriver); free(orank); free(ocomp); free(ocomp_col);
        set_note("sheet unavailable: out of memory");
        return;
    }
    memcpy(odom, dom_, sizeof(uint64_t) * cells);
    memcpy(oriver, river_, cells);
    memcpy(orank, river_rank_, sizeof(int) * cells);
    memcpy(ocomp, comp_, sizeof(int) * cells);
    memcpy(ocomp_col, comp_col_, sizeof(RGB) * cells);
    uint64_t saved_seed = g_seed, saved_rs = rs_;
    int saved_decided = g_decided, saved_n_comp = n_comp_;
    int saved_n_river = n_river_, saved_river_show = g_river_show;
    bool saved_comp_ready = g_comp_ready;
    bool saved_hero_on = g_hero_on;
    int saved_hx = g_hx, saved_hy = g_hy;
    int saved_loot = g_loot, saved_loot_tot = g_loot_tot;
    int saved_vx = g_vx, saved_vy = g_vy;
    int pw = W_ / 3 - 1, ph = H_ / 3 - 2;
    if (pw > 14) pw = 14;
    if (ph > 7) ph = 7;
    if (pw < 4) pw = 4;
    if (ph < 3) ph = 3;
    for (int mi = 0; mi < NMODES; mi++) {
        bool solved = gallery_solve(mi, 42, pw, ph);
        sheet_w[mi] = pw;
        sheet_h[mi] = ph;
        int art = strcmp(MODES[mi], "terrain") ? 8 : 16;
        for (int y = 0; y < ph; y++)
            for (int x = 0; x < pw; x++) {
                int ix = art / 2, iy = art / 2;
                RGB c = solved ? img_px(x, y, ix, iy, art) : C_BG;
                sheet_cell[mi][y][x] = c;
            }
    }
    setup_mode(saved_mode);
    g_seed = saved_seed;
    rs_ = saved_rs;
    W_ = ow; H_ = oh;
    grid_alloc(ow, oh);
    memcpy(dom_, odom, sizeof(uint64_t) * cells);
    memcpy(river_, oriver, cells);
    memcpy(river_rank_, orank, sizeof(int) * cells);
    memcpy(comp_, ocomp, sizeof(int) * cells);
    memcpy(comp_col_, ocomp_col, sizeof(RGB) * cells);
    free(odom); free(oriver); free(orank); free(ocomp); free(ocomp_col);
    n_comp_ = saved_n_comp;
    n_river_ = saved_n_river;
    g_river_show = saved_river_show;
    g_comp_ready = saved_comp_ready;
    g_decided = saved_decided;
    g_hero_on = saved_hero_on;
    g_hx = saved_hx; g_hy = saved_hy;
    g_loot = saved_loot; g_loot_tot = saved_loot_tot;
    g_vx = saved_vx; g_vy = saved_vy;
    world_sync();
    apply_bias();
    full_repaint_ = true;
}

static void render_sheet(void) {
    static double last_build = -1e9;
    if (now_ms() - last_build > 4000) {
        last_build = now_ms();
        sheet_scan();
    }
    const char *mode = MODES[g_mode_idx];
    bool braille = strcmp(mode, "terrain") != 0;
    (void)braille;
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    char st[96];
    int cellw = sheet_w[0] > 0 ? sheet_w[0] : 8;
    int cellh = sheet_h[0] > 0 ? sheet_h[0] : 5;
    for (int mi = 0; mi < NMODES; mi++) {
        int px = (mi % 3) * (cellw * 2 + 6) + 2;
        int py = (mi / 3) * (cellh + 3) + 1;
        snprintf(st, sizeof st, "\x1b[%d;%dH", py, px);
        fb_puts(st);
        if (mi == g_mode_idx) fb_fg((RGB){74, 222, 128});
        else fb_fg((RGB){148, 163, 184});
        if (mi == g_mode_idx) fb_puts("\xe2\x96\xb8 ");
        fb_puts(MODES[mi]);
        for (int y = 0; y < sheet_h[mi]; y++) {
            snprintf(st, sizeof st, "\x1b[%d;%dH", py + 1 + y, px);
            fb_puts(st);
            for (int x = 0; x < sheet_w[mi]; x++) {
                RGB c = sheet_cell[mi][y][x];
                snprintf(st, sizeof st,
                         "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm  ",
                         c.r, c.g, c.b, c.r, c.g, c.b);
                fb_puts(st);
            }
        }
    }
    fb_puts("\x1b[0m");
    fb_fg((RGB){100, 120, 150});
    fb_puts("\x1b[1;1H");
    fb_puts("  wfc sheet \xe2\x80\x94 all twenty-five worlds\xe2\x80\xa6 live\xe2\x80\xa6 (z to close) \xe2\x94\x82 [m]ode selected: ");
    fb_fg((RGB){74, 222, 128});
    fb_puts(MODES[g_mode_idx]);
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

static bool run_gallery(const char *htmlpath) {
    static const uint64_t seeds[3] = {7, 42, 2026};
    int gw = 36, gh = 20;
    Buf page;
    buf_init(&page);
    buf_puts(&page,
             "<!doctype html><html><head><meta charset='utf-8'>"
             "<title>wave function collapse</title><style>"
             "body{background:#0b0e14;color:#94a3b8;font-family:-apple-system,sans-serif;"
             "margin:24px}h1{color:#e2e8f0;font-weight:600}h1 span{color:#22d3ee}"
             ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:18px}"
             ".card{background:#131926;border-radius:12px;overflow:hidden}"
             ".card img{width:100%;display:block}.cap{padding:10px 14px;font-size:13px;"
             "display:flex;justify-content:space-between}.cap b{color:#e2e8f0}</style></head><body>"
             "<h1>WAVE FUNCTION <span>COLLAPSE</span></h1>"
             "<p>procedural worlds grown by constraint propagation \u00b7 one file of C \u00b7 "
             "<code>cc -O2 -std=c11 -o wfc wfc.c -lz</code></p><div class='grid'>");
    int made = 0;
    for (int mi = 0; mi < NMODES; mi++)
        for (int si = 0; si < 3; si++) {
        bool solved = gallery_solve(mi, seeds[si], gw, gh);
        int art = strcmp(MODES[mi], "terrain") ? 8 : 16;
        int f = strcmp(MODES[mi], "terrain") ? 3 : 3;
        int pw, ph;
        uint8_t *rgb = raster_rgb(art, f, &pw, &ph);
        if (!rgb) {
            fprintf(stderr, "gallery: raster allocation failed for %s\n", MODES[mi]);
            continue;
        }
        if (!solved) fprintf(stderr, "gallery: solver failed for %s seed %llu\n",
                             MODES[mi], (unsigned long long)seeds[si]);
        Buf img = png_bytes(rgb, pw, ph);
            free(rgb);
            char head[256];
            snprintf(head, sizeof head,
                     "<div class='card'><img alt='%s' src='data:image/png;base64,",
                     MODES[mi]);
            buf_puts(&page, head);
            b64_append(&page, img.b, img.n);
            snprintf(head, sizeof head,
                     "'><div class='cap'><b>%s</b><span>seed %llu</span></div></div>",
                     MODES[mi], (unsigned long long)seeds[si]);
            buf_puts(&page, head);
            buf_free(&img);
            made++;
            fprintf(stderr, "\rgallery: %d/%d", made, NMODES * 3);
        }
    buf_puts(&page, "</div></body></html>");
    FILE *fp = fopen(htmlpath, "wb");
    bool ok = false;
    if (fp) {
        bool wrote = fwrite(page.b, 1, page.n, fp) == page.n;
        int close_rc = fclose(fp);
        ok = wrote && close_rc == 0;
    }
    buf_free(&page);
    fprintf(stderr, "\n");
    if (!ok) {
        fprintf(stderr, "gallery: failed to write %s: %s\n", htmlpath, strerror(errno));
        return false;
    }
    printf("gallery: %s (%d maps)\n", htmlpath, made);
    return true;
}

/* ---------------- terminal / input ---------------- */

/* dispatch by extension */
static bool save_image(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot && (!strcmp(dot, ".png") || !strcmp(dot, ".PNG")))
        return save_png(path);
    return save_bmp(path);
}

/* ---------------- animated gif export (pure C, GIF89a) ---------------- */
static uint8_t qidx(RGB c) {
    int r = c.r * 6 / 256, g = c.g * 6 / 256, b = c.b * 6 / 256;
    if (r > 5) r = 5; if (g > 5) g = 5; if (b > 5) b = 5;
    return (uint8_t)((r * 6 + g) * 6 + b);
}

static int16_t (*gif_kids)[256];
#define GIF_KID_ROWS 512
static void gif_emit(Buf *o, int code, int width, int *accbits, int *acc) {
    *acc |= code << *accbits;
    *accbits += width;
    while (*accbits >= 8) {
        uint8_t byte = (uint8_t)(*acc & 255);
        buf_put(o, &byte, 1);
        *acc >>= 8;
        *accbits -= 8;
    }
}
static void gif_lzw_frame(Buf *o, const uint8_t *pix, size_t n) {
    if (!gif_kids) {
        gif_kids = malloc(sizeof *gif_kids * GIF_KID_ROWS);
        if (!gif_kids) { perror("malloc"); exit(1); }
    }
    /* fixed 9-bit codes: reset dictionary before it could outgrow 9 bits */
    memset(gif_kids, -1, sizeof *gif_kids * GIF_KID_ROWS);
    int width = 9, next = 258, acc = 0, ab = 0;
    gif_emit(o, 256, width, &ab, &acc);
    int cur = pix[0];
    for (size_t i = 1; i < n; i++) {
        int k = pix[i];
        if (gif_kids[cur][k] >= 0) { cur = gif_kids[cur][k]; continue; }
        gif_emit(o, cur, width, &ab, &acc);
        if (next >= 511) {
            memset(gif_kids, -1, sizeof *gif_kids * GIF_KID_ROWS);
            next = 258;
            gif_emit(o, 256, width, &ab, &acc);
        } else {
            gif_kids[cur][k] = (int16_t)next++;
        }
        cur = k;
    }
    gif_emit(o, cur, width, &ab, &acc);
    gif_emit(o, 257, width, &ab, &acc);
    if (ab > 0) { uint8_t b2 = (uint8_t)(acc & 255); buf_put(o, &b2, 1); }
}
static void gif_palette(Buf *o) {
    static uint8_t p[768];
    for (int i = 0; i < 216; i++)
        p[i * 3] = (uint8_t)((i / 36) * 51), p[i * 3 + 1] = (uint8_t)(((i / 6) % 6) * 51),
        p[i * 3 + 2] = (uint8_t)((i % 6) * 51);
    for (int i = 216; i < 252; i++) {
        uint8_t g = (uint8_t)((i - 216) * 255 / 35);
        p[i * 3] = p[i * 3 + 1] = p[i * 3 + 2] = g;
    }
    for (int i = 252; i < 256; i++) p[i * 3] = p[i * 3 + 1] = p[i * 3 + 2] = 0;
    buf_put(o, p, 768);
}
static void gif_blocks(Buf *o, const uint8_t *d, size_t n) {
    while (n) {
        size_t k = n > 255 ? 255 : n;
        buf_u8(o, (int)k);
        buf_put(o, d, k);
        d += k; n -= k;
    }
    buf_u8(o, 0);
}

typedef struct { uint8_t *px; int pw, ph; } Frame;
static Frame *g_frames;
static int g_nframes, g_fcap;
static int g_gif_on = 0;
static char g_gif_path[512] = {0};

static void frames_clear(void) {
    for (int i = 0; i < g_nframes; i++) free(g_frames[i].px);
    g_nframes = 0;
}
static void capture_frame(void) {
    if (g_nframes == g_fcap) {
        g_fcap = g_fcap ? g_fcap * 2 : 128;
        g_frames = realloc(g_frames, (size_t)g_fcap * sizeof *g_frames);
        if (!g_frames) { perror("realloc"); exit(1); }
    }
    int art = strcmp(MODES[g_mode_idx], "terrain") ? 8 : 16, f = 16 / art;
    int pw = W_ * 16, ph = H_ * 16;
    uint8_t *rgb = malloc((size_t)pw * ph * 3);
    uint8_t *ixs = malloc((size_t)pw * ph);
    if (!rgb || !ixs) { perror("malloc"); exit(1); }
    for (int y = 0; y < ph; y++) {
        int cy = y / 16, iy = (y / f) % art;
        for (int x = 0; x < pw; x++) {
            int cx = x / 16, ix = (x / f) % art;
            RGB c = img_px(cx, cy, ix, iy, art);
            size_t i = ((size_t)y * pw + x) * 3;
            rgb[i] = c.r; rgb[i + 1] = c.g; rgb[i + 2] = c.b;
        }
    }
    apply_bloom(rgb, pw, ph);
    for (size_t i = 0; i < (size_t)pw * ph; i++)
        ixs[i] = qidx((RGB){rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]});
    free(rgb);
    g_frames[g_nframes].px = ixs;
    g_frames[g_nframes].pw = pw;
    g_frames[g_nframes].ph = ph;
    g_nframes++;
}
static bool write_gif(const char *path) {
    if (!path || !g_nframes) return false;
    int pw = g_frames[0].pw, ph = g_frames[0].ph;
    if (pw <= 0 || ph <= 0 || pw > UINT16_MAX || ph > UINT16_MAX) {
        set_note("gif dimensions unsupported");
        return false;
    }
    Buf o;
    buf_init(&o);
    buf_put(&o, "GIF89a", 6);
    buf_u16(&o, pw); buf_u16(&o, ph);
    buf_u8(&o, 0xF7); buf_u8(&o, 0); buf_u8(&o, 0);
    gif_palette(&o);
    buf_put(&o, "\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
    int written = 0;
    for (int i = 0; i < g_nframes; i++) {
        if (g_frames[i].pw != pw || g_frames[i].ph != ph) continue; /* resize mid-recording */
        buf_put(&o, "\x21\xF9\x04\x00\x28\x00\x00\x00", 8); /* 40cs = 400ms? no: 0x28=40 -> 0.4s */
        buf_u8(&o, 0x2C);
        buf_u16(&o, 0); buf_u16(&o, 0); buf_u16(&o, pw); buf_u16(&o, ph);
        buf_u8(&o, 0);
        buf_u8(&o, 8);
        Buf lw;
        buf_init(&lw);
        gif_lzw_frame(&lw, g_frames[i].px, (size_t)pw * ph);
        gif_blocks(&o, lw.b, lw.n);
        buf_free(&lw);
        written++;
    }
    if (!written) { buf_free(&o); return false; }
    buf_u8(&o, 0x3B);
    bool ok = false;
    FILE *fp = fopen(path, "wb");
    if (fp) {
        ok = fwrite(o.b, 1, o.n, fp) == o.n;
        if (fclose(fp) != 0) ok = false;
        if (!ok) set_note("gif failed (disk?)");
    }
    else set_note("gif failed: %s", strerror(errno));
    buf_free(&o);
    return ok;
}

/* ---------------- inline-image graphics (iTerm2/WezTerm OSC 1337) ---------------- */
static void render_frame(long steps, int attempts, double pulse);
static void emit_frame_img(void);
static int g_gfx = 0;           /* 0 braille, 1 iterm2, 2 kitty */
static bool g_sound = false;
static void ambient_update(void);
static void draw_any(long steps, int attempts, double pulse) {
    double now = now_ms();
    if (now - last_draw_ms_ < 24) return; /* ~40fps ceiling */
    last_draw_ms_ = now;
    if (W_ > 0 && H_ > 0 && (g_decided < W_ * H_ || g_quality_live < 0.0))
        quality_record(quality_measure(false));
    if (g_sound) ambient_update();
    if (g_rt) { render_rt_frame(now); last_draw_ms_ = now_ms(); }
    else if (g_iso) { render_iso_frame(); last_draw_ms_ = now_ms(); }
    else if (g_gfx) emit_frame_img();
    else render_frame(steps, attempts, pulse);
}

static bool g_no_gfx = false;
static bool g_force_gfx = false;

static int gfx_kind(void) { /* 1 = iterm2 OSC1337, 2 = kitty APC */
    const char *tp = getenv("TERM_PROGRAM");
    if (tp && (!strcmp(tp, "iTerm.app") || !strcmp(tp, "WezTerm"))) return 1;
    if (getenv("KITTY_WINDOW_ID")) return 2;
    const char *tm = getenv("TERM");
    if (tm && strstr(tm, "kitty")) return 2;
    tp = getenv("TERM_PROGRAM");
    if (tp && !strcmp(tp, "ghostty")) return 2;
    return 0;
}
static bool gfx_supported(void) {
    if (g_no_gfx) return false;
    if (g_force_gfx) return true;
    return gfx_kind() != 0;
}

/* draw one full-res frame of the current grid as an inline image */
static void emit_frame_img(void) {
    int art = strcmp(MODES[g_mode_idx], "terrain") ? 8 : 16;
    int raw_pw = W_ * (g_quad || g_twin ? 32 : 16);
    int raw_ph = H_ * (g_quad ? 32 : 16);
    int image_scale = 1;
    while (raw_pw / image_scale > 2048 || raw_ph / image_scale > 2048)
        image_scale *= 2;
    int cell_px = 16 / image_scale;
    int pw = raw_pw / image_scale, ph = raw_ph / image_scale;
    uint8_t *rgb = malloc((size_t)pw * ph * 3);
    if (!rgb) { perror("malloc"); exit(1); }
    for (int y = 0; y < ph; y++) {
        int qrow = g_quad ? (y >= ph / 2) : 0;
        int local_y = y - qrow * (g_quad ? ph / 2 : 0);
        int cy = local_y / cell_px;
        int iy = (local_y % cell_px) * art / cell_px;
        for (int x = 0; x < pw; x++) {
            int qcol = g_quad ? (x >= pw / 2) : (g_twin && x >= pw / 2);
            int local_x = x - qcol * (g_nworlds > 1 ? pw / 2 : 0);
            int cx = local_x / cell_px;
            int ix = (local_x % cell_px) * art / cell_px;
            uint64_t *saved = dom_;
            if (g_nworlds > 1) {
                int slot = qrow * 2 + qcol;
                dom_ = slots_dom[slot] ? slots_dom[slot] : saved;
            }
            RGB c = img_px(cx, cy, ix, iy, art);
            dom_ = saved;
            size_t i = ((size_t)y * pw + x) * 3;
            rgb[i] = c.r; rgb[i + 1] = c.g; rgb[i + 2] = c.b;
        }
    }
    Buf img = png_bytes(rgb, pw, ph);
    free(rgb);
    Buf b64;
    buf_init(&b64);
    b64_append(&b64, img.b, img.n);
    buf_free(&img);
    char head[128];
    snprintf(head, sizeof head,
             "\x1b]1337;File=inline=1;size=%dx%d;height=%d;preserveAspectRatio=1;base64:",
             pw, ph, (g_quad ? H_ * 2 : H_) + 1);
    if (g_gfx == 2) {
        /* kitty graphics protocol: chunked APC with fixed image id (replaces) */
        fputs("\x1b[H", stdout);
        size_t off = 0, n = b64.n;
        int chunk = 0;
        while (off < n) {
            size_t take = n - off > 4096 ? 4096 : n - off;
            chunk++;
            int last = off + take >= n;
            fprintf(stdout, "\x1b_Gf=32,s=%d,v=%d,i=7,q=1,a=T,m=%d;%.*s\x1b\\",
                    pw, ph, last ? 0 : 1, (int)take, b64.b + off);
            off += take;
            (void)chunk;
        }
    } else {
        fwrite(head, 1, strlen(head), stdout);
        fwrite(b64.b, 1, b64.n, stdout);
        buf_free(&b64);
        fwrite("\a", 1, 1, stdout);
        return;
    }
    buf_free(&b64);
}

/* ---------------- tiny synth: procedural wav sfx via afplay ---------------- */
static char g_gallery_path[512] = {0};
static bool g_sfx_ready = false;

static void put_u32(FILE *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}
static void put_u16(FILE *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}
static void write_wav(const char *path, const float *samples, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t datasz = (uint32_t)n * 2;
    fwrite("RIFF", 1, 4, f); put_u32(f, 36 + datasz);
    fwrite("WAVEfmt ", 1, 8, f); put_u32(f, 16); put_u16(f, 1); put_u16(f, 1);
    put_u32(f, 44100); put_u32(f, 88200); put_u16(f, 2); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, datasz);
    for (int i = 0; i < n; i++) {
        float s = samples[i];
        if (s > 1) s = 1; if (s < -1) s = -1;
        int16_t v16 = (int16_t)(s * 32000);
        put_u16(f, (uint16_t)v16);
    }
    fclose(f);
}
static void ensure_sfx(void) {
    if (g_sfx_ready) return;
    const int SR = 44100;
    /* completion chord: A4 C#5 E5 A5 arpeggio with soft decay */
    static float done[44100 * 1];
    for (int i = 0; i < 44100; i++) done[i] = 0;
    float notes[4] = {440.0f, 554.37f, 659.25f, 880.0f};
    for (int ni = 0; ni < 4; ni++) {
        int start = ni * 6200;
        for (int i = start; i < 44100; i++) {
            double t = (double)(i - start) / SR;
            float env = (float)exp(-t * 7.0);
            done[i] += 0.16f * env *
                       (sinf(2 * (float)M_PI * notes[ni] * t) +
                        0.35f * sinf(2 * (float)M_PI * notes[ni] * 2 * t));
        }
    }
    write_wav("/tmp/wfc_done.wav", done, 44100);
    /* click blip */
    static float blip[2600];
    for (int i = 0; i < 2600; i++) {
        double t = (double)i / SR;
        blip[i] = 0.18f * (float)exp(-t * 60.0) * sinf(2 * (float)M_PI * 950 * t);
    }
    write_wav("/tmp/wfc_blip.wav", blip, 2600);
    /* per-world stingers */
    static const char *names[NMODES] = {"circuit","terrain","truchet","fire","waves","dungeon","maze","galaxy","city","aurora","matrix","pipes","mondrian","koi","lava","sakura","geode","lantern","dunes","reef","stained","streets","neurons","mycelium","delta"};
    for (int mi2 = 0; mi2 < NMODES; mi2++) {
        static float st[44100 / 2];
        for (int i = 0; i < 22050; i++) st[i] = 0;
        float seq[NMODES][6][2] = {   /* {freq, startSeconds} pairs, 0-terminated */
            {{523,0},{659,0.09},{784,0.18},{1046,0.27},{0,0}},
            {{392,0},{440,0.11},{494,0.22},{587,0.33},{0,0}},
            {{440,0},{554,0.1},{440,0.2},{554,0.3},{0,0}},
            {{196,0},{220,0.12},{247,0.24},{196,0.36},{0,0}},
            {{659,0},{880,0.14},{987,0.28},{1319,0.42},{0,0}},
            {{110,0},{165,0.16},{110,0.32},{147,0.48},{0,0}},
            {{330,0},{330,0.13},{392,0.26},{330,0.39},{0,0}},
            {{1046,0},{1319,0.08},{1568,0.16},{2093,0.24},{0,0}},
            {{262,0},{330,0.14},{392,0.28},{523,0.42},{0,0}},
            {{392,0},{523,0.12},{659,0.24},{784,0.36},{0,0}},
            {{330,0},{277,0.16},{330,0.32},{277,0.48},{0,0}},
            {{294,0},{440,0.12},{392,0.24},{494,0.36},{0,0}},
            {{247,0},{262,0.16},{294,0.32},{330,0.48},{0,0}},
            {{440,0},{523,0.14},{587,0.28},{659,0.42},{0,0}},
            {{98,0},{123,0.2},{98,0.4},{87,0.6},{0,0}},
            {{659,0},{587,0.12},{523,0.24},{440,0.36},{0,0}},
            {{1319,0},{1046,0.09},{1568,0.18},{2093,0.27},{0,0}},
            {{262,0},{330,0.12},{392,0.24},{523,0.36},{0,0}},
            {{147,0},{220,0.14},{196,0.28},{294,0.42},{0,0}},
            {{523,0},{659,0.1},{784,0.2},{988,0.3},{0,0}},
            {{392,0},{587,0.14},{784,0.28},{1175,0.42},{0,0}},
            {{196,0},{294,0.14},{392,0.28},{587,0.42},{0,0}},
            {{220,0},{330,0.10},{440,0.20},{660,0.30},{0,0}},
            {{147,0},{196,0.14},{247,0.28},{330,0.42},{0,0}},
            {{196,0},{247,0.12},{294,0.24},{392,0.36},{0,0}},
        };
        for (int nn2 = 0; nn2 < 5 && seq[mi2][nn2][0] > 0; nn2++) {
            int start = (int)(seq[mi2][nn2][1] * SR);
            float fr = seq[mi2][nn2][0];
            for (int i = start; i < 22050; i++) {
                double t2 = (double)(i - start) / SR;
                float env = (float)exp(-t2 * 9.0) * (t2 < 0.01 ? t2 * 100 : 1);
                st[i] += 0.15f * env * sinf(2 * (float)M_PI * fr * t2);
            }
        }
        char p2[64];
        snprintf(p2, sizeof p2, "/tmp/wfx_st_%s.wav", names[mi2]);
        write_wav(p2, st, 22050);
    }
    g_sfx_ready = true;
}
static void play_sfx(const char *path);
static void play_stinger(int mode_idx) {
    if (!g_sound) return;
    ensure_sfx();
    char p[96];
    snprintf(p, sizeof p, "/tmp/wfx_st_%s.wav", MODES[mode_idx]);
    play_sfx(p);
}

/* ---------------- per-world ambient drones ----------------
 * A quiet 24s seamless loop per mode: a low root, a fifth, and a
 * soft octave partial with a slow beat; the LFO rates divide the
 * loop length so the seam is inaudible. Restarted slightly early. */
#define AMBIENT_SECS 24
#define AMBIENT_MS (AMBIENT_SECS * 1000 - 500)
static int g_amb_mode = -1;
static long g_amb_t0 = 0;

static void ambient_stop(void) {
    if (g_amb_mode >= 0)
        system("pkill -f 'afplay /tmp/wfx_amb' >/dev/null 2>&1; "
               "pkill -f 'aplay -q /tmp/wfx_amb' >/dev/null 2>&1;");
    g_amb_mode = -1;
    g_amb_t0 = 0;
}

static void ensure_ambient(int mi) {
    char p[96];
    snprintf(p, sizeof p, "/tmp/wfx_amb_%s.wav", MODES[mi]);
    FILE *probe = fopen(p, "rb");
    if (probe) { fclose(probe); return; } /* already synthesized */
    const int SR = 44100;
    int n = SR * AMBIENT_SECS;
    float *s = malloc(sizeof(float) * (size_t)n);
    if (!s) return;
    /* pentatonic-ish roots so adjacent modes drift musically */
    static const float roots[NMODES] = {
        110.0f, 98.0f, 164.8f, 87.3f, 73.4f, 61.7f, 82.4f, 130.8f,
        73.4f, 110.0f, 92.5f, 87.3f, 123.5f, 98.0f, 61.7f,
        116.5f, 146.8f, 82.4f, 87.3f, 98.0f, 110.0f, 98.0f, 220.0f, 73.4f,
        82.4f,
    };
    double f = roots[mi];
    double lfo1 = 1.0 / AMBIENT_SECS, lfo2 = 3.0 / AMBIENT_SECS;
    double beat = 1.0 / AMBIENT_SECS; /* detune that wraps the seam */
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR;
        double env = 0.75 + 0.25 * sin(2 * M_PI * lfo1 * t) *
                     (0.8 + 0.2 * sin(2 * M_PI * lfo2 * t));
        double v = sin(2 * M_PI * f * t) +
                   0.55 * sin(2 * M_PI * f * 1.5 * t + 1.3) +
                   0.30 * sin(2 * M_PI * (f * 2 + beat) * t) +
                   0.45 * sin(2 * M_PI * f * 0.5 * t + 0.5);
        s[i] = (float)(0.055 * env * v);
    }
    write_wav(p, s, n);
    free(s);
}

static void ambient_update(void) {
    if (!g_sound || g_nworlds > 1) { if (g_amb_mode >= 0) ambient_stop(); return; }
    long now = now_ms();
    if (g_amb_mode != g_mode_idx) {
        ambient_stop();
        ensure_ambient(g_mode_idx);
        char p[96];
        snprintf(p, sizeof p, "/tmp/wfx_amb_%s.wav", MODES[g_mode_idx]);
        play_sfx(p);
        g_amb_mode = g_mode_idx;
        g_amb_t0 = now;
        return;
    }
    if (g_amb_t0 && now - g_amb_t0 > AMBIENT_MS) { /* seamless-ish retrigger */
        char p[96];
        snprintf(p, sizeof p, "/tmp/wfx_amb_%s.wav", MODES[g_mode_idx]);
        play_sfx(p);
        g_amb_t0 = now;
    }
}
#ifdef __APPLE__
#define PLAY_CMD(p) snprintf(cmd, sizeof cmd, "afplay '%s' >/dev/null 2>&1 &", p)
#else
#define PLAY_CMD(p) snprintf(cmd, sizeof cmd, "aplay -q '%s' >/dev/null 2>&1 &", p)
#endif
static void play_sfx(const char *path) {
    if (!g_sound) return;
    char cmd[128];
    PLAY_CMD(path);
    system(cmd);
}

/* ---------------- terminal / input ---------------- */
static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }
static void on_fatal(int s) {
    /* best-effort terminal restore, then re-raise so the shell reports it */
    static const char restore[] = "\x1b[0m\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[?25h\x1b[?1049l";
    write(STDOUT_FILENO, restore, sizeof restore - 1);
    signal(s, SIG_DFL);
    raise(s);
}
static void on_winch(int s) { (void)s; g_winch = 1; }

static struct termios g_origtio;
static bool g_raw_on = false;
static void raw_off(void) {
    if (g_raw_on) { tcsetattr(STDIN_FILENO, TCSANOW, &g_origtio); g_raw_on = false; }
}
static void raw_on(void) {
    if (tcgetattr(STDIN_FILENO, &g_origtio) != 0) return;
    struct termios t = g_origtio;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0) g_raw_on = true;
}

static void term_fit(void) {
    int cw = strcmp(MODES[g_mode_idx], "terrain") ? 4 : 2;
    int ch = strcmp(MODES[g_mode_idx], "terrain") ? 2 : 1;
    int mw = 140, mh = 70; /* auto caps */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 8 && ws.ws_row > 6) {
        if (ws.ws_col / cw < mw) mw = ws.ws_col / cw;
        if ((ws.ws_row - 1) / ch < mh) mh = (ws.ws_row - 1) / ch;
    }
    W_ = g_user_w < mw ? g_user_w : mw;
    H_ = g_user_h < mh ? g_user_h : mh;
    if (g_inf) { g_fit_w = W_; g_fit_h = H_; }
    if (g_quad && g_is_tty) { W_ /= 2; H_ /= 2; }
    else if (g_twin && g_is_tty) W_ /= 2; /* two worlds share the row */
    if (W_ < 4) W_ = 4;
    if (H_ < 4) H_ = 4;
}
static void apply_size(void) {
    int ow = W_, oh = H_;
    term_fit();
    if (ow != W_ || oh != H_ || !dom_) grid_alloc(W_, H_);
}

static void speed_changed(void) {
    g_delay_ms = g_speed > 0 ? 1000.0 / g_speed : 0;
    g_render_every = g_speed > 60 ? (int)(g_speed / 60) : 1;
}

/* ---------------- thermo solver (Extropic THRML bridge) ----------------
 * `--solver thermo` hands the WFC constraint system to wfc_thermo.py,
 * which runs it as a pairwise Potts EBM with Extropic's THRML (block
 * Gibbs, annealed, vmapped chains) and reports the p-bit budget of the
 * domain-wall Ising form a Z1-class thermodynamic chip would run.
 * This is the WFC problem *as a graphical model*: cells = categorical
 * nodes, cdir = pairwise energy, tile weights = unary.
 *
 * The sidecar is a long-lived JSONL worker. C remains authoritative for
 * domains, propagation, rollback, and deterministic quality; the worker
 * proposes soft assignments and learns only from C's reward.
 *
 * Env: WFC_PYTHON -> interpreter (default python3)
 *      WFC_THERMO_PY -> path to wfc_thermo.py (default ./wfc_thermo.py)
 */
static bool g_thermo = false;
static char g_thermo_form[8] = "potts";
static bool g_thermo_learn = true;
static char g_thermo_profile[512] = "";
static bool g_thermo_reset_learning = false;
static int thermo_pid_ = -1;
static int thermo_in_fd_ = -1;
static FILE *thermo_in_ = NULL;
static int thermo_fd_ = -1;
static FILE *thermo_fp_ = NULL;
static double thermo_t0_ = 0;
static bool thermo_valid_ = false;
static long thermo_bad_ = 0;
static int thermo_pbits_ = 0;
static int thermo_launches_ = 0;
static bool thermo_inflight_ = false;
static bool thermo_ready_ = false;
static bool thermo_waiting_sample_ = false;
static bool thermo_waiting_feedback_ = false;
static bool thermo_reset_pending_ = false;
static double thermo_beta_ = 0;
static double thermo_confidence_ = 0;
static long thermo_observations_ = 0;
static long thermo_round_ = 0;
static long thermo_proposals_ = 0;
static long thermo_accepts_ = 0;
static long thermo_rejects_ = 0;
static long thermo_contradictions_ = 0;
static double thermo_quality_ = 0;
static char thermo_sampler_[8] = "idle";
static uint64_t *thermo_snap_ = NULL;
static size_t thermo_snap_cap_ = 0;

static void thermo_kill(void) {
    if (thermo_in_) { fclose(thermo_in_); thermo_in_ = NULL; thermo_in_fd_ = -1; }
    if (thermo_fp_) { fclose(thermo_fp_); thermo_fp_ = NULL; thermo_fd_ = -1; }
    if (thermo_pid_ > 0) {
        kill(thermo_pid_, SIGKILL);
        waitpid(thermo_pid_, NULL, 0); /* reap so we don't leak zombies */
        thermo_pid_ = -1;
    }
    thermo_inflight_ = false;
    thermo_ready_ = false;
    thermo_waiting_sample_ = false;
    thermo_waiting_feedback_ = false;
}

static void thermo_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\r') fputs("\\r", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 32) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static char g_argv0[512] = "wfc";
static bool thermo_launch(void) {
    if (g_gallery_path[0] || g_collage_path[0] || g_nworlds > 1 || g_inf) return false;
    if (getenv("WFC_NO_THERMO")) return false;
    char py[512];
    const char *pyp = getenv("WFC_THERMO_PY");
    if (pyp) snprintf(py, sizeof py, "%s", pyp);
    else {
        /* find wfc_thermo.py beside the executable first, then the CWD,
         * so `WFC_PYTHON=... wfc --solver thermo` works from anywhere */
        const char *sep = strrchr(g_argv0, '/');
        if (sep) {
            snprintf(py, sizeof py, "%.*s/wfc_thermo.py", (int)(sep - g_argv0), g_argv0);
            if (access(py, R_OK) != 0) snprintf(py, sizeof py, "wfc_thermo.py");
        } else snprintf(py, sizeof py, "wfc_thermo.py");
    }
    const char *pys = getenv("WFC_PYTHON");
    if (!pys) pys = "python3"; /* users with thrml in a venv: WFC_PYTHON=/path/venv/bin/python */
    int in_pipe[2] = {-1, -1}, out_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0) return false;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        /* silence jax/absl spam; keep our own stderr */
        int err_flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
        err_flags |= O_NOFOLLOW;
#endif
        int dfd = open("/tmp/wfc_thermo_err.log", err_flags, 0600);
        if (dfd >= 0) {
            (void)fchmod(dfd, 0600);
            if (dfd != STDERR_FILENO) {
                (void)dup2(dfd, STDERR_FILENO);
                close(dfd);
            }
        }
        execlp(pys, pys, py, (char *)NULL);
        /* if the interpreter itself is missing, try `uv run python` auto-env */
        if (!getenv("WFC_THERMO_PY"))
            execlp("uv", "uv", "run", "--quiet", py, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    thermo_in_fd_ = in_pipe[1];
    thermo_fd_ = out_pipe[0];
    thermo_in_ = fdopen(thermo_in_fd_, "w");
    if (!thermo_in_) {
        close(thermo_in_fd_); thermo_in_fd_ = -1;
        close(thermo_fd_); thermo_fd_ = -1;
        kill(pid, SIGKILL); waitpid(pid, NULL, 0);
        return false;
    }
    /* non-blocking: the C loop keeps rendering while the anneal cools */
    int fl = fcntl(thermo_fd_, F_GETFL, 0);
    fcntl(thermo_fd_, F_SETFL, fl | O_NONBLOCK);
    thermo_fp_ = fdopen(thermo_fd_, "r");
    if (!thermo_fp_) { /* child is running with nobody to read it */
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        fclose(thermo_in_); thermo_in_ = NULL; thermo_in_fd_ = -1;
        close(thermo_fd_);
        thermo_fd_ = -1;
        return false;
    }
    thermo_pid_ = (int)pid;
    thermo_t0_ = now_ms();
    thermo_inflight_ = true;
    thermo_ready_ = false;
    thermo_waiting_sample_ = false;
    thermo_waiting_feedback_ = false;
    thermo_reset_pending_ = g_thermo_reset_learning;
    g_thermo_reset_learning = false;
    thermo_launches_++;
    thermo_valid_ = false; /* a fresh run must earn its own result */
    thermo_beta_ = 0;
    thermo_confidence_ = 0;
    thermo_observations_ = 0;
    thermo_round_ = 0;
    thermo_proposals_ = 0;
    thermo_accepts_ = 0;
    thermo_rejects_ = 0;
    thermo_contradictions_ = 0;
    thermo_quality_ = 0;
    snprintf(thermo_sampler_, sizeof thermo_sampler_, "boot");
    signal(SIGPIPE, SIG_IGN);

    fprintf(thermo_in_, "{\"v\":1,\"t\":\"init\",\"mode\":");
    thermo_json_string(thermo_in_, MODES[g_mode_idx]);
    QualityProfile profile = quality_profile();
    fprintf(thermo_in_, ",\"w\":%d,\"h\":%d,\"ntiles\":%d,\"seed\":%llu,"
                     "\"torus\":%s,\"smooth\":%s,\"form\":\"%s\",\"learn\":%s,"
                     "\"quality_focus\":",
            W_, H_, ntiles_, (unsigned long long)g_seed,
            g_torus ? "true" : "false", g_smooth ? "true" : "false", g_thermo_form,
            g_thermo_learn ? "true" : "false");
    thermo_json_string(thermo_in_, profile.focus);
    fputs(",\"quality_weights\":{", thermo_in_);
    fprintf(thermo_in_, "\"validity\":%.9g,\"boundary\":%.9g,\"coverage\":%.9g,"
                     "\"diversity\":%.9g,\"smoothness\":%.9g,\"stability\":%.9g,"
                     "\"topology\":%.9g},\"quality_priors\":[",
            profile.validity, profile.boundary, profile.coverage, profile.diversity,
            profile.smoothness, profile.stability, profile.topology);
    for (int i = 0; i < ntiles_; i++)
        fprintf(thermo_in_, "%s%.9g", i ? "," : "", quality_tile_prior(i));
    fputs("],\"macro_name\":", thermo_in_);
    thermo_json_string(thermo_in_, macro_name());
    fprintf(thermo_in_, ",\"macro_guided_cells\":%d", macro_guided_cells());
    fputs(",\"unary\":[", thermo_in_);
    for (int i = 0; i < ntiles_; i++)
        fprintf(thermo_in_, "%s%.9g", i ? "," : "", tiles_[i].weight);
    fputs("],\"cdir\":[", thermo_in_);
    for (int d = 0; d < NDIR; d++) {
        if (d) fputc(',', thermo_in_);
        fputc('[', thermo_in_);
        for (int a = 0; a < ntiles_; a++)
            fprintf(thermo_in_, "%s%llu", a ? "," : "", (unsigned long long)cdir_[d][a]);
        fputc(']', thermo_in_);
    }
    fputs("],\"domains\":[", thermo_in_);
    for (int i = 0; i < W_ * H_; i++)
        fprintf(thermo_in_, "%s%llu", i ? "," : "", (unsigned long long)dom_[i]);
    fputs("]", thermo_in_);
    if (g_thermo_profile[0]) {
        fputs(",\"profile_dir\":", thermo_in_);
        thermo_json_string(thermo_in_, g_thermo_profile);
    }
    fputs("}\n", thermo_in_);
    if (fflush(thermo_in_) != 0 || ferror(thermo_in_)) {
        thermo_kill();
        return false;
    }
    return true;
}

static bool thermo_send_sample(void) {
    if (!thermo_in_ || !thermo_ready_) return false;
    int budget = W_ * H_ > 900 ? 8 : W_ * H_ > 300 ? 12 : 20;
    double beta = ntiles_ > 16 ? 1.8 : 2.4;
    fprintf(thermo_in_, "{\"v\":1,\"t\":\"sample\",\"domains\":[");
    for (int i = 0; i < W_ * H_; i++)
        fprintf(thermo_in_, "%s%llu", i ? "," : "", (unsigned long long)dom_[i]);
    fprintf(thermo_in_, "],\"budget\":%d,\"beta_target\":%.6g}\n", budget, beta);
    if (fflush(thermo_in_) != 0 || ferror(thermo_in_)) return false;
    thermo_waiting_sample_ = true;
    return true;
}

static bool thermo_send_reset(void) {
    if (!thermo_in_ || !thermo_ready_) return false;
    fputs("{\"v\":1,\"t\":\"reset\"}\n", thermo_in_);
    if (fflush(thermo_in_) != 0 || ferror(thermo_in_)) return false;
    thermo_waiting_feedback_ = true;
    return true;
}

/* minimal extractors for the fixed JSON line schema */
static const char *json_str(const char *s, const char *key) {
    static char k[64];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return NULL;
    p += strlen(k);
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static long json_num(const char *s, const char *key, long fallback) {
    const char *p = json_str(s, key);
    if (!p) return fallback;
    return atol(p);
}

static int thermo_context_index(int cell) {
    int x = cell % W_, y = cell / W_, degree = 0, unresolved = 0;
    for (int d = 0; d < NDIR; d++) {
        int nx = x, ny = y;
        if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
        else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
        else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
        else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
        if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
        degree++;
        if (pc64(dom_[IDX(nx, ny)]) != 1) unresolved++;
    }
    int result = (degree < 4 ? 4 : 0) + (unresolved > 3 ? 3 : unresolved);
    return result > 7 ? 7 : result;
}

static bool thermo_send_feedback(const int *cells, const int *tiles, int count,
                                 int accepted, int rejected, int contradictions,
                                 QualityMetrics before, QualityMetrics after) {
    if (!thermo_in_) return false;
    double reward = quality_reward(before, after, accepted, rejected);
    QualityProfile profile = quality_profile();
    thermo_quality_ = after.total;
    fprintf(thermo_in_, "{\"v\":1,\"t\":\"feedback\",\"reward\":%.9g,"
                     "\"quality\":%.9g,\"accepted\":%d,\"rejected\":%d,"
                     "\"contradictions\":%d,\"quality_focus\":",
            reward, after.total, accepted, rejected, contradictions);
    thermo_json_string(thermo_in_, profile.focus);
    fputs(",\"metrics\":", thermo_in_);
    quality_json(thermo_in_, after);
    fputs(",\"metrics_delta\":", thermo_in_);
    quality_delta_json(thermo_in_, before, after);
    fputs(",\"tile_events\":[", thermo_in_);
    for (int i = 0; i < count; i++)
        fprintf(thermo_in_, "%s{\"index\":%d,\"value\":%.3g}",
                i ? "," : "", tiles[i], accepted ? 1.0 : -1.0);
    fputs("],\"pair_events\":[", thermo_in_);
    bool first = true;
    for (int i = 0; i < count; i++) {
        int x = cells[i] % W_, y = cells[i] / W_;
        for (int d = 0; d < NDIR; d++) {
            int nx = x, ny = y;
            if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
            else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
            else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
            else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
            int n = IDX(nx, ny);
            if (pc64(dom_[n]) != 1) continue;
            int nt = __builtin_ctzll(dom_[n]);
            int pair = (d * ntiles_ + tiles[i]) * ntiles_ + nt;
            bool compatible = (cdir_[d][tiles[i]] >> nt) & 1ULL;
            fprintf(thermo_in_, "%s{\"index\":%d,\"value\":%.3g}",
                    first ? "" : ",", pair, accepted && compatible ? 1.0 : -1.0);
            first = false;
        }
    }
    fputs("],\"context_events\":[", thermo_in_);
    for (int i = 0; i < count; i++)
        fprintf(thermo_in_, "%s{\"index\":%d,\"value\":%.3g}",
                i ? "," : "", thermo_context_index(cells[i]), accepted ? 1.0 : -1.0);
    fprintf(thermo_in_, "],\"final\":%s}\n", g_decided == W_ * H_ ? "true" : "false");
    if (fflush(thermo_in_) != 0 || ferror(thermo_in_)) return false;
    thermo_waiting_feedback_ = true;
    return true;
}

static bool thermo_apply_patch(const char *s) {
    const char *p = json_str(s, "patch");
    if (!p || *p != '[') return false;
    int cells[32], tiles[32], count = 0;
    p++;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        if (*p == ']') break;
        if (*p != '{' || count >= 32) return false;
        const char *end = strchr(p, '}');
        if (!end) return false;
        long cell = json_num(p, "i", -1), tile = json_num(p, "tile", -1);
        if (cell < 0 || cell >= W_ * H_ || tile < 0 || tile >= ntiles_) return false;
        for (int j = 0; j < count; j++) if (cells[j] == cell) return false;
        cells[count] = (int)cell;
        tiles[count] = (int)tile;
        count++;
        p = end + 1;
    }
    if (!count) return false;
    size_t cells_n = (size_t)W_ * H_;
    if (thermo_snap_cap_ < cells_n) {
        uint64_t *next = realloc(thermo_snap_, sizeof(uint64_t) * cells_n);
        if (!next) return false;
        thermo_snap_ = next;
        thermo_snap_cap_ = cells_n;
    }
    QualityMetrics before = quality_measure(false);
    memcpy(thermo_snap_, dom_, sizeof(uint64_t) * cells_n);
    bool ok = true;
    for (int i = 0; i < count; i++) {
        uint64_t mask = (uint64_t)1 << tiles[i];
        if (!(dom_[cells[i]] & mask)) { ok = false; break; }
        dom_[cells[i]] = mask;
        if (!propagate_from(cells[i])) { ok = false; break; }
    }
    int accepted = ok ? count : 0;
    int rejected = ok ? 0 : count;
    int contradictions = ok ? 0 : 1;
    if (!ok) memcpy(dom_, thermo_snap_, sizeof(uint64_t) * cells_n);
    thermo_round_++;
    thermo_proposals_ += count;
    thermo_accepts_ += accepted;
    thermo_rejects_ += rejected;
    thermo_contradictions_ += contradictions;
    g_decided = 0;
    for (size_t i = 0; i < cells_n; i++) if (pc64(dom_[i]) == 1) g_decided++;
    QualityMetrics after = quality_measure(false);
    quality_record(after);
    return thermo_send_feedback(cells, tiles, count, accepted, rejected,
                                contradictions, before, after);
}

static bool thermo_apply_cfg(const char *s) {
    const char *p = json_str(s, "cfg");
    if (!p || *p != '[') return false;
    size_t cells = (size_t)W_ * (size_t)H_;
    uint64_t *next = malloc(sizeof(uint64_t) * cells);
    if (!next) return false;
    p++;
    for (size_t i = 0; i < cells; i++) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (i > 0) {
            if (*p != ',') { free(next); return false; }
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        }
        if (*p < '0' || *p > '9') { free(next); return false; }
        errno = 0;
        char *end = NULL;
        unsigned long long tile = strtoull(p, &end, 10);
        if (errno == ERANGE || end == p || tile >= (unsigned long long)ntiles_) {
            free(next);
            return false;
        }
        uint64_t mask = (uint64_t)1 << tile;
        /* Only accept tiles allowed by this cell's original domain. */
        if (!(dom_[i] & mask)) { free(next); return false; }
        next[i] = mask;
        p = end;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ']') { free(next); return false; }

    /* The sidecar is allowed to propose, never to redefine hard WFC rules.
     * Re-check every directed neighbor pair before accepting its completion. */
    for (size_t i = 0; i < cells; i++) {
        int x = (int)(i % (size_t)W_), y = (int)(i / (size_t)W_);
        int source = __builtin_ctzll(next[i]);
        for (int d = 0; d < NDIR; d++) {
            int nx = x, ny = y;
            if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
            else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
            else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
            else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_ || (nx == x && ny == y)) continue;
            int target = __builtin_ctzll(next[IDX(nx, ny)]);
            if (!((cdir_[d][source] >> target) & 1ULL)) {
                free(next);
                return false;
            }
        }
    }
    memcpy(dom_, next, sizeof(uint64_t) * cells);
    free(next);
    return true;
}

/* Poll the long-lived thermo worker: 0 in progress, 1 solved, -1 failed. */
static char *thermo_lbuf = NULL;
static size_t thermo_lbl = 0, thermo_lcap = 0;
static int thermo_poll(void) {
    if (!thermo_inflight_) {
        set_note("thermo: launching THRML\xe2\x80\xa6");
        if (!thermo_launch()) return -1;
    }
    if (now_ms() - thermo_t0_ > 240000) goto thermo_fail;
    ssize_t got;
    char chunk[4096];
    while ((got = read(thermo_fd_, chunk, sizeof chunk)) > 0) {
        for (ssize_t k = 0; k < got; k++) {
            char c = chunk[k];
            if (c != '\n') {
                if (thermo_lbl + 1 >= thermo_lcap) {
                    if (thermo_lcap >= (1u << 25)) { /* 32MB: runaway child */ continue; }
                    thermo_lcap = thermo_lcap ? thermo_lcap * 2 : 65536;
                    char *nb = realloc(thermo_lbuf, thermo_lcap);
                    if (!nb) goto thermo_fail;
                    thermo_lbuf = nb;
                }
                thermo_lbuf[thermo_lbl++] = c;
                continue;
            }
            thermo_lbuf[thermo_lbl] = 0;
            if (thermo_lbl > 0) {
                const char *s = thermo_lbuf;
                /* json.dumps puts a space after the colon: match key "t" */
                const char *tp = json_str(s, "t");
                if (tp && tp[0] == '"') {
                    if (!strncmp(tp + 1, "ready", 5) && tp[6] == '"') {
                        thermo_ready_ = true;
                        thermo_pbits_ = (int)json_num(s, "pbits", thermo_pbits_);
                        thermo_observations_ = json_num(s, "observations", thermo_observations_);
                        const char *sp = json_str(s, "sampler");
                        if (sp && sp[0] == '"') {
                            const char *end = strchr(sp + 1, '"');
                            size_t len = end && (size_t)(end - sp - 1) < sizeof thermo_sampler_ - 1
                                       ? (size_t)(end - sp - 1) : 0;
                            if (len) {
                                memcpy(thermo_sampler_, sp + 1, len);
                                thermo_sampler_[len] = 0;
                            }
                        }
                        set_note("thermo online â adaptive proposals (%s)",
                                 thermo_sampler_);
                        if (thermo_reset_pending_) {
                            thermo_reset_pending_ = false;
                            if (!thermo_send_reset()) goto thermo_fail;
                        } else if (!thermo_send_sample()) goto thermo_fail;
                    } else if (!strncmp(tp + 1, "meta", 4) && tp[5] == '"') {
                        thermo_pbits_ = (int)json_num(s, "pbits", 0);
                    } else if (!strncmp(tp + 1, "stats", 5) && tp[6] == '"') {
                        const char *bp = json_str(s, "beta");
                        const char *cp = json_str(s, "confidence");
                        thermo_round_ = json_num(s, "round", thermo_round_);
                        thermo_beta_ = bp ? strtod(bp, NULL) : thermo_beta_;
                        thermo_confidence_ = cp ? strtod(cp, NULL) : thermo_confidence_;
                        thermo_bad_ = json_num(s, "bad", thermo_bad_);
                    } else if (!strncmp(tp + 1, "proposal", 8) && tp[9] == '"') {
                        if (!thermo_waiting_sample_ || !thermo_apply_patch(s)) goto thermo_fail;
                        thermo_waiting_sample_ = false;
                    } else if (!strncmp(tp + 1, "learn", 5) && tp[6] == '"') {
                        thermo_observations_ = json_num(s, "observations", thermo_observations_);
                        thermo_waiting_feedback_ = false;
                        if (thermo_reset_pending_) {
                            thermo_reset_pending_ = false;
                            if (!thermo_send_reset()) goto thermo_fail;
                        } else if (!thermo_send_sample()) goto thermo_fail;
                    } else if (!strncmp(tp + 1, "done", 4) && (tp[5] == '"' || tp[5] == ',')) {
                        thermo_valid_ = json_num(s, "valid", 0) > 0;
                        thermo_bad_ = json_num(s, "bad", -1);
                        if (thermo_valid_ && !thermo_apply_cfg(s)) {
                            thermo_valid_ = false;
                            goto thermo_fail;
                        }
                        if (thermo_valid_) {
                            thermo_kill();
                            g_decided = W_ * H_;
                            set_note("thermo %s \xe2\x80\x94 %d pbits of Z1-class spins",
                                     g_thermo_form, thermo_pbits_);
                            return 1;
                        }
                        goto thermo_fail;
                    } else if (!strncmp(tp + 1, "fatal", 5) && tp[6] == '"') {
                        goto thermo_fail;
                    }
                }
            }
            thermo_lbl = 0;
        }
    }
    if (got == 0) { /* child closed the pipe */
        if (thermo_valid_) { thermo_kill(); return 1; }
        goto thermo_fail;
    }
    return 0;
thermo_fail:
    thermo_kill();
    set_note("thermo failed \xe2\x80\x94 classic solver");
    return -1;
}

/* Persistent one-line HUD: keep the canvas legible while making the solver's
 * state visible between transient notes.  It is rendered below the art so it
 * never competes with the procedural image itself. */
static void render_status(int vh, int ch) {
    if (W_ <= 0 || H_ <= 0) return;
    int total = W_ * H_;
    int pct = total > 0 ? (int)(100.0 * g_decided / total) : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char core[160], line[256];
    const char *mode = MODES[g_mode_idx];
    if (g_thermo) {
        const char *engine = thermo_ready_ ? thermo_sampler_ :
                             thermo_inflight_ ? "boot" :
                             pct == 100 ? "done" : "idle";
        const char *learning = g_thermo_learn ? "learn" : "ephem";
        if (g_quality_live >= 0.0) {
            snprintf(core, sizeof core, "%s %3d%% | T/%s q%.2f o%ld/%ld P%d %s",
                     mode, pct, engine, g_quality_live, thermo_observations_,
                     thermo_round_, studio_pin_count_, learning);
        } else {
            snprintf(core, sizeof core, "%s %3d%% | T/%s P%d %s",
                     mode, pct, engine, studio_pin_count_, learning);
        }
    } else if (g_quality_live >= 0.0) {
        snprintf(core, sizeof core, "%s %3d%% | classic q%.2f P%d",
                 mode, pct, g_quality_live, studio_pin_count_);
    } else {
        snprintf(core, sizeof core, "%s %3d%% | classic P%d", mode, pct, studio_pin_count_);
    }
    if (now_ms() < g_note_until && g_note[0])
        snprintf(line, sizeof line, "%s | %s", core, g_note);
    else
        snprintf(line, sizeof line, "%s", core);

    char pos[32];
    snprintf(pos, sizeof pos, "\x1b[%d;1H", vh * ch + 1);
    fb_puts(pos);
    fb_bg((RGB){10, 12, 18});
    fb_fg(g_thermo ? (RGB){172, 224, 214} : (RGB){170, 196, 224});
    fb_puts(" ");
    fb_puts(line);
    fb_puts("\x1b[K");
    fb_bg((RGB){0, 0, 0});
}

static bool save_report(const char *path) {
    if (!path || !*path) return false;
    QualityMetrics q = quality_measure(true);
    quality_record(q);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\"schema\":2,\"mode\":\"%s\",\"seed\":%llu,"
               "\"dimensions\":{\"w\":%d,\"h\":%d},\"solver\":\"%s\","
               "\"quality\":",
            MODES[g_mode_idx], (unsigned long long)g_seed, W_, H_,
            g_thermo ? "thermo" : "classic");
    quality_json(f, q);
    fprintf(f, ",\"thermo\":{\"enabled\":%s,\"sampler\":",
            g_thermo ? "true" : "false");
    thermo_json_string(f, thermo_sampler_);
    fprintf(f, ",\"round\":%ld,\"observations\":%ld,\"proposals\":%ld,"
               "\"accepted\":%ld,\"rejected\":%ld,\"contradictions\":%ld,"
               "\"beta\":%.9g,\"confidence\":%.9g},"
               "\"studio\":{\"pins\":%d},\"macro\":{\"name\":",
            thermo_round_, thermo_observations_, thermo_proposals_,
            thermo_accepts_, thermo_rejects_, thermo_contradictions_,
            thermo_beta_, thermo_confidence_, studio_pin_count_);
    thermo_json_string(f, macro_name());
    fprintf(f, ",\"guided_cells\":%d},\"evolution\":{\"candidates\":%d,"
               "\"winner_seed\":%llu,\"scores\":[",
            macro_guided_cells(), g_evolution_n,
            (unsigned long long)g_evolution_winner_seed);
    for (int i = 0; i < g_evolution_n; i++)
        fprintf(f, "%s%.9g", i ? "," : "", quality_clamp(g_evolution_scores[i]));
    fputs("]}}\n", f);
    bool ok = ferror(f) == 0 && fclose(f) == 0;
    if (!ok) return false;
    return true;
}

static void render_observatory(void) {
    QualityMetrics q = quality_measure(false);
    QualityProfile profile = quality_profile();
    QualityHotspot hotspot = quality_hotspot();
    const double *live[] = {
        &g_quality_live, &g_quality_validity_live, &g_quality_boundary_live,
        &g_quality_coverage_live, &g_quality_diversity_live,
        &g_quality_smoothness_live, &g_quality_stability_live,
        &g_quality_topology_live,
    };
    const double current[] = {
        q.total, q.validity, q.boundary, q.coverage, q.diversity,
        q.smoothness, q.stability, q.topology,
    };
    static const char *labels[] = {
        "total", "validity", "boundary", "coverage", "diversity",
        "smoothness", "stability", "topology",
    };
    static const char *spark[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇"};
    char line[256];
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    fb_fg((RGB){150, 232, 255});
    fb_puts("QUALITY OBSERVATORY\n\n");
    fb_fg((RGB){210, 220, 232});
    snprintf(line, sizeof line, "mode %-10s  focus %-10s  seed %llu  pins %d\n",
             MODES[g_mode_idx], profile.focus, (unsigned long long)g_seed,
             studio_pin_count_);
    fb_puts(line);
    snprintf(line, sizeof line, "solver %-7s  sampler %-8s  learning %-5s  beta %.2f  confidence %.2f\n",
             g_thermo ? "thermo" : "classic", thermo_sampler_,
             g_thermo_learn ? "on" : "off", thermo_beta_, thermo_confidence_);
    fb_puts(line);
    snprintf(line, sizeof line, "round %ld  observations %ld  proposals %ld  accepted %ld  rejected %ld  contradictions %ld\n",
             thermo_round_, thermo_observations_, thermo_proposals_, thermo_accepts_,
             thermo_rejects_, thermo_contradictions_);
    fb_puts(line);
    snprintf(line, sizeof line, "macro %-16s guided %d  hotspot (%d,%d) score %.3f reason %s\n\n",
             macro_name(), macro_guided_cells(), hotspot.x, hotspot.y,
             hotspot.score, hotspot.reason);
    fb_puts(line);
    for (int i = 0; i < 8; i++) {
        double value = *live[i] >= 0.0 ? *live[i] : current[i];
        int filled = (int)round(20.0 * quality_clamp(value));
        if (filled > 20) filled = 20;
        snprintf(line, sizeof line, "%-10s %.3f  [", labels[i], value);
        fb_puts(line);
        fb_fg(i == 0 ? (RGB){255, 210, 110} : (RGB){126, 210, 180});
        for (int j = 0; j < 20; j++) fb_puts(j < filled ? "#" : ".");
        fb_fg((RGB){210, 220, 232});
        fb_puts("]\n");
    }
    fb_puts("\ntrend ");
    if (!g_quality_trace_len_) {
        fb_puts("waiting for live samples");
    } else {
        int start = (g_quality_trace_pos_ - g_quality_trace_len_ + QUALITY_TRACE_N) % QUALITY_TRACE_N;
        for (int i = 0; i < g_quality_trace_len_; i++) {
            double value = quality_clamp(g_quality_trace_[(start + i) % QUALITY_TRACE_N]);
            int level = (int)round(value * 7.0);
            if (level < 0) level = 0;
            if (level > 7) level = 7;
            fb_puts(spark[level]);
        }
    }
    fb_puts("\n\n");
    fb_fg((RGB){150, 180, 205});
    fb_puts("l return   Q heatmap   E evolution   P pin/unpin hover   right-click unpin   h help   q quit\n");
    fb_puts("report: use --report FILE.json for reproducible quality + thermo + studio data");
    fb_puts("\x1b[0m");
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

static void render_evolution_lab(void) {
    QualityHotspot hotspot = quality_hotspot();
    char line[256];
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    fb_fg((RGB){255, 210, 130});
    fb_puts("EVOLUTION LAB\n\n");
    fb_fg((RGB){220, 225, 235});
    snprintf(line, sizeof line, "mode %-10s  focus %-10s  base seed %llu\n",
             MODES[g_mode_idx], quality_profile().focus,
             (unsigned long long)g_evolution_winner_seed);
    fb_puts(line);
    snprintf(line, sizeof line, "winner quality %.3f  hotspot (%d,%d) %s %.3f\n\n",
             g_evolution_n > 0 ? g_evolution_scores[0] : g_quality_live,
             hotspot.x, hotspot.y, hotspot.reason, hotspot.score);
    fb_puts(line);
    fb_puts("rank  seed                 quality\n");
    for (int i = 0; i < g_evolution_n; i++) {
        snprintf(line, sizeof line, " %2d   %-20llu  %.4f%s\n", i + 1,
                 (unsigned long long)g_evolution_seeds[i],
                 quality_clamp(g_evolution_scores[i]),
                 i == 0 ? "  <- winner" : "");
        fb_puts(line);
    }
    fb_puts("\nE return   Q heatmap   l observatory   P pin/unpin   q quit");
    fb_puts("\x1b[0m");
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
}

/* key + mouse handling: returns request code 0 none, 1 new map, 2 quit */
static uint64_t *snap_;

#define UNDO_N 12
static uint64_t *undo_[UNDO_N];
static uint8_t *undo_pin_[UNDO_N];
static uint8_t *undo_tile_[UNDO_N];
static int undo_len_ = 0, undo_pos_ = 0;
void click_bufs_invalidate(void) {
    free(snap_); snap_ = NULL;
    for (int i = 0; i < UNDO_N; i++) {
        free(undo_[i]); undo_[i] = NULL;
        free(undo_pin_[i]); undo_pin_[i] = NULL;
        free(undo_tile_[i]); undo_tile_[i] = NULL;
    }
    undo_len_ = 0; undo_pos_ = 0;
    hist_clear();
}

/* ---------------- time traveler ----------------
 * a ring of domain snapshots so `,` / `.` can scrub the
 * collapse backwards and forwards while pausing the solve. */
#define HIST_N 384
static uint64_t *hist_bufs_[HIST_N];
static int hist_len_ = 0, hist_pos_ = -1;  /* -1 = live */
static void hist_clear(void) {
    for (int i = 0; i < HIST_N; i++) { free(hist_bufs_[i]); hist_bufs_[i] = NULL; }
    hist_len_ = 0; hist_pos_ = -1; hist_cnt_ = 0;
}
static void hist_push(void) {
    if (hist_pos_ >= 0) { /* scrubbing: discard the future and fork from there */
        hist_len_ = hist_pos_ + 1;
        hist_pos_ = -1;
    }
    if (hist_len_ == HIST_N) {
        uint64_t *drop = hist_bufs_[0];
        memmove(&hist_bufs_[0], &hist_bufs_[1], (size_t)(HIST_N - 1) * sizeof(uint64_t *));
        hist_bufs_[HIST_N - 1] = drop;
        memcpy(drop, dom_, sizeof(uint64_t) * (size_t)W_ * H_);
    } else {
        if (!hist_bufs_[hist_len_]) {
            hist_bufs_[hist_len_] = malloc(sizeof(uint64_t) * (size_t)W_ * H_);
            if (!hist_bufs_[hist_len_]) {
                set_note("history unavailable");
                return;
            }
        }
        memcpy(hist_bufs_[hist_len_], dom_, sizeof(uint64_t) * (size_t)W_ * H_);
        hist_len_++;
    }
}
static bool hist_back(void) {
    if (hist_pos_ == -1) {
        if (hist_len_ == 0) return false;
        hist_pos_ = hist_len_ - 1;
    } else if (hist_pos_ > 0) hist_pos_--;
    else return false;
    memcpy(dom_, hist_bufs_[hist_pos_], sizeof(uint64_t) * (size_t)W_ * H_);
    return true;
}
static bool hist_fwd(void) {
    if (hist_pos_ < 0 || hist_pos_ >= hist_len_ - 1) return false;
    hist_pos_++;
    memcpy(dom_, hist_bufs_[hist_pos_], sizeof(uint64_t) * (size_t)W_ * H_);
    return true;
}

/* ---------------- the crawler ----------------
 * once a dungeon solves, a hero spawns mid-floor. WASD walks cell
 * to cell over floor tiles; the torch field lights the world, and
 * standing next to a sconce lights it for the counter. */
static void hero_spawn(void) {
    if (strcmp(MODES[g_mode_idx], "dungeon") || g_nworlds > 1) { g_hero_on = false; return; }
    int best = -1, bd = 1 << 30;
    g_loot_tot = 0;
    for (int i = 0; i < W_ * H_; i++) {
        if (pc64(dom_[i]) != 1) continue;
        int t = __builtin_ctzll(dom_[i]);
        if (tiles_[t].flag) g_loot_tot++;
        else if (tiles_[t].e[0] == 2 || tiles_[t].e[1] == 2 ||
                 tiles_[t].e[2] == 2 || tiles_[t].e[3] == 2) {
            int dx = i % W_ - W_ / 2, dy = i / W_ - H_ / 2;
            int d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
    }
    if (best < 0) { g_hero_on = false; return; }
    g_hx = best % W_;
    g_hy = best / W_;
    g_loot = 0;
    g_hero_on = true;
    set_note("crawler: %d torches to light (WASD)", g_loot_tot);
}
static void hero_move(char k) {
    if (pc64(dom_[IDX(g_hx, g_hy)]) != 1) return;
    int d = k == 'w' ? 0 : k == 'd' ? 1 : k == 's' ? 2 : 3;
    int nx = g_hx + (d == 1) - (d == 3);
    int ny = g_hy + (d == 2) - (d == 0);
    if (!g_torus && (nx < 0 || ny < 0 || nx >= W_ || ny >= H_)) {
        set_note("edge of the world");
        return;
    }
    nx = (nx + W_) % W_;
    ny = (ny + H_) % H_;
    uint64_t dc = dom_[IDX(g_hx, g_hy)], dn = dom_[IDX(nx, ny)];
    if (pc64(dn) != 1) return;
    int tc = __builtin_ctzll(dc), tn = __builtin_ctzll(dn);
    if (tiles_[tc].e[d] != 2 || tiles_[tn].e[OPPOSITE(d)] != 2) {
        set_note("solid rock ahead");
        return;
    }
    g_hx = nx; g_hy = ny;
    static const int DX4[4] = {0, 1, 0, -1}, DY4[4] = {-1, 0, 1, 0};
    for (int d2 = 0; d2 < 4; d2++) {
        int tx = nx + DX4[d2], ty = ny + DY4[d2];
        if (g_torus) { tx = (tx + W_) % W_; ty = (ty + H_) % H_; }
        else if (tx < 0 || ty < 0 || tx >= W_ || ty >= H_) continue;
        int ti = IDX(tx, ty);
        if (river_[ti] || pc64(dom_[ti]) != 1) continue;
        if (tiles_[__builtin_ctzll(dom_[ti])].flag) {
            river_[ti] = 1;
            g_loot++;
            if (g_loot >= g_loot_tot && g_loot_tot > 0)
                set_note("all %d torches lit!", g_loot_tot);
            else
                set_note("torch lit \xe2\x80\x94 %d/%d", g_loot, g_loot_tot);
        }
    }
}

/* fast-forward the solve until the world is whole, with post-passes */
static void fast_solve(void) {
    for (int rt_try = 0; rt_try < 200; rt_try++) {
        bool all_done = true;
        for (int i = 0; i < W_ * H_; i++)
            if (pc64(dom_[i]) != 1) { all_done = false; break; }
        if (all_done) break;
        int r2 = wfc_step();
        if (r2 == 1) break;
        if (r2 == -1) grid_reset();
    }
    bool all_done = true;
    for (int i = 0; i < W_ * H_; i++)
        if (pc64(dom_[i]) != 1) { all_done = false; break; }
    if (all_done) {
        if (!strcmp(MODES[g_mode_idx], "circuit") || !strcmp(MODES[g_mode_idx], "pipes"))
            label_components();
        if (!strcmp(MODES[g_mode_idx], "terrain")) {
            carve_rivers();
            g_river_show = n_river_;
        }
    }
}
static bool undo_push(void) {
    if (!undo_[undo_pos_]) {
        undo_[undo_pos_] = malloc(sizeof(uint64_t) * (size_t)W_ * H_);
        if (!undo_[undo_pos_]) return false;
    }
    if (!undo_pin_[undo_pos_]) {
        undo_pin_[undo_pos_] = malloc((size_t)W_ * H_);
        if (!undo_pin_[undo_pos_]) return false;
    }
    if (!undo_tile_[undo_pos_]) {
        undo_tile_[undo_pos_] = malloc((size_t)W_ * H_);
        if (!undo_tile_[undo_pos_]) return false;
    }
    memcpy(undo_[undo_pos_], dom_, sizeof(uint64_t) * (size_t)W_ * H_);
    memcpy(undo_pin_[undo_pos_], studio_pin_, (size_t)W_ * H_);
    memcpy(undo_tile_[undo_pos_], studio_tile_, (size_t)W_ * H_);
    undo_pos_ = (undo_pos_ + 1) % UNDO_N;
    if (undo_len_ < UNDO_N) undo_len_++;
    return true;
}
static bool undo_pop(void) {
    if (undo_len_ == 0) return false;
    int previous = (undo_pos_ - 1 + UNDO_N) % UNDO_N;
    if (!undo_[previous]) return false;
    memcpy(dom_, undo_[previous], sizeof(uint64_t) * (size_t)W_ * H_);
    if (undo_pin_[previous] && undo_tile_[previous]) {
        memcpy(studio_pin_, undo_pin_[previous], (size_t)W_ * H_);
        memcpy(studio_tile_, undo_tile_[previous], (size_t)W_ * H_);
        studio_pin_count_ = 0;
        for (int i = 0; i < W_ * H_; i++) studio_pin_count_ += studio_pin_[i] != 0;
    }
    undo_pos_ = previous;
    undo_len_--;
    quality_record(quality_measure(false));
    return true;
}

static uint64_t studio_neighbor_mask(int cx, int cy) {
    uint64_t mask = grid_cell_mask(cx, cy);
    for (int d = 0; d < NDIR; d++) {
        int nx = cx, ny = cy;
        if (d == 0) ny = g_torus ? (cy + H_ - 1) % H_ : cy - 1;
        else if (d == 1) nx = g_torus ? (cx + 1) % W_ : cx + 1;
        else if (d == 2) ny = g_torus ? (cy + 1) % H_ : cy + 1;
        else nx = g_torus ? (cx + W_ - 1) % W_ : cx - 1;
        if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
        uint64_t neighbor = dom_[IDX(nx, ny)];
        if (pc64(neighbor) != 1) continue;
        int nt = __builtin_ctzll(neighbor);
        for (int tile = 0; tile < ntiles_; tile++)
            if (!((cdir_[d][tile] >> nt) & 1ULL)) mask &= ~(1ULL << tile);
    }
    return mask;
}

static bool studio_unpin_cell(int cx, int cy) {
    if (!studio_pin_ || cx < 0 || cy < 0 || cx >= W_ || cy >= H_ ||
        !studio_pin_[IDX(cx, cy)]) return false;
    uint64_t mask = studio_neighbor_mask(cx, cy);
    if (!mask || !undo_push()) return false;
    int cell = IDX(cx, cy);
    dom_[cell] = mask;
    studio_pin_[cell] = 0;
    studio_tile_[cell] = 0;
    if (studio_pin_count_ > 0) studio_pin_count_--;
    if (!propagate_from(cell)) {
        (void)undo_pop();
        return false;
    }
    quality_record(quality_measure(false));
    if (g_thermo) thermo_kill();
    return true;
}

static bool studio_pin_cell(int cx, int cy) {
    if (!studio_pin_ || cx < 0 || cy < 0 || cx >= W_ || cy >= H_) return false;
    int cell = IDX(cx, cy);
    if (studio_pin_[cell]) return studio_unpin_cell(cx, cy);
    uint64_t domain = dom_[cell];
    if (!domain || !undo_push()) return false;
    int tile = pc64(domain) == 1 ? __builtin_ctzll(domain) : weighted_pick_at(domain, cell);
    dom_[cell] = 1ULL << tile;
    if (!propagate_from(cell)) {
        (void)undo_pop();
        return false;
    }
    studio_pin_[cell] = 1;
    studio_tile_[cell] = (uint8_t)tile;
    studio_pin_count_++;
    quality_record(quality_measure(false));
    if (g_thermo) thermo_kill();
    return true;
}

static void studio_pin_hover(void) {
    if (g_hover_x < 0 || g_hover_y < 0) {
        set_note("hover a cell before pinning");
        return;
    }
    bool was_pinned = studio_pin_[IDX(g_hover_x, g_hover_y)] != 0;
    if (studio_pin_cell(g_hover_x, g_hover_y))
        set_note(was_pinned ? "unpinned (%d,%d)" : "pinned (%d,%d)", g_hover_x, g_hover_y);
    else
        set_note(was_pinned ? "unpin refused" : "pin refused there");
}
static int mouse_esc = 0;      /* 0 normal, 1 got ESC, 2 got '[' */
static char mouse_buf[32];
static int mouse_len = 0;

static void handle_click(int btn, int px, int py) {
    int cw = strcmp(MODES[g_mode_idx], "terrain") ? 4 : 2;
    int ch = strcmp(MODES[g_mode_idx], "terrain") ? 2 : 1;
    if (g_gfx) { cw = 16; ch = 16; } /* emit_frame_img renders 16 px/cell */
    int cx = (px - 1) / cw, cy = (py - 1) / ch;
    if (cx < 0 || cy < 0 || cx >= W_ || cy >= H_) return;
    int cell = IDX(cx, cy);
    static size_t snap_sz = 0;
    if (!snap_ || snap_sz != (size_t)W_ * H_) {
        free(snap_);
        snap_ = malloc(sizeof(uint64_t) * (size_t)W_ * H_);
        if (!snap_) return;
        snap_sz = (size_t)W_ * H_;
    }

    if (btn & 2) { /* right-click: carve cell back open */
        if (studio_pin_ && studio_pin_[cell]) {
            bool unpinned = studio_unpin_cell(cx, cy);
            set_note(unpinned ? "unpinned (%d,%d)" : "unpin refused", cx, cy);
            return;
        }
        if (pc64(dom_[cell]) == ntiles_ || pc64(dom_[cell]) == 0) return;
        memcpy(snap_, dom_, sizeof(uint64_t) * (size_t)W_ * H_);
        int cx2 = cell % W_, cy2 = cell / W_;
        uint64_t m = grid_cell_mask(cx2, cy2);
        for (int d = 0; d < NDIR; d++) {
            int nx = cx2, ny = cy2;
            if (d == 0) ny = g_torus ? (cy2 + H_ - 1) % H_ : cy2 - 1;
            else if (d == 1) nx = g_torus ? (cx2 + 1) % W_ : cx2 + 1;
            else if (d == 2) ny = g_torus ? (cy2 + 1) % H_ : cy2 + 1;
            else nx = g_torus ? (cx2 + W_ - 1) % W_ : cx2 - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
            uint64_t nd = dom_[IDX(nx, ny)];
            if (pc64(nd) != 1) continue;
            int tn = __builtin_ctzll(nd);
            for (int b = 0; b < ntiles_; b++)
                if (!((cdir_[d][b] >> tn) & 1)) m &= ~(1ULL << b);
        }
        if (!m) { set_note("carve refused"); return; }
        if (!undo_push()) { set_note("undo unavailable"); return; }
        dom_[cell] = m;
        set_note("carved (%d,%d)", cx2, cy2);
        return;
    }

    /* left-click: force collapse */
    if (pc64(dom_[cell]) <= 1) return;
    if (!undo_push()) { set_note("undo unavailable"); return; }
    dom_[cell] = 1ULL << weighted_pick_at(dom_[cell], cell);
    if (propagate_from(cell)) { set_note("seeded (%d,%d)", cx, cy); if (g_sound) { ensure_sfx(); play_sfx("/tmp/wfc_blip.wav"); } }
    else {
        undo_pop();
        set_note("collapse refused there");
    }
}

static uint64_t evolution_seed(uint64_t base, int index) {
    if (index == 0) return base;
    uint64_t x = base + 0x9E3779B97F4A7C15ULL * (uint64_t)(index + 1);
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static bool evolution_replay_pins(const uint8_t *pins, const uint8_t *pin_tiles) {
    size_t cells = (size_t)W_ * H_;
    memset(studio_pin_, 0, cells);
    memset(studio_tile_, 0, cells);
    studio_pin_count_ = 0;
    for (size_t i = 0; i < cells; i++) {
        if (!pins[i]) continue;
        int tile = pin_tiles[i];
        uint64_t mask = tile >= 0 && tile < ntiles_ ? 1ULL << tile : 0;
        if (!mask || !(dom_[i] & mask)) return false;
        dom_[i] = mask;
        if (!propagate_from((int)i)) return false;
        studio_pin_[i] = 1;
        studio_tile_[i] = (uint8_t)tile;
        studio_pin_count_++;
    }
    g_decided = 0;
    for (size_t i = 0; i < cells; i++) g_decided += pc64(dom_[i]) == 1;
    return true;
}

static bool evolution_run(int requested) {
    if (W_ <= 0 || H_ <= 0 || !dom_ || g_nworlds != 1 || g_inf || g_thermo)
        return false;
    if (requested < 2) requested = 2;
    if (requested > EVOLUTION_MAX) requested = EVOLUTION_MAX;
    size_t cells = (size_t)W_ * H_;
    uint64_t *base_dom = malloc(sizeof(uint64_t) * cells);
    uint64_t *best_dom = malloc(sizeof(uint64_t) * cells);
    uint8_t *base_pin = malloc(cells), *base_tile = malloc(cells);
    uint8_t *best_pin = malloc(cells), *best_tile = malloc(cells);
    if (!base_dom || !best_dom || !base_pin || !base_tile || !best_pin || !best_tile) {
        free(base_dom); free(best_dom); free(base_pin); free(base_tile);
        free(best_pin); free(best_tile);
        set_note("evolution memory unavailable");
        return false;
    }
    memcpy(base_dom, dom_, sizeof(uint64_t) * cells);
    memcpy(base_pin, studio_pin_, cells);
    memcpy(base_tile, studio_tile_, cells);
    g_evolution_n = requested;
    uint64_t base_seed = g_seed;
    bool found = false;
    QualityMetrics best_quality = {0};
    for (int candidate = 0; candidate < requested; candidate++) {
        uint64_t seed = evolution_seed(base_seed, candidate);
        g_evolution_seeds[candidate] = seed;
        g_evolution_scores[candidate] = -1.0;
        bool solved = false;
        for (int attempt = 0; attempt < 80 && !solved; attempt++) {
            g_seed = seed;
            rs_ = seed ^ 0xD1B54A32D192ED03ULL ^
                   (uint64_t)attempt * 0xA24BAED4963EE407ULL;
            grid_reset();
            if (!evolution_replay_pins(base_pin, base_tile)) continue;
            long limit = (long)cells * 12 + 96;
            for (long step = 0; step < limit; step++) {
                int result = wfc_step();
                if (result == 1) { solved = true; break; }
                if (result < 0) break;
            }
        }
        if (!solved) continue;
        QualityMetrics quality = quality_measure(true);
        g_evolution_scores[candidate] = quality.total;
        bool better = !found || quality.total > best_quality.total + 1e-12 ||
                      (fabs(quality.total - best_quality.total) <= 1e-12 &&
                       quality.validity > best_quality.validity + 1e-12) ||
                      (fabs(quality.total - best_quality.total) <= 1e-12 &&
                       fabs(quality.validity - best_quality.validity) <= 1e-12 &&
                       quality.boundary > best_quality.boundary);
        if (better) {
            found = true;
            best_quality = quality;
            memcpy(best_dom, dom_, sizeof(uint64_t) * cells);
            memcpy(best_pin, studio_pin_, cells);
            memcpy(best_tile, studio_tile_, cells);
        }
    }
    if (!found) {
        g_seed = base_seed;
        memcpy(dom_, base_dom, sizeof(uint64_t) * cells);
        memcpy(studio_pin_, base_pin, cells);
        memcpy(studio_tile_, base_tile, cells);
        studio_pin_count_ = 0;
        for (size_t i = 0; i < cells; i++) studio_pin_count_ += studio_pin_[i] != 0;
        macro_build();
        free(base_dom); free(best_dom); free(base_pin); free(base_tile);
        free(best_pin); free(best_tile);
        return false;
    }
    /* Put the best candidate first so the overlay and report can be read at a
     * glance, while retaining every score for comparison. */
    for (int i = 0; i < requested; i++) {
        int best = i;
        for (int j = i + 1; j < requested; j++)
            if (g_evolution_scores[j] > g_evolution_scores[best] + 1e-12)
                best = j;
        if (best != i) {
            uint64_t seed = g_evolution_seeds[i];
            double score = g_evolution_scores[i];
            g_evolution_seeds[i] = g_evolution_seeds[best];
            g_evolution_scores[i] = g_evolution_scores[best];
            g_evolution_seeds[best] = seed;
            g_evolution_scores[best] = score;
        }
    }
    g_evolution_winner_seed = g_evolution_seeds[0];
    g_seed = g_evolution_winner_seed;
    rs_ = g_seed ^ 0xD1B54A32D192ED03ULL;
    memcpy(dom_, best_dom, sizeof(uint64_t) * cells);
    memcpy(studio_pin_, best_pin, cells);
    memcpy(studio_tile_, best_tile, cells);
    studio_pin_count_ = 0;
    for (size_t i = 0; i < cells; i++) studio_pin_count_ += studio_pin_[i] != 0;
    g_decided = 0;
    for (size_t i = 0; i < cells; i++) g_decided += pc64(dom_[i]) == 1;
    macro_build();
    hist_clear();
    g_comp_ready = false;
    quality_record(best_quality);
    full_repaint_ = true;
    free(base_dom); free(best_dom); free(base_pin); free(base_tile);
    free(best_pin); free(best_tile);
    set_note("evolution winner %.3f from %d candidates", best_quality.total, requested);
    return true;
}

static int pump_keys(bool tty) {
    if (!tty) return 0;
    int req = 0;
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (mouse_esc == 1) {
            mouse_esc = c == '[' ? 2 : 0;
            continue;
        }
        if (mouse_esc == 2) {
            if (c == 'M' || c == 'm') {
                mouse_buf[mouse_len] = 0;
                int btn = 0, mx = 0, my = 0;
                if (sscanf(mouse_buf, "%d;%d;%d", &btn, &mx, &my) == 3 && c == 'M') {
                    if (btn == 35 || btn == 34 + 32 - 2) { /* plain hover (no button) */ }
                    if ((btn & 32) && (btn & 3) == 0) {
                        /* left-drag paint */
                        handle_click(0, mx, my);
                    } else if ((btn & 32) && (btn & 3) == 2) {
                        handle_click(2, mx, my);
                    } else if (btn & 32) { /* hover readout */
                        int cw2 = strcmp(MODES[g_mode_idx], "terrain") ? 4 : 2;
                        int ch2 = strcmp(MODES[g_mode_idx], "terrain") ? 2 : 1;
                        if (g_gfx) { cw2 = 8; ch2 = 1; }
                        int hx = (mx - 1) / cw2, hy = (my - 1) / ch2;
                        int qcol = g_quad ? (hx >= W_) : (g_twin && hx >= W_);
                        int qrow = g_quad ? (hy >= H_) : 0;
                        if (g_nworlds > 1) {
                            if (qcol) hx -= W_;
                            if (qrow) hy -= H_;
                        } else {
                            /* track the drift camera */
                            hx = (hx + g_vx + W_) % W_;
                            hy = (hy + g_vy + H_) % H_;
                        }
                        if (hx >= 0 && hy >= 0 && hx < W_ && hy < H_) {
                            g_hover_x = hx; g_hover_y = hy;
                            uint64_t hd = dom_[IDX(hx, hy)];
                            if (g_nworlds > 1) {
                                load_world(qrow * 2 + qcol);
                                hd = dom_[IDX(hx, hy)];
                                load_world(0);
                            }
                            g_hover_k = pc64(hd);
                        } else g_hover_k = -1;
                    } else if (btn & 3)
                        handle_click(btn & 3, mx, my);
                }
                mouse_esc = 0;
            } else if (mouse_len < (int)sizeof mouse_buf - 1) {
                mouse_buf[mouse_len++] = (char)c;
            } else mouse_esc = 0;
            continue;
        }
        if (c == 0x1b) { mouse_esc = 1; mouse_len = 0; continue; }
        if (g_help) {
            g_help = false;
            fputs("\x1b[2J", stdout);
            if (c == 'q' || c == 3) req = req == 1 ? 1 : 2;
            continue;
        }
        if (g_observe) {
            if (c == 'l') {
                g_observe = false;
                g_paused = g_observe_was_paused;
                full_repaint_ = true;
                fputs("\x1b[2J", stdout);
            } else if (c == 'h' || c == '?') {
                g_observe = false;
                g_paused = g_observe_was_paused;
                g_help = true;
            } else if (c == 'q' || c == 3) {
                g_observe = false;
                req = req == 1 ? 1 : 2;
            } else if (c == 'Q') {
                g_observe = false;
                g_paused = g_observe_was_paused;
                g_heatmap = !g_heatmap;
                full_repaint_ = true;
                set_note("quality heatmap %s", g_heatmap ? "ON" : "off");
            } else if (c == 'E') {
                g_observe = false;
                g_paused = true;
                if (g_thermo) set_note("evolution: turn thermo off first");
                else if (evolution_run(4)) g_evolve_view = true;
            }
            continue;
        }
        if (g_evolve_view) {
            if (c == 'E') {
                g_evolve_view = false;
                g_paused = g_evolve_was_paused;
                full_repaint_ = true;
            } else if (c == 'Q') {
                g_heatmap = !g_heatmap;
                set_note("quality heatmap %s", g_heatmap ? "ON" : "off");
            } else if (c == 'q' || c == 3) {
                g_evolve_view = false;
                req = req == 1 ? 1 : 2;
            }
            continue;
        }
        if ((c == 'h' || c == '?') ) { g_help = true; continue; }
        if (c == 'l') {
            g_observe_was_paused = g_paused;
            g_paused = true;
            g_observe = true;
            full_repaint_ = true;
            continue;
        }
        if (c == 'Q') {
            g_heatmap = !g_heatmap;
            full_repaint_ = true;
            set_note("quality heatmap %s", g_heatmap ? "ON" : "off");
        }
        if (c == 'E') {
            if (g_nworlds > 1 || g_inf || g_thermo) {
                set_note("evolution: classic single-world only");
            } else {
                g_evolve_was_paused = g_paused;
                g_paused = true;
                if (evolution_run(4)) g_evolve_view = true;
                else g_paused = g_evolve_was_paused;
            }
        }
        if (c == 'q' || c == 3) req = req == 1 ? 1 : 2;
        else if (c == ' ') { g_seed = rnd(); req = 1; }
        else if (c == 'p') g_paused = !g_paused;
        else if (c == 'g') {
            g_gif_on = !g_gif_on;
            set_note("gif recording %s", g_gif_on ? "ON" : "off");
        }
        else if (c == 'y') {
            g_theme++;
            if ((g_theme & 7) >= 4 && strcmp(MODES[g_mode_idx], "terrain")) g_theme = (g_theme & 7) >= 6 ? 0 : 4;
            BIOMES = BIOMES_SEASONAL[(g_theme & 7) >= 4 ? (g_theme & 7) - 4 : 0];
            set_note("theme %d", (g_theme & 7) + 1);
        }
        else if (c == 'a') {
            g_sound = !g_sound;
            set_note("audio %s", g_sound ? "ON" : "off");
            if (!g_sound) ambient_stop();
        }
        else if (c == 'z') {            if (g_nworlds > 1) { set_note("sheet: single-world only"); continue; }
            g_sheet_opened = !g_sheet_opened;
            if (g_sheet_opened) { sheet_scan(); render_sheet(); }
            else full_repaint_ = true;
        }
        else if (c == 'r') {
            g_rt = !g_rt;
            g_iso = false;
            if (g_rt) fast_solve();
            full_repaint_ = true;
        }
        else if (c == 'i') {
            g_iso = !g_iso;
            g_rt = false;
            if (g_iso) fast_solve();
            full_repaint_ = true;
        }
        else if (c == ',') {
            if (g_nworlds > 1) set_note("scrub: single-world only");
            else if (hist_back()) { g_paused = true; set_note("scrub \xe2\x80\x94 . rewinds forward"); }
            else set_note("no history to scrub yet");
        }
        else if (c == '.') {
            if (g_nworlds > 1) set_note("scrub: single-world only");
            else if (hist_fwd()) set_note("back to the future");
            else if (g_paused) { g_paused = false; hist_pos_ = -1; set_note("live collapse"); }
        }
        else if (c == 'w' || c == 'a' || c == 's' || c == 'd') {
            if (g_hero_on && !strcmp(MODES[g_mode_idx], "dungeon")) hero_move(c);
        }
        else if (c == 'k') { g_crt = !g_crt; set_note("CRT %s", g_crt ? "on" : "off"); }
        else if (c == 'W') {
            FILE *f = fopen("/tmp/wfc_world.bin", "wb");
            if (!f) { set_note("save failed"); }
            else {
                uint32_t hdr[5] = {(uint32_t)g_mode_idx, (uint32_t)W_, (uint32_t)H_,
                                   (uint32_t)(g_seed & 0xffffffff), (uint32_t)(g_bias * 1000)};
                bool ok = fwrite(hdr, 4, 5, f) == 5 &&
                          fwrite(dom_, sizeof(uint64_t), (size_t)W_ * H_, f) == (size_t)W_ * H_;
                uint32_t pin_count = (uint32_t)studio_pin_count_;
                if (ok) ok = fwrite(&pin_count, sizeof pin_count, 1, f) == 1;
                for (int i2 = 0; ok && i2 < W_ * H_; i2++) {
                    if (!studio_pin_[i2]) continue;
                    uint32_t record[2] = {(uint32_t)i2, studio_tile_[i2]};
                    ok = fwrite(record, sizeof record, 1, f) == 1;
                }
                if (fclose(f) != 0) ok = false;
                set_note(ok ? "world saved (/tmp/wfc_world.bin)" : "save failed (disk?)");
            }
        }
        else if (c == 'L') {
            FILE *f = fopen("/tmp/wfc_world.bin", "rb");
            if (!f) { set_note("no saved world"); }
            else {
                uint32_t hdr[5];
                if (fread(hdr, 4, 5, f) == 5 && hdr[0] < NMODES &&
                    hdr[1] == (uint32_t)W_ && hdr[2] == (uint32_t)H_) {
                    setup_mode((int)hdr[0]);
                    g_bias = hdr[4] / 1000.0;
                    apply_bias();
                    if (fread(dom_, sizeof(uint64_t), (size_t)W_ * H_, f) != (size_t)W_ * H_) {
                        set_note("truncated save");
                    } else {
                        memset(studio_pin_, 0, (size_t)W_ * H_);
                        memset(studio_tile_, 0, (size_t)W_ * H_);
                        studio_pin_count_ = 0;
                        uint32_t pin_count = 0;
                        bool pin_ok = true;
                        size_t pin_header = fread(&pin_count, sizeof pin_count, 1, f);
                        if (pin_header == 1) {
                            uint32_t records = pin_count;
                            if (records > (uint32_t)(W_ * H_)) {
                                pin_ok = false;
                                records = 0;
                            }
                            for (uint32_t p2 = 0; p2 < records; p2++) {
                                uint32_t record[2];
                                if (fread(record, sizeof record, 1, f) != 1) { pin_ok = false; break; }
                                if (record[0] >= (uint32_t)(W_ * H_) || record[1] >= (uint32_t)ntiles_ ||
                                    pc64(dom_[record[0]]) != 1 ||
                                    (uint32_t)__builtin_ctzll(dom_[record[0]]) != record[1] ||
                                    studio_pin_[record[0]]) {
                                    pin_ok = false;
                                    continue;
                                }
                                studio_pin_[record[0]] = 1;
                                studio_tile_[record[0]] = (uint8_t)record[1];
                                studio_pin_count_++;
                            }
                        } else if (ferror(f)) pin_ok = false;
                        if (!pin_ok) {
                            memset(studio_pin_, 0, (size_t)W_ * H_);
                            memset(studio_tile_, 0, (size_t)W_ * H_);
                            studio_pin_count_ = 0;
                        }
                        hist_clear(); /* scrub history predates this load */
                        g_comp_ready = false;
                        n_river_ = 0; g_river_show = 0;
                        memset(river_, 0, (size_t)W_ * H_);
                        g_decided = 0;
                        for (int i2 = 0; i2 < W_ * H_; i2++) {
                            river_rank_[i2] = -1;
                            if (pc64(dom_[i2]) == 1) g_decided++;
                        }
                        quality_record(quality_measure(false));
                        set_note(pin_ok ? "world loaded (%d pins)" : "world loaded; pin metadata ignored",
                                 studio_pin_count_);
                    }
                } else set_note("incompatible save");
                fclose(f);
            }
        }
        else if (c == 'u') {
            if (undo_pop()) set_note("undid sculpt");
            else set_note("nothing to undo");
        }
        else if (c == 'o') {
            char code[96], cmd[160];
            snprintf(code, sizeof code, "wfc://%s/%llu", MODES[g_mode_idx],
                     (unsigned long long)g_seed);
#ifdef __APPLE__
            snprintf(cmd, sizeof cmd, "echo -n '%s' | pbcopy", code);
            system(cmd);
#endif
            set_note("%s (copied)", code);
        }
        else if (c == 'v') {
            const char *mode2 = MODES[g_mode_idx];
            int art2 = strcmp(mode2, "terrain") ? 8 : 16;
            int f2 = strcmp(mode2, "terrain") ? 6 : 4;
            int pw2, ph2;
            uint8_t *rgb2 = raster_rgb(art2, f2, &pw2, &ph2);
            if (!rgb2) { set_note("clipboard shot too large"); continue; }
            Buf img2 = png_bytes(rgb2, pw2, ph2);
            free(rgb2);
            FILE *fp2 = fopen("/tmp/wfc_shot.png", "wb");
            if (fp2) { fwrite(img2.b, 1, img2.n, fp2); fclose(fp2); }
            buf_free(&img2);
#ifdef __APPLE__
            system("osascript -e 'set the clipboard to (read (POSIX file \"/tmp/wfc_shot.png\") as class PNGF)' >/dev/null 2>&1");
            set_note("map copied to clipboard");
#else
            set_note("saved /tmp/wfc_shot.png");
#endif
        }
        else if (c == 'f') { g_pan = !g_pan; set_note("drift %s", g_pan ? "on" : "off"); }
        else if (c == 'T') {
            if (g_nworlds > 1 || g_inf) set_note("thermo: single-world only");
            else {
                g_thermo = !g_thermo;
                thermo_kill();
                full_repaint_ = true;
                set_note("thermo solver %s", g_thermo ? "ON" : "off");
            }
        }
        else if (c == 'R') {
            if (thermo_inflight_ && thermo_ready_) {
                thermo_reset_pending_ = true;
                set_note("thermo learning reset on next round");
            } else {
                g_thermo_reset_learning = true;
                set_note("thermo learning will reset on next run");
            }
        }
        else if (c == 'P') studio_pin_hover();
        else if (c == 'I') { /* slow-mo: quarter pace while solving */            g_slowmo = !g_slowmo;
            full_repaint_ = true;
        }
        else if (c == 'e') {
            g_entropy_view = !g_entropy_view;
            full_repaint_ = true;
        }
        else if (c == '[' || c == ']') {
            g_bias += (c == ']' ? 0.08 : -0.08);
            if (g_bias < 0.04) g_bias = 0.04;
            if (g_bias > 0.96) g_bias = 0.96;
            apply_bias();
            set_note("density %d%%", (int)(g_bias * 100));
        }
        else if (c == 'c') {
            g_cycle = !g_cycle;
            set_note("auto-cycle %s", g_cycle ? "ON" : "off");
        }
        else if (c == 'n') {
            g_zen = !g_zen;
            if (!g_zen) g_ghost_t0 = 0;
            set_note("zen %s \xe2\x80\x94 worlds %s", g_zen ? "on" : "off",
                     g_zen ? "morph" : "restart");
        }
        else if (c == '+' || c == '=') { if (g_speed < 200000) g_speed *= 2; speed_changed(); set_note("speed %ld steps/s", g_speed); }
        else if (c == '-' || c == '_') { if (g_speed > 1) g_speed /= 2; speed_changed(); set_note("speed %ld steps/s", g_speed); }
        else if (c == 'm') { setup_mode(g_mode_idx + 1); g_seed = rnd(); g_paused = false; apply_size(); if (g_sound) play_stinger(g_mode_idx); req = 1; }
        else if (c == 's') {
            char path[512];
            if (!g_save_path[0])
                snprintf(path, sizeof path, "wfc-%s-%llu.png", MODES[g_mode_idx],
                         (unsigned long long)(g_seed % 100000000ULL));
            else snprintf(path, sizeof path, "%s", g_save_path);
            if (save_image(path)) set_note("saved %s (%dx%d)", path, W_, H_);
            else set_note("save failed: %s", path);
        }
    }
    return req;
}

static void msleep(double ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)((ms / 1000 - (double)(long)(ms / 1000)) * 1e9)};
    nanosleep(&ts, NULL);
}
/* sub-ms per-step delays batch up into whole sleeps; nanosleep's real
 * granularity is ~1ms, so sleeping 1us per step caps --speed at ~800/s */
static double g_msdebt = 0;
static void pace(double dms) {
    g_msdebt += dms;
    if (g_msdebt >= 1) {
        long z = (long)g_msdebt;
        msleep((double)z);
        g_msdebt -= z;
    }
}

/* ---------------- main ---------------- */

static bool parse_long_range(const char *text, long min, long max, long *out) {
    if (!text || !*text) return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < min || value > max)
        return false;
    if (out) *out = value;
    return true;
}

static bool parse_double_range(const char *text, double min, double max, double *out) {
    if (!text || !*text) return false;
    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(value) ||
        value < min || value > max)
        return false;
    if (out) *out = value;
    return true;
}

static bool parse_u64_decimal(const char *text, uint64_t *out) {
    if (!text || !*text || *text == '-' || *text == '+') return false;
    for (const char *p = text; *p; p++)
        if (*p < '0' || *p > '9') return false;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value > UINT64_MAX)
        return false;
    if (out) *out = (uint64_t)value;
    return true;
}

static uint64_t hash_seed_word(const char *text) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

static bool parse_seed_value(const char *text, uint64_t *out) {
    if (!text || !*text) return false;
    bool digits = true;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') { digits = false; break; }
    }
    if (digits) return parse_u64_decimal(text, out);
    if (out) *out = hash_seed_word(text);
    return true;
}

static int find_mode(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < NMODES; i++)
        if (!strcmp(name, MODES[i])) return i;
    return -1;
}

static void cfg_expand(char *out, size_t cap) {
    const char *h = getenv("HOME");
    snprintf(out, cap, "%s/.wfcrc", h ? h : "/tmp");
}
static void cfg_load(void) {
    char p[512]; cfg_expand(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(k, "mode")) {
            int mode = find_mode(v);
            if (mode >= 0) setup_mode(mode);
        } else if (!strcmp(k, "theme")) {
            long value;
            if (parse_long_range(v, 0, INT_MAX, &value)) g_theme = (int)value;
        } else if (!strcmp(k, "speed")) {
            long value;
            if (parse_long_range(v, 1, MAX_SPEED, &value)) g_speed = value;
        } else if (!strcmp(k, "bias")) {
            double value;
            if (parse_double_range(v, 40, 960, &value)) g_bias = value / 1000.0;
        } else if (!strcmp(k, "sound")) {
            long value;
            if (parse_long_range(v, 0, 1, &value)) g_sound = value != 0;
        } else if (!strcmp(k, "crt")) {
            long value;
            if (parse_long_range(v, 0, 1, &value)) g_crt = value != 0;
        } else if (!strcmp(k, "zen")) {
            long value;
            if (parse_long_range(v, 0, 1, &value)) g_zen = value != 0;
        }
    }
    fclose(f);
}
static void cfg_save(void) {
    char p[512]; cfg_expand(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "mode=%s\ntheme=%d\nspeed=%ld\nbias=%d\nsound=%d\ncrt=%d\nzen=%d\n",
            MODES[g_mode_idx], g_theme & 7, g_speed,
            (int)(g_bias * 1000), (int)g_sound, (int)g_crt, (int)g_zen);
    fclose(f);
}

int main(int argc, char **argv) {
    {
        const char *a0 = argc > 0 && argv[0] ? argv[0] : "wfc";
        snprintf(g_argv0, sizeof g_argv0, "%s", a0);
    }
    cfg_load();
#define NEXTV() (++i < argc ? argv[i] : (fprintf(stderr, "missing value for %s\n", argv[i - 1]), exit(2), (char *)NULL))
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode")) {
            const char *m = NEXTV();
            int found = -1;
            for (int k = 0; k < NMODES; k++) if (!strcmp(m, MODES[k])) found = k;
            if (found < 0) { fprintf(stderr, "mode must be circuit|terrain|truchet|fire|waves|dungeon|maze|galaxy|city|aurora|matrix|pipes|mondrian|koi|lava|sakura|geode|lantern|dunes|reef|stained|streets|neurons|mycelium|delta\n"); return 2; }
            setup_mode(found);
        }
        else if (!strcmp(argv[i], "--w")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 1, INT_MAX, &value)) {
                fprintf(stderr, "invalid value for --w: %s (expected a positive integer)\n", v);
                return 2;
            }
            g_user_w = (int)value;
        }
        else if (!strcmp(argv[i], "--h")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 1, INT_MAX, &value)) {
                fprintf(stderr, "invalid value for --h: %s (expected a positive integer)\n", v);
                return 2;
            }
            g_user_h = (int)value;
        }
        else if (!strcmp(argv[i], "--seed")) {
            const char *sv = NEXTV();
            if (!parse_seed_value(sv, &g_seed)) {
                fprintf(stderr, "invalid value for --seed: %s\n", sv);
                return 2;
            }
            g_seed_set = true;
        }
        else if (!strcmp(argv[i], "--speed")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 1, MAX_SPEED, &value)) {
                fprintf(stderr, "invalid value for --speed: %s (expected 1-%ld)\n", v, MAX_SPEED);
                return 2;
            }
            g_speed = value;
        }
        else if (!strcmp(argv[i], "--solver")) {
            const char *sv = NEXTV();
            if (!strcmp(sv, "thermo") || !strcmp(sv, "thermo=potts")) {
                g_thermo = true; snprintf(g_thermo_form, sizeof g_thermo_form, "potts");
            }
            else if (!strcmp(sv, "thermo=ising")) {
                g_thermo = true; snprintf(g_thermo_form, sizeof g_thermo_form, "ising");
            }
            else if (!strcmp(sv, "classic")) g_thermo = false;
            else { fprintf(stderr, "solver must be classic|thermo(=potts|=ising)\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--no-learn")) g_thermo_learn = false;
        else if (!strcmp(argv[i], "--thermo-profile")) {
            const char *profile = NEXTV();
            if (!*profile || strlen(profile) >= sizeof g_thermo_profile) {
                fprintf(stderr, "invalid value for --thermo-profile\n");
                return 2;
            }
            snprintf(g_thermo_profile, sizeof g_thermo_profile, "%s", profile);
        }
        else if (!strcmp(argv[i], "--reset-learning")) g_thermo_reset_learning = true;
        else if (!strcmp(argv[i], "--once")) g_once = true;
        else if (!strcmp(argv[i], "--cycle")) g_cycle = true;
        else if (!strcmp(argv[i], "--zen")) g_zen = true;
        else if (!strcmp(argv[i], "--sound")) g_sound = true;
        else if (!strcmp(argv[i], "--gallery")) { snprintf(g_gallery_path, sizeof g_gallery_path, "%s", NEXTV()); }
        else if (!strcmp(argv[i], "--gfx")) g_force_gfx = true;
        else if (!strcmp(argv[i], "--no-gfx")) g_no_gfx = true;
        else if (!strcmp(argv[i], "--link")) {
            char lk[128];
            const char *link = NEXTV();
            if (strlen(link) >= sizeof lk) {
                fprintf(stderr, "invalid --link value: too long\n");
                return 2;
            }
            snprintf(lk, sizeof lk, "%s", link);
            char *s2 = strstr(lk, "://");
            char *modepart = s2 ? s2 + 3 : lk;
            char *slash = strchr(modepart, '/');
            uint64_t seed;
            if (!slash || slash == modepart || !slash[1] || strchr(slash + 1, '/') ) {
                fprintf(stderr, "invalid --link value: %s\n", link);
                return 2;
            }
            *slash = '\0';
            int mode = find_mode(modepart);
            if (mode < 0 || !parse_u64_decimal(slash + 1, &seed)) {
                fprintf(stderr, "invalid --link value: %s\n", link);
                return 2;
            }
            setup_mode(mode);
            g_seed = seed;
            g_seed_set = true;
        }
        else if (!strcmp(argv[i], "--pan")) g_pan = true;
        else if (!strcmp(argv[i], "--theme")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 0, 7, &value)) {
                fprintf(stderr, "invalid value for --theme: %s (expected 0-7)\n", v);
                return 2;
            }
            g_theme = (int)value;
        }
        else if (!strcmp(argv[i], "--list-modes")) {
            for (int k = 0; k < NMODES; k++) printf("%s\n", MODES[k]);
            return 0;
        }
        else if (!strcmp(argv[i], "--twin")) { g_twin = true; g_nworlds = 2; }
        else if (!strcmp(argv[i], "--quad")) { g_quad = true; g_twin = false; g_nworlds = 4; }
        else if (!strcmp(argv[i], "--bench")) g_bench = true;
        else if (!strcmp(argv[i], "--evolve")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 2, EVOLUTION_MAX, &value)) {
                fprintf(stderr, "invalid value for --evolve: %s (expected 2-%d)\n",
                        v, EVOLUTION_MAX);
                return 2;
            }
            g_evolve_count = (int)value;
        }
        else if (!strcmp(argv[i], "--infinite")) { g_inf = true; g_once = false; }
        else if (!strcmp(argv[i], "--no-bloom")) g_no_bloom = true;
        else if (!strcmp(argv[i], "--no-weather")) g_no_weather = true;
        else if (!strcmp(argv[i], "--collage")) snprintf(g_collage_path, sizeof g_collage_path, "%s", NEXTV());
        else if (!strcmp(argv[i], "--daycycle")) g_daycycle = true;
        else if (!strcmp(argv[i], "--zoom")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 1, 6, &value)) {
                fprintf(stderr, "invalid value for --zoom: %s (expected 1-6)\n", v);
                return 2;
            }
            g_zoom = (int)value;
        }
        else if (!strcmp(argv[i], "--save")) { snprintf(g_save_path, sizeof g_save_path, "%s", NEXTV()); g_save_auto = true; }
        else if (!strcmp(argv[i], "--report")) {
            const char *report = NEXTV();
            if (!*report || strlen(report) >= sizeof g_report_path) {
                fprintf(stderr, "invalid value for --report\n");
                return 2;
            }
            snprintf(g_report_path, sizeof g_report_path, "%s", report);
        }
        else if (!strcmp(argv[i], "--gif")) { snprintf(g_gif_path, sizeof g_gif_path, "%s", NEXTV()); g_gif_on = 1; }
        else if (!strcmp(argv[i], "--version")) { printf("wfc 5.3 \u2014 quality evolution studio\n"); return 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("wave function collapse, animated in your terminal\n\n"
                   "  --mode circuit|terrain|truchet|fire|waves|dungeon|maze|galaxy|city|aurora|matrix|pipes|mondrian|koi|lava|sakura|geode|lantern|dunes|reef|stained|streets|neurons|mycelium|delta  tileset\n"
                   "  --w N --h N                      grid cells (default: auto-fit window)\n"
                   "  --seed N|word                    rng seed (words are hashed)\n"
                   "  --speed N                        collapse steps/sec (default 1600)\n"
                   "  --save FILE.png|.bmp             auto-save map on completion\n"
                   "  --report FILE.json               quality + thermo + studio report\n"
                   "  --solver classic|thermo         collapse engine (thermo = adaptive sidecar)\n"
                   "  --no-learn                       disable persistent thermo preferences\n"
                   "  --thermo-profile DIR             store thermo profiles in DIR\n"
                   "  --reset-learning                clear the active thermo profile\n"
                   "                   THRML is optional; bounded Python proposals remain available\n"
                   "  --gif FILE.gif                   record collapse animation\n"
                   "  --cycle                          auto-cycle modes forever (screensaver)\n"
                   "  --zen                            worlds dissolve into each other, never restarts\n"
                   "  --theme N                        color theme 0-7 (same as y)\n"
                   "  --list-modes                     print all mode names and exit\n"
                   "  --sound                          synth sound effects (afplay)\n"
                   "  --gallery FILE.html              render all modes to a web showcase\n"
                   "  --gfx / --no-gfx                 inline-image rendering (iTerm2/WezTerm/kitty)\n"
                   "  --link wfc://mode/seed           replay a shared world\n"
                   "  --pan                            drift the camera after solving\n"
                   "  --bench                          performance table across modes\n"
                   "  --evolve N                       rank 2-8 deterministic seed variants\n"
                   "  --twin / --quad                  two or four worlds at once\n"
                   "  --infinite                       ever-growing world (toroidal modes)\n"
                   "  --zoom N                         export/pixel scale 1-6\n"
                   "  --no-bloom                       disable glow post-processing\n"
                   "  --no-weather                     disable rain/snow/fireflies\n"
                   "  --collage FILE.png               mosaic of all modes\n"
                   "  --daycycle                       terrain dawn/noon/dusk cycle\n"
                   "  --once                           exit after first map\n\n"
                   "keys: space new | m mode | y theme | c cycle | a audio | h help\n"
                   "      i iso | , . scrub collapse | wasd hero in dungeon\n"
                   "      l observatory | Q heatmap | E evolution | P pin/unpin hover\n"
                   "      +/- speed p pause g gif s save q quit\n"
                   "build: cc -O2 -std=c11 -o wfc wfc.c -lz\n");
            return 0;
        }
        else { fprintf(stderr, "unknown arg %s (try --help)\n", argv[i]); return 2; }
    }

    if (g_nworlds > 1 && g_inf) {
        fprintf(stderr, "--infinite cannot be combined with --twin or --quad\n");
        return 2;
    }
    if (g_thermo && (g_nworlds > 1 || g_inf)) {
        fprintf(stderr, "--solver thermo cannot be combined with --twin, --quad, or --infinite\n");
        return 2;
    }
    if (g_evolve_count && (g_thermo || g_nworlds > 1 || g_inf ||
                           g_gallery_path[0] || g_collage_path[0] || g_bench)) {
        fprintf(stderr, "--evolve requires a classic single-world solve (use E live)\n");
        return 2;
    }

    if (g_gallery_path[0]) return run_gallery(g_gallery_path) ? 0 : 1;
    if (g_collage_path[0]) {
        int pw = 24, ph = 14;
        int pxs = pw * 8, pys = ph * 8;
        int collage_cols = 6, collage_rows = (NMODES + collage_cols - 1) / collage_cols;
        int W3 = pxs * collage_cols, H3 = pys * collage_rows;
        uint8_t *big = calloc((size_t)W3 * H3, 3);
        if (!big) { perror("malloc"); return 1; }
        for (int mi = 0; mi < NMODES; mi++) {
            bool is_terrain = !strcmp(MODES[mi], "terrain");
            int art = is_terrain ? 16 : 8, f = 8 / (art / 8);
            (void)gallery_solve(mi, 42, pw, ph);
            int px = (mi % collage_cols) * pxs, py = (mi / collage_cols) * pys;
            if (px + pxs > W3 || py + pys > H3) continue;
            for (int y = 0; y < pys; y++)
                for (int x = 0; x < pxs; x++) {
                    RGB c = img_px(x / 8, y / 8, (x / f) % art, (y / f) % art, art);
                    size_t o = ((size_t)(py + y) * W3 + px + x) * 3;
                    big[o] = c.r; big[o + 1] = c.g; big[o + 2] = c.b;
                }
        }
        apply_bloom(big, W3, H3);
        Buf img = png_bytes(big, W3, H3);
        free(big);
        FILE *fp = fopen(g_collage_path, "wb");
        bool ok = false;
        if (fp) {
            bool wrote = fwrite(img.b, 1, img.n, fp) == img.n;
            int close_rc = fclose(fp);
            ok = wrote && close_rc == 0;
        }
        buf_free(&img);
        if (!ok) {
            fprintf(stderr, "collage: failed to write %s: %s\n",
                    g_collage_path, strerror(errno));
            return 1;
        }
        printf("collage: %s\n", g_collage_path);
        return 0;
    }
    if (g_bench) {
        printf("%-9s %8s %10s %8s %7s\n", "mode", "solves", "avg-steps", "avg-ms", "fails");
        for (int mi = 0; mi < NMODES; mi++) {
            long tot_steps = 0; double tot_ms = 0; int fails = 0;
            int N = 30;
            for (int s = 1; s <= N; s++) {
                struct timespec a, b2;
                clock_gettime(CLOCK_MONOTONIC, &a);
                gallery_solve(mi, (uint64_t)s * 7919, 48, 30);
                clock_gettime(CLOCK_MONOTONIC, &b2);
                tot_ms += (b2.tv_sec - a.tv_sec) * 1000.0 + (b2.tv_nsec - a.tv_nsec) / 1e6;
                long st = 0;
                for (int i2 = 0; i2 < W_ * H_; i2++) st += pc64(dom_[i2]) == 1;
                if (st != W_ * H_) fails++;
                tot_steps += W_ * H_;
            }
            printf("%-9s %8d %10ld %8.1f %7d\n", MODES[mi], N,
                   tot_steps / N, tot_ms / N, fails);
        }
        return 0;
    }

    bool tty = isatty(STDOUT_FILENO);
    g_is_tty = tty;
    if (!g_seed_set) g_seed = (uint64_t)time(NULL) * 2654435761u + (uint64_t)getpid();
    rs_ = g_seed ^ 0xD1B54A32D192ED03ULL;
    if (ntiles_ == 0) setup_mode(0);
    speed_changed();

    if (!tty && (g_nworlds > 1 || g_inf)) {
        fprintf(stderr, "--twin, --quad, and --infinite require an interactive terminal\n");
        return 2;
    }

    if (!tty) {
        if (g_user_w > 64) g_user_w = 64;
        if (g_user_h > 40) g_user_h = 40;
    }
    W_ = H_ = 0;
    apply_size();

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGWINCH, on_winch);
    signal(SIGBUS, on_fatal);
    signal(SIGSEGV, on_fatal);
    signal(SIGABRT, on_fatal);
    signal(SIGILL, on_fatal);

    /* headless solve */
    if (!tty) {
        long total = 0; int tries = 0, done = 0;
        if (g_evolve_count) {
            if (!evolution_run(g_evolve_count)) {
                fprintf(stderr, "evolution: no valid candidate\n");
                return 1;
            }
            tries = 1;
            total = W_ * H_;
            done = 1;
            printf("evolution candidates=%d winner_seed=%llu quality=%.3f focus=%s scores=",
                   g_evolution_n, (unsigned long long)g_evolution_winner_seed,
                   quality_clamp(g_evolution_scores[0]), quality_profile().focus);
            for (int i = 0; i < g_evolution_n; i++)
                printf("%s%.3f", i ? "," : "", quality_clamp(g_evolution_scores[i]));
            putchar('\n');
        } else for (; tries < 10000 && !done; tries++) {
            grid_reset(); frames_clear(); long s = 0;
            long cap_every = W_ * H_ > 120 ? W_ * H_ / 120 : 1;
            if (g_thermo) {
                /* thermal attempt: poll the THRML child until done/fail;
                 * on fail fall back to classic steps for this attempt */
                while (!done) {
                    int rs = thermo_poll();
                    if (rs == 1) { done = 1; break; }
                    if (rs == -1) { g_thermo = false; break; }
                    usleep(30000);
                }
                continue; /* thermo run still counts as one try */
            }
            for (;;) {
                int r = wfc_step(); s++;
                if (g_gif_on && s % cap_every == 0) capture_frame();
                if (r == -1 && getenv("WFC_DEBUG"))
                    fprintf(stderr, "try %d: contradiction after %ld steps (%.1f%%)\n",
                            tries, s, 100.0 * s / (W_ * H_));
                if (r != 0) { if (r == 1) done = 1; break; }
            }
            total += s;
        }
        if (!done) { fprintf(stderr, "no solution after 10000 tries\n"); return 1; }
        if (getenv("WFC_DEBUG")) {
            fprintf(stderr, "ntiles=%d dom[0]=%llx pc=%d full=%llx\n",
                    ntiles_, (unsigned long long)dom_[0], pc64(dom_[0]),
                    (unsigned long long)(((uint64_t)1 << ntiles_) - 1));
        }
        char gp[512];
        if (g_nworlds == 1) {
            if (!strcmp(MODES[g_mode_idx], "circuit") || !strcmp(MODES[g_mode_idx], "pipes")) label_components();
            if (!strcmp(MODES[g_mode_idx], "terrain")) { carve_rivers(); g_river_show = n_river_; }
        }
        if (g_gif_on) {
            capture_frame();
            if (g_gif_path[0]) snprintf(gp, sizeof gp, "%s", g_gif_path);
            else snprintf(gp, sizeof gp, "wfc-%s-%llu.gif", MODES[g_mode_idx],
                          (unsigned long long)(g_seed % 100000000ULL));
            if (!write_gif(gp)) {
                frames_clear();
                fprintf(stderr, "failed to save %s\n", gp);
                return 1;
            }
            frames_clear();
        }
        if (getenv("WFC_DEBUG")) {
            int eh[8] = {0}, em[5] = {0};
            for (int i2 = 0; i2 < W_ * H_; i2++) {
                int t2 = __builtin_ctzll(dom_[i2]);
                eh[tiles_[t2].e[0] >> 4]++; em[tiles_[t2].e[0] & 15]++;
            }
            fprintf(stderr, "elev:");
            for (int k = 0; k < 8; k++) fprintf(stderr, " %d:%.0f%%", k, 100.0 * eh[k] / (W_ * H_));
            fprintf(stderr, "\nmoist:");
            for (int k = 0; k < 5; k++) fprintf(stderr, " %d:%.0f%%", k, 100.0 * em[k] / (W_ * H_));
            fprintf(stderr, "\n");
        }
        QualityMetrics qm = quality_measure(true);
        quality_record(qm);
        if (getenv("WFC_DEBUG")) {
            double qr = quality_reward((QualityMetrics){0}, qm, W_ * H_, 0);
            fprintf(stderr, "quality=%.3f validity=%.3f boundary=%.3f coverage=%.3f diversity=%.3f "
                            "smoothness=%.3f stability=%.3f topology=%.3f focus=%s reward=%+.3f\n",
                    qm.total, qm.validity, qm.boundary, qm.coverage, qm.diversity,
                    qm.smoothness, qm.stability, qm.topology, quality_profile().focus, qr);
        }
        if (g_save_path[0] && !save_image(g_save_path)) {
            fprintf(stderr, "failed to save %s\n", g_save_path);
            return 1;
        }
        if (g_report_path[0] && !save_report(g_report_path)) {
            fprintf(stderr, "failed to write report %s: %s\n", g_report_path, strerror(errno));
            return 1;
        }
        printf("OK mode=%s %dx%d seed=%llu tries=%d steps=%ld%s%s%s%s%s%s\n",
               MODES[g_mode_idx], W_, H_, (unsigned long long)g_seed, tries, total,
               g_save_path[0] ? " saved=" : "", g_save_path[0] ? g_save_path : "",
               g_gif_on ? " gif=" : "", g_gif_on ? gp : "",
               g_report_path[0] ? " report=" : "", g_report_path[0] ? g_report_path : "");
        return 0;
    }

    /* interactive */
    atexit(raw_off);
    raw_on();
    g_gfx = gfx_supported() ? gfx_kind() : 0;
    if (g_force_gfx && !g_gfx) {
        fputs("\x1b[0m\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[?25h\x1b[?1049l", stdout);
        fprintf(stderr, "warning: this terminal has no pixel protocol; using braille\n");
        g_force_gfx = false;
    }
    if (!g_gfx) fb_reserve(1 << 20);
    fputs("\x1b[?1049h\x1b[?25l\x1b[?1000;1003;1006h", stdout); /* + any-motion tracking */
    if (g_gfx) fputs("\x1b[2J", stdout);
    int attempts = 0;
    bool inf_cont = false;
    if (getenv("WFC_DEBUG")) { FILE *df=fopen("/tmp/wfc_dbg.log","a"); if(df){fprintf(df,"[start inf=%d once=%d]\n",(int)g_inf,(int)g_once); fclose(df);} }
    long steps = 0;
    long cap_every = 1, gfx_every = 1;
inf_continue:
    while (!g_stop) {
        if (!inf_cont) {
            attempts++;
            g_msdebt = 0;
            grid_reset();
            g_inf_ax = W_ / 2;
            g_inf_ay = H_ / 2;
            if (g_twin) {
                load_world(1);
                rs_ = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
                grid_reset();
                load_world(0);
            }
            if (!(g_zen && g_gif_on)) frames_clear(); /* zen recording keeps rolling */
            cap_every = W_ * H_ > 120 ? W_ * H_ / 120 : 1;
            gfx_every = g_speed > 12 ? g_speed / 12 : 1;
            steps = 0;
            draw_any(steps, attempts, 1.0);
        } else {
            cap_every = W_ * H_ > 120 ? W_ * H_ / 120 : 1;
            gfx_every = g_speed > 12 ? g_speed / 12 : 1;
            draw_any(steps, attempts, 1.0);
        }
        inf_cont = false;

        int r = 0;
        for (;;) {
            int req = pump_keys(true);
            if (g_winch) { g_winch = 0; apply_size(); req = 1; }
            if (req == 2) g_stop = 1;
            if (g_stop) break;
            if (req == 1) break;
            if (g_help) { if (!g_gfx) render_help(); msleep(50); continue; }
            if (g_observe) { if (!g_gfx) render_observatory(); msleep(80); continue; }
            if (g_evolve_view) { if (!g_gfx) render_evolution_lab(); msleep(80); continue; }
            if (g_sheet_opened && !g_gfx) { render_sheet(); msleep(500); continue; }
            if (g_paused) { if (!g_gfx) render_frame(steps, attempts, 1.0); msleep(40); continue; }
            if (g_nworlds > 1) {
                static bool done_[4];
                static long last_attempt = -1;
                if (last_attempt != attempts) { for (int w2 = 0; w2 < 4; w2++) done_[w2] = false; last_attempt = attempts; }
                for (int w = 0; w < g_nworlds; w++) {
                    if (done_[w]) continue;
                    load_world(w);
                    int rw = wfc_step();
                    if (rw == -1) grid_soft_reset();
                    else if (rw == 1) done_[w] = true;
                }
                load_world(0);
                steps++;
                if (g_gif_on && steps % cap_every == 0) capture_frame();
                long ev = g_gfx ? gfx_every : g_render_every;
                bool all = true;
                for (int w = 0; w < g_nworlds; w++) all &= done_[w];
                if (steps % ev == 0 || all) {
                    if (g_gfx) emit_frame_img();
                    else if (g_quad) render_quad_frame(steps, 1.0);
                    else render_twin_frame(steps, steps, 1.0);
                }
                if (g_delay_ms > 0) pace(g_delay_ms);
                if (all) { r = 1; break; }
                continue;
            }
            if (g_thermo && g_nworlds == 1) {
                int rs = thermo_poll();
                steps++;
                if (rs == 1) { r = 1; }
                else if (rs == -1) { g_thermo = false; continue; }
                else {
                    r = 0;
                    if (steps % g_render_every == 0 || steps % 7 == 0) {
                        set_note("thermo: annealing \xe2\x80\x94 %d launched, %d pbits so far",
                                 thermo_launches_, thermo_pbits_);
                        draw_any(steps, attempts, 1.0);
                    }
                    msleep(40);
                    continue;
                }
            } else {
            r = wfc_step(); steps++;
            if (g_nworlds == 1 && r == 0 && ++hist_cnt_ >= hist_stride_) {
                hist_cnt_ = 0;
                hist_push();
            }
            }
            if (g_gif_on && steps % cap_every == 0 &&
                !(g_zen && g_nframes >= 480)) capture_frame();
            if (r == -1) {
                if (g_inf) {
                    static int soft_tries = 0;
                    if (++soft_tries > 20) { grid_reset(); soft_tries = 0; }
                    else grid_soft_reset();
                    set_note("frontier recalibrating");
                    if (!g_gfx) render_frame(steps, attempts, 1.0);
                    msleep(140);
                    continue;
                }
                set_note("contradiction \xe2\x80\x94 retrying");
                if (!g_gfx) render_frame(steps, attempts, 1.0);
                msleep(160);
                break;
            }
            double pctn = (double)g_decided / (W_ * H_);
            bool finale = pctn > 0.86;
            long every = finale ? 1 : (g_gfx ? gfx_every : g_render_every);
            double dms = g_delay_ms * (finale ? 4.5 : (g_slowmo ? 4.0 : 1.0));
            if (steps % every == 0 || r == 1) draw_any(steps, attempts, 1.0);
            pace(dms);
            if (r == 1) break;
        }
        if (getenv("WFC_DEBUG")) { FILE *df=fopen("/tmp/wfc_dbg.log","a"); if(df){fprintf(df,"[inner exit r=%d steps=%ld stop=%d once=%d]\n",r,steps,(int)g_stop,(int)g_once); fclose(df);} }
        if (g_stop || g_once) break;
        if (r == 1) { /* solved */
            if (getenv("WFC_DEBUG")) { FILE *df=fopen("/tmp/wfc_dbg.log","a"); if(df){fprintf(df,"[solved inf=%d size %dx%d]\n",(int)g_inf,W_,H_); fclose(df);} }
            if (g_sound) { ensure_sfx(); play_sfx("/tmp/wfc_done.wav"); }
            int is_terrain = !strcmp(MODES[g_mode_idx], "terrain");
            load_world(0);
            quality_record(quality_measure(true));
            if ((!strcmp(MODES[g_mode_idx], "circuit") || !strcmp(MODES[g_mode_idx], "pipes")) && !g_stop && g_nworlds == 1) label_components();
            if (is_terrain && !g_stop && g_nworlds == 1) {
                if (g_inf) rivers_clear();
                carve_rivers();
                int step_amt = n_river_ > 40 ? n_river_ / 40 : 1;
                for (int shown = 0; shown <= n_river_ && !g_stop; shown += step_amt) {
                    g_river_show = shown;
                    draw_any(steps, attempts, 1.0);
                    msleep(26);
                }
                g_river_show = n_river_;
            }
            if (!strcmp(MODES[g_mode_idx], "dungeon") && !g_stop) hero_spawn();
            if (g_gif_on) {
                if (g_zen && !g_inf) {
                    /* zen: keep rolling across morphs; one big loop written on quit */
                    if (g_nframes < 480) capture_frame();
                } else {
                    if (g_inf) frames_clear();
                    capture_frame();
                    char gp[512];
                    if (g_gif_path[0]) snprintf(gp, sizeof gp, "%s", g_gif_path);
                    else snprintf(gp, sizeof gp, "wfc-%s-%llu.gif", MODES[g_mode_idx],
                                  (unsigned long long)(g_seed % 100000000ULL));
                    bool gif_ok = write_gif(gp);
                    frames_clear();
                    if (gif_ok) set_note("saved %s", gp);
                    else set_note("gif failed: %s", gp);
                    msleep(600);
                }
            }
            if (g_save_auto && g_save_path[0]) {
                if (save_image(g_save_path)) set_note("saved %s", g_save_path);
                else set_note("save failed: %s", g_save_path);
            }
            if (g_report_path[0]) {
                if (save_report(g_report_path)) set_note("report saved %s", g_report_path);
                else set_note("report failed: %s", g_report_path);
            }
            double t0 = now_ms();
            bool anim = !strcmp(MODES[g_mode_idx], "fire") || !strcmp(MODES[g_mode_idx], "waves")
                        || !strcmp(MODES[g_mode_idx], "galaxy") || !strcmp(MODES[g_mode_idx], "city") || !strcmp(MODES[g_mode_idx], "aurora");
            double linger = anim ? 4500 : 1800;
            while (!g_stop && now_ms() - t0 < linger) {
                pump_keys(true);
                if (g_paused) { msleep(30); continue; }
                if (g_pan) { g_vx = (g_vx + 1) % W_; }
                if (g_pan) { g_vx = (g_vx + 1) % W_; }
                if (g_gfx) {
                    static int skip = 0;
                    if (++skip % 3 == 0) emit_frame_img();
                } else if (g_quad)
                    render_quad_frame(steps, 0.68 + 0.32 * sin((now_ms() - t0) * 0.006));
                else if (g_twin)
                    render_twin_frame(steps, steps, 0.68 + 0.32 * sin((now_ms() - t0) * 0.006));
                else
                    render_frame(steps, attempts, 0.68 + 0.32 * sin((now_ms() - t0) * 0.006));
                msleep(33);
            }
            if (g_zen && !g_inf && g_nworlds == 1 && !g_stop) {
                zen_capture(); /* solved world lingers; the next one dissolves in */
                if (!g_cycle) g_seed = rnd();
            }
            if (g_inf && !g_stop) {
                { FILE *df=fopen("/tmp/wfc_dbg.log","a"); if(df){fprintf(df,"[reaching grow branch]\n"); fclose(df);} }
                if (world_grow()) {
                    set_note("world grew to %dx%d", W_, H_);
                    msleep(500);
                    /* continue same attempt: skip reset via marker */
                    inf_cont = true;
                    goto inf_continue;
                }
            }
            if (g_cycle && !g_stop) {
                setup_mode(g_mode_idx + 1);
                g_seed = rnd();
                full_repaint_ = true;
                g_paused = false;
                apply_size();
                set_note("%s", MODES[g_mode_idx]);
                play_stinger(g_mode_idx);
            }
        }
        if (!g_gfx && !g_stop && !g_once && !(g_zen && g_nworlds == 1)) { /* fade between worlds */
            for (double p2 = 1; p2 > 0 && !g_stop; p2 -= 0.16) {
                if (g_quad) render_quad_frame(steps, p2 * p2);
                else if (g_twin) render_twin_frame(steps, steps, p2 * p2);
                else render_frame(steps, attempts, p2 * p2);
                msleep(22);
            }
            fputs("\x1b[2J", stdout);
        }
    }
    fputs("\x1b[0m\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[?25h\x1b[?1049l", stdout);
    putchar('\n');
    ambient_stop();
    if (g_zen && g_gif_on && g_nframes > 1) { /* flush the accumulated morph loop */
        char gp[512];
        if (g_gif_path[0]) snprintf(gp, sizeof gp, "%s", g_gif_path);
        else snprintf(gp, sizeof gp, "wfc-zen-%llu.gif",
                      (unsigned long long)(g_seed % 100000000ULL));
        int gif_frames = g_nframes;
        if (write_gif(gp)) printf("saved %s (%d frames)\n", gp, gif_frames);
        else fprintf(stderr, "failed to save %s\n", gp);
        frames_clear();
    }
    cfg_save();
    free_world_buffers();
    free(fb_);
    free(prev_sig_);
    free(ghost_);
    free(g_frames);
    free(river_); free(river_rank_); free(comp_); free(comp_col_); free(macro_role_);
    free(thermo_lbuf);
    free(gif_kids);
    free(snap_);
    for (int i = 0; i < UNDO_N; i++) {
        free(undo_[i]);
        free(undo_pin_[i]);
        free(undo_tile_[i]);
    }
    free(studio_pin_);
    free(studio_tile_);
    hist_clear();
    return 0;
}
