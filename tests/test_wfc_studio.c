#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
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

    char config_dir[] = "/tmp/wfc-config-test-XXXXXX";
    assert(mkdtemp(config_dir));
    char config_sentinel[256], config_path[256];
    assert(snprintf(config_sentinel, sizeof config_sentinel, "%s/sentinel", config_dir) > 0);
    assert(snprintf(config_path, sizeof config_path, "%s/.wfcrc", config_dir) > 0);
    FILE *config_file = fopen(config_sentinel, "w");
    assert(config_file);
    assert(fputs("keep me\n", config_file) >= 0);
    assert(fclose(config_file) == 0);
    assert(symlink(config_sentinel, config_path) == 0);
    assert(setenv("HOME", config_dir, 1) == 0);
    cfg_save();
    struct stat config_stat;
    assert(lstat(config_path, &config_stat) == 0 && !S_ISLNK(config_stat.st_mode));
    config_file = fopen(config_sentinel, "r");
    assert(config_file);
    char config_line[32] = {0};
    assert(fgets(config_line, sizeof config_line, config_file));
    assert(fclose(config_file) == 0);
    assert(strcmp(config_line, "keep me\n") == 0);
    unlink(config_path);
    unlink(config_sentinel);
    assert(rmdir(config_dir) == 0);

    char wav_dir[] = "/tmp/wfc-wav-test-XXXXXX";
    assert(mkdtemp(wav_dir));
    char wav_sentinel[256], wav_path[256];
    assert(snprintf(wav_sentinel, sizeof wav_sentinel, "%s/sentinel", wav_dir) > 0);
    assert(snprintf(wav_path, sizeof wav_path, "%s/out.wav", wav_dir) > 0);
    FILE *wav_file = fopen(wav_sentinel, "w");
    assert(wav_file);
    assert(fputs("keep me\n", wav_file) >= 0);
    assert(fclose(wav_file) == 0);
    assert(symlink(wav_sentinel, wav_path) == 0);
    float sample = 0.0f;
    write_wav(wav_path, &sample, 1);
    assert(lstat(wav_path, &config_stat) == 0 && !S_ISLNK(config_stat.st_mode));
    wav_file = fopen(wav_sentinel, "r");
    assert(wav_file);
    memset(config_line, 0, sizeof config_line);
    assert(fgets(config_line, sizeof config_line, wav_file));
    assert(fclose(wav_file) == 0);
    assert(strcmp(config_line, "keep me\n") == 0);
    wav_file = fopen(wav_path, "rb");
    assert(wav_file);
    char riff[4];
    assert(fread(riff, 1, sizeof riff, wav_file) == sizeof riff);
    assert(fclose(wav_file) == 0);
    assert(memcmp(riff, "RIFF", sizeof riff) == 0);
    unlink(wav_path);
    unlink(wav_sentinel);
    assert(rmdir(wav_dir) == 0);

    char session_path[] = "/tmp/wfc-session-test-XXXXXX";
    int session_fd = mkstemp(session_path);
    assert(session_fd >= 0);
    close(session_fd);
    unlink(session_path);
    assert(snprintf(g_world_path, sizeof g_world_path, "%s", session_path) > 0);
    assert(snprintf(g_session_meta_path, sizeof g_session_meta_path, "%s.meta", session_path) > 0);
    g_session_enabled = true;
    g_theme = 5;
    g_speed = 321;
    g_bias = 0.73;
    g_sound = true;
    g_crt = true;
    g_zen = true;
    g_colorblind = true;
    g_pan = true;
    g_heatmap = true;
    g_entropy_view = true;
    assert(studio_session_save());
    g_theme = 0;
    g_speed = 1600;
    g_bias = 0.5;
    g_sound = false;
    g_crt = false;
    g_zen = false;
    g_colorblind = false;
    g_pan = false;
    g_heatmap = false;
    g_entropy_view = false;
    assert(studio_session_load());
    assert(g_theme == 5 && g_speed == 321 && g_bias > 0.729 && g_bias < 0.731);
    assert(g_sound && g_crt && g_zen && g_colorblind && g_pan && g_heatmap && g_entropy_view);
    unlink(g_session_meta_path);
    unlink(g_world_path);
    g_session_enabled = false;
    g_session_meta_path[0] = 0;
    snprintf(g_world_path, sizeof g_world_path, "/tmp/wfc_world.bin");

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
