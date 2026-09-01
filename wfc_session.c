#include "wfc_session.h"

#include "wfc_artifact.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool path_join(char *out, size_t cap, const char *directory,
                      const char *name) {
    if (!out || !cap || !directory || !*directory || !name || !*name) return false;
    int n = snprintf(out, cap, "%s/%s", directory, name);
    return n >= 0 && (size_t)n < cap;
}

static bool regular_file(const char *path, struct stat *st) {
    return path && stat(path, st) == 0 && S_ISREG(st->st_mode);
}

bool wfc_session_ensure_dir(const char *directory) {
    struct stat st;
    if (!directory || !*directory) {
        errno = EINVAL;
        return false;
    }
    if (stat(directory, &st) == 0) return S_ISDIR(st.st_mode);
    if (errno != ENOENT) return false;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return false;
    return stat(directory, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool session_candidate(const char *name) {
    if (!name || name[0] == '.' || strstr(name, ".tmp.")) return false;
    size_t len = strlen(name);
    if (len < 4 || (len >= 5 && !strcmp(name + len - 5, ".meta"))) return false;
    const char *dot = strrchr(name, '.');
    return dot && (!strcmp(dot, ".wfc") || !strcmp(dot, ".bin") ||
                   !strcmp(dot, ".snapshot"));
}

static void copy_line_value(char *dst, size_t cap, const char *line,
                            const char *key) {
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') return;
    const char *value = line + key_len + 1;
    size_t len = strcspn(value, "\r\n");
    if (len >= cap) len = cap - 1;
    memcpy(dst, value, len);
    dst[len] = 0;
}

static void read_metadata(WfcSessionEntry *entry) {
    entry->mode[0] = 0;
    entry->annotation[0] = 0;
    entry->seed = 0;
    entry->favorite = false;
    entry->has_metadata = false;
    FILE *fp = fopen(entry->meta_path, "r");
    if (!fp) return;
    entry->has_metadata = true;
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        copy_line_value(entry->mode, sizeof entry->mode, line, "mode");
        copy_line_value(entry->annotation, sizeof entry->annotation, line, "annotation");
        char value[64] = {0};
        copy_line_value(value, sizeof value, line, "seed");
        if (value[0]) {
            errno = 0;
            char *end = NULL;
            unsigned long long parsed = strtoull(value, &end, 10);
            if (errno == 0 && end != value && *end == 0) entry->seed = (uint64_t)parsed;
        }
        value[0] = 0;
        copy_line_value(value, sizeof value, line, "favorite");
        if (value[0]) entry->favorite = value[0] == '1' || value[0] == 'y' || value[0] == 'Y';
    }
    fclose(fp);
}

static int session_compare(const void *left, const void *right) {
    const WfcSessionEntry *a = left, *b = right;
    if (a->favorite != b->favorite) return b->favorite - a->favorite;
    if (a->modified != b->modified) return a->modified < b->modified ? 1 : -1;
    return strcmp(a->name, b->name);
}

int wfc_session_scan(const char *directory, WfcSessionEntry *entries, int capacity) {
    if (!directory || !*directory || !entries || capacity <= 0) return 0;
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    int count = 0;
    struct dirent *item;
    while (count < capacity && (item = readdir(dir)) != NULL) {
        if (!session_candidate(item->d_name)) continue;
        WfcSessionEntry *entry = &entries[count];
        memset(entry, 0, sizeof *entry);
        if (!path_join(entry->path, sizeof entry->path, directory, item->d_name)) continue;
        if (!regular_file(entry->path, &(struct stat){0})) continue;
        snprintf(entry->name, sizeof entry->name, "%s", item->d_name);
        if (snprintf(entry->meta_path, sizeof entry->meta_path, "%s.meta", entry->path) >=
            (int)sizeof entry->meta_path) continue;
        struct stat st;
        if (stat(entry->path, &st) == 0) entry->modified = st.st_mtime;
        read_metadata(entry);
        count++;
    }
    closedir(dir);
    qsort(entries, (size_t)count, sizeof *entries, session_compare);
    return count;
}

static bool rewrite_favorite(const WfcSessionEntry *entry, bool favorite) {
    char *source = NULL;
    size_t length = 0;
    FILE *fp = fopen(entry->meta_path, "rb");
    if (fp) {
        if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
        long size = ftell(fp);
        if (size < 0 || size > 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            return false;
        }
        length = (size_t)size;
        source = malloc(length + 1);
        if (!source || fread(source, 1, length, fp) != length) {
            free(source);
            fclose(fp);
            return false;
        }
        source[length] = 0;
        fclose(fp);
    }
    const char *line = "favorite=0\n";
    char replacement[32];
    snprintf(replacement, sizeof replacement, "favorite=%d\n", favorite ? 1 : 0);
    size_t output_cap = length + strlen(replacement) + 2;
    char *output = calloc(output_cap, 1);
    if (!output) { free(source); return false; }
    bool replaced = false;
    if (source) {
        char *cursor = source;
        while (*cursor) {
            char *end = strchr(cursor, '\n');
            size_t line_len = end ? (size_t)(end - cursor + 1) : strlen(cursor);
            if (!strncmp(cursor, "favorite=", 9)) {
                memcpy(output + strlen(output), replacement, strlen(replacement));
                replaced = true;
            } else {
                strncat(output, cursor, line_len);
            }
            cursor += line_len;
        }
    }
    if (!replaced) strncat(output, line, strlen(line));
    char temp[WFC_ARTIFACT_TEMP_CAP];
    FILE *out = wfc_artifact_open(entry->meta_path, temp, sizeof temp);
    bool ok = out && fwrite(output, 1, strlen(output), out) == strlen(output);
    if (out) {
        if (ok) ok = wfc_artifact_commit(out, temp, entry->meta_path);
        else wfc_artifact_abort(out, temp);
    }
    free(source);
    free(output);
    return ok;
}

bool wfc_session_toggle_favorite(WfcSessionEntry *entry) {
    if (!entry || !entry->path[0]) return false;
    bool next = !entry->favorite;
    if (!rewrite_favorite(entry, next)) return false;
    entry->favorite = next;
    entry->has_metadata = true;
    return true;
}

static bool safe_name(const char *name) {
    if (!name || !*name || strlen(name) >= WFC_SESSION_NAME_CAP) return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == ' ' || *p == '.')) return false;
    return true;
}

