#include "wfc_artifact.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

FILE *wfc_artifact_open(const char *path, char *temp, size_t temp_cap) {
    if (!path || !*path || !temp || temp_cap == 0) {
        errno = EINVAL;
        return NULL;
    }
    int n = snprintf(temp, temp_cap, "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= temp_cap) {
        errno = ENAMETOOLONG;
        temp[0] = 0;
        return NULL;
    }
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
    if (fd < 0) return NULL;
    FILE *fp = fdopen(fd, "wb");
    if (fp) return fp;
    int saved = errno;
    close(fd);
    unlink(temp);
    errno = saved;
    return NULL;
}

void wfc_artifact_abort(FILE *fp, const char *temp) {
    int saved = errno;
    if (fp) fclose(fp);
    if (temp && *temp) unlink(temp);
    errno = saved;
}

bool wfc_artifact_commit(FILE *fp, const char *temp, const char *path) {
    if (!fp || !temp || !*temp || !path || !*path) {
        wfc_artifact_abort(fp, temp);
        errno = EINVAL;
        return false;
    }
    bool ok = fflush(fp) == 0;
    if (fclose(fp) != 0) ok = false;
    if (!ok || rename(temp, path) != 0) {
        int saved = errno;
        unlink(temp);
        errno = saved;
        return false;
    }
    return true;
}
