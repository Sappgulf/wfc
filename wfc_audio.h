/* wfc_audio.h -- part of wfc, included by wfc.c.
 *
 * the procedural synth: per-world stingers, blips
 * and ambient drones, all derived from the registry
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `cc -O2 -std=c11 -o wfc wfc.c -lz` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_AUDIO_H
#define WFC_AUDIO_H
/* ---------------- tiny synth: procedural wav sfx via afplay ---------------- */
static char g_gallery_path[512] = {0};
static bool g_sfx_ready = false;
/* Cached wavs live in /tmp and outlive the binary, so the synth is versioned:
 * change the sound and the old files are no longer the ones we look for. */
#define SFX_VERSION 2
#define SFX_DONE "/tmp/wfx2_done.wav"
#define SFX_BLIP "/tmp/wfx2_blip.wav"

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
/* Two hand-kept tables of 25 used to carry the sound: stinger note rows and
 * drone roots, both indexed `mi % 25`. Eight worlds past the twenty-fifth
 * therefore played another world's music — storm sounded exactly like
 * circuit. Both are now derived from the registry's `tone` and `group`, so a
 * new world arrives with its own key and cannot borrow anyone else's. */
static double mode_hz(int mi, int octave) {
    if (mi < 0 || mi >= NMODES) mi = 0;
    return 110.0 * pow(2.0, MODESPEC[mi].tone / 12.0 + octave);
}

/* The chord shape says what kind of world it is: fields open out, connector
 * lattices move in fourths and fifths, carved worlds sit in a minor. */
static void mode_arpeggio(int mi, int steps[4]) {
    static const int FIELD[4]     = {0, 4, 7, 12};
    static const int CONNECTOR[4] = {0, 5, 7, 12};
    static const int CARVE[4]     = {0, 3, 7, 10};
    const int *shape = FIELD;
    if (mi >= 0 && mi < NMODES) {
        if (MODESPEC[mi].group == MG_CONNECTOR) shape = CONNECTOR;
        else if (MODESPEC[mi].group == MG_CARVE) shape = CARVE;
    }
    for (int i = 0; i < 4; i++) steps[i] = shape[i];
}

/* One struck voice: a couple of detuned partials over a short attack. Pure
 * sines read as a test tone; the detune and the odd harmonic give it body. */
static void voice_add(float *buf, int n, int start, double hz,
                      double amp, double decay) {
    const double SR = 44100.0;
    for (int i = start; i < n; i++) {
        double t = (double)(i - start) / SR;
        double env = exp(-t * decay) * (t < 0.006 ? t / 0.006 : 1.0);
        double v = sin(2 * M_PI * hz * t) +
                   0.45 * sin(2 * M_PI * hz * 2.0 * t + 0.7) +
                   0.22 * sin(2 * M_PI * hz * 3.0 * t + 1.9) +
                   0.30 * sin(2 * M_PI * hz * 1.005 * t);   /* slow beat */
        buf[i] += (float)(amp * env * v);
    }
}

static void ensure_sfx(void) {
    if (g_sfx_ready) return;
    const int SR = 44100;
    /* completion chord: A4 C#5 E5 A5 arpeggio with soft decay */
    static float done[44100 * 1];
    for (int i = 0; i < 44100; i++) done[i] = 0;
    int chord[4];
    mode_arpeggio(g_mode_idx, chord);
    double base = mode_hz(g_mode_idx, 2);
    for (int ni = 0; ni < 4; ni++)
        voice_add(done, 44100, ni * 6200,
                  base * pow(2.0, chord[ni] / 12.0), 0.135, 6.0);
    write_wav(SFX_DONE, done, 44100);
    /* click blip, pitched into the active world's key so it is not a beep */
    static float blip[2600];
    for (int i = 0; i < 2600; i++) blip[i] = 0;
    voice_add(blip, 2600, 0, mode_hz(g_mode_idx, 3), 0.16, 55.0);
    write_wav(SFX_BLIP, blip, 2600);
    /* per-world stingers, built from the registry's key and family */
    for (int mi2 = 0; mi2 < NMODES; mi2++) {
        static float st[44100 / 2];
        for (int i = 0; i < 22050; i++) st[i] = 0;
        int steps[4];
        mode_arpeggio(mi2, steps);
        double root = mode_hz(mi2, 2);          /* two octaves over the drone */
        bool descend = MODESPEC[mi2].group == MG_CARVE;
        for (int nn2 = 0; nn2 < 4; nn2++) {
            int step = steps[descend ? 3 - nn2 : nn2];
            double hz = root * pow(2.0, step / 12.0);
            voice_add(st, 22050, (int)(nn2 * 0.085 * SR), hz, 0.115, 8.5);
        }
        char p2[96];
        snprintf(p2, sizeof p2, "/tmp/wfx%d_st_%s.wav", SFX_VERSION, MODESPEC[mi2].name);
        write_wav(p2, st, 22050);
    }
    g_sfx_ready = true;
}
static void play_sfx(const char *path);
static void play_stinger(int mode_idx) {
    if (!g_sound) return;
    if (mode_idx < 0 || mode_idx >= NMODES) mode_idx = g_mode_idx;
    if (mode_idx < 0 || mode_idx >= NMODES) return;
    ensure_sfx();
    char p[96];
    snprintf(p, sizeof p, "/tmp/wfx%d_st_%s.wav", SFX_VERSION, MODESPEC[mode_idx].name);
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
        system("pkill -f 'afplay /tmp/wfx' >/dev/null 2>&1; "
               "pkill -f 'aplay -q /tmp/wfx' >/dev/null 2>&1;");
    g_amb_mode = -1;
    g_amb_t0 = 0;
}

