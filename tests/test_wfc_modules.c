#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../wfc_mode.h"
#include "../wfc_commands.h"
#include "../wfc_platform.h"
#include "../wfc_quality.h"
#include "../wfc_solver.h"

typedef struct {
    double now;
    int sleeps;
    int reads;
} FakePlatform;

static double fake_now(void *context) {
    return ((FakePlatform *)context)->now;
}

static int fake_sleep(void *context, double milliseconds) {
    FakePlatform *fake = context;
    fake->sleeps++;
    fake->now += milliseconds;
    return 0;
}

static ssize_t fake_read(void *context, int fd, void *buffer, size_t count) {
    (void)fd;
    FakePlatform *fake = context;
    fake->reads++;
    if (!count) return 0;
    ((unsigned char *)buffer)[0] = 'x';
    return 1;
}

int main(void) {
    assert(wfc_command_count() >= 10);
    const WfcCommand *sessions = wfc_command_at(WFC_COMMAND_SESSIONS);
    assert(sessions && wfc_command_matches(sessions, "rename"));
    assert(!wfc_command_matches(sessions, "not-a-command"));

    assert(WFC_MODE_COUNT == 37);
    assert(wfc_mode_id_from_name("rail") == WFC_MODE_RAIL);
    assert(strcmp(wfc_mode_name(WFC_MODE_DELTA), "delta") == 0);
    assert(wfc_mode_id_from_name("missing") == WFC_MODE_INVALID);
    assert(!wfc_mode_id_valid(WFC_MODE_INVALID));

    assert(wfc_quality_clamp(-1.0) == 0.0);
    assert(wfc_quality_clamp(2.0) == 1.0);
    assert(wfc_quality_clamp(NAN) == 0.0);
    WfcQualityProfile profile = wfc_quality_profile_for_mode("delta");
    assert(strcmp(profile.focus, "delta") == 0);
    WfcQualityComponents components = {1, 1, 1, 1, 1, 1, 1};
    assert(wfc_quality_score(profile, components) > 0.99);

    uint64_t domains[3] = {1, 3, 3};
    int stack[3 * WFC_SOLVER_MAX_TILES];
    uint64_t compatibility[WFC_SOLVER_MAX_DIRECTIONS][WFC_SOLVER_MAX_TILES] = {{0}};
    compatibility[1][0] = 2; /* tile 0 forces tile 1 to the right */
    compatibility[1][1] = 1; /* tile 1 forces tile 0 to the right */
    compatibility[3][0] = 2; /* and the matching left-facing relation */
    compatibility[3][1] = 1;
    WfcSolver solver = {
        domains, stack, sizeof stack / sizeof stack[0], 3, 1, 2, false,
        compatibility,
    };
    assert(wfc_solver_propagate(&solver, 0));
    assert(domains[0] == 1 && domains[1] == 2 && domains[2] == 1);

    FakePlatform fake = {123.0, 0, 0};
    WfcPlatformOps ops = {fake_now, fake_sleep, fake_read};
    wfc_platform_set_ops(&ops, &fake);
    assert(wfc_platform_now_ms() == 123.0);
    assert(wfc_platform_sleep_ms(7.5) == 0);
    assert(fake.sleeps == 1 && fake.now == 130.5);
    unsigned char byte = 0;
    assert(wfc_platform_read_input(0, &byte, 1) == 1 && byte == 'x');
    assert(fake.reads == 1);
    wfc_platform_reset_ops();
    return 0;
}
