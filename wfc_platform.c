#include "wfc_platform.h"

#include <errno.h>
#include <time.h>
#include <unistd.h>

static WfcPlatformOps g_ops;
static void *g_context;

static double default_now_ms(void *context) {
    (void)context;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int default_sleep_ms(void *context, double milliseconds) {
    (void)context;
    if (milliseconds <= 0.0) return 0;
    time_t seconds = (time_t)(milliseconds / 1000.0);
    double fraction = milliseconds / 1000.0 - (double)seconds;
    long nanoseconds = (long)(fraction * 1e9);
    if (nanoseconds < 0) nanoseconds = 0;
    if (nanoseconds >= 1000000000L) {
        seconds++;
        nanoseconds -= 1000000000L;
    }
    struct timespec ts = {seconds, nanoseconds};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
    return 0;
}

static ssize_t default_read_input(void *context, int fd, void *buffer,
                                  size_t count) {
    (void)context;
    return read(fd, buffer, count);
}

static void ensure_defaults(void) {
    if (!g_ops.now_ms) g_ops.now_ms = default_now_ms;
    if (!g_ops.sleep_ms) g_ops.sleep_ms = default_sleep_ms;
    if (!g_ops.read_input) g_ops.read_input = default_read_input;
}

void wfc_platform_set_ops(const WfcPlatformOps *ops, void *context) {
    g_ops = ops ? *ops : (WfcPlatformOps){0};
    g_context = context;
    ensure_defaults();
}

void wfc_platform_reset_ops(void) {
    g_ops = (WfcPlatformOps){0};
    g_context = NULL;
    ensure_defaults();
}

double wfc_platform_now_ms(void) {
    ensure_defaults();
    return g_ops.now_ms(g_context);
}

int wfc_platform_sleep_ms(double milliseconds) {
    ensure_defaults();
    return g_ops.sleep_ms(g_context, milliseconds);
}

ssize_t wfc_platform_read_input(int fd, void *buffer, size_t count) {
    ensure_defaults();
    return g_ops.read_input(g_context, fd, buffer, count);
}
