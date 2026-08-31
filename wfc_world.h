/* wfc_world.h -- part of wfc, included by wfc.c.
 *
 * mode registry, rng, tiles, the solver, rivers,
 * learned tile preferences, and the deterministic quality metrics
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `cc -O2 -std=c11 -o wfc wfc.c -lz` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_WORLD_H
#define WFC_WORLD_H
/* ---------------- mode registry ----------------
 * One row per world. Everything that used to be a strcmp chain — which
 * builder runs, which compatibility rule applies, whether the world wraps,
 * whether it renders smoothed, the vertical band ramp the grid seeds along,
 * which family the density bias tilts, and the idle-animation clock — lives
 * here. Adding a world is a row plus its render branch. */
static void build_circuit(void);
static void build_terrain(void);
static void build_truchet(void);
static void build_fire(void);
static void build_waves(void);
static void build_dungeon(void);
static void build_maze(void);
static void build_galaxy(void);
static void build_city(void);
static void build_aurora(void);
static void build_matrix(void);
static void build_pipes(void);
static void build_mondrian(void);
static void build_koi(void);
static void build_lava(void);
static void build_sakura(void);
static void build_geode(void);
static void build_lantern(void);
static void build_dunes(void);
static void build_reef(void);
static void build_stained(void);
static void build_streets(void);
static void build_neurons(void);
static void build_mycelium(void);
static void build_delta(void);
static void build_storm(void);
static void build_glacier(void);
static void build_bamboo(void);
static void build_solar(void);
static void build_rail(void);
static void build_canyon(void);
static void build_vinyl(void);
static void build_loom(void);

enum {              /* which family apply_bias() tilts */
    MG_FIELD = 0,       /* band worlds: tilt along band order */
    MG_CONNECTOR,       /* connector lattice: tilt empty vs drawn */
    MG_CARVE,           /* dungeon/maze: tilt solid vs carved */
};

typedef struct {
    const char *name;
    const char *blurb;
    void (*build)(void);
    bool smooth_compat;   /* build_compat(): |band diff| <= 1, not equality */
    bool torus;           /* the world wraps at its edges */
    bool smooth_render;
    bool band_ramp;       /* seed domains along a vertical band gradient */
    bool ramp_flip;       /* ...running bottom-to-top instead */
    bool network;         /* macro-guided connector world the thermo solver learns */
    bool coarse;          /* seed bands from 2D value noise, not just the ramp */
    unsigned char group;
    unsigned tick_ms;     /* idle-animation clock bucket; 0 = static */
    signed char tone;     /* semitones from A2: the world's key, drone and stinger */
} ModeSpec;

