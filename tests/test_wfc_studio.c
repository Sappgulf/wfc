#include <assert.h>
#include <stdint.h>

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
