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
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
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

/* The program is one translation unit, split into parts purely so it can
 * be navigated. Order matters: each part depends on the ones above it. */
#include "wfc_world.h"
#include "wfc_render.h"
#include "wfc_export.h"
#include "wfc_audio.h"
#include "wfc_thermo.h"
#include "wfc_ui.h"

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
        if (!strcmp(name, MODESPEC[i].name)) return i;
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
        } else if (!strcmp(k, "colorblind")) {
            long value;
            if (parse_long_range(v, 0, 1, &value)) g_colorblind = value != 0;
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
    fprintf(f, "mode=%s\ntheme=%d\nspeed=%ld\nbias=%d\nsound=%d\ncrt=%d\nzen=%d\n"
               "colorblind=%d\n",
            mode_name(), g_theme & 7, g_speed,
            (int)(g_bias * 1000), (int)g_sound, (int)g_crt, (int)g_zen,
            (int)g_colorblind);
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
            for (int k = 0; k < NMODES; k++) if (!strcmp(m, MODESPEC[k].name)) found = k;
            if (found < 0) {
                fputs("mode must be one of:", stderr);
                for (int k = 0; k < NMODES; k++)
                    fprintf(stderr, "%s%s", k % 6 ? " " : "\n  ", MODESPEC[k].name);
                fputc('\n', stderr);
                return 2;
            }
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
        else if (!strcmp(argv[i], "--learned")) g_learned = true;
        else if (!strcmp(argv[i], "--colorblind")) g_colorblind = true;
        else if (!strcmp(argv[i], "--modes")) {
            static const char *FAMILY[] = {"field", "connector", "carve"};
            static const char *NOTE[12] = {"A", "A#", "B", "C", "C#", "D",
                                           "D#", "E", "F", "F#", "G", "G#"};
            for (int k = 0; k < NMODES; k++) {
                /* the drone root: A2 is 110 Hz, so tone 0 reads as A2 */
                int tone = MODESPEC[k].tone;
                int semi = ((tone % 12) + 12) % 12;
                int octave = 2 + (tone - semi) / 12;
                char key[8];
                snprintf(key, sizeof key, "%s%d", NOTE[semi], octave);
                printf("%-9s %-9s %-4s %s\n", MODESPEC[k].name,
                       FAMILY[MODESPEC[k].group], key, MODESPEC[k].blurb);
            }
            return 0;
        }
        else if (!strcmp(argv[i], "--density")) {
            long value;
            const char *v = NEXTV();
            if (!parse_long_range(v, 4, 96, &value)) {
                fprintf(stderr, "invalid value for --density: %s (expected 4-96)\n", v);
                return 2;
            }
            g_bias = value / 100.0;
        }
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
        else if (!strcmp(argv[i], "--fullscreen")) g_fullscreen = true;
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
            for (int k = 0; k < NMODES; k++) printf("%s\n", MODESPEC[k].name);
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
                   "  --mode NAME                      tileset, one of %d worlds:\n", NMODES);
            /* wrapped straight from the registry, so --help cannot go stale */
            for (int m = 0; m < NMODES; m += 6) {
                fputs("                                   ", stdout);
                for (int k = m; k < m + 6 && k < NMODES; k++)
                    printf("%s%s", k > m ? " " : "", MODESPEC[k].name);
                fputc('\n', stdout);
            }
            printf("  --w N --h N                      grid cells (default: auto-fit window)\n"
                   "  --seed N|word                    rng seed (words are hashed)\n"
                   "  --speed N                        collapse steps/sec (default 1600)\n"
                   "  --save FILE.png|.bmp             auto-save map on completion\n"
                   "  --report FILE.json               quality + thermo + studio report\n"
                   "  --solver classic|thermo          collapse engine (thermo = adaptive sidecar)\n"
                   "  --no-learn                       disable persistent thermo preferences\n"
                   "  --thermo-profile DIR             store thermo profiles in DIR\n"
                   "  --reset-learning                 clear the active thermo profile\n"
                   "  --learned                        let the classic solver use the learned profile\n"
                   "  --colorblind                     separate red/green for deuteranopia (same as Y)\n"
                   "                                   THRML optional; bounded proposals always work\n"
                   "  --gif FILE.gif                   record collapse animation\n"
                   "  --cycle                          auto-cycle modes forever (screensaver)\n"
                   "  --zen                            worlds dissolve into each other, never restarts\n"
                   "  --theme N                        color theme 0-7 (same as y)\n"
                   "  --list-modes                     print all mode names and exit\n"
                   "  --modes                          print each world with its blurb\n"
                   "  --density N                      how full a world packs, 4-96 (same as [ ])\n"
                   "  --sound                          synth sound effects (afplay)\n"
                   "  --gallery FILE.html              render all modes to a web showcase\n"
                   "  --gfx / --no-gfx                 inline-image rendering (iTerm2/WezTerm/kitty)\n"
                   "  --link wfc://mode/seed           replay a shared world\n"
                   "  --pan                            drift the camera after solving\n"
                   "  --fullscreen                     fill the live terminal viewport (interactive)\n"
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
                   "keys: space new | / pick world | m next mode | y theme | c cycle\n"
                   "      [ ] density | Y red/green assist | a audio | h help\n"
                   "      i iso | , . scrub collapse | wasd hero in dungeon\n"
                   "      l observatory | Q heatmap | E evolution | P pin/unpin hover\n"
                   "      F fullscreen fit | +/- speed p pause g gif s save q quit\n"
                   "build: cc -O2 -std=c11 -o wfc wfc.c -lz\n");
            return 0;
        }
        else { fprintf(stderr, "unknown arg %s (try --help)\n", argv[i]); return 2; }
    }

    if (g_nworlds > 1 && g_inf) {
        fprintf(stderr, "--infinite cannot be combined with --twin or --quad\n");
        return 2;
    }
    if (g_thermo && g_nworlds > 1) {
        fprintf(stderr, "--solver thermo cannot be combined with --twin or --quad: they run one\n"
                "mode across several rng streams and would share a single profile\n");
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
            bool is_terrain = !strcmp(MODESPEC[mi].name, "terrain");
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
            printf("%-9s %8d %10ld %8.1f %7d\n", MODESPEC[mi].name, N,
                   tot_steps / N, tot_ms / N, fails);
        }
        return 0;
    }

    bool tty = isatty(STDOUT_FILENO);
    if (!tty) clock_freeze_for_export();
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
                    thermo_wait_readable(250);
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
            if (!strcmp(mode_name(), "circuit") || !strcmp(mode_name(), "pipes")) label_components();
            if (!strcmp(mode_name(), "terrain")) { carve_rivers(); g_river_show = n_river_; }
        }
        if (g_gif_on) {
            capture_frame();
            if (g_gif_path[0]) snprintf(gp, sizeof gp, "%s", g_gif_path);
            else snprintf(gp, sizeof gp, "wfc-%s-%llu.gif", mode_name(),
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
               mode_name(), W_, H_, (unsigned long long)g_seed, tries, total,
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
            if (g_winch) {
                g_winch = 0;
                if (g_thermo) thermo_kill();
                apply_size();
                req = 1;
            }
            if (req == 2) g_stop = 1;
            if (g_stop) break;
            if (req == 1) break;
            if (g_help) { if (!g_gfx) render_help(); msleep(50); continue; }
            if (g_picker) { if (!g_gfx) render_picker(); msleep(50); continue; }
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
            if (g_sound) { ensure_sfx(); play_sfx(SFX_DONE); }
            int is_terrain = !strcmp(mode_name(), "terrain");
            load_world(0);
            quality_record(quality_measure(true));
            if ((!strcmp(mode_name(), "circuit") || !strcmp(mode_name(), "pipes")) && !g_stop && g_nworlds == 1) label_components();
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
            if (!strcmp(mode_name(), "dungeon") && !g_stop) hero_spawn();
            if (g_gif_on) {
                if (g_zen && !g_inf) {
                    /* zen: keep rolling across morphs; one big loop written on quit */
                    if (g_nframes < 480) capture_frame();
                } else {
                    if (g_inf) frames_clear();
                    capture_frame();
                    char gp[512];
                    if (g_gif_path[0]) snprintf(gp, sizeof gp, "%s", g_gif_path);
                    else snprintf(gp, sizeof gp, "wfc-%s-%llu.gif", mode_name(),
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
            bool anim = !strcmp(mode_name(), "storm") || !strcmp(mode_name(), "solar")
                        || !strcmp(mode_name(), "vinyl")
                        || !strcmp(mode_name(), "bamboo")
                        || !strcmp(mode_name(), "fire") || !strcmp(mode_name(), "waves")
                        || !strcmp(mode_name(), "galaxy") || !strcmp(mode_name(), "city") || !strcmp(mode_name(), "aurora");
            double linger = anim ? 4500 : 1800;
            bool cut_short = false;
            while (!g_stop && now_ms() - t0 < linger) {
                /* The return value used to be dropped here, so every key that
                 * asks for something — q to quit, space for a new world, m for
                 * the next one — was swallowed for the whole linger while the
                 * finished world sat on screen. It reads as the app ignoring
                 * you, because it is. */
                int lreq = pump_keys(true);
                if (lreq == 2) { g_stop = 1; break; }
                if (lreq == 1) { cut_short = true; break; }
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
            if (cut_short) continue;      /* the user asked to move on */
            if (g_zen && !g_inf && g_nworlds == 1 && !g_stop) {
                zen_capture(); /* solved world lingers; the next one dissolves in */
                if (!g_cycle) g_seed = rnd();
            }
            if (g_inf && !g_stop) {
                if (world_grow()) {
                    /* the worker's spec is bound to the old dimensions */
                    if (g_thermo) thermo_kill();
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
                set_note("%s", mode_name());
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