/*             name        blurb                                             build            sc     tor    sr     ramp   flip   net    crse   group         tick  key */
static const ModeSpec MODESPEC[] = {
    {"circuit",  "rainbow circuit boards, signals racing traces",   build_circuit,  false, true,  false, false, false, false, false, MG_CONNECTOR, 140,   0},
    {"terrain",  "hillshaded biomes + carved rivers + day cycle",   build_terrain,  true,  true,  true,  false, false, false, false, MG_FIELD,     260,  -2},
    {"truchet",  "woven arcs with a travelling light pulse",        build_truchet,  false, false, false, false, false, false, false, MG_CONNECTOR, 500,   7},
    {"fire",     "living flames with rising embers",                build_fire,     true,  false, true,  true,  false, false, false, MG_FIELD,      90,  -3},
    {"waves",    "rolling ocean, crest lines, moonpath",            build_waves,    true,  true,  true,  false, false, false, true,  MG_FIELD,     160,  -5},
    {"dungeon",  "torch-lit catacombs with breathing warmth",       build_dungeon,  false, true,  false, false, false, false, false, MG_CARVE,     220, -10},
    {"maze",     "labyrinth with wall pulses",                      build_maze,     false, true,  false, false, false, false, false, MG_CARVE,     360,  -7},
    {"galaxy",   "nebulae with shooting stars",                     build_galaxy,   true,  true,  true,  false, false, false, true,  MG_FIELD,     100,   5},
    {"city",     "night skylines with beacons + rain",              build_city,     true,  false, true,  true,  false, false, false, MG_FIELD,     400,  -6},
    {"aurora",   "drifting green curtains over stars",              build_aurora,   true,  false, true,  true,  true, false, false,  MG_FIELD,     150,   9},
    {"matrix",   "digital rain with white-hot glyph heads",         build_matrix,   true,  true,  true,  false, false, false, true,  MG_FIELD,     140,  -8},
    {"pipes",    "water pressure networks, pulses racing runs",     build_pipes,    false, true,  false, false, false, false, false, MG_CONNECTOR, 180,   2},
    {"mondrian", "painted plazas split by charcoal rules",          build_mondrian, true,  true,  true,  false, false, false, true,  MG_FIELD,     400,   4},
    {"koi",      "pond bands, koi gliding between lily pads",       build_koi,      true,  true,  true,  false, false, false, true,  MG_FIELD,     250,  -1},
    {"lava",     "crusting basalt over a molten breath",            build_lava,     true,  false, true,  true,  true, false, false,  MG_FIELD,     130, -12},
    {"sakura",   "spring night, blossom petals drifting down",      build_sakura,   true,  false, true,  true,  false, false, false, MG_FIELD,     120,   3},
    {"geode",    "crystal cavern facets with wandering glints",     build_geode,    true,  true,  true,  false, false, false, true,  MG_FIELD,     160,   8},
    {"lantern",  "festival sky, lanterns rising past the stars",    build_lantern,  true,  false, true,  true,  true, false, false,  MG_FIELD,     220,  11},
    {"dunes",    "heat shimmer under a fixed blazing sun",          build_dunes,    true,  false, true,  true,  false, false, false, MG_FIELD,     200,  -4},
    {"reef",     "caustic water, coral, bubbles, a fish school",    build_reef,     true,  false, true,  true,  false, false, false, MG_FIELD,     160,   1},
    {"stained",  "jewel-glass panes, lead lines, roaming light",    build_stained,  true,  true,  true,  false, false, false, true,  MG_FIELD,     300,   6},
    {"streets",  "arterial grid, lanes, signals, intersections",    build_streets,  false, false, false, false, false, true, false,  MG_CONNECTOR, 170,  -8},
    {"neurons",  "branching dendrites with travelling potentials",  build_neurons,  false, true,  false, false, false, true, false,  MG_CONNECTOR,  95,  14},
    {"mycelium", "living root networks, knots, drifting spores",    build_mycelium, false, true,  false, false, false, true, false,  MG_CONNECTOR, 260,  -9},
    {"delta",    "tidal estuary channels, confluences, sandbars",   build_delta,    false, false, false, false, false, true, false,  MG_CONNECTOR, 145,  -1},
    {"storm",    "thunderhead anvils, rain veils, forked lightning", build_storm,    true,  false, true,  true,  false, false, true,  MG_FIELD,     110, -11},
    {"glacier",  "blue ice shelves split by crevasses and glints",   build_glacier,  true,  false, true,  true,  true,  false, true,  MG_FIELD,     280,  10},
    {"bamboo",   "swaying stalks and nodes under a lit canopy",      build_bamboo,   true,  false, true,  true,  false, false, false, MG_FIELD,     130,  12},
    {"solar",    "granulated photosphere, sunspots, arcing flares",  build_solar,    true,  true,  true,  false, false, false, true,  MG_FIELD,     100,  15},
    {"rail",     "marshalling yard, running trains, switch lamps",   build_rail,     false, false, false, false, false, true, false,  MG_CONNECTOR, 155,  -6},
    {"canyon",   "banded strata, a river cut, dust in the light",    build_canyon,   true,  false, true,  true,  false, false, false, MG_FIELD,     240,   7},
    {"vinyl",    "concentric grooves under a sweeping highlight",    build_vinyl,    true,  true,  true,  false, false, false, false, MG_FIELD,      90,   2},
    {"loom",     "warp and weft crossing over and under on the web", build_loom,     false, false, false, false, false, false, false, MG_CONNECTOR, 320,   5},
};
#define NMODES ((int)(sizeof MODESPEC / sizeof *MODESPEC))
static int g_mode_idx = 0;
static const ModeSpec *mode_spec(void);
static const char *mode_name(void);
static int g_user_w = 999, g_user_h = 999;
static const ModeSpec *mode_spec(void) { return &MODESPEC[g_mode_idx]; }
static const char *mode_name(void) { return MODESPEC[g_mode_idx].name; }
static bool g_fullscreen = false;
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
static bool g_colorblind = false;
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
    unsigned tick = MODESPEC[g_mode_idx].tick_ms;
    return tick ? (uint32_t)(now_ms() / tick) : 0;
}
static int g_bulk_idx = -1;
static bool g_cycle = false;
static bool g_help = false;
static bool g_picker = false;          /* the world picker overlay */
static bool g_picker_was_paused = false;
static int g_picker_sel = 0;
static char g_picker_query[24] = "";
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
/* loom: cloth on the web — long warp and weft runs that mostly cross rather
 * than turn, so the lattice reads as weave instead of circuitry. */