bool wfc_session_rename(WfcSessionEntry *entry, const char *new_name) {
    if (!entry || !entry->path[0] || !safe_name(new_name)) return false;
    const char *base = strrchr(entry->path, '/');
    base = base ? base + 1 : entry->path;
    const char *dot = strrchr(base, '.');
    const char *extension = dot ? dot : ".wfc";
    char directory[WFC_SESSION_PATH_CAP];
    size_t directory_len = (size_t)(base - entry->path);
    if (directory_len == 0 || directory_len >= sizeof directory) return false;
    memcpy(directory, entry->path, directory_len - 1);
    directory[directory_len - 1] = 0;
    char next_path[WFC_SESSION_PATH_CAP], next_meta[WFC_SESSION_PATH_CAP];
    if (snprintf(next_path, sizeof next_path, "%s/%s%s", directory, new_name, extension) >=
            (int)sizeof next_path ||
        snprintf(next_meta, sizeof next_meta, "%s.meta", next_path) >= (int)sizeof next_meta)
        return false;
    if (!strcmp(next_path, entry->path)) return true;
    if (access(next_path, F_OK) == 0 || access(next_meta, F_OK) == 0) {
        errno = EEXIST;
        return false;
    }
    if (rename(entry->path, next_path) != 0) return false;
    if (access(entry->meta_path, F_OK) == 0 && rename(entry->meta_path, next_meta) != 0) {
        int saved = errno;
        (void)rename(next_path, entry->path);
        errno = saved;
        return false;
    }
    snprintf(entry->name, sizeof entry->name, "%s%s", new_name, extension);
    snprintf(entry->path, sizeof entry->path, "%s", next_path);
    snprintf(entry->meta_path, sizeof entry->meta_path, "%s", next_meta);
    return true;
}

bool wfc_session_delete(const WfcSessionEntry *entry) {
    if (!entry || !entry->path[0]) return false;
    bool ok = true;
    if (entry->meta_path[0] && access(entry->meta_path, F_OK) == 0)
        ok = unlink(entry->meta_path) == 0;
    if (unlink(entry->path) != 0) ok = false;
    return ok;
}
