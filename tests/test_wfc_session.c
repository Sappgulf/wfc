#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../wfc_session.h"

static void make_snapshot(const char *dir, const char *name, const char *mode,
                          unsigned long seed, int favorite) {
    char path[512], meta[512];
    assert(snprintf(path, sizeof path, "%s/%s.wfc", dir, name) < (int)sizeof path);
    assert(snprintf(meta, sizeof meta, "%s.meta", path) < (int)sizeof meta);
    FILE *fp = fopen(path, "wb");
    assert(fp && fputs("WFC1", fp) >= 0 && fclose(fp) == 0);
    fp = fopen(meta, "w");
    assert(fp);
    assert(fprintf(fp, "mode=%s\nseed=%lu\nannotation=%s map\nfavorite=%d\n",
                   mode, seed, name, favorite) > 0);
    assert(fclose(fp) == 0);
}

int main(void) {
    char dir[] = "/tmp/wfc-session-test-XXXXXX";
    assert(mkdtemp(dir));
    assert(wfc_session_ensure_dir(dir));
    make_snapshot(dir, "alpha", "delta", 7, 1);
    make_snapshot(dir, "beta", "rail", 42, 0);
    char short_name[512];
    assert(snprintf(short_name, sizeof short_name, "%s/a.bc", dir) < (int)sizeof short_name);
    FILE *short_file = fopen(short_name, "wb");
    assert(short_file && fclose(short_file) == 0);

    WfcSessionEntry entries[WFC_SESSION_MAX];
    int count = wfc_session_scan(dir, entries, WFC_SESSION_MAX);
    assert(count == 2);
    assert(strcmp(entries[0].name, "alpha.wfc") == 0);
    assert(entries[0].favorite && entries[0].seed == 7);
    assert(strcmp(entries[0].mode, "delta") == 0);
    assert(strcmp(entries[0].annotation, "alpha map") == 0);

    WfcSessionEntry beta = entries[1];
    assert(wfc_session_toggle_favorite(&beta));
    assert(beta.favorite);
    count = wfc_session_scan(dir, entries, WFC_SESSION_MAX);
    assert(count == 2);
    for (int i = 0; i < count; i++) assert(entries[i].favorite);

    assert(wfc_session_rename(&entries[0], "renamed"));
    assert(strstr(entries[0].path, "/renamed.wfc"));
    count = wfc_session_scan(dir, entries, WFC_SESSION_MAX);
    assert(count == 2);
    bool found = false;
    for (int i = 0; i < count; i++) found |= !strcmp(entries[i].name, "renamed.wfc");
    assert(found);
    WfcSessionEntry removed = entries[0];
    assert(wfc_session_delete(&removed));
    count = wfc_session_scan(dir, entries, WFC_SESSION_MAX);
    assert(count == 1);
    assert(wfc_session_delete(&entries[0]));
    assert(wfc_session_scan(dir, entries, WFC_SESSION_MAX) == 0);
    assert(unlink(short_name) == 0);
    assert(rmdir(dir) == 0);
    return 0;
}
