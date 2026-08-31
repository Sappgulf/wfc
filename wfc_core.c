#include "wfc_core.h"

uint64_t wfc_core_full_mask(unsigned tile_count) {
    if (tile_count == 0) return 0;
    if (tile_count >= 64) return UINT64_MAX;
    return (UINT64_C(1) << tile_count) - 1;
}

bool wfc_core_domain_valid(uint64_t domain, unsigned tile_count) {
    if (tile_count > 64) return false;
    uint64_t full = wfc_core_full_mask(tile_count);
    return full != 0 && domain != 0 && (domain & ~full) == 0;
}

bool wfc_core_domains_valid(const uint64_t *domains, size_t count,
                            unsigned tile_count) {
    if (count != 0 && !domains) return false;
    for (size_t i = 0; i < count; i++)
        if (!wfc_core_domain_valid(domains[i], tile_count)) return false;
    return true;
}

size_t wfc_core_count_singletons(const uint64_t *domains, size_t count) {
    if (count != 0 && !domains) return 0;
    size_t singletons = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t domain = domains[i];
        if (domain && !(domain & (domain - 1))) singletons++;
    }
    return singletons;
}
