/* wfc_thermo.h -- part of wfc, included by wfc.c.
 *
 * the thermodynamic sidecar bridge: the JSONL protocol,
 * the counterfactual guard, and the learned-profile loader
 *
 * wfc is deliberately one translation unit: wfc.c includes these parts in
 * order, so `make` still builds the whole thing
 * with no build system. They are cut at the section boundaries that were
 * already there, in the order the compiler saw them, so the token stream is
 * unchanged -- these are not independent modules and have no include guards
 * of their own beyond the one below.
 */
#ifndef WFC_THERMO_H
#define WFC_THERMO_H
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
static bool thermo_failed_ = false;
static double thermo_beta_ = 0;
static double thermo_confidence_ = 0;
static double thermo_energy_ = 0;
static bool thermo_energy_valid_ = false;
static long thermo_observations_ = 0;
static long thermo_round_ = 0;
static long thermo_proposals_ = 0;
static long thermo_accepts_ = 0;
static long thermo_rejects_ = 0;
static long thermo_contradictions_ = 0;
static double thermo_quality_ = 0;
static char thermo_sampler_[8] = "idle";
static long thermo_displaced_ = 0;
static uint64_t *thermo_snap_ = NULL;
static size_t thermo_snap_cap_ = 0;
static uint64_t *thermo_try_ = NULL;
static size_t thermo_try_cap_ = 0;
static long thermo_stall_rounds_ = 0;
static bool thermo_last_progress_ = false;
static bool thermo_stalled_ = false;
#define THERMO_STALL_LIMIT 32

/* the reader's partial-line buffer; a kill has to clear it, see below */
static char *thermo_lbuf = NULL;
static size_t thermo_lbl = 0, thermo_lcap = 0;

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
    thermo_last_progress_ = false;
    /* the guard's scratch grids are sized to a world that is now gone */
    free(thermo_snap_); thermo_snap_ = NULL; thermo_snap_cap_ = 0;
    free(thermo_try_);  thermo_try_ = NULL;  thermo_try_cap_ = 0;
    /* Drop whatever half a line the dead worker left behind. Without this the
     * next worker's first bytes are appended to that stale prefix and parse as
     * one spliced frame — which is how a relaunch after --infinite grew the
     * world produced a cfg of the wrong length and failed the whole solver.
     * Any relaunch hits this: toggling T off and on, or --reset-learning. */
    thermo_lbl = 0;
}

static const char *thermo_backend_name(void) {
    if (!g_thermo) return "classic";
    if (!strcmp(thermo_sampler_, "python")) return "python-bounded";
    if (!strcmp(thermo_sampler_, "thrml")) return "thrml-jax";
    return "thermo-sidecar";
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
    /* --twin/--quad run one mode across several rng streams, so a sidecar per
     * world would contend over a single profile file. --infinite is one world
     * and one profile: it regrows the grid, and the worker is re-initialised
     * against the new one. */
    if (g_gallery_path[0] || g_collage_path[0] || g_nworlds > 1) return false;
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
    thermo_failed_ = false;
    g_thermo_reset_learning = false;
    thermo_launches_++;
    thermo_valid_ = false; /* a fresh run must earn its own result */
    thermo_beta_ = 0;
    thermo_confidence_ = 0;
    thermo_energy_ = 0;
    thermo_energy_valid_ = false;
    thermo_bad_ = -1;
    thermo_observations_ = 0;
    thermo_round_ = 0;
    thermo_proposals_ = 0;
    thermo_accepts_ = 0;
    thermo_rejects_ = 0;
    thermo_displaced_ = 0;
    thermo_contradictions_ = 0;
    thermo_quality_ = 0;
    thermo_stall_rounds_ = 0;
    thermo_stalled_ = false;
    snprintf(thermo_sampler_, sizeof thermo_sampler_, "boot");
    signal(SIGPIPE, SIG_IGN);

    fprintf(thermo_in_, "{\"v\":1,\"t\":\"init\",\"mode\":");
    thermo_json_string(thermo_in_, mode_name());
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
    fputs("],\"tile_openness\":[", thermo_in_);
    for (int i = 0; i < ntiles_; i++)
        fprintf(thermo_in_, "%s%.9g", i ? "," : "", tile_openness(i));
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
    errno = 0;
    char *end = NULL;
    long value = strtol(p, &end, 10);
    return errno == ERANGE || end == p ? fallback : value;
}