static void build_loom(void) {
    static const double weights[14] = {3, 40, 40, 1.6, 1.6, 1.6, 1.6, 4, 4, 0.4, 0.4, 0.4, 0.4, 11};
    build_connector_world(weights);
}
/* rail: a marshalling yard — long parallel runs, sparing points work, and
 * buffer stops where a siding ends. Crossings are the rarest thing here. */
static void build_rail(void) {
    static const double weights[14] = {17, 34, 11, 5.0, 5.0, 5.0, 5.0, 3.6, 3.6, 1.4, 1.4, 2.0, 2.0, 0.4};
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
/* Bumped whenever tile weights change, so the entropy cache invalidates
 * itself rather than relying on every writer to remember it. */
static uint32_t weights_gen_ = 1;

static void apply_bias(void) {
    weights_gen_++;
    for (int i = 0; i < ntiles_; i++) {
        if (tiles_[i].wbase <= 0) {
            tiles_[i].wbase = tiles_[i].weight > 0 ? tiles_[i].weight : 1.0;
            if (tiles_[i].weight <= 0) tiles_[i].weight = tiles_[i].wbase;
        }
        if (tiles_[i].weight < 1e-9) tiles_[i].weight = 1e-9;
        tiles_[i].lw = log2(tiles_[i].weight);
    }
    if (mode_spec()->group == MG_CONNECTOR) {
        g_bulk_idx = 0;
        for (int i = 0; i < ntiles_; i++)
            tiles_[i].weight = tiles_[i].wbase *
                (i == 0 ? pow(0.22, (g_bias - 0.5) * 4) : pow(2.2, (g_bias - 0.5) * 2));
        return;
    }
    if (mode_spec()->group == MG_CARVE) {
        /* tilt rock/solid vs carved: scale pure-bulk tile and wall/passage weights */
        for (int i = 0; i < ntiles_; i++) {
            bool bulk = true;
            for (int d = 0; d < NDIR; d++)
                if (tiles_[i].e[d] != 0) { bulk = false; break; }
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
/* Eleven worlds are the same eight-band field — a scalar height/intensity
 * carried on every edge, smoothed to |diff| <= 1 by build_compat(true). Only
 * the band weights and the render treatment differ. */
static void build_band_world(const double weights[8]) {
    for (int i = 0; i < 8; i++) {
        Tile *t = &tiles_[ntiles_++];
        memset(t, 0, sizeof *t);
        t->e[0] = t->e[1] = t->e[2] = t->e[3] = (uint8_t)(i << 4);
        t->weight = t->wbase = weights[i];
    }
}
/* waves: wave-height bands 0..7 on a torus, animated crests */
static void build_waves(void) {
    static const double w[8] = {6, 9, 12, 10, 7, 4, 2, 1};
    build_band_world(w);
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
    static const double w[8] = {10, 9, 8, 7, 6, 4, 3, 2};
    build_band_world(w);
}
/* sakura: spring-night sky bands 0..7, petals drift down in render */
static void build_sakura(void) {
    static const double w[8] = {3, 5, 7, 9, 11, 12, 10, 8};
    build_band_world(w);
}
/* geode: mineral richness bands 0..7 on a torus; facets glint in render */
static void build_geode(void) {
    static const double w[8] = {2, 3, 4, 5, 6, 8, 10, 12};
    build_band_world(w);
}
/* lantern festival: night-glow bands, brightest near the horizon */
static void build_lantern(void) {
    static const double w[8] = {14, 11, 8, 6, 5, 4, 3, 2};
    build_band_world(w);
}
/* dunes: sky bands up top, ridge shadow below */
static void build_dunes(void) {
    static const double w[8] = {10, 9, 8, 7, 6, 5, 4, 3};
    build_band_world(w);
}
/* reef: water depth bands, sunlit surface over the deep */
static void build_reef(void) {
    static const double w[8] = {4, 6, 8, 10, 11, 10, 8, 6};
    build_band_world(w);
}
/* stained: jewel panes; lead gathers where panes meet */
static void build_stained(void) {
    static const double w[8] = {9, 9, 9, 9, 9, 9, 9, 6};
    build_band_world(w);
}
/* storm: cloud-density bands; the anvil stacks up top, rain veils below */
static void build_storm(void) {
    static const double w[8] = {5, 7, 9, 11, 12, 11, 8, 5};
    build_band_world(w);
}
/* glacier: ice-thickness bands; thin meltwater blue up to packed white */
static void build_glacier(void) {
    static const double w[8] = {8, 8, 9, 10, 11, 11, 10, 9};
    build_band_world(w);
}
/* bamboo: stand density bands; open floor below a closed canopy */
static void build_bamboo(void) {
    static const double w[8] = {6, 8, 10, 11, 11, 10, 8, 6};
    build_band_world(w);
}
/* canyon: strata bands, thickest through the middle of the wall */
static void build_canyon(void) {
    static const double w[8] = {6, 8, 10, 12, 12, 10, 8, 6};
    build_band_world(w);
}
/* vinyl: groove-depth bands, near-flat so the rings carry the image */
static void build_vinyl(void) {
    static const double w[8] = {9, 9, 10, 11, 11, 10, 9, 8};
    build_band_world(w);
}
/* solar: photosphere brightness bands; granules dominate, spots are rare */
static void build_solar(void) {
    static const double w[8] = {2, 3, 6, 10, 13, 12, 9, 5};
    build_band_world(w);
}
/* city: altitude bands 0..7 - sky above, glowing streets below */
static void build_city(void) {
    static const double w[8] = {12, 10, 8, 6, 5, 5, 4, 3};
    build_band_world(w);
}
/* aurora: curtain-intensity bands, bright at the top of the sky */
static void build_aurora(void) {
    static const double w[8] = {3, 5, 8, 11, 13, 9, 4, 2};
    build_band_world(w);
}
/* matrix: rain-intensity bands; deep columns pour harder */
static void build_matrix(void) {
    static const double w[8] = {4, 7, 10, 12, 11, 8, 5, 3};
    build_band_world(w);
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
    const ModeSpec *spec = mode_spec();
    ntiles_ = 0;
    spec->build();
    build_compat(spec->smooth_compat);
    g_torus = spec->torus;
    g_smooth = spec->smooth_render;
    g_hero_on = false;
    apply_bias();
    if (g_is_tty) set_title(spec->name);
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
/* connector strokes must not run off a world that does not wrap */
static bool bounded_connector_mode(void) {
    return !g_torus && mode_spec()->group == MG_CONNECTOR;
}
/* Smoothed compatibility only constrains neighbours, so a band field random
 * walks and every world built on one reads as noise at arm's length. This
 * seeds the domains from bilinear value noise on a coarse lattice instead:
 * broad forms come from the prior, detail and every hard constraint still
 * come from WFC. The lattice wraps so toroidal worlds stay seamless, and the
 * interpolation keeps neighbouring targets within a band or two of each
 * other — the +/-2 windows below have to overlap or the grid contradicts. */
#define FIELD_LATTICE 9
static double field_noise(int x, int y) {
    /* The lattice has to divide the world exactly, or the wrap lands mid-cell
     * and the seam jumps several bands at once — neighbouring +/-2 windows
     * stop overlapping there and the grid cannot be solved at all. */
    int lw = W_ / FIELD_LATTICE; if (lw < 2) lw = 2;
    int lh = H_ / FIELD_LATTICE; if (lh < 2) lh = 2;
    double u = (double)x / (W_ > 0 ? W_ : 1) * lw;
    double v = (double)y / (H_ > 0 ? H_ : 1) * lh;
    int gx = (int)u, gy = (int)v;
    double fx = u - gx, fy = v - gy;
    fx = fx * fx * (3.0 - 2.0 * fx);          /* smoothstep: gentle gradients */
    fy = fy * fy * (3.0 - 2.0 * fy);
    double corner[2][2];
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int cx = (gx + i) % lw, cy = (gy + j) % lh;
            corner[j][i] = (hash3((uint32_t)cx, (uint32_t)cy,
                                  (uint32_t)(g_seed ^ (g_seed >> 32))) % 1024) / 1023.0;
        }
    double a = corner[0][0] + (corner[0][1] - corner[0][0]) * fx;
    double b = corner[1][0] + (corner[1][1] - corner[1][0]) * fx;
    return a + (b - a) * fy;
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
    const ModeSpec *spec = mode_spec();
    if (spec->band_ramp || spec->coarse) {
        int tb;
        if (spec->band_ramp) {
            tb = (int)(7.0 * (H_ - 1 - y) / (H_ > 1 ? H_ - 1 : 1));
            if (spec->ramp_flip) tb = 7 - tb;
            /* a coarse world with a ramp keeps the ramp and lets the field
             * bend it, so dunes still crest and strata still lie flat-ish */
            if (spec->coarse)
                tb = (int)((2.0 * tb + 7.0 * field_noise(x, y)) / 3.0 + 0.5);
        } else {
            tb = (int)(field_noise(x, y) * 7.999);
        }
        if (tb < 0) tb = 0;
        if (tb > 7) tb = 7;
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
    if (!strcmp(mode_name(), "koi")) koi_seed();
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

static bool macro_network_mode(void) { return mode_spec()->network; }

static const char *macro_name(void) {
    const char *m = mode_name();
    if (!strcmp(m, "streets")) return "arterial-grid";
    if (!strcmp(m, "neurons")) return "soma-branches";
    if (!strcmp(m, "mycelium")) return "spore-tendrils";
    if (!strcmp(m, "delta")) return "delta-channel";
    if (!strcmp(m, "rail")) return "rail-yard";
    return "none";
}

static int macro_role_at(int x, int y) {
    if (!macro_network_mode() || W_ <= 0 || H_ <= 0) return MACRO_NONE;
    const char *m = mode_name();
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
    } else if (!strcmp(m, "rail")) {
        /* a yard is parallel trunk roads with ladder crossovers between
         * them, and buffer stops where a siding runs out of frame. */
        int pitch = H_ > 14 ? 4 : 3;
        int off = (int)(salt % (uint32_t)pitch);
        bool trunk = ((y + off) % pitch) == 0;
        int ladder = 6 + (int)(salt % 5);
        bool cross = ((x + (int)(salt % (uint32_t)ladder)) % ladder) == 0;
        if (trunk && cross) return MACRO_INTERSECTION;
        if (trunk) return MACRO_ARTERIAL;
        if (cross && ((y + off) % pitch) == 1) return MACRO_ARTERIAL;
        if ((x == 0 || x == W_ - 1) && (h % 5) < 2) return MACRO_TERMINAL;
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
    const char *m = mode_name();
    if (!strcmp(m, "streets"))
        return role == MACRO_INTERSECTION ? 4 : 2;
    if (!strcmp(m, "neurons"))
        return role == MACRO_SOMA ? 3 : role == MACRO_TERMINAL ? 1 : 2;
    if (!strcmp(m, "mycelium"))
        return role == MACRO_SOURCE ? 1 : role == MACRO_TERMINAL ? 1 : 2;
    if (!strcmp(m, "delta"))
        return role == MACRO_SOURCE ? 1 : role == MACRO_CONFLUENCE ? 3 :
               role == MACRO_BRANCH ? 3 : 2;
    if (!strcmp(m, "rail"))
        return role == MACRO_INTERSECTION ? 3 : role == MACRO_TERMINAL ? 1 : 2;
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
    const char *m = mode_name();
    double ideal = !strcmp(m, "streets") ? 2.2 : !strcmp(m, "neurons") ? 2.0 :
                   !strcmp(m, "mycelium") ? 1.7 : !strcmp(m, "rail") ? 2.0 : 1.9;
    int degree = 0;
    for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
    return quality_signed_clamp((1.0 - fabs(degree - ideal) / 3.5) * 2.0 - 1.0);
}

/* ---------------- learned tile preferences in the classic solver ----------
 * The thermo sidecar learns which tiles pay off in a world and writes them to
 * a profile, but only the sidecar ever consulted them: run --solver classic
 * and every lesson sat unused on disk. --learned reads the profile the worker
 * wrote and applies its tile preferences to the classic weighted pick, in the
 * same log-space the worker scores in — so a world you have been solving gets
 * better even with no Python running.
 *
 * The profile is keyed by a fingerprint the worker computes, so rather than
 * reimplement that hash we take the first profile for this mode and validate
 * it by length: a tileset that has changed shape cannot be applied by
 * accident. */
static const char *json_str(const char *s, const char *key);
static char g_thermo_profile[512];

static int thermo_context_index(int cell);

static bool g_learned = false;
static double learned_bias_[MAXT];
static double learned_context_[8];
static bool learned_context_ready_ = false;
static double *learned_pair_ = NULL;      /* [dir][tile][target], the worker's layout */
static int learned_pair_n_ = 0;
static bool learned_pair_ready_ = false;
static bool learned_ready_ = false;
static int learned_for_mode_ = -1;

static bool learned_parse_array(const char *json, const char *key,
                                double *out, int want) {
    const char *p = json_str(json, key);
    if (!p || *p != '[') return false;
    p++;
    int n = 0;
    for (;;) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']') break;
        if (n >= want) return false;          /* longer than our tileset */
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p || !isfinite(v)) return false;
        if (v < -8.0 || v > 8.0) return false; /* the worker clamps to 2.5 */
        out[n++] = v;
        p = end;
    }
    return n == want;
}

static void learned_load(void) {
    learned_ready_ = false;
    learned_for_mode_ = g_mode_idx;
    for (int i = 0; i < MAXT; i++) learned_bias_[i] = 0.0;
    if (!g_learned || ntiles_ <= 0) return;
    char dir[512];
    if (g_thermo_profile[0]) snprintf(dir, sizeof dir, "%s", g_thermo_profile);
    else {
        const char *home = getenv("HOME");
        snprintf(dir, sizeof dir, "%s/.wfc-thermo", home ? home : "/tmp");
    }
    DIR *d = opendir(dir);
    if (!d) return;
    char prefix[80];
    snprintf(prefix, sizeof prefix, "%s-", mode_name());
    char path[1024];
    path[0] = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
        if (len < 6 || strcmp(e->d_name + len - 5, ".json") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        break;
    }
    closedir(d);
    if (!path[0]) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    static char buf[1 << 21];
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    bool truncated = !feof(f);
    fclose(f);
    if (truncated) return;                    /* refuse to read half a profile */
    buf[got] = 0;
    if (learned_parse_array(buf, "tile_bias", learned_bias_, ntiles_))
        learned_ready_ = true;
    else
        for (int i = 0; i < MAXT; i++) learned_bias_[i] = 0.0;
    learned_context_ready_ =
        learned_parse_array(buf, "context_bias", learned_context_, 8);
    if (!learned_context_ready_)
        for (int i = 0; i < 8; i++) learned_context_[i] = 0.0;

    /* pair preferences are the largest table by far — 4 x ntiles^2 — so it is
     * allocated rather than sitting in bss for a tileset that may not need it */
    int want = NDIR * ntiles_ * ntiles_;
    learned_pair_ready_ = false;
    if (want > 0 && want != learned_pair_n_) {
        double *grown = realloc(learned_pair_, sizeof(double) * (size_t)want);
        if (grown) { learned_pair_ = grown; learned_pair_n_ = want; }
        else { free(learned_pair_); learned_pair_ = NULL; learned_pair_n_ = 0; }
    }
    if (learned_pair_ && learned_pair_n_ == want)
        learned_pair_ready_ = learned_parse_array(buf, "pair_bias", learned_pair_, want);
}

/* How much a tile puts down, in [-1, 1] — the feature a context preference
 * acts through, so it can say "where the neighbourhood is unresolved, lean
 * heavier" rather than nudging every tile at the cell by the same amount.
 *
 * It has to be read off the edges. My first attempt used the popcount of a
 * tile's compatibility mask, which is nearly constant inside a binary-edge
 * connector set — the term cancelled again and the bias stayed inert. Edge
 * degree separates a crossing from a dead end; the band nibble separates a
 * trough from a crest. The worker cannot derive either from the masks it is
 * sent, so C computes this and ships it in the init frame. */
static double tile_openness(int tile) {
    if (tile < 0 || tile >= ntiles_ || ntiles_ <= 0) return 0.0;
    if (mode_spec()->smooth_compat) {         /* band world: height in the ramp */
        return (tiles_[tile].e[0] >> 4) / 3.5 - 1.0;
    }
    int degree = 0;
    for (int d = 0; d < NDIR; d++) degree += tiles_[tile].e[d] != 0;
    return degree / 2.0 - 1.0;
}

/* The worker scores a tile by averaging its learned preference over every
 * neighbour the tile could still legally sit beside, per direction. Mirrored
 * here so the same profile means the same thing to both solvers. */
static double learned_pair_term(int tile, int cell) {
    if (!learned_pair_ready_ || cell < 0) return 0.0;
    int x = cell % W_, y = cell / W_;
    double total = 0.0;
    int dirs = 0;
    for (int d = 0; d < NDIR; d++) {
        int nx = x, ny = y;
        if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
        else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
        else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
        else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
        if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
        uint64_t allowed = cdir_[d][tile] & dom_[IDX(nx, ny)];
        if (!allowed) continue;
        double sum = 0.0;
        int count = 0;
        while (allowed) {
            int target = __builtin_ctzll(allowed);
            allowed &= allowed - 1;
            sum += learned_pair_[(d * ntiles_ + tile) * ntiles_ + target];
            count++;
        }
        total += sum / count;
        dirs++;
    }
    return dirs ? total / dirs : 0.0;
}

static double learned_weight(int tile, int cell) {
    if (!g_learned) return 1.0;
    if (learned_for_mode_ != g_mode_idx) learned_load();
    if (tile < 0 || tile >= ntiles_) return 1.0;
    double logw = 0.0;
    if (learned_ready_) logw += learned_bias_[tile];
    if (learned_context_ready_ && cell >= 0 && cell < W_ * H_)
        logw += 0.18 * learned_context_[thermo_context_index(cell)] * tile_openness(tile);
    if (learned_pair_ready_) logw += 0.34 * learned_pair_term(tile, cell);
    return exp(logw);                         /* the worker's own log space */
}

static int weighted_pick_at(uint64_t m, int cell) {
    /* one pass: the weight of a candidate now costs a pair-table walk, and
     * the old shape paid for every candidate twice */
    double weight[MAXT];
    double tot = 0, acc = 0;
    for (uint64_t mm = m; mm; mm &= mm - 1) {
        int tile = __builtin_ctzll(mm);
        double w = tiles_[tile].weight * learned_weight(tile, cell);
        if (cell >= 0) w *= exp(0.52 * macro_tile_bonus(cell, tile));
        weight[tile] = w;
        tot += w;
    }
    double r = rndf() * tot;
    for (uint64_t mm = m; mm; mm &= mm - 1) {
        int tile = __builtin_ctzll(mm);
        acc += weight[tile];
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
/* Weight entropy is a function of the domain mask alone, but the scan
 * recomputed it for every undecided cell on every step — a loop over the set
 * bits, so the solve cost ran with the tileset size. dungeon carries 49 tiles
 * against maze's 16 and took three times as long for the same grid.
 *
 * The cache is keyed on the mask it was computed from, so it validates
 * itself: nothing has to invalidate it, and the many places that assign or
 * memcpy dom_ wholesale (thermo rollback, undo, history, pins) stay correct
 * without knowing it exists. The random tiebreak jitter deliberately stays
 * outside — it draws from the rng every step, and caching it would change how
 * much rng each solve consumes and therefore every world it produces. */
static double *ent_val_ = NULL;      /* cached weight entropy */
static uint64_t *ent_key_ = NULL;    /* the mask it was computed from */
static size_t ent_cap_ = 0;

static bool entropy_cache_fit(size_t cells) {
    if (ent_cap_ >= cells) return true;
    double *v = realloc(ent_val_, sizeof(double) * cells);
    if (!v) return false;
    ent_val_ = v;
    uint64_t *k = realloc(ent_key_, sizeof(uint64_t) * cells);
    if (!k) return false;
    ent_key_ = k;
    /* 0 is never a live domain, so a zeroed key always misses */
    memset(ent_key_, 0, sizeof(uint64_t) * cells);
    ent_cap_ = cells;
    return true;
}

static double weight_entropy(int i, uint64_t mask) {
    static uint32_t seen_gen = 0;
    if (seen_gen != weights_gen_) {         /* weights moved: drop everything */
        seen_gen = weights_gen_;
        if (ent_key_ && ent_cap_) memset(ent_key_, 0, sizeof(uint64_t) * ent_cap_);
    }
    if (ent_key_ && (size_t)i < ent_cap_ && ent_key_[i] == mask) return ent_val_[i];
    double sw = 0, slw = 0;
    uint64_t m = mask;
    while (m) {
        int b = __builtin_ctzll(m); m &= m - 1;
        sw += tiles_[b].weight;
        slw += tiles_[b].weight * tiles_[b].lw;
    }
    double e = sw > 0 ? log2(sw) - slw / sw : 0;
    if (ent_key_ && (size_t)i < ent_cap_) { ent_key_[i] = mask; ent_val_[i] = e; }
    return e;
}

static int wfc_step(void) {
    int best = -1, ties = 0;
    double be = 1e9;
    g_decided = 0;
    (void)entropy_cache_fit((size_t)W_ * (size_t)H_);
    for (int i = 0; i < W_ * H_; i++) {
        int k = pc64(dom_[i]);
        if (k == 0) return -1;
        if (k == 1) g_decided++;
        if (k > 1) {
            double e = weight_entropy(i, dom_[i]);
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
    const char *m = mode_name();
    if (!strcmp(m, "streets"))
        return (QualityProfile){"streets", 0.30, 0.18, 0.14, 0.08, 0.08, 0.06, 0.16};
    if (!strcmp(m, "neurons"))
        return (QualityProfile){"neurons", 0.24, 0.05, 0.12, 0.15, 0.08, 0.06, 0.30};
    if (!strcmp(m, "mycelium"))
        return (QualityProfile){"mycelium", 0.24, 0.04, 0.16, 0.14, 0.12, 0.06, 0.24};
    if (!strcmp(m, "delta"))
        return (QualityProfile){"delta", 0.27, 0.12, 0.14, 0.08, 0.10, 0.05, 0.24};
    if (!strcmp(m, "rail"))
        return (QualityProfile){"rail", 0.30, 0.16, 0.14, 0.07, 0.09, 0.06, 0.18};
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
    const char *m = mode_name();
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
        const char *m = mode_name();
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

/* profile-weighted scalar the thermo guard ranks two candidate states by.
 * same blend quality_reward() uses, so accepting a patch and rewarding it
 * agree on what "better" means. */
static double quality_objective(QualityMetrics q) {
    QualityProfile p = quality_profile();
    double weighted = p.validity * q.validity + p.boundary * q.boundary +
                      p.coverage * q.coverage + p.diversity * q.diversity +
                      p.smoothness * q.smoothness + p.stability * q.stability +
                      p.topology * q.topology;
    return 0.75 * weighted + 0.25 * q.total;
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

#endif
