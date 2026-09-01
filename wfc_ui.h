/* wfc_ui.h -- part of wfc, included by wfc.c.
 *
 * time travel, the dungeon crawler, key handling,
 * the world picker, the observatory and the evolution lab
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `make` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_UI_H
#define WFC_UI_H
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
    if (strcmp(mode_name(), "dungeon") || g_nworlds > 1) { g_hero_on = false; return; }
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

static bool dispatch_hero_key(int c) {
    if (!g_hero_on || strcmp(mode_name(), "dungeon")) return false;
    if (c != 'w' && c != 'a' && c != 's' && c != 'd') return false;
    hero_move((char)c);
    return true;
}

/* ---------------- world snapshots ----------------
 * W/L stores the semantic grid state with an explicit format. Raw C
 * structs and host-endian integers made the old file silently unsafe to load
 * after truncation or on another architecture. */
#define WORLD_SAVE_MAGIC UINT32_C(0x31434657) /* bytes: WFC1 */
#define WORLD_SAVE_VERSION UINT32_C(1)
#define WORLD_SAVE_MAX_CELLS ((size_t)1000000)
#define WORLD_HASH_OFFSET UINT64_C(1469598103934665603)

static uint64_t world_hash_bytes(uint64_t hash, const unsigned char *bytes,
                                 size_t count) {
    for (size_t i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool world_write_bytes(FILE *f, const unsigned char *bytes, size_t count,
                              uint64_t *hash) {
    if (fwrite(bytes, 1, count, f) != count) return false;
    if (hash) *hash = world_hash_bytes(*hash, bytes, count);
    return true;
}

static bool world_read_bytes(FILE *f, unsigned char *bytes, size_t count,
                             uint64_t *hash) {
    if (fread(bytes, 1, count, f) != count) return false;
    if (hash) *hash = world_hash_bytes(*hash, bytes, count);
    return true;
}

static bool world_put_u32(FILE *f, uint32_t value, uint64_t *hash) {
    unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                              (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return world_write_bytes(f, bytes, sizeof bytes, hash);
}

static bool world_put_u64(FILE *f, uint64_t value, uint64_t *hash) {
    unsigned char bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = (unsigned char)(value >> (8 * i));
    return world_write_bytes(f, bytes, sizeof bytes, hash);
}

static bool world_get_u32(FILE *f, uint32_t *value, uint64_t *hash) {
    unsigned char bytes[4];
    if (!world_read_bytes(f, bytes, sizeof bytes, hash)) return false;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

static bool world_get_u64(FILE *f, uint64_t *value, uint64_t *hash) {
    unsigned char bytes[8];
    if (!world_read_bytes(f, bytes, sizeof bytes, hash)) return false;
    *value = 0;
    for (int i = 0; i < 8; i++) *value |= (uint64_t)bytes[i] << (8 * i);
    return true;
}

static bool world_save_dims(uint32_t w, uint32_t h, size_t *cells_out) {
    if (w == 0 || h == 0 || w > 1000 || h > 1000) return false;
    uint64_t cells = (uint64_t)w * h;
    if (cells > WORLD_SAVE_MAX_CELLS) return false;
    if (cells_out) *cells_out = (size_t)cells;
    return true;
}

typedef struct {
    uint32_t mode, w, h, ntiles, bias_milli, pin_count;
    uint64_t seed;
    size_t cells;
    uint64_t *domains;
    uint8_t *pins, *pin_tiles;
} WorldSnapshot;

static void world_snapshot_free(WorldSnapshot *snapshot) {
    if (!snapshot) return;
    free(snapshot->domains);
    free(snapshot->pins);
    free(snapshot->pin_tiles);
    memset(snapshot, 0, sizeof *snapshot);
}

/* A valid checksum only proves that the bytes agree with the checksum stored
 * in the same file.  Rebuild the selected mode's grammar and verify that a
 * snapshot is also a legal WFC state before exposing it to the live grid. */
static bool world_snapshot_semantically_valid(const WorldSnapshot *snapshot) {
    if (!snapshot || !snapshot->domains || snapshot->mode >= (uint32_t)NMODES ||
        snapshot->w == 0 || snapshot->h == 0 || snapshot->cells == 0)
        return false;

    int saved_mode = g_mode_idx, saved_w = W_, saved_h = H_;
    uint64_t saved_seed = g_seed;
    bool saved_seed_set = g_seed_set;
    double saved_bias = g_bias;
    g_seed = snapshot->seed;
    g_seed_set = true;
    g_bias = snapshot->bias_milli / 1000.0;
    setup_mode((int)snapshot->mode);
    W_ = (int)snapshot->w;
    H_ = (int)snapshot->h;

    bool valid = ntiles_ == (int)snapshot->ntiles;
    uint64_t full = valid ? wfc_core_full_mask((unsigned)ntiles_) : 0;
    for (size_t i = 0; valid && i < snapshot->cells; i++) {
        uint64_t domain = snapshot->domains[i];
        uint64_t initial = grid_cell_mask((int)(i % snapshot->w),
                                           (int)(i / snapshot->w));
        if (!wfc_core_domain_valid(domain, (unsigned)ntiles_) ||
            (domain & ~full) || (domain & ~initial)) {
            valid = false;
            break;
        }
        int x = (int)(i % snapshot->w), y = (int)(i / snapshot->w);
        for (int d = 0; d < NDIR && valid; d++) {
            int nx = x, ny = y;
            if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
            else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
            else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
            else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_) continue;
            uint64_t neighbor = snapshot->domains[IDX(nx, ny)];
            uint64_t allowed = 0, choices = domain;
            while (choices) {
                int tile = __builtin_ctzll(choices);
                allowed |= cdir_[d][tile];
                choices &= choices - 1;
            }
            if (!(neighbor & allowed)) valid = false;
        }
    }

    g_seed = saved_seed;
    g_seed_set = saved_seed_set;
    g_bias = saved_bias;
    W_ = saved_w;
    H_ = saved_h;
    setup_mode(saved_mode);
    return valid;
}

static bool world_save_file(const char *path) {
    if (!path || !*path || strlen(path) >= sizeof g_world_path ||
        W_ <= 0 || H_ <= 0 || ntiles_ <= 0 || ntiles_ > MAXT || !dom_ ||
        !studio_pin_ || !studio_tile_) return false;
    size_t cells;
    if (!world_save_dims((uint32_t)W_, (uint32_t)H_, &cells) ||
        !wfc_core_domains_valid(dom_, cells, (unsigned)ntiles_)) return false;

    uint32_t pin_count = 0;
    for (size_t i = 0; i < cells; i++) {
        if (!studio_pin_[i]) continue;
        if (pin_count == UINT32_MAX || !wfc_core_domain_valid(dom_[i], (unsigned)ntiles_) ||
            pc64(dom_[i]) != 1 || studio_tile_[i] >= (uint8_t)ntiles_ ||
            (uint32_t)__builtin_ctzll(dom_[i]) != studio_tile_[i]) return false;
        pin_count++;
    }

    char temp[sizeof g_world_path + 32];
    int temp_len = snprintf(temp, sizeof temp, "%s.tmp.%ld", path, (long)getpid());
    if (temp_len < 0 || (size_t)temp_len >= sizeof temp) return false;
    FILE *f = fopen(temp, "wb");
    if (!f) return false;

    uint64_t hash = WORLD_HASH_OFFSET;
    bool ok = world_put_u32(f, WORLD_SAVE_MAGIC, NULL) &&
              world_put_u32(f, WORLD_SAVE_VERSION, NULL) &&
              world_put_u32(f, (uint32_t)g_mode_idx, &hash) &&
              world_put_u32(f, (uint32_t)W_, &hash) &&
              world_put_u32(f, (uint32_t)H_, &hash) &&
              world_put_u32(f, (uint32_t)ntiles_, &hash) &&
              world_put_u64(f, g_seed, &hash) &&
              world_put_u32(f, (uint32_t)(g_bias * 1000.0 + 0.5), &hash) &&
              world_put_u32(f, (uint32_t)cells, &hash);
    for (size_t i = 0; ok && i < cells; i++) ok = world_put_u64(f, dom_[i], &hash);
    if (ok) ok = world_put_u32(f, pin_count, &hash);
    for (size_t i = 0; ok && i < cells; i++) {
        if (!studio_pin_[i]) continue;
        ok = world_put_u32(f, (uint32_t)i, &hash) &&
             world_put_u32(f, studio_tile_[i], &hash);
    }
    if (ok) ok = world_put_u64(f, hash, NULL);
    if (ok && fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(temp, path) != 0) {
        unlink(temp);
        return false;
    }
    return true;
}

static bool world_snapshot_read(const char *path, WorldSnapshot *snapshot) {
    if (!snapshot || !path || !*path || strlen(path) >= sizeof g_world_path) return false;
    *snapshot = (WorldSnapshot){0};
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint64_t *domains = NULL;
    uint8_t *pins = NULL, *pin_tiles = NULL;
    uint32_t magic = 0, version = 0, mode = 0, w = 0, h = 0;
    uint32_t saved_ntiles = 0, bias_milli = 0, saved_cells = 0, pin_count = 0;
    uint64_t seed = 0, stored_hash = 0;
    size_t cells = 0;
    uint64_t hash = WORLD_HASH_OFFSET;
    bool ok = world_get_u32(f, &magic, NULL) &&
              world_get_u32(f, &version, NULL) &&
              magic == WORLD_SAVE_MAGIC && version == WORLD_SAVE_VERSION &&
              world_get_u32(f, &mode, &hash) &&
              world_get_u32(f, &w, &hash) && world_get_u32(f, &h, &hash) &&
              world_get_u32(f, &saved_ntiles, &hash) &&
              world_get_u64(f, &seed, &hash) &&
              world_get_u32(f, &bias_milli, &hash) &&
              world_get_u32(f, &saved_cells, &hash) &&
              mode < (uint32_t)NMODES && saved_ntiles > 0 && saved_ntiles <= MAXT &&
              bias_milli >= 40 && bias_milli <= 960 &&
              world_save_dims(w, h, &cells) && saved_cells == (uint32_t)cells;
    if (!ok) goto world_load_fail;

    domains = malloc(sizeof *domains * cells);
    pins = calloc(cells, 1);
    pin_tiles = calloc(cells, 1);
    if (!domains || !pins || !pin_tiles) goto world_load_fail;
    for (size_t i = 0; i < cells; i++)
        if (!world_get_u64(f, &domains[i], &hash)) goto world_load_fail;
    if (!wfc_core_domains_valid(domains, cells, saved_ntiles) ||
        !world_get_u32(f, &pin_count, &hash) || pin_count > (uint32_t)cells)
        goto world_load_fail;
    for (uint32_t i = 0; i < pin_count; i++) {
        uint32_t cell = 0, tile = 0;
        if (!world_get_u32(f, &cell, &hash) || !world_get_u32(f, &tile, &hash) ||
            cell >= (uint32_t)cells || tile >= saved_ntiles || pins[cell] ||
            pc64(domains[cell]) != 1 ||
            (uint32_t)__builtin_ctzll(domains[cell]) != tile) goto world_load_fail;
        pins[cell] = 1;
        pin_tiles[cell] = (uint8_t)tile;
    }
    if (!world_get_u64(f, &stored_hash, NULL) || stored_hash != hash ||
        fgetc(f) != EOF || ferror(f)) goto world_load_fail;
    bool close_ok = fclose(f) == 0;
    f = NULL;
    if (!close_ok) goto world_load_fail;

    snapshot->mode = mode;
    snapshot->w = w;
    snapshot->h = h;
    snapshot->ntiles = saved_ntiles;
    snapshot->bias_milli = bias_milli;
    snapshot->pin_count = pin_count;
    snapshot->seed = seed;
    snapshot->cells = cells;
    snapshot->domains = domains;
    snapshot->pins = pins;
    snapshot->pin_tiles = pin_tiles;
    return true;

world_load_fail:
    if (f) fclose(f);
    free(domains); free(pins); free(pin_tiles);
    return false;
}

static bool world_inspect_file(const char *path) {
    WorldSnapshot snapshot = {0};
    if (!world_snapshot_read(path, &snapshot) ||
        !world_snapshot_semantically_valid(&snapshot)) {
        fprintf(stderr, "invalid world snapshot: %s\n", path ? path : "(null)");
        world_snapshot_free(&snapshot);
        return false;
    }
    size_t decided = wfc_core_count_singletons(snapshot.domains, snapshot.cells);
    printf("{\"format\":\"WFC1\",\"version\":%u,\"mode\":\"%s\","
           "\"dimensions\":{\"w\":%u,\"h\":%u},\"tiles\":%u,"
           "\"seed\":%llu,\"bias\":%.3f,\"pins\":%u,\"decided\":%zu}\n",
           WORLD_SAVE_VERSION, MODESPEC[snapshot.mode].name,
           snapshot.w, snapshot.h, snapshot.ntiles,
           (unsigned long long)snapshot.seed, snapshot.bias_milli / 1000.0,
           snapshot.pin_count, decided);
    world_snapshot_free(&snapshot);
    return true;
}

static bool world_load_file(const char *path) {
    WorldSnapshot snapshot = {0};
    if (!world_snapshot_read(path, &snapshot) ||
        !world_snapshot_semantically_valid(&snapshot)) {
        world_snapshot_free(&snapshot);
        return false;
    }

    int old_mode = g_mode_idx;
    uint64_t old_seed = g_seed;
    bool old_seed_set = g_seed_set;
    double old_bias = g_bias;
    g_seed = snapshot.seed;
    g_seed_set = true;
    setup_mode((int)snapshot.mode);
    if ((uint32_t)ntiles_ != snapshot.ntiles) {
        g_seed = old_seed;
        g_seed_set = old_seed_set;
        g_bias = old_bias;
        setup_mode(old_mode);
        world_snapshot_free(&snapshot);
        return false;
    }
    if (g_thermo) thermo_kill();
    hist_clear();
    W_ = (int)snapshot.w;
    H_ = (int)snapshot.h;
    g_user_w = W_;
    g_user_h = H_;
    grid_alloc(W_, H_);
    memcpy(dom_, snapshot.domains, sizeof *snapshot.domains * snapshot.cells);
    memcpy(studio_pin_, snapshot.pins, snapshot.cells);
    memcpy(studio_tile_, snapshot.pin_tiles, snapshot.cells);
    studio_pin_count_ = (int)snapshot.pin_count;
    g_bias = snapshot.bias_milli / 1000.0;
    apply_bias();
    macro_build();
    g_comp_ready = false;
    n_river_ = 0;
    g_river_show = 0;
    memset(river_, 0, snapshot.cells);
    for (size_t i = 0; i < snapshot.cells; i++) river_rank_[i] = -1;
    g_decided = (int)wfc_core_count_singletons(dom_, snapshot.cells);
    g_vx = g_vy = 0;
    full_repaint_ = true;
    quality_record(quality_measure(false));
    world_snapshot_free(&snapshot);
    return true;
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
        if (!strcmp(mode_name(), "circuit") || !strcmp(mode_name(), "pipes"))
            label_components();
        if (!strcmp(mode_name(), "terrain")) {
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
    int cw = strcmp(mode_name(), "terrain") ? 4 : 2;
    int ch = strcmp(mode_name(), "terrain") ? 2 : 1;
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
    if (propagate_from(cell)) { set_note("seeded (%d,%d)", cx, cy); if (g_sound) { ensure_sfx(); play_sfx(SFX_BLIP); } }
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

/* Re-score the current world with a small seed population. Evolution already
 * snapshots and replays the studio pins, so this is a safe observatory repair
 * action: it improves the weakest local quality signal without erasing the
 * user's authored constraints. */
static bool repair_hotspot(void) {
    if (g_thermo) {
        set_note("repair: turn thermo off first");
        return false;
    }
    if (g_nworlds > 1 || g_inf) {
        set_note("repair: classic single-world only");
        return false;
    }
    QualityHotspot hotspot = quality_hotspot();
    if (studio_pin_ && studio_pin_[IDX(hotspot.x, hotspot.y)])
        set_note("repair: hotspot is pinned; evolution will preserve it");
    return evolution_run(4);
}

static void toggle_fullscreen_fit(void) {
    if (g_thermo) thermo_kill();
    g_fullscreen = !g_fullscreen;
    apply_size();
    full_repaint_ = true;
    set_note("fullscreen fit %s [%dx%d]", g_fullscreen ? "ON" : "off", W_, H_);
}

/* Keys that arrive as escape sequences, mapped above the byte range.
 * Before this, ESC put the reader into the mouse state machine and any
 * cursor key left it stuck there until an unrelated 'M' or 32 more bytes
 * arrived — arrows jammed input entirely. */
enum { KEY_ESC = 256, KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };

static int pump_keys(bool tty) {
    if (!tty) return 0;
    int req = 0;
    unsigned char c;
    int key, pushback = -1;
    for (;;) {
        if (pushback >= 0) { key = pushback; pushback = -1; c = (unsigned char)key; }
        else {
        if (wfc_platform_read_input(STDIN_FILENO, &c, 1) != 1) break;
        key = c;
        if (mouse_esc == 1) {
            if (c == '[') { mouse_esc = 2; mouse_len = 0; continue; }
            /* a bare ESC: deliver it, then let the next byte stand on its own */
            mouse_esc = 0;
            pushback = c;
            key = KEY_ESC;
            c = 0;
            goto dispatch;
        }
        if (mouse_esc == 2) {
            if (c >= 0x40 && c <= 0x7E && c != 'M' && c != 'm') {
                mouse_esc = 0;              /* final byte of some other CSI */
                if (mouse_len != 0) continue;
                if (c == 'A') key = KEY_UP;
                else if (c == 'B') key = KEY_DOWN;
                else if (c == 'C') key = KEY_RIGHT;
                else if (c == 'D') key = KEY_LEFT;
                else continue;
                c = 0;
                goto dispatch;
            }
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
                        int cw2 = strcmp(mode_name(), "terrain") ? 4 : 2;
                        int ch2 = strcmp(mode_name(), "terrain") ? 2 : 1;
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
                continue;
            }
            if (mouse_len < (int)sizeof mouse_buf - 1) mouse_buf[mouse_len++] = (char)c;
            else mouse_esc = 0;
            continue;
        }
        if (c == 0x1b) {
            /* A lone ESC has to act immediately. Waiting for the byte that
             * would complete a CSI meant Esc did nothing at all until some
             * other key happened along — the picker would not close. Nothing
             * readable within a frame means the user pressed Escape. */
            struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
            if (poll(&pfd, 1, 25) <= 0) { key = KEY_ESC; c = 0; goto dispatch; }
            mouse_esc = 1;
            mouse_len = 0;
            continue;
        }
        }
    dispatch:
        (void)key;
        if (g_help) {
            g_help = false;
            fputs("\x1b[2J", stdout);
            if (c == 'q' || c == 3) req = req == 1 ? 1 : 2;
            continue;
        }
        if (g_picker) {
            int hits[NMODES];
            int n = picker_collect(hits, NMODES);
            if (key == KEY_ESC || c == 3) {
                g_picker = false;
                g_paused = g_picker_was_paused;
                full_repaint_ = true;
                fputs("\x1b[2J", stdout);
            } else if (key == KEY_UP) {
                if (n) g_picker_sel = (g_picker_sel + n - 1) % n;
            } else if (key == KEY_DOWN) {
                if (n) g_picker_sel = (g_picker_sel + 1) % n;
            } else if (c == '\r' || c == '\n') {
                g_picker = false;
                g_paused = g_picker_was_paused;
                if (n) {
                    int chosen = hits[g_picker_sel < n ? g_picker_sel : 0];
                    setup_mode(chosen);
                    mode_recent_push(chosen);
                    g_seed = rnd();
                    apply_size();
                    if (g_sound) play_stinger(g_mode_idx);
                    req = 1;
                }
                full_repaint_ = true;
                fputs("\x1b[2J", stdout);
            } else if (c == 'F') {
                if (n) {
                    int chosen = hits[g_picker_sel < n ? g_picker_sel : 0];
                    mode_favorite_toggle(chosen);
                    set_note("%s %s", mode_favorite(chosen) ? "favorited" : "unfavorited",
                             MODESPEC[chosen].name);
                }
            } else if (c == 127 || c == 8) {
                size_t len = strlen(g_picker_query);
                if (len) g_picker_query[len - 1] = 0;
                g_picker_sel = 0;
            } else if (c >= 32 && c < 127) {
                size_t len = strlen(g_picker_query);
                if (len + 1 < sizeof g_picker_query) {
                    g_picker_query[len] = (char)c;
                    g_picker_query[len + 1] = 0;
                    g_picker_sel = 0;
                }
            }
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
            } else if (c == 'x' || c == 'X') {
                g_observe = false;
                g_paused = true;
                if (repair_hotspot()) req = 1;
            } else if (c == 'F') {
                toggle_fullscreen_fit();
                req = 1;
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
            } else if (c == 'F') {
                toggle_fullscreen_fit();
                req = 1;
            } else if (c == 'q' || c == 3) {
                g_evolve_view = false;
                req = req == 1 ? 1 : 2;
            }
            continue;
        }
        if (c == '/' || c == 'M') {
            /* the picker is easier than reaching every world with `m` */
            g_picker_was_paused = g_paused;
            g_paused = true;
            g_picker = true;
            g_picker_query[0] = 0;
            g_picker_sel = g_mode_idx;
            if (!sheet_ready_) sheet_scan();   /* one small solve per world */
            full_repaint_ = true;
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
            if ((g_theme & 7) >= 4 && strcmp(mode_name(), "terrain")) g_theme = (g_theme & 7) >= 6 ? 0 : 4;
            BIOMES = BIOMES_SEASONAL[(g_theme & 7) >= 4 ? (g_theme & 7) - 4 : 0];
            set_note("theme %d", (g_theme & 7) + 1);
        }
        else if (dispatch_hero_key(c)) { }
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
        else if (c == 'w' || c == 's' || c == 'd') { }
        else if (c == 'k') { g_crt = !g_crt; set_note("CRT %s", g_crt ? "on" : "off"); }
        else if (c == 'W') {
            set_note(studio_session_save() ?
                     (g_session_enabled ? "session saved (%s)" : "world saved (%s)") :
                     "save failed (%s)",
                     g_world_path);
        }
        else if (c == 'L') {
            if (studio_session_load())
                set_note(g_session_enabled ? "session loaded (%d pins)" : "world loaded (%d pins)",
                         studio_pin_count_);
            else set_note("load failed or incompatible save");
        }
        else if (c == 'u') {
            if (undo_pop()) set_note("undid sculpt");
            else set_note("nothing to undo");
        }
        else if (c == 'o') {
            char code[96], cmd[160];
            snprintf(code, sizeof code, "wfc://%s/%llu", mode_name(),
                     (unsigned long long)g_seed);
#ifdef __APPLE__
            snprintf(cmd, sizeof cmd, "echo -n '%s' | pbcopy", code);
            system(cmd);
#endif
            set_note("%s (copied)", code);
        }
        else if (c == 'v') {
            const char *mode2 = mode_name();
            int art2 = strcmp(mode2, "terrain") ? 8 : 16;
            int f2 = strcmp(mode2, "terrain") ? 6 : 4;
            int pw2, ph2;
            uint8_t *rgb2 = raster_rgb(art2, f2, &pw2, &ph2);
            if (!rgb2) { set_note("clipboard shot too large"); continue; }
            Buf img2 = png_bytes(rgb2, pw2, ph2);
            free(rgb2);
            char temp2[ARTIFACT_TEMP_CAP];
            FILE *fp2 = artifact_open("/tmp/wfc_shot.png", temp2, sizeof temp2);
            bool shot_ok = false;
            if (fp2) {
                bool wrote = fwrite(img2.b, 1, img2.n, fp2) == img2.n;
                if (wrote) shot_ok = artifact_commit(fp2, temp2, "/tmp/wfc_shot.png");
                else artifact_abort(fp2, temp2);
            }
            buf_free(&img2);
            if (!shot_ok) { set_note("clipboard shot failed"); continue; }
#ifdef __APPLE__
            system("osascript -e 'set the clipboard to (read (POSIX file \"/tmp/wfc_shot.png\") as class PNGF)' >/dev/null 2>&1");
            set_note("map copied to clipboard");
#else
            set_note("saved /tmp/wfc_shot.png");
#endif
        }
        else if (c == 'f') { g_pan = !g_pan; set_note("drift %s", g_pan ? "on" : "off"); }
        else if (c == 'F') {
            toggle_fullscreen_fit();
            req = 1;
        }
        else if (c == 'T') {
            if (g_nworlds > 1)
                set_note("thermo: --twin/--quad would share one profile");
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
        else if (c == 'Y') {
            g_colorblind = !g_colorblind;
            full_repaint_ = true;
            set_note("colour assist %s", g_colorblind ? "ON" : "off");
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
        else if (c == 'm') { setup_mode(g_mode_idx + 1); mode_recent_push(g_mode_idx); g_seed = rnd(); g_paused = false; apply_size(); if (g_sound) play_stinger(g_mode_idx); req = 1; }
        else if (c == 's') {
            char path[512];
            if (!g_save_path[0])
                snprintf(path, sizeof path, "wfc-%s-%llu.png", mode_name(),
                         (unsigned long long)(g_seed % 100000000ULL));
            else snprintf(path, sizeof path, "%s", g_save_path);
            if (save_image(path)) set_note("saved %s (%dx%d)", path, W_, H_);
            else set_note("save failed: %s", path);
        }
    }
    return req;
}

static void msleep(double ms) {
    (void)wfc_platform_sleep_ms(ms);
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

#endif
