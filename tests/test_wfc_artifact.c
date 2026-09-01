#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../wfc_artifact.h"

static void read_text(const char *path, char *out, size_t cap) {
    FILE *fp = fopen(path, "rb");
    assert(fp);
    size_t n = fread(out, 1, cap - 1, fp);
    assert(!ferror(fp));
    out[n] = 0;
    assert(fclose(fp) == 0);
}

int main(void) {
    char dir[] = "/tmp/wfc-artifact-test-XXXXXX";
    assert(mkdtemp(dir));
    char path[512];
    assert(snprintf(path, sizeof path, "%s/output.txt", dir) < (int)sizeof path);
    char temp[WFC_ARTIFACT_TEMP_CAP];
    FILE *fp = wfc_artifact_open(path, temp, sizeof temp);
    assert(fp);
    assert(fputs("first\n", fp) >= 0);
    assert(wfc_artifact_commit(fp, temp, path));
    assert(access(temp, F_OK) != 0);
    char text[32];
    read_text(path, text, sizeof text);
    assert(strcmp(text, "first\n") == 0);

    fp = wfc_artifact_open(path, temp, sizeof temp);
    assert(fp);
    assert(fputs("discarded\n", fp) >= 0);
    wfc_artifact_abort(fp, temp);
    assert(access(temp, F_OK) != 0);
    read_text(path, text, sizeof text);
    assert(strcmp(text, "first\n") == 0);

    assert(unlink(path) == 0);
    assert(rmdir(dir) == 0);
    return 0;
}
