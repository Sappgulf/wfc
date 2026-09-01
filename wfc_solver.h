#ifndef WFC_SOLVER_H
#define WFC_SOLVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WFC_SOLVER_MAX_DIRECTIONS 4
#define WFC_SOLVER_MAX_TILES 64

/* Generic hard-constraint propagation.  The application supplies its domain
 * storage and compatibility masks; this module knows nothing about worlds,
 * rendering, RNG, or the terminal. */
typedef struct {
    uint64_t *domains;
    int *stack;
    size_t stack_capacity;
    int width;
    int height;
    unsigned tile_count;
    bool torus;
    const uint64_t (*compatibility)[WFC_SOLVER_MAX_TILES];
} WfcSolver;

bool wfc_solver_propagate(WfcSolver *solver, int start);

#endif
