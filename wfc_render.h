/* wfc_render.h -- part of wfc, included by wfc.c.
 *
 * palettes, the framebuffer, the colour-vision assist,
 * every world's render branch, zen dissolves, the raymarcher and iso view
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `cc -O2 -std=c11 -o wfc wfc.c -lz` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_RENDER_H
#define WFC_RENDER_H
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
static const RGB STORMPAL[8] = {
    {196, 202, 216}, {156, 164, 186}, {120, 130, 158}, {90, 100, 130},
    {64, 74, 102}, {44, 52, 76}, {28, 34, 54}, {15, 18, 32},
};
static const RGB GLACIERPAL[8] = {
    {10, 26, 46}, {17, 43, 72}, {26, 64, 101}, {41, 92, 133},
    {70, 128, 167}, {113, 168, 199}, {166, 209, 228}, {228, 246, 251},
};
static const RGB BAMBOOPAL[8] = {
    {9, 19, 13}, {15, 31, 20}, {22, 47, 28}, {31, 66, 36},
    {45, 89, 46}, {68, 114, 59}, {99, 145, 73}, {145, 180, 99},
};
static const RGB SOLARPAL[8] = {
    {58, 10, 2}, {108, 22, 3}, {158, 44, 4}, {204, 78, 8},
    {236, 120, 18}, {250, 168, 46}, {255, 212, 112}, {255, 246, 198},
};
static const RGB CANYONPAL[8] = {
    {26, 17, 15}, {61, 31, 21}, {103, 53, 29}, {145, 79, 39},
    {181, 109, 57}, {211, 145, 85}, {233, 185, 131}, {247, 221, 185},
};
static const RGB VINYLPAL[8] = {
    {7, 7, 9}, {15, 15, 18}, {25, 25, 29}, {37, 37, 43},
    {51, 51, 59}, {69, 69, 79}, {95, 95, 107}, {139, 139, 155},
};
static const RGB LOOMPAL[8] = {
    {17, 13, 11}, {53, 33, 25}, {95, 57, 39}, {139, 87, 53},
    {175, 123, 73}, {205, 163, 109}, {227, 199, 157}, {241, 227, 203},
};
static const RGB RAILPAL[8] = {
    {12, 13, 16}, {24, 26, 31}, {40, 43, 49}, {60, 64, 72},
    {88, 94, 104}, {124, 131, 143}, {168, 176, 188}, {216, 224, 234},
};
static const RGB DELTAPAL[8] = {
    {3, 22, 34}, {4, 42, 58}, {6, 68, 82}, {8, 96, 104},
    {16, 126, 126}, {38, 158, 142}, {104, 190, 156}, {210, 224, 180},
};
static RGB network_bg(const char *mode) {
    if (!strcmp(mode, "loom")) return (RGB){14, 11, 9};
    if (!strcmp(mode, "rail")) return (RGB){9, 9, 11};
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
    if (!strcmp(mode, "loom")) {
        /* warp runs down the web, weft across it; at a crossing one thread
         * lies over the other, and that alternation is the weave. */
        bool warp = tiles_[tile].e[0] || tiles_[tile].e[2];
        bool weft = tiles_[tile].e[1] || tiles_[tile].e[3];
        bool over = ((cx + cy) & 1) == 0;
        int band = warp && weft ? (over ? 6 : 3) : warp ? 5 : 4;
        RGB c = LOOMPAL[band];
        /* a few dyed threads run the length of the cloth */
        if (warp && hash3((uint32_t)cx, 1, 6161) % 9 == 0)
            c = lerp(c, (RGB){186, 74, 92}, 0.5);
        else if (weft && hash3((uint32_t)cy, 2, 6161) % 11 == 0)
            c = lerp(c, (RGB){74, 122, 162}, 0.45);
        double sheen = 0.88 + 0.12 * sin(now_ms() * 0.0006 + cx * 0.3 + cy * 0.2);
        return scalec(c, pulse * sheen);
    }
    if (!strcmp(mode, "rail")) {
        /* cold steel over dark ballast, with a service running the network:
         * the head is a moving diagonal wavefront, its tail fading behind. */
        int band = degree >= 3 ? 3 + (int)(h % 2) : degree == 2 ? 4 + (int)(h % 2) : 2;
        RGB c = RAILPAL[band > 7 ? 7 : band];
        double phase = now_ms() * 0.0009 - (cx * 0.9 + cy * 0.5);
        double wave = sin(phase);
        if (wave > 0.986) c = (RGB){255, 246, 214};          /* headlamp */
        else if (wave > 0.93) c = lerp(c, (RGB){248, 214, 148}, (wave - 0.93) / 0.056);
        if (degree >= 3) {
            /* points: a switch lamp sits on every junction, green or amber */
            bool clear = ((h >> 3) + (uint32_t)(now_ms() / 1900)) % 3 != 0;
            c = lerp(c, clear ? (RGB){96, 226, 138} : (RGB){248, 186, 72}, 0.42);
        } else if (degree == 1) {
            c = lerp(c, (RGB){206, 78, 66}, 0.55);           /* buffer stop */
        }
        return scalec(c, pulse * (0.86 + 0.14 * sin(now_ms() * 0.0013 + cx * 0.2)));
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
    bool circ = !strcmp(mode_name(), "circuit");
    bool pipes = !strcmp(mode_name(), "pipes");
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
/* ---------------- colour-vision assist ----------------
 * Several worlds carry meaning in red against green — rail's switch lamps
 * against its buffer stops, streets' dead-end markers, the quality heatmap.
 * Deuteranopia collapses exactly that axis, so those worlds lose the
 * distinction that makes them readable.
 *
 * The textbook answer is daltonization: simulate what a deuteranope receives,
 * take the error, and redistribute it. I implemented that first and measured
 * it against a deuteranopia simulation — it moved the pairs that carry
 * meaning 35% *closer* together, because redistributing green error back into
 * green is invisible to the very viewer it is meant to help.
 *
 * What works is to move the red-green signal onto the blue axis, which is
 * intact: greens gain blue, reds lose it. On the same measurement that
 * separates those pairs by 55% instead.
 */
static RGB colour_assist(RGB c) {
    double signal = 0.8 * ((double)c.g - (double)c.r);
    double b = (double)c.b + signal;
    RGB out = {c.r, c.g, (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b)};
    return out;
}

static RGB crt(RGB c) {
    if (g_colorblind) c = colour_assist(c);
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
/* Renderers sample the clock for every animation — drifting clouds, the rail
 * headlamp, glacier glints, the stained-glass sweep. That is right for the
 * live view and wrong for an export: the same seed saved twice produced two
 * different images, in a program whose whole premise is that a seed is a
 * world you can share. A headless run therefore freezes the clock at a phase
 * derived from the seed, so exports are reproducible while different seeds
 * still catch their animations at different moments. */
static bool g_clock_frozen = false;
static double g_clock_frozen_ms = 0;

static double now_ms(void) {
    if (g_clock_frozen) return g_clock_frozen_ms;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void clock_freeze_for_export(void) {
    g_clock_frozen = true;
    /* a seed-derived phase: stable per seed, varied across seeds */
    g_clock_frozen_ms = (double)(hash3((uint32_t)g_seed,
                                       (uint32_t)(g_seed >> 32), 0x0C10CBu) % 600000u);
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
        "  /       pick a world        m     next mode",
        "  Y       red/green assist     k     CRT scanlines",
        "",
        "  space   new map              [ ]   density",
        "  y       color theme          c     auto-cycle modes",
        "  +/-     collapse speed       p     pause",
        "  g       record gif           s     save image",
        "  h       this help            z     all-worlds sheet\n",
        "  r       raytrace view        e     entropy view\n",
        "  i       isometric view       , .    scrub time\n",
        "  T       thermo solver        R     reset thermo learning\n",
        "  l       quality observatory   P     pin/unpin hovered cell\n",
        "  Q       quality heatmap      E     evolve/rank seed variants\n",
        "  F       fit fullscreen       f     drift camera\n",
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
    const char *mode = mode_name();
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
/* A lightning stroke is a seeded random walk down the sky: each row nudges
 * the column left or right, so the bolt reads as one forked line instead of
 * unrelated flashes. Only evaluated inside the ~200ms flash window. */
static int storm_bolt_x(uint32_t strike, int row) {
    int x = (int)(hash3(strike, 17, 4711) % (uint32_t)(W_ > 0 ? W_ : 1));
    for (int r = 0; r <= row; r++) {
        uint32_t st = hash3(strike, (uint32_t)r, 0x5B1Fu) % 7;
        x += st < 3 ? -1 : st < 6 ? 1 : 0;
        if (st == 6 && (r & 1)) x += 1;
    }
    return ((x % W_) + W_) % W_;
}

/* A crevasse is only worth drawing where the shelf steps down along a run —
 * an isolated one-cell step is just texture. 1 = east-west seam, 2 = north-
 * south seam, 3 = the uphill lip that catches the low sun, 0 = flat ice. */
static int elev_of_cell(int x, int y);
static int glacier_seam(int wx, int wy, int band) {
    int n = elev_of_cell(wx, (wy + H_ - 1) % H_);
    int w = elev_of_cell((wx + W_ - 1) % W_, wy);
    int ne = elev_of_cell((wx + 1) % W_, (wy + H_ - 1) % H_);
    int nw = elev_of_cell((wx + W_ - 1) % W_, (wy + H_ - 1) % H_);
    int sw = elev_of_cell((wx + W_ - 1) % W_, (wy + 1) % H_);
    /* a ledge, not a speck: the whole edge of the neighbourhood has to drop */
    if (n < band && ne < band && nw < band) return 1;
    if (w < band && nw < band && sw < band) return 2;
    return (n > band && w > band) ? 3 : 0;
}

/* Granules are convection cells a couple of grid cells across, not per-dot
 * noise: one id per block, with the block's rim reading as a cooler lane. */
static uint32_t solar_granule(int wx, int wy, int *rim) {
    int gy = wy / 2;
    int stagger = (int)(hash3((uint32_t)gy, 11, 0x9E37u) & 1);   /* brick the rows */
    int gx = (wx + stagger) / 3;
    uint32_t id = hash3((uint32_t)gx, (uint32_t)gy, 313);
    /* the lane sits on one column of each granule, picked by its own hash,
     * so the gaps never line up into corduroy */
    if (rim) *rim = ((wx + stagger) % 3) == (int)(id % 3);
    return id;
}

/* Sunspot field: three umbrae seeded from the world seed, each with a
 * penumbra ring. Returns 0 clear, 1 penumbra, 2 umbra. */
static int solar_spot(int wx, int wy) {
    int worst = 0;
    for (int k = 0; k < 3; k++) {
        uint32_t h = hash3((uint32_t)(g_seed >> (k * 8)), (uint32_t)k, 0x50A2u);
        int sx = (int)(h % (uint32_t)(W_ > 0 ? W_ : 1));
        int sy = (int)((h >> 9) % (uint32_t)(H_ > 0 ? H_ : 1));
        int r = 2 + (int)((h >> 18) % 3);
        int dx = wx - sx, dy = (wy - sy) * 2;
        if (dx > W_ / 2) dx -= W_;
        if (dx < -W_ / 2) dx += W_;
        int d2 = dx * dx + dy * dy;
        if (d2 <= r * r) return 2;
        if (d2 <= (r + 2) * (r + 2)) worst = 1;
    }
    return worst;
}

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
    int art = strcmp(mode_name(), "terrain") ? 8 : 16;
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
    const char *mode = mode_name();
    const bool m_circuit = !strcmp(mode, "circuit"), m_terrain = !strcmp(mode, "terrain"), m_truchet = !strcmp(mode, "truchet"), m_fire = !strcmp(mode, "fire"), m_waves = !strcmp(mode, "waves"), m_dungeon = !strcmp(mode, "dungeon"), m_maze = !strcmp(mode, "maze"), m_galaxy = !strcmp(mode, "galaxy"), m_city = !strcmp(mode, "city"), m_aurora = !strcmp(mode, "aurora"), m_matrix = !strcmp(mode, "matrix"), m_pipes = !strcmp(mode, "pipes"), m_mondrian = !strcmp(mode, "mondrian"), m_koi = !strcmp(mode, "koi"), m_lava = !strcmp(mode, "lava"), m_sakura = !strcmp(mode, "sakura"), m_geode = !strcmp(mode, "geode"), m_lantern = !strcmp(mode, "lantern"), m_dunes = !strcmp(mode, "dunes"), m_reef = !strcmp(mode, "reef"), m_stained = !strcmp(mode, "stained"), m_streets = !strcmp(mode, "streets"), m_neurons = !strcmp(mode, "neurons"), m_mycelium = !strcmp(mode, "mycelium"), m_delta = !strcmp(mode, "delta"), m_storm = !strcmp(mode, "storm"), m_glacier = !strcmp(mode, "glacier"), m_bamboo = !strcmp(mode, "bamboo"), m_solar = !strcmp(mode, "solar"), m_rail = !strcmp(mode, "rail"), m_canyon = !strcmp(mode, "canyon"), m_vinyl = !strcmp(mode, "vinyl"), m_loom = !strcmp(mode, "loom");
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
                            } else if (m_streets || m_neurons || m_mycelium || m_delta || m_rail || m_loom) {
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
                            } else if (m_canyon || m_vinyl) {
                                int band = tiles_[t].e[0] >> 4;
                                double tsec = now_ms() * 0.001;
                                uint32_t h = hash3((uint32_t)(cx * 43 + chi),
                                                   (uint32_t)(cy * 47 + sub * 3), 55);
                                if (m_canyon) {
                                    /* each stratum keeps its own hard tone — a bedding
                                     * plane shades where one layer meets the next, and
                                     * the river runs the floor of the cut */
                                    uint32_t sh = hash3((uint32_t)band, 3, 21);
                                    bits = (uint8_t)(0xFFu ^ ((h & 0x11u) & (sh & 0xFFu)));
                                    col = scalec(CANYONPAL[band], pulse * (0.93 + (sh % 15) * 0.01));
                                    if (elev_of_cell(wx, (cy + H_ - 1) % H_) != band)
                                        col = scalec(col, sub == 0 ? 0.72 : 1.12);
                                    if (cy >= H_ - 2) {
                                        col = scalec(lerp(CANYONPAL[1], (RGB){30, 74, 96}, 0.75),
                                                     pulse);
                                        if ((wx * 5 + (int)(tsec * 6)) % 17 < 2)
                                            col = scalec((RGB){126, 176, 196}, pulse);
                                    } else if (!g_no_weather &&
                                               hash3((uint32_t)wx, (uint32_t)cy,
                                                     (uint32_t)(now_ms() / 600)) % 337 == 11) {
                                        /* dust turning in a shaft of light */
                                        bits = BRAILLE_BIT[h & 3][(h >> 2) & 1];
                                        col = scalec((RGB){255, 236, 196}, pulse);
                                    }
                                } else {
                                    /* a record: concentric grooves cut from the rim
                                     * in to the label, under a highlight that sweeps */
                                    double ddx = (wx - W_ / 2.0) * 0.5;
                                    double ddy = (cy - H_ / 2.0);
                                    double rad = sqrt(ddx * ddx + ddy * ddy);
                                    double edge = (H_ < W_ ? H_ : W_) * 0.5;
                                    int groove = (int)(rad * 2.4) & 1;
                                    bits = groove ? 0x33 : 0xFF;
                                    col = scalec(VINYLPAL[clampb(band / 2 + (groove ? 0 : 4))],
                                                 pulse);
                                    if (rad < edge * 0.30) {          /* the label */
                                        bits = 0xFF;
                                        RGB label = {182, 58, 46};
                                        if (rad < edge * 0.05) label = (RGB){20, 20, 24};
                                        else if (((int)(rad * 3) & 1)) label = (RGB){208, 176, 96};
                                        col = scalec(label, pulse);
                                    } else if (rad > edge * 0.98) {
                                        col = scalec((RGB){14, 14, 17}, pulse);
                                    }
                                    /* specular sweep, as if turning under a lamp */
                                    double ang = atan2(ddy, ddx) - tsec * 0.55;
                                    double sweep = cos(ang);
                                    if (sweep > 0.972 && rad > edge * 0.30)
                                        col = scalec(lerp(col, (RGB){226, 230, 240},
                                                          (sweep - 0.972) / 0.028), pulse);
                                }
                            } else if (m_storm || m_glacier || m_bamboo || m_solar) {
                                int band = tiles_[t].e[0] >> 4;
                                double tsec = now_ms() * 0.001;
                                uint32_t h = hash3((uint32_t)(cx * 37 + chi),
                                                   (uint32_t)(cy * 41 + sub * 5), 88);
                                if (m_storm) {
                                    /* cloud reads as masses, not noise: one blob id
                                     * per 2x1 cells shades the whole block together,
                                     * and it creeps sideways with the front. */
                                    int shift = (int)(tsec * 0.55);
                                    uint32_t blob = hash3((uint32_t)((wx + shift) / 2),
                                                          (uint32_t)wy, 271);
                                    int lift = (int)(blob % 3) - 1;
                                    int tb = dither_band(clampb(band + lift),
                                                         wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h | (h >> 4)) | 0x7E);
                                    col = scalec(STORMPAL[tb], pulse * (0.94 + (blob % 13) * 0.01));
                                    if (band <= 2 && !g_no_weather) {
                                        /* rain hangs in slanted veils below the base */
                                        int row = cy * 2 + sub;
                                        int slant = (int)(((long)(now_ms() / 52) + row * 2) % 9);
                                        if ((wx * 2 + slant + chi) % 9 < 2) {
                                            bits = 0x99;
                                            col = scalec((RGB){160, 182, 214}, pulse * 0.75);
                                        }
                                    }
                                    long age = (long)now_ms() % 2600;
                                    if (age < 210) {
                                        uint32_t strike = (uint32_t)(now_ms() / 2600);
                                        double flash = 1.0 + 0.95 * (1.0 - age / 210.0);
                                        col = scalec(col, flash);
                                        int reach = H_ * (int)(50 + hash3(strike, 3, 61) % 40) / 100;
                                        if (cy <= reach) {
                                            int lx = storm_bolt_x(strike, cy);
                                            int off = wx - lx;
                                            if (off > W_ / 2) off -= W_;
                                            if (off < -W_ / 2) off += W_;
                                            if (off == 0) { bits = 0xFF; col = (RGB){255, 255, 250}; }
                                            else if (off == 1 || off == -1) {
                                                col = lerp(col, (RGB){226, 232, 255}, 0.55);
                                            }
                                        }
                                    }
                                } else if (m_glacier) {
                                    /* flat ice facets cut by crevasses wherever the
                                     * shelf steps by more than one band */
                                    int tb = dither_band(band, wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 5)) | 0x66);
                                    col = scalec(GLACIERPAL[tb], pulse);
                                    /* smoothed compatibility means the shelf can only
                                     * ever step by one, so the crevasse follows that
                                     * contour: a thin dark seam on the downhill side. */
                                    int seam = glacier_seam(wx, cy, band);
                                    if (seam == 1) {           /* seam runs east-west */
                                        bits = 0x0Fu;
                                        col = scalec((RGB){6, 17, 36}, pulse);
                                    } else if (seam == 2) {    /* seam runs north-south */
                                        bits = 0x55u;
                                        col = scalec((RGB){6, 17, 36}, pulse);
                                    } else if (seam == 3) {    /* uphill lip in the low sun */
                                        col = scalec(lerp(col, (RGB){238, 250, 255}, 0.30), pulse);
                                    }
                                    /* meltwater glints wander across the packed ice */
                                    uint32_t gl = hash3((uint32_t)(cx * 19 + chi),
                                                        (uint32_t)(cy * 7 + sub),
                                                        (uint32_t)(now_ms() / 420));
                                    if (band >= 5 && gl % 211 == 61) {
                                        bits = BRAILLE_BIT[gl & 3][(gl >> 2) & 1];
                                        col = scalec((RGB){236, 252, 255}, pulse);
                                    }
                                } else if (m_bamboo) {
                                    /* stalks stand in world columns chosen by hash;
                                     * the whole stand leans together in the wind */
                                    int tb = dither_band(band, wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 7)) & 0x11);
                                    /* canopy light filters down: keep the band ramp,
                                     * just held well behind the stalks */
                                    col = scalec(BAMBOOPAL[tb], pulse * 0.42);
                                    double sway = sin(tsec * 0.9 + cy * 0.11) * 1.6;
                                    int dotx = wx * 8 + chi * 2;
                                    for (int st = -2; st <= 2; st++) {
                                        int scol = wx + st;
                                        uint32_t sh = hash3((uint32_t)((scol % W_ + W_) % W_), 3, 1717);
                                        if (sh % 5 >= 2) continue;             /* no stalk here */
                                        int thick = 1 + (int)(sh % 2);
                                        int sx = (int)(scol * 8 + 3 + sway * (1.0 + (sh % 3) * 0.4));
                                        if (dotx > sx || dotx + 1 < sx - thick) continue;
                                        int shade = 4 + (int)(sh % 4);
                                        col = scalec(BAMBOOPAL[shade], pulse);
                                        bits = 0xFF;
                                        int row = cy * 8 + sub * 4;
                                        if (((row + (int)(sh % 9)) % 11) < 2) {   /* node ring */
                                            col = scalec(BAMBOOPAL[7], pulse * 1.1);
                                        }
                                        break;
                                    }
                                    if (band <= 2 && !g_no_weather &&
                                        hash3((uint32_t)wx, (uint32_t)cy,
                                              (uint32_t)(now_ms() / 700)) % 401 == 7) {
                                        bits = BRAILLE_BIT[h & 3][(h >> 2) & 1];
                                        col = scalec((RGB){226, 240, 138},
                                                     pulse * (0.5 + 0.5 * sin(tsec * 5)));
                                    }
                                } else {
                                    /* photosphere: bright granules divided by cooler
                                     * lanes, dark spots where the field is strongest */
                                    int rim = 0;
                                    uint32_t gr = solar_granule(wx, wy, &rim);
                                    int tb = dither_band(clampb(band - 1 + (int)(gr % 3) - 1),
                                                         wx, wy, chi * 2, sub * 8);
                                    bits = (uint8_t)((h ^ (h >> 3)) | 0xDB);
                                    /* intergranular lanes are the cool gaps between
                                     * rising cells, so they sit two stops down */
                                    col = scalec(SOLARPAL[rim ? clampb(tb - 3) : tb], pulse);
                                    int spot = solar_spot(wx, cy);
                                    if (spot) {
                                        bits = 0xFF;
                                        col = scalec(spot == 2 ? (RGB){22, 7, 3}
                                                               : (RGB){96, 32, 6}, pulse);
                                    }
                                    /* a flare arcs up and fades, once every few seconds */
                                    uint32_t fl = (uint32_t)(now_ms() / 3100);
                                    uint32_t fk = hash3(fl, 5, 2027);
                                    long fage = (long)now_ms() % 3100;
                                    if (fage < 900) {
                                        int fx = (int)(fk % (uint32_t)(W_ > 0 ? W_ : 1));
                                        int dxf = wx - fx;
                                        if (dxf > W_ / 2) dxf -= W_;
                                        if (dxf < -W_ / 2) dxf += W_;
                                        int arc = (int)(H_ * 0.22 * sin(abs(dxf) * 0.5));
                                        if (abs(dxf) <= 4 && cy == arc + H_ / 3) {
                                            double fade = 1.0 - fage / 900.0;
                                            bits = 0xFF;
                                            col = scalec((RGB){255, 236, 176}, pulse * fade);
                                        }
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
                            } else if (m_streets || m_neurons || m_mycelium || m_delta || m_rail || m_loom) {
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
                            const char *shm = mode_name();
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
                            else if (!strcmp(shm, "storm")) { da = (RGB){12, 15, 26}; db = (RGB){96, 110, 146}; }
                            else if (!strcmp(shm, "glacier")) { da = (RGB){8, 20, 38}; db = (RGB){86, 148, 190}; }
                            else if (!strcmp(shm, "bamboo")) { da = (RGB){8, 17, 12}; db = (RGB){74, 122, 62}; }
                            else if (!strcmp(shm, "solar")) { da = (RGB){28, 8, 2}; db = (RGB){214, 108, 20}; }
                            else if (!strcmp(shm, "rail")) { da = (RGB){9, 9, 11}; db = (RGB){110, 118, 130}; }
                            else if (!strcmp(shm, "canyon")) { da = (RGB){24, 15, 13}; db = (RGB){186, 114, 62}; }
                            else if (!strcmp(shm, "vinyl")) { da = (RGB){7, 7, 9}; db = (RGB){96, 96, 110}; }
                            else if (!strcmp(shm, "loom")) { da = (RGB){14, 11, 9}; db = (RGB){178, 128, 78}; }
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
    const char *mode = mode_name();
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
    const char *mode = mode_name();
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
    const char *m = mode_name();
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
    const char *mode = mode_name();
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

#endif