static void ensure_ambient(int mi) {
    if (mi < 0 || mi >= NMODES) mi = g_mode_idx;
    if (mi < 0 || mi >= NMODES) return;
    char p[96];
    snprintf(p, sizeof p, "/tmp/wfx%d_amb_%s.wav", SFX_VERSION, MODESPEC[mi].name);
    FILE *probe = fopen(p, "rb");
    if (probe) { fclose(probe); return; } /* already synthesized */
    const int SR = 44100;
    int n = SR * AMBIENT_SECS;
    float *s = malloc(sizeof(float) * (size_t)n);
    if (!s) return;
    /* The drone sits in the same key as the world's stinger, an octave below,
     * with the chord voiced under it. Every LFO rate divides the loop length,
     * so the seam stays inaudible when the file retriggers. */
    double f = mode_hz(mi, 0);
    int chord[4];
    mode_arpeggio(mi, chord);
    double fifth = f * pow(2.0, chord[2] / 12.0);
    double colour = f * pow(2.0, chord[1] / 12.0);
    double lfo1 = 1.0 / AMBIENT_SECS, lfo2 = 3.0 / AMBIENT_SECS;
    double lfo3 = 2.0 / AMBIENT_SECS;
    double beat = 1.0 / AMBIENT_SECS; /* detune that wraps the seam */
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR;
        double env = 0.75 + 0.25 * sin(2 * M_PI * lfo1 * t) *
                     (0.8 + 0.2 * sin(2 * M_PI * lfo2 * t));
        /* the colour tone breathes in and out so the pad is not static */
        double swell = 0.5 + 0.5 * sin(2 * M_PI * lfo3 * t);
        double v = sin(2 * M_PI * f * t) +
                   0.55 * sin(2 * M_PI * fifth * t + 1.3) +
                   0.30 * sin(2 * M_PI * (f * 2 + beat) * t) +
                   0.45 * sin(2 * M_PI * f * 0.5 * t + 0.5) +
                   0.26 * swell * sin(2 * M_PI * colour * t + 2.1);
        s[i] = (float)(0.052 * env * v);
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
        snprintf(p, sizeof p, "/tmp/wfx%d_amb_%s.wav", SFX_VERSION, mode_name());
        play_sfx(p);
        g_amb_mode = g_mode_idx;
        g_amb_t0 = now;
        return;
    }
    if (g_amb_t0 && now - g_amb_t0 > AMBIENT_MS) { /* seamless-ish retrigger */
        char p[96];
        snprintf(p, sizeof p, "/tmp/wfx%d_amb_%s.wav", SFX_VERSION, mode_name());
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

static void term_fit_for(int term_cols, int term_rows) {
    int cw = strcmp(mode_name(), "terrain") ? 4 : 2;
    int ch = strcmp(mode_name(), "terrain") ? 2 : 1;
    int mw = 140, mh = 70; /* auto caps */
    if (term_cols > 8 && term_rows > 6) {
        if (term_cols / cw < mw) mw = term_cols / cw;
        if ((term_rows - 1) / ch < mh) mh = (term_rows - 1) / ch;
    }
    if (g_fullscreen && g_is_tty) {
        W_ = mw;
        H_ = mh;
    } else {
        W_ = g_user_w < mw ? g_user_w : mw;
        H_ = g_user_h < mh ? g_user_h : mh;
    }
    if (g_inf) { g_fit_w = W_; g_fit_h = H_; }
    if (g_quad && g_is_tty) { W_ /= 2; H_ /= 2; }
    else if (g_twin && g_is_tty) W_ /= 2; /* two worlds share the row */
    if (W_ < 4) W_ = 4;
    if (H_ < 4) H_ = 4;
}
static void term_fit(void) {
    struct winsize ws = {0};
    int term_cols = 0, term_rows = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_cols = ws.ws_col;
        term_rows = ws.ws_row;
    }
    term_fit_for(term_cols, term_rows);
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

#endif
