#ifndef WFC_CORE_H
#define WFC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Domain-mask invariants shared by the solver, persistence, and tests. */
uint64_t wfc_core_full_mask(unsigned tile_count);
bool wfc_core_domain_valid(uint64_t domain, unsigned tile_count);
bool wfc_core_domains_valid(const uint64_t *domains, size_t count,
                            unsigned tile_count);
size_t wfc_core_count_singletons(const uint64_t *domains, size_t count);

#endif