static bool json_double_set(const char *s, const char *key, double *out) {
    const char *p = json_str(s, key);
    if (!p || !out) return false;
    errno = 0;
    char *end = NULL;
    double value = strtod(p, &end);
    if (errno == ERANGE || end == p || !isfinite(value)) return false;
    *out = value;
    return true;
}

static double json_double(const char *s, const char *key, double fallback) {
    double value = fallback;
    return json_double_set(s, key, &value) ? value : fallback;
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

/* `margin` is the guard's verdict: how far the sidecar's own result landed
 * above (or below) the tiles the classic heuristic would have placed. It is
 * the one number that tells the learner whether the proposal was actually
 * worth taking, so it is folded into the reward. Zero when no guard ran. */
static bool thermo_send_feedback(const int *cells, const int *tiles, int count,
                                 int accepted, int rejected, int contradictions,
                                 QualityMetrics before, QualityMetrics after,
                                 double margin) {
    if (!thermo_in_) return false;
    double reward = quality_reward(before, after, accepted, rejected);
    reward = quality_signed_clamp(reward + 6.0 * margin);
    QualityProfile profile = quality_profile();
    thermo_quality_ = after.total;
    fprintf(thermo_in_, "{\"v\":1,\"t\":\"feedback\",\"reward\":%.9g,"
                     "\"quality\":%.9g,\"accepted\":%d,\"rejected\":%d,"
                     "\"contradictions\":%d,\"margin\":%.9g,\"quality_focus\":",
            reward, after.total, accepted, rejected, contradictions, margin);
    thermo_json_string(thermo_in_, profile.focus);
    fputs(",\"metrics\":", thermo_in_);
    quality_json(thermo_in_, after);
    fputs(",\"metrics_delta\":", thermo_in_);
    quality_delta_json(thermo_in_, before, after);
    /* Events mark which features this round used; the reward carries whether
     * that was a good idea. Signing the feature by acceptance too made the two
     * negatives cancel — a displaced proposal *raised* the bias of the tile
     * that lost, so every bias drifted up together and a uniform shift is
     * invisible to the softmax. Presence only. */
    fputs(",\"tile_events\":[", thermo_in_);
    for (int i = 0; i < count; i++)
        fprintf(thermo_in_, "%s{\"index\":%d,\"value\":1}", i ? "," : "", tiles[i]);
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
            if (!compatible) continue;   /* hard constraint, not a preference */
            fprintf(thermo_in_, "%s{\"index\":%d,\"value\":1}", first ? "" : ",", pair);
            first = false;
        }
    }
    fputs("],\"context_events\":[", thermo_in_);
    for (int i = 0; i < count; i++)
        fprintf(thermo_in_, "%s{\"index\":%d,\"value\":1}",
                i ? "," : "", thermo_context_index(cells[i]));
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
    QualityMetrics after = ok ? quality_measure(false) : before;
    double margin = 0.0;
    bool kept_baseline = false;
    if (ok) {
        /* counterfactual guard: keep the sidecar's assignment only when it
         * beats the tiles the classic heuristic would have laid in the same
         * cells. legal-but-bland proposals otherwise displace better classic
         * picks and drag --solver thermo below --solver classic. */
        if (thermo_try_cap_ < cells_n) {
            uint64_t *next = realloc(thermo_try_, sizeof(uint64_t) * cells_n);
            if (!next) {
                memcpy(dom_, thermo_snap_, sizeof(uint64_t) * cells_n);
                return false;
            }
            thermo_try_ = next;
            thermo_try_cap_ = cells_n;
        }
        memcpy(thermo_try_, dom_, sizeof(uint64_t) * cells_n);
        memcpy(dom_, thermo_snap_, sizeof(uint64_t) * cells_n);
        /* the baseline draw runs on a derived stream and restores rs_, so the
         * main rng sequence is byte-identical with and without the guard. */
        uint64_t saved_rs = rs_;
        rs_ ^= 0xD1B54A32D192ED03ULL * (uint64_t)(thermo_round_ + 1);
        bool base_ok = true;
        for (int i = 0; i < count; i++) {
            uint64_t m = dom_[cells[i]];
            if (!m) { base_ok = false; break; }
            int tile = pc64(m) == 1 ? __builtin_ctzll(m) : weighted_pick_at(m, cells[i]);
            dom_[cells[i]] = (uint64_t)1 << tile;
            if (!propagate_from(cells[i])) { base_ok = false; break; }
        }
        QualityMetrics base = base_ok ? quality_measure(false) : before;
        rs_ = saved_rs;
        margin = base_ok ? quality_objective(after) - quality_objective(base) : 0.0;
        /* ties go to the sidecar, so learning keeps receiving signal */
        if (margin >= 0.0) {
            memcpy(dom_, thermo_try_, sizeof(uint64_t) * cells_n);
        } else {
            accepted = 0;
            rejected = count;
            thermo_displaced_ += count;
            kept_baseline = true;
        }
    }
    thermo_round_++;
    thermo_proposals_ += count;
    thermo_accepts_ += accepted;
    thermo_rejects_ += rejected;
    thermo_contradictions_ += contradictions;
    g_decided = 0;
    for (size_t i = 0; i < cells_n; i++) if (pc64(dom_[i]) == 1) g_decided++;
    /* the live trace follows the grid, which is the baseline when displaced */
    quality_record(kept_baseline ? quality_measure(false) : after);
    /* The learner has to see the consequence of *its* proposal. When the guard
     * kept the classic result, dom_ holds the baseline, so its metrics and its
     * neighbour tiles would be reported as the sidecar's own outcome. Build the
     * frame against the sidecar's state and put the winner back afterwards. */
    bool sent;
    if (kept_baseline) {
        uint64_t *winner = malloc(sizeof(uint64_t) * cells_n);
        if (!winner) return false;
        memcpy(winner, dom_, sizeof(uint64_t) * cells_n);
        memcpy(dom_, thermo_try_, sizeof(uint64_t) * cells_n);
        sent = thermo_send_feedback(cells, tiles, count, accepted, rejected,
                                    contradictions, before, after, margin);
        memcpy(dom_, winner, sizeof(uint64_t) * cells_n);
        free(winner);
    } else {
        sent = thermo_send_feedback(cells, tiles, count, accepted, rejected,
                                    contradictions, before, after, margin);
    }
    thermo_last_progress_ = memcmp(thermo_snap_, dom_, sizeof(uint64_t) * cells_n) != 0;
    return sent;
}

