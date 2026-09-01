#include "wfc_solver.h"

static int bit_count(uint64_t value) {
    int count = 0;
    while (value) {
        value &= value - 1;
        count++;
    }
    return count;
}

bool wfc_solver_propagate(WfcSolver *solver, int start) {
    if (!solver || !solver->domains || !solver->stack ||
        !solver->compatibility || solver->width <= 0 || solver->height <= 0 ||
        solver->tile_count == 0 || solver->tile_count > WFC_SOLVER_MAX_TILES ||
        start < 0 || start >= solver->width * solver->height ||
        solver->stack_capacity == 0)
        return false;

    size_t sp = 0;
    solver->stack[sp++] = start;
    while (sp) {
        int cell = solver->stack[--sp];
        int cx = cell % solver->width;
        int cy = cell / solver->width;
        uint64_t domain = solver->domains[cell];
        if (!domain) return false;
        for (int direction = 0; direction < WFC_SOLVER_MAX_DIRECTIONS; direction++) {
            int nx = cx, ny = cy;
            if (direction == 0)
                ny = solver->torus ? (cy + solver->height - 1) % solver->height : cy - 1;
            else if (direction == 1)
                nx = solver->torus ? (cx + 1) % solver->width : cx + 1;
            else if (direction == 2)
                ny = solver->torus ? (cy + 1) % solver->height : cy + 1;
            else
                nx = solver->torus ? (cx + solver->width - 1) % solver->width : cx - 1;
            if (nx < 0 || ny < 0 || nx >= solver->width || ny >= solver->height)
                continue;

            int neighbor = ny * solver->width + nx;
            uint64_t allowed = 0;
            uint64_t choices = domain;
            while (choices) {
                int tile = __builtin_ctzll(choices);
                if ((unsigned)tile >= solver->tile_count) return false;
                allowed |= solver->compatibility[direction][tile];
                choices &= choices - 1;
            }
            uint64_t next = solver->domains[neighbor] & allowed;
            if (!next || bit_count(next) == 0) return false;
            if (next != solver->domains[neighbor]) {
                solver->domains[neighbor] = next;
                if (sp >= solver->stack_capacity) return false;
                solver->stack[sp++] = neighbor;
            }
        }
    }
    return true;
}
