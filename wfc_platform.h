#ifndef WFC_PLATFORM_H
#define WFC_PLATFORM_H

#include <stddef.h>
#include <sys/types.h>

/* Small host seam for the live loop.  Production uses POSIX defaults; tests
 * can replace the clock, sleep, or input reader without emulating a terminal. */
typedef struct {
    double (*now_ms)(void *context);
    int (*sleep_ms)(void *context, double milliseconds);
    ssize_t (*read_input)(void *context, int fd, void *buffer, size_t count);
} WfcPlatformOps;

void wfc_platform_set_ops(const WfcPlatformOps *ops, void *context);
void wfc_platform_reset_ops(void);
double wfc_platform_now_ms(void);
int wfc_platform_sleep_ms(double milliseconds);
ssize_t wfc_platform_read_input(int fd, void *buffer, size_t count);

#endif
