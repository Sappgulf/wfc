#ifndef WFC_SESSION_H
#define WFC_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define WFC_SESSION_MAX 128
#define WFC_SESSION_PATH_CAP 512
#define WFC_SESSION_NAME_CAP 256
#define WFC_SESSION_MODE_CAP 64

typedef struct {
    char name[WFC_SESSION_NAME_CAP];
    char path[WFC_SESSION_PATH_CAP];
    char meta_path[WFC_SESSION_PATH_CAP];
    char mode[WFC_SESSION_MODE_CAP];
    char annotation[WFC_SESSION_NAME_CAP];
    uint64_t seed;
    time_t modified;
    bool favorite;
    bool has_metadata;
} WfcSessionEntry;

bool wfc_session_ensure_dir(const char *directory);
int wfc_session_scan(const char *directory, WfcSessionEntry *entries, int capacity);
bool wfc_session_toggle_favorite(WfcSessionEntry *entry);
bool wfc_session_rename(WfcSessionEntry *entry, const char *new_name);
bool wfc_session_delete(const WfcSessionEntry *entry);

#endif