static bool thermo_apply_cfg(const char *s) {
    const char *p = json_str(s, "cfg");
    if (!p || *p != '[') return false;
    size_t cells = (size_t)W_ * (size_t)H_;
    uint64_t *next = calloc(cells, sizeof *next);
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
        if (!(dom_[i] & mask) || !wfc_core_domain_valid(mask, (unsigned)ntiles_)) {
            free(next);
            return false;
        }
        next[i] = mask;
        p = end;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ']') { free(next); return false; }

    /* The sidecar is allowed to propose, never to redefine hard WFC rules.
     * Re-check every directed neighbor pair before accepting its completion. */
    for (size_t i = 0; i < cells; i++) {
        int x = (int)(i % (size_t)W_), y = (int)(i / (size_t)W_);
        if (!next[i] || (next[i] & (next[i] - 1))) { free(next); return false; }
        int source = __builtin_ctzll(next[i]);
        for (int d = 0; d < NDIR; d++) {
            int nx = x, ny = y;
            if (d == 0) ny = g_torus ? (y + H_ - 1) % H_ : y - 1;
            else if (d == 1) nx = g_torus ? (x + 1) % W_ : x + 1;
            else if (d == 2) ny = g_torus ? (y + 1) % H_ : y + 1;
            else nx = g_torus ? (x + W_ - 1) % W_ : x - 1;
            if (nx < 0 || ny < 0 || nx >= W_ || ny >= H_ || (nx == x && ny == y)) continue;
            uint64_t target_domain = next[IDX(nx, ny)];
            if (!target_domain || (target_domain & (target_domain - 1))) {
                free(next);
                return false;
            }
            int target = __builtin_ctzll(target_domain);
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

/* Sleep until the worker has a line for us, or `ms` elapses. Headless runs
 * have no frame to draw between rounds, so a fixed usleep() spent most of the
 * solve waiting on a child that had already answered. */
static void thermo_wait_readable(int ms) {
    if (thermo_fd_ < 0) { usleep((useconds_t)ms * 1000); return; }
    struct pollfd pfd = {.fd = thermo_fd_, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, ms) < 0 && errno != EINTR) usleep((useconds_t)ms * 1000);
}

/* Poll the long-lived thermo worker: 0 in progress, 1 solved, -1 failed. */
static int thermo_poll(void) {
    bool stalled;
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
                    if (thermo_lcap >= (1u << 25)) goto thermo_fail;
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
                        thermo_round_ = json_num(s, "round", thermo_round_);
                        thermo_beta_ = json_double(s, "beta", thermo_beta_);
                        thermo_confidence_ = json_double(s, "confidence", thermo_confidence_);
                        if (json_double_set(s, "energy", &thermo_energy_))
                            thermo_energy_valid_ = true;
                        thermo_bad_ = json_num(s, "bad", thermo_bad_);
                    } else if (!strncmp(tp + 1, "proposal", 8) && tp[9] == '"') {
                        thermo_bad_ = json_num(s, "bad", thermo_bad_);
                        if (json_double_set(s, "energy", &thermo_energy_))
                            thermo_energy_valid_ = true;
                        if (!thermo_waiting_sample_ || !thermo_apply_patch(s)) goto thermo_fail;
                        thermo_waiting_sample_ = false;
                        if (thermo_last_progress_) {
                            thermo_stall_rounds_ = 0;
                        } else if (++thermo_stall_rounds_ >= THERMO_STALL_LIMIT) {
                            thermo_stalled_ = true;
                            goto thermo_fail;
                        }
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
                        if (json_double_set(s, "energy", &thermo_energy_))
                            thermo_energy_valid_ = true;
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
    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        goto thermo_fail;
    if (got == 0) { /* child closed the pipe */
        if (thermo_valid_) { thermo_kill(); return 1; }
        goto thermo_fail;
    }
    return 0;
thermo_fail:
    stalled = thermo_stalled_;
    thermo_failed_ = true;
    thermo_kill();
    if (stalled) {
        set_note("thermo stalled \xe2\x80\x94 classic solver");
        fprintf(stderr, "thermo stalled - classic solver\n");
    } else {
        set_note("thermo failed \xe2\x80\x94 classic solver");
    }
    return -1;
}

/* The HUD accent used to come from a table of eight, wrapped by mode index,
 * so every eighth world shared a colour and a new world inherited whichever
 * slot it landed in. It is derived from the world's own identity instead: the
 * chromatic circle of its key mapped onto the colour wheel, its family in the
 * saturation, its octave in the brightness. Each world gets its own, and none
 * of it can go stale. */
static RGB mode_accent(void) {
    const ModeSpec *spec = mode_spec();
    int semi = ((spec->tone % 12) + 12) % 12;
    int octave = 2 + (spec->tone - semi) / 12;
    double hue = semi * 30.0;
    double sat = spec->group == MG_CONNECTOR ? 0.58
               : spec->group == MG_CARVE     ? 0.44
                                             : 0.70;
    double val = 0.74 + 0.07 * (octave < 1 ? 1 : octave > 4 ? 4 : octave);
    return hsv(hue, sat, val > 1.0 ? 1.0 : val);
}

static const char *thermo_phase(void) {
    if (!g_thermo) return "classic";
    if (thermo_failed_) return "error";
    if (thermo_valid_) return "done";
    if (!thermo_inflight_) return "idle";
    if (!thermo_ready_) return "boot";
    if (thermo_waiting_sample_) return "anneal";
    if (thermo_waiting_feedback_) return "learn";
    return "ready";
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
    char core[320], line[320], bad[24], energy[24], progress[21];
    const char *mode = mode_name();
    const char *fit = g_fullscreen ? " FILL" : "";
    const char *phase = thermo_phase();
    int filled = (pct * 20 + 50) / 100;
    if (filled < 0) filled = 0;
    if (filled > 20) filled = 20;
    for (int i = 0; i < 20; i++) progress[i] = i < filled ? '#' : '.';
    progress[20] = 0;
    snprintf(bad, sizeof bad, "n/a");
    snprintf(energy, sizeof energy, "n/a");
    if (thermo_bad_ >= 0) snprintf(bad, sizeof bad, "%ld", thermo_bad_);
    if (thermo_energy_valid_) snprintf(energy, sizeof energy, "%.2f", thermo_energy_);
    if (g_thermo) {
        const char *engine = thermo_ready_ ? thermo_sampler_ : phase;
        const char *learning = g_thermo_learn ? "learn" : "ephem";
        if (g_quality_live >= 0.0) {
            snprintf(core, sizeof core,
                     "%s %3d%% [%s] | T/%-7s %-5s/%s q%.2f beta %.2f conf %.2f e %s bad %s r%ld o%ld P%4d [%dx%d]%s",
                     mode, pct, progress, engine, phase, learning, g_quality_live, thermo_beta_,
                     thermo_confidence_, energy, bad, thermo_round_, thermo_observations_,
                     studio_pin_count_, W_, H_, fit);
        } else {
            snprintf(core, sizeof core,
                     "%s %3d%% [%s] | T/%-7s %-5s/%s beta %.2f conf %.2f e %s bad %s r%ld o%ld P%4d [%dx%d]%s",
                     mode, pct, progress, engine, phase, learning, thermo_beta_, thermo_confidence_,
                     energy, bad, thermo_round_, thermo_observations_, studio_pin_count_,
                     W_, H_, fit);
        }
    } else if (g_quality_live >= 0.0) {
        snprintf(core, sizeof core, "%s %3d%% [%s] | classic q%.2f P%d [%dx%d]%s",
                 mode, pct, progress, g_quality_live, studio_pin_count_, W_, H_, fit);
    } else {
        snprintf(core, sizeof core, "%s %3d%% [%s] | classic P%d [%dx%d]%s",
                 mode, pct, progress, studio_pin_count_, W_, H_, fit);
    }
    if (now_ms() < g_note_until && g_note[0])
        snprintf(line, sizeof line, "%s | %s", core, g_note);
    else
        snprintf(line, sizeof line, "%s", core);

    char pos[32];
    snprintf(pos, sizeof pos, "\x1b[%d;1H", vh * ch + 1);
    fb_puts(pos);
    fb_bg((RGB){10, 12, 18});
    RGB status_color = mode_accent();
    if (!strcmp(phase, "error")) status_color = (RGB){255, 112, 112};
    fb_fg(status_color);
    fb_puts(" ");
    fb_puts(line);
    fb_puts("\x1b[K");
    fb_bg((RGB){0, 0, 0});
}

static bool save_report(const char *path) {
    if (!path || !*path) return false;
    QualityMetrics q = quality_measure(true);
    quality_record(q);
    char temp[ARTIFACT_TEMP_CAP];
    FILE *f = artifact_open(path, temp, sizeof temp);
    if (!f) return false;
    fprintf(f, "{\"schema\":2,\"mode\":\"%s\",\"seed\":%llu,"
               "\"dimensions\":{\"w\":%d,\"h\":%d},\"solver\":\"%s\","
               "\"backend\":",
            mode_name(), (unsigned long long)g_seed, W_, H_,
            g_thermo ? "thermo" : "classic");
    thermo_json_string(f, thermo_backend_name());
    fputs(",\"quality\":", f);
    quality_json(f, q);
    fprintf(f, ",\"thermo\":{\"enabled\":%s,\"sampler\":",
            g_thermo ? "true" : "false");
    thermo_json_string(f, thermo_sampler_);
    fputs(",\"backend\":", f);
    thermo_json_string(f, thermo_backend_name());
    fprintf(f, ",\"round\":%ld,\"observations\":%ld,\"proposals\":%ld,"
               "\"accepted\":%ld,\"rejected\":%ld,\"displaced\":%ld,"
               "\"contradictions\":%ld,"
               "\"bad\":%ld,\"beta\":%.9g,\"confidence\":%.9g,"
               "\"energy_valid\":%s,\"energy\":%.9g},"
               "\"studio\":{\"pins\":%d},\"macro\":{\"name\":",
            thermo_round_, thermo_observations_, thermo_proposals_,
            thermo_accepts_, thermo_rejects_, thermo_displaced_,
            thermo_contradictions_, thermo_bad_, thermo_beta_,
            thermo_confidence_, thermo_energy_valid_ ? "true" : "false",
            thermo_energy_, studio_pin_count_);
    thermo_json_string(f, macro_name());
    fprintf(f, ",\"guided_cells\":%d},\"evolution\":{\"candidates\":%d,"
               "\"winner_seed\":%llu,\"scores\":[",
            macro_guided_cells(), g_evolution_n,
            (unsigned long long)g_evolution_winner_seed);
    for (int i = 0; i < g_evolution_n; i++)
        fprintf(f, "%s%.9g", i ? "," : "", quality_clamp(g_evolution_scores[i]));
    fputs("]}}\n", f);
    bool ok = ferror(f) == 0;
    if (ok) ok = artifact_commit(f, temp, path);
    else artifact_abort(f, temp);
    if (!ok) return false;
    return true;
}

/* Subsequence match, so "sak" finds sakura and "gly" does not. Case folded. */
static bool picker_matches(const char *name, const char *query) {
    for (const char *q = query; *q; q++) {
        int want = tolower((unsigned char)*q);
        while (*name && tolower((unsigned char)*name) != want) name++;
        if (!*name) return false;
        name++;
    }
    return true;
}

static bool picker_tag_matches(const ModeSpec *mode, const char *tag) {
    if (!strcmp(tag, "field")) return mode->group == MG_FIELD;
    if (!strcmp(tag, "connector")) return mode->group == MG_CONNECTOR;
    if (!strcmp(tag, "carve")) return mode->group == MG_CARVE;
    if (!strcmp(tag, "network")) return mode->network;
    if (!strcmp(tag, "animated")) return mode->tick_ms != 0;
    if (!strcmp(tag, "coarse")) return mode->coarse;
    if (!strcmp(tag, "torus")) return mode->torus;
    if (!strcmp(tag, "static")) return mode->tick_ms == 0;
    if (!strcmp(tag, "favorite")) return mode_favorite((int)(mode - MODESPEC));
    if (!strcmp(tag, "recent")) return mode_recent((int)(mode - MODESPEC));
    return false;
}

static bool picker_matches_mode(const ModeSpec *mode, const char *query) {
    if (query[0] != '#') return picker_matches(mode->name, query);
    char tag[sizeof g_picker_query];
    size_t len = strlen(query + 1);
    if (!len || len >= sizeof tag) return false;
    for (size_t i = 0; i < len; i++)
        tag[i] = (char)tolower((unsigned char)query[i + 1]);
    tag[len] = 0;
    return picker_tag_matches(mode, tag);
}

/* Fill `out` with the indices of worlds matching the query; returns the count.
 * With no query every world matches, so the picker doubles as the mode list.
 * A #tag query is derived from the authoritative mode registry. */
static int picker_collect(int *out, int cap) {
    int n = 0;
    for (int i = 0; i < NMODES && n < cap; i++)
        if (picker_matches_mode(&MODESPEC[i], g_picker_query)) out[n++] = i;
    return n;
}

static void render_picker(void) {
    int hits[NMODES];
    int n = picker_collect(hits, NMODES);
    if (g_picker_sel >= n) g_picker_sel = n ? n - 1 : 0;
    if (g_picker_sel < 0) g_picker_sel = 0;
    char line[256];
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    fb_fg((RGB){150, 232, 255});
    fb_puts("PICK A WORLD\n\n");
    fb_fg((RGB){245, 220, 120});
    snprintf(line, sizeof line, "  search: %s\xe2\x96\x88\n\n", g_picker_query);
    fb_puts(line);
    fb_fg((RGB){120, 132, 148});
    fb_puts("  tags: #field #connector #network #animated #coarse #torus #static #favorite #recent\n\n");
    if (!n) {
        fb_fg((RGB){244, 120, 110});
        fb_puts("  no world matches\n");
    }
    /* keep the selection on screen when the terminal is short */
    struct winsize ws = {0};
    int trows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) trows = ws.ws_row;
    int rows = trows > 12 ? trows - 9 : 6;
    int first = 0;
    if (n > rows) {
        first = g_picker_sel - rows / 2;
        if (first < 0) first = 0;
        if (first > n - rows) first = n - rows;
    }
    /* leave room for the preview when the terminal is wide enough for it */
    int pw = n ? sheet_w[hits[g_picker_sel]] : 0;
    bool preview = sheet_ready_ && pw > 0 && trows > 12 &&
                   ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
                   ws.ws_col >= 34 + pw * 2;
    int blurb = preview ? (int)ws.ws_col - 26 - pw * 2 : 46;
    if (blurb > 46) blurb = 46;
    if (blurb < 8) blurb = 8;
    int listrow = 6;
    for (int k = first; k < n && k < first + rows; k++) {
        const ModeSpec *m = &MODESPEC[hits[k]];
        bool sel = k == g_picker_sel, cur = hits[k] == g_mode_idx;
        char note[80], marker[8];
        snprintf(note, sizeof note, "%.*s", blurb, m->blurb);
        snprintf(marker, sizeof marker, "%s", cur ? "\xe2\x97\x8f" :
                 mode_favorite(hits[k]) ? "\xe2\x98\x85" : mode_recent(hits[k]) ? "\xc2\xb7" : " ");
        fb_fg(sel ? (RGB){20, 24, 30} : cur ? (RGB){245, 200, 90} : (RGB){206, 214, 226});
        if (sel) fb_puts("\x1b[48;2;150;232;255m");
        snprintf(line, sizeof line, " %s %-9s %-*s ",
                 marker, m->name, blurb, note);
        fb_puts(line);
        if (sel) fb_puts("\x1b[49m");
        fb_puts("\n");
        listrow++;
    }
    if (preview) {
        /* the same solved previews the all-worlds sheet uses, so choosing a
         * world is a matter of looking at it rather than reading its name */
        int mi = hits[g_picker_sel];
        int ph = sheet_h[mi];
        int col = 16 + blurb + 6;
        for (int y = 0; y + 1 < ph; y += 2) {
            snprintf(line, sizeof line, "\x1b[%d;%dH", 6 + y / 2, col);
            fb_puts(line);
            for (int x = 0; x < sheet_w[mi]; x++)
                fb_half(sheet_cell[mi][y][x], sheet_cell[mi][y + 1][x]);
        }
        fb_puts("\x1b[0m");
        snprintf(line, sizeof line, "\x1b[%d;%dH", 6 + ph / 2, col);
        fb_puts(line);
        fb_fg((RGB){120, 132, 148});
        fb_puts(MODESPEC[mi].name);
        snprintf(line, sizeof line, "\x1b[%d;1H", listrow);
        fb_puts(line);
    }
    fb_fg((RGB){120, 132, 148});
    snprintf(line, sizeof line,
             "\n  %d/%d worlds   type or #tag to filter   up/down select   enter go   F favorite   esc cancel\n",
             n, NMODES);
    fb_puts(line);
    fb_puts("\x1b[0m");
    frame_begin();
    fwrite(fb_, 1, fblen_, stdout);
    frame_end();
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
    char bad[24], energy[24], line[256];
    snprintf(bad, sizeof bad, "n/a");
    snprintf(energy, sizeof energy, "n/a");
    if (thermo_bad_ >= 0) snprintf(bad, sizeof bad, "%ld", thermo_bad_);
    if (thermo_energy_ >= 0) snprintf(energy, sizeof energy, "%.2f", thermo_energy_);
    fb_reset();
    fb_puts("\x1b[H\x1b[2J");
    fb_fg((RGB){150, 232, 255});
    fb_puts("QUALITY OBSERVATORY\n\n");
    fb_fg((RGB){210, 220, 232});
    snprintf(line, sizeof line, "mode %-10s  focus %-10s  seed %llu  pins %d\n",
             mode_name(), profile.focus, (unsigned long long)g_seed,
             studio_pin_count_);
    fb_puts(line);
    snprintf(line, sizeof line,
             "solver %-7s  sampler %-8s  learning %-5s  beta %.2f  energy %s  confidence %.2f  bad %s\n",
             g_thermo ? "thermo" : "classic", thermo_sampler_,
             g_thermo_learn ? "on" : "off", thermo_beta_,
             energy, thermo_confidence_, bad);
    fb_puts(line);
    snprintf(line, sizeof line, "round %ld  observations %ld  proposals %ld  accepted %ld  rejected %ld  displaced %ld  contradictions %ld\n",
             thermo_round_, thermo_observations_, thermo_proposals_, thermo_accepts_,
             thermo_rejects_, thermo_displaced_, thermo_contradictions_);
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
    fb_puts("l return   Q heatmap   E evolution   x repair hotspot (keeps pins)   F fit fullscreen\n");
    fb_puts("P pin/unpin hover   right-click unpin   h help   q quit\n");
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
             mode_name(), quality_profile().focus,
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
    fb_puts("\nE return   Q heatmap   l observatory   F fit fullscreen   P pin/unpin   q quit");
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

#endif
