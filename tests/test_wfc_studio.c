#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#define main wfc_program_main
int wfc_program_main(int argc, char **argv);
#include "../wfc.c"
#undef main

int main(void) {
    g_seed = 7;
    g_seed_set = true;
    rs_ = g_seed ^ 0xD1B54A32D192ED03ULL;
    setup_mode(find_mode("delta"));

    g_is_tty = true;
    g_user_w = 36;
    g_user_h = 16;
    g_fullscreen = false;
    term_fit_for(202, 58);
    assert(W_ == 36 && H_ == 16);
    g_fullscreen = true;
    term_fit_for(202, 58);
    assert(W_ == 50 && H_ == 28);
    g_fullscreen = false;

    W_ = 8;
    g_pan = true;
    g_vx = 7;
    pan_camera_tick();
    assert(g_vx == 0);
    g_pan = false;

    W_ = 8;
    H_ = 6;
    grid_alloc(W_, H_);
    grid_reset();

    assert(strcmp(macro_name(), "delta-channel") == 0);
    assert(macro_guided_cells() > 0);
    QualityHotspot hotspot = quality_hotspot();
    assert(hotspot.x >= 0 && hotspot.x < W_);
    assert(hotspot.y >= 0 && hotspot.y < H_);
    assert(hotspot.score >= 0.0 && hotspot.score <= 1.0);

    int cell = IDX(3, 3);
    uint64_t original = dom_[cell];
    assert(original && pc64(original) > 1);
    assert(studio_pin_cell(3, 3));
    assert(studio_pin_[cell] == 1);
    assert(studio_pin_count_ == 1);
    assert(pc64(dom_[cell]) == 1);

    assert(studio_unpin_cell(3, 3));
    assert(studio_pin_[cell] == 0);
    assert(studio_pin_count_ == 0);
    assert(dom_[cell] != 0);

    assert(undo_pop());
    assert(studio_pin_[cell] == 1);
    assert(studio_pin_count_ == 1);
    assert(pc64(dom_[cell]) == 1);

    assert(undo_pop());
    assert(studio_pin_[cell] == 0);
    assert(studio_pin_count_ == 0);
    assert(dom_[cell] == original);

    char save_path[] = "/tmp/wfc-save-test-XXXXXX";
    int save_fd = mkstemp(save_path);
    assert(save_fd >= 0);
    close(save_fd);
    unlink(save_path);
    uint64_t saved_domain = dom_[0];
    assert(world_save_file(save_path));
    dom_[0] = 1;
    assert(world_load_file(save_path));
    assert(dom_[0] == saved_domain);
    FILE *bad_save = fopen(save_path, "wb");
    assert(bad_save);
    assert(fputs("not a wfc save", bad_save) >= 0);
    assert(fclose(bad_save) == 0);
    assert(!world_load_file(save_path));
    unlink(save_path);

    setup_mode(find_mode("dungeon"));
    W_ = 4;
    H_ = 4;
    grid_alloc(W_, H_);
    grid_reset();
    int hero_tile = -1, left_tile = -1;
    for (int t = 0; t < ntiles_; t++) {
        if (tiles_[t].e[3] == 2 && hero_tile < 0) hero_tile = t;
        if (tiles_[t].e[1] == 2 && left_tile < 0) left_tile = t;
    }
    assert(hero_tile >= 0 && left_tile >= 0);
    dom_[IDX(2, 2)] = (uint64_t)1 << hero_tile;
    dom_[IDX(1, 2)] = (uint64_t)1 << left_tile;
    g_hx = 2;
    g_hy = 2;
    g_hero_on = true;
    g_sound = false;
    assert(dispatch_hero_key('a'));
    assert(g_hx == 1 && g_hy == 2);
    assert(!g_sound);

    click_bufs_invalidate();
    free_world_buffers();
    free(river_);
    free(river_rank_);
    free(comp_);
    free(comp_col_);
    free(studio_pin_);
    free(studio_tile_);
    return 0;
}
