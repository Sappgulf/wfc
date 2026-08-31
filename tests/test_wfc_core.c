#include <assert.h>
#include <stdint.h>

#include "../wfc_core.h"

int main(void) {
    assert(wfc_core_full_mask(1) == UINT64_C(1));
    assert(wfc_core_full_mask(63) == (UINT64_MAX >> 1));
    assert(wfc_core_full_mask(64) == UINT64_MAX);

    uint64_t valid[] = {UINT64_C(1), UINT64_C(2), UINT64_C(4)};
    assert(wfc_core_domains_valid(valid, 3, 3));
    assert(wfc_core_count_singletons(valid, 3) == 3);

    uint64_t zero[] = {UINT64_C(1), UINT64_C(0), UINT64_C(4)};
    assert(!wfc_core_domains_valid(zero, 3, 3));
    uint64_t outside[] = {UINT64_C(1), UINT64_C(2), UINT64_C(8)};
    assert(!wfc_core_domains_valid(outside, 3, 3));
    assert(wfc_core_count_singletons(outside, 3) == 3);
    assert(!wfc_core_domain_valid(UINT64_C(1), 0));
    assert(!wfc_core_domain_valid(UINT64_C(1), 65));
    assert(!wfc_core_domain_valid(UINT64_C(1) << 4, 4));
    return 0;
}
