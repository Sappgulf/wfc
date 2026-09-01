/* wfc_export.h -- part of wfc, included by wfc.c.
 *
 * image sampling shared by the exporters, BMP/PNG,
 * the web gallery, animated GIF, and inline-image graphics
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `cc -O2 -std=c11 -o wfc wfc.c wfc_core.c -lz` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_EXPORT_H
#define WFC_EXPORT_H
/* ---------------- image sampling (shared by BMP + GIF export) ---------------- */
static RGB img_px_raw(int cx, int cy, int ix, int iy, int art);

/* exports bypass fb_fg(), so the assist is applied on the way out here too */
static RGB img_px(int cx, int cy, int ix, int iy, int art) {
    RGB c = img_px_raw(cx, cy, ix, iy, art);
    return g_colorblind ? colour_assist(c) : c;
}

static RGB img_px_raw(int cx, int cy, int ix, int iy, int art) {
    const char *mode = mode_name();
    const bool m_circuit = !strcmp(mode, "circuit"), m_terrain = !strcmp(mode, "terrain"), m_fire = !strcmp(mode, "fire"), m_waves = !strcmp(mode, "waves"), m_dungeon = !strcmp(mode, "dungeon"), m_maze = !strcmp(mode, "maze"), m_galaxy = !strcmp(mode, "galaxy"), m_city = !strcmp(mode, "city"), m_aurora = !strcmp(mode, "aurora"), m_matrix = !strcmp(mode, "matrix"), m_pipes = !strcmp(mode, "pipes"), m_mondrian = !strcmp(mode, "mondrian"), m_koi = !strcmp(mode, "koi"), m_lava = !strcmp(mode, "lava"), m_sakura = !strcmp(mode, "sakura"), m_geode = !strcmp(mode, "geode"), m_lantern = !strcmp(mode, "lantern"), m_dunes = !strcmp(mode, "dunes"), m_reef = !strcmp(mode, "reef"), m_stained = !strcmp(mode, "stained"), m_streets = !strcmp(mode, "streets"), m_neurons = !strcmp(mode, "neurons"), m_mycelium = !strcmp(mode, "mycelium"), m_delta = !strcmp(mode, "delta"), m_storm = !strcmp(mode, "storm"), m_glacier = !strcmp(mode, "glacier"), m_bamboo = !strcmp(mode, "bamboo"), m_solar = !strcmp(mode, "solar"), m_rail = !strcmp(mode, "rail"), m_canyon = !strcmp(mode, "canyon"), m_vinyl = !strcmp(mode, "vinyl"), m_loom = !strcmp(mode, "loom"), m_tide = !strcmp(mode, "tide"), m_marble = !strcmp(mode, "marble"), m_cinder = !strcmp(mode, "cinder"), m_origami = !strcmp(mode, "origami");
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
    if (m_tide) {
        int band = tiles_[t].e[0] >> 4;
        double tsec = now_ms() * 0.001;
        double ph = cx * 0.42 - tsec * 0.95 + sin(cy * 0.78) * 1.4;
        int current = (int)round(sin(ph) * 1.4);
        uint32_t h = hash3((uint32_t)(cx * art + ix), (uint32_t)(cy * art + iy), 144);
        RGB c = TIDEPAL[dither_band(clampb(band + current), cx, cy, ix, iy)];
        if (band >= 5 && h % 19 < 4) c = (RGB){224, 246, 222};
        int moonx = (int)(W_ * 0.22 + sin(tsec * 0.12) * W_ * 0.12);
        int moond = abs(cx - moonx);
        if (moond > W_ / 2) moond = W_ - moond;
        if (band >= 5 && moond <= 1) c = (RGB){238, 248, 218};
        return c;
    }
    if (m_marble) {
        int band = tiles_[t].e[0] >> 4;
        double tsec = now_ms() * 0.001;
        double vein = sin(cx * 0.24 + cy * 0.51 +
                          1.7 * sin(cy * 0.17 - cx * 0.09));
        double polish = 0.82 + 0.18 * sin(cx * 0.21 - cy * 0.13 + tsec * 0.22);
        RGB c = MARBLEPAL[dither_band(clampb(band + (int)round(vein * 0.65)),
                                      cx, cy, ix, iy)];
        if (fabs(vein) > 0.82) c = (RGB){32, 56, 78};
        return scalec(c, polish);
    }
    if (m_cinder) {
        int band = tiles_[t].e[0] >> 4;
        double tsec = now_ms() * 0.001;
        int drift = (int)round(sin(cy * 0.29 + tsec * 0.34) * 0.8);
        uint32_t h = hash3((uint32_t)(cx * art + ix), (uint32_t)(cy * art + iy), 144);
        RGB c = CINDERPAL[dither_band(clampb(band + drift), cx, cy, ix, iy)];
        if ((h + (uint32_t)(tsec * 2.0)) % 67 < 3 && band <= 4)
            c = (RGB){255, 176, 72};
        if (fabs(sin(cx * 0.18 + cy * 0.42)) > 0.93) c = (RGB){28, 20, 20};
        return c;
    }
    if (m_origami) {
        int band = tiles_[t].e[0] >> 4;
        uint32_t h = hash3((uint32_t)(cx * art + ix), (uint32_t)(cy * art + iy), 144);
        double fold = sin((cx + cy) * 0.44 + sin(cx * 0.19) * 1.5);
        int lift = fold > 0.42 ? 1 : fold < -0.42 ? -1 : 0;
        RGB c = ORIGAMIPAL[dither_band(clampb(band + lift), cx, cy, ix, iy)];
        if (fabs(fold) < 0.065) c = (RGB){54, 62, 78};
        return scalec(c, 0.92 + 0.08 * fold + 0.02 * (h % 8));
    }
    if (m_canyon) {
        int band = tiles_[t].e[0] >> 4;
        if (cy >= H_ - 2) return lerp(CANYONPAL[1], (RGB){30, 74, 96}, 0.75);
        RGB c = CANYONPAL[band];
        if (elev_of_cell(cx, (cy + H_ - 1) % H_) != band)
            c = scalec(c, iy < art / 2 ? 0.72 : 1.12);
        return c;
    }
    if (m_vinyl) {
        double ddx = (cx - W_ / 2.0) * 0.5, ddy = cy - H_ / 2.0;
        double rad = sqrt(ddx * ddx + ddy * ddy);
        double edge = (H_ < W_ ? H_ : W_) * 0.5;
        int band = tiles_[t].e[0] >> 4;
        if (rad < edge * 0.05) return (RGB){20, 20, 24};
        if (rad < edge * 0.30)
            return ((int)(rad * 3) & 1) ? (RGB){208, 176, 96} : (RGB){182, 58, 46};
        if (rad > edge * 0.98) return (RGB){14, 14, 17};
        return VINYLPAL[clampb(band / 2 + (((int)(rad * 2.4) & 1) ? 0 : 4))];
    }
    if (m_storm) {
        int band = tiles_[t].e[0] >> 4;
        int shift = (int)(now_ms() * 0.00055);
        uint32_t blob = hash3((uint32_t)((cx + shift) / 2), (uint32_t)cy, 271);
        RGB c = STORMPAL[dither_band(clampb(band + (int)(blob % 3) - 1), cx, cy, ix, iy)];
        if ((long)now_ms() % 2600 < 210) {
            uint32_t strike = (uint32_t)(now_ms() / 2600);
            int reach = H_ * (int)(50 + hash3(strike, 3, 61) % 40) / 100;
            if (cy <= reach && storm_bolt_x(strike, cy) == cx) return (RGB){255, 255, 250};
            c = scalec(c, 1.6);
        }
        if (band <= 2 && (cx * 2 + (cy * 8 + iy) / 2) % 9 < 1)
            c = lerp(c, (RGB){160, 182, 214}, 0.5);
        return c;
    }
    if (m_glacier) {
        int band = tiles_[t].e[0] >> 4;
        RGB c = GLACIERPAL[dither_band(band, cx, cy, ix, iy)];
        int seam = glacier_seam(cx, cy, band);
        if (seam == 1 || seam == 2) return (RGB){7, 20, 40};
        if (seam == 3) return lerp(c, (RGB){238, 250, 255}, 0.30);
        return c;
    }
    if (m_bamboo) {
        int band = tiles_[t].e[0] >> 4;
        double sway = sin(now_ms() * 0.0009 + cy * 0.11) * 1.6;
        int dotx = cx * 8 + ix;
        for (int st = -2; st <= 2; st++) {
            int scol = cx + st;
            uint32_t sh = hash3((uint32_t)((scol % W_ + W_) % W_), 3, 1717);
            if (sh % 5 >= 2) continue;
            int thick = 1 + (int)(sh % 2);
            int sx = (int)(scol * 8 + 3 + sway * (1.0 + (sh % 3) * 0.4));
            if (dotx > sx || dotx + 1 < sx - thick) continue;
            int row = cy * 8 + iy;
            if (((row + (int)(sh % 9)) % 11) < 2) return BAMBOOPAL[7];
            return BAMBOOPAL[4 + (int)(sh % 4)];
        }
        return scalec(BAMBOOPAL[dither_band(band, cx, cy, ix, iy)], 0.42);
    }
    if (m_solar) {
        int band = tiles_[t].e[0] >> 4;
        int spot = solar_spot(cx, cy);
        if (spot) return spot == 2 ? (RGB){22, 7, 3} : (RGB){96, 32, 6};
        int rim = 0;
        uint32_t gr = solar_granule(cx, cy, &rim);
        int tb = dither_band(clampb(band - 1 + (int)(gr % 3) - 1), cx, cy, ix, iy);
        return SOLARPAL[rim ? clampb(tb - 3) : tb];
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
    if (m_streets || m_neurons || m_mycelium || m_delta || m_rail || m_loom) {
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

/* Publish generated artifacts through a same-directory temporary file.  The
 * final rename is atomic and replaces a symlink itself, so a failed render or
 * an unexpected link cannot leave a half-written destination behind. */
#define ARTIFACT_TEMP_CAP 544
static FILE *artifact_open(const char *path, char *temp, size_t temp_cap) {
    if (!path || !*path || !temp || temp_cap == 0) {
        errno = EINVAL;
        return NULL;
    }
    int n = snprintf(temp, temp_cap, "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= temp_cap) {
        errno = ENAMETOOLONG;
        if (temp_cap) temp[0] = 0;
        return NULL;
    }
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
    if (fd < 0) return NULL;
    FILE *fp = fdopen(fd, "wb");
    if (!fp) {
        int saved = errno;
        close(fd);
        unlink(temp);
        errno = saved;
    }
    return fp;
}

static void artifact_abort(FILE *fp, const char *temp) {
    int saved = errno;
    if (fp) fclose(fp);
    if (temp && *temp) unlink(temp);
    errno = saved;
}

static bool artifact_commit(FILE *fp, const char *temp, const char *path) {
    if (!fp || !temp || !*temp || !path || !*path) {
        artifact_abort(fp, temp);
        errno = EINVAL;
        return false;
    }
    bool ok = fflush(fp) == 0;
    if (fclose(fp) != 0) ok = false;
    if (!ok || rename(temp, path) != 0) {
        int saved = errno;
        unlink(temp);
        errno = saved;
        return false;
    }
    return true;
}

static bool save_bmp(const char *path) {
    const char *mode = mode_name();
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
    char temp[ARTIFACT_TEMP_CAP];
    FILE *fp = artifact_open(path, temp, sizeof temp);
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
        artifact_abort(fp, temp);
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
    if (ok) ok = artifact_commit(fp, temp, path);
    else artifact_abort(fp, temp);
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
    const char *mode = mode_name();
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
    char temp[ARTIFACT_TEMP_CAP];
    FILE *fp = artifact_open(path, temp, sizeof temp);
    if (!fp) { set_note("save failed: %s", strerror(errno)); buf_free(&o); return false; }
    bool ok = fwrite(o.b, 1, o.n, fp) == o.n;
    if (ok) ok = artifact_commit(fp, temp, path);
    else artifact_abort(fp, temp);
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
    if (solved && !strcmp(mode_name(), "terrain")) {
        carve_rivers(); g_river_show = n_river_;
    }
    if (solved && (!strcmp(mode_name(), "circuit") || !strcmp(mode_name(), "pipes")))
        label_components();
    return solved;
}
/* solve every mode tiny; cache one color per cell */
static RGB sheet_cell[NMODES][48][24];
static int sheet_w[NMODES], sheet_h[NMODES];
static bool sheet_ready_ = false;   /* sheet_cell[] holds solved previews */
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
        int art = strcmp(MODESPEC[mi].name, "terrain") ? 8 : 16;
        for (int y = 0; y < ph; y++)
            for (int x = 0; x < pw; x++) {
                int ix = art / 2, iy = art / 2;
                RGB c = solved ? img_px(x, y, ix, iy, art) : C_BG;
                sheet_cell[mi][y][x] = c;
            }
    }
    sheet_ready_ = true;
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
    const char *mode = mode_name();
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
        fb_puts(MODESPEC[mi].name);
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
    fb_puts(mode_name());
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
             ".toolbar{display:flex;gap:10px;align-items:center;margin:18px 0}.toolbar input,.toolbar select{"
             "background:#0f172a;color:#dbeafe;border:1px solid #334155;border-radius:7px;padding:8px 10px}"
             ".toolbar small{color:#64748b}"
             ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:18px}"
             ".card{background:#131926;border-radius:12px;overflow:hidden}"
             ".card img{width:100%;display:block}.cap{padding:10px 14px;font-size:13px;"
             "display:flex;justify-content:space-between}.cap b{color:#e2e8f0}</style></head><body>"
             "<h1>WAVE FUNCTION <span>COLLAPSE</span></h1>"
             "<p>procedural worlds grown by constraint propagation \u00b7 one file of C \u00b7 "
             "<code>make</code></p><div class='toolbar'><input id='filter' type='search' "
             "placeholder='filter mode, focus, or seed'><select id='sort'><option value='mode'>sort by mode</option>"
             "<option value='quality'>sort by quality</option></select><small id='count'></small></div>"
             "<div class='grid'>");
    int made = 0;
    for (int mi = 0; mi < NMODES; mi++)
        for (int si = 0; si < 3; si++) {
        bool solved = gallery_solve(mi, seeds[si], gw, gh);
        int art = strcmp(MODESPEC[mi].name, "terrain") ? 8 : 16;
        int f = strcmp(MODESPEC[mi].name, "terrain") ? 3 : 3;
        int pw, ph;
        uint8_t *rgb = raster_rgb(art, f, &pw, &ph);
        if (!rgb) {
            fprintf(stderr, "gallery: raster allocation failed for %s\n", MODESPEC[mi].name);
            continue;
        }
        if (!solved) fprintf(stderr, "gallery: solver failed for %s seed %llu\n",
                             MODESPEC[mi].name, (unsigned long long)seeds[si]);
        QualityMetrics gallery_quality = solved ? quality_measure(true) : (QualityMetrics){0};
        QualityHotspot gallery_hotspot = quality_hotspot();
        Buf img = png_bytes(rgb, pw, ph);
            free(rgb);
            char head[256];
            snprintf(head, sizeof head,
                     "<article class='card' data-mode='%s' data-seed='%llu' data-quality='%.3f' "
                     "data-focus='%s'><img alt='%s' src='data:image/png;base64,",
                     MODESPEC[mi].name, (unsigned long long)seeds[si],
                     gallery_quality.total, quality_profile().focus, MODESPEC[mi].name);
            buf_puts(&page, head);
            b64_append(&page, img.b, img.n);
            snprintf(head, sizeof head,
                     "'><div class='cap'><b>%s</b><span>seed %llu</span></div>"
                     "<div class='meta'><strong>quality %.3f</strong> - focus %s - hotspot %s %.3f</div></article>",
                     MODESPEC[mi].name, (unsigned long long)seeds[si],
                     gallery_quality.total, quality_profile().focus,
                     gallery_hotspot.reason, gallery_hotspot.score);
            buf_puts(&page, head);
            buf_free(&img);
            made++;
            fprintf(stderr, "\rgallery: %d/%d", made, NMODES * 3);
        }
    buf_puts(&page,
             "</div><script>const input=document.getElementById('filter'),sort=document.getElementById('sort'),"
             "grid=document.querySelector('.grid'),cards=[...document.querySelectorAll('.card')],count=document.getElementById('count');"
             "function refresh(){const q=input.value.toLowerCase();cards.forEach(c=>c.hidden=q&&!((c.dataset.mode+' '+c.dataset.focus+' '+c.dataset.seed).toLowerCase().includes(q)));"
             "const shown=cards.filter(c=>!c.hidden);count.textContent=shown.length+' of '+cards.length+' maps';"
             "cards.sort((a,b)=>sort.value==='quality'?Number(b.dataset.quality)-Number(a.dataset.quality):a.dataset.mode.localeCompare(b.dataset.mode)||Number(a.dataset.seed)-Number(b.dataset.seed));"
             "cards.forEach(c=>grid.appendChild(c));}input.addEventListener('input',refresh);sort.addEventListener('change',refresh);refresh();</script></body></html>");
    char temp[ARTIFACT_TEMP_CAP];
    FILE *fp = artifact_open(htmlpath, temp, sizeof temp);
    bool ok = false;
    if (fp) {
        bool wrote = fwrite(page.b, 1, page.n, fp) == page.n;
        if (wrote) ok = artifact_commit(fp, temp, htmlpath);
        else artifact_abort(fp, temp);
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
    int art = strcmp(mode_name(), "terrain") ? 8 : 16, f = 16 / art;
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
    char temp[ARTIFACT_TEMP_CAP];
    FILE *fp = artifact_open(path, temp, sizeof temp);
    if (fp) {
        bool wrote = fwrite(o.b, 1, o.n, fp) == o.n;
        if (wrote) ok = artifact_commit(fp, temp, path);
        else artifact_abort(fp, temp);
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
    int art = strcmp(mode_name(), "terrain") ? 8 : 16;
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

#endif
