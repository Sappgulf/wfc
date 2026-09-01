#ifndef WFC_MODE_H
#define WFC_MODE_H

#include <stdbool.h>

/* Stable identities for the built-in worlds.  The numeric values are an API:
 * saves, reports, and integrations should use the name at the boundary but
 * can use this enum internally without stringly-typed dispatch. */
#define WFC_MODE_LIST(X) \
    X(CIRCUIT, "circuit") \
    X(TERRAIN, "terrain") \
    X(TRUCHET, "truchet") \
    X(FIRE, "fire") \
    X(WAVES, "waves") \
    X(DUNGEON, "dungeon") \
    X(MAZE, "maze") \
    X(GALAXY, "galaxy") \
    X(CITY, "city") \
    X(AURORA, "aurora") \
    X(MATRIX, "matrix") \
    X(PIPES, "pipes") \
    X(MONDRIAN, "mondrian") \
    X(KOI, "koi") \
    X(LAVA, "lava") \
    X(SAKURA, "sakura") \
    X(GEODE, "geode") \
    X(LANTERN, "lantern") \
    X(DUNES, "dunes") \
    X(REEF, "reef") \
    X(STAINED, "stained") \
    X(STREETS, "streets") \
    X(NEURONS, "neurons") \
    X(MYCELIUM, "mycelium") \
    X(DELTA, "delta") \
    X(STORM, "storm") \
    X(GLACIER, "glacier") \
    X(BAMBOO, "bamboo") \
    X(SOLAR, "solar") \
    X(RAIL, "rail") \
    X(CANYON, "canyon") \
    X(VINYL, "vinyl") \
    X(LOOM, "loom") \
    X(TIDE, "tide") \
    X(MARBLE, "marble") \
    X(CINDER, "cinder") \
    X(ORIGAMI, "origami")

typedef enum {
#define WFC_MODE_ENUM(id, name) WFC_MODE_##id,
    WFC_MODE_LIST(WFC_MODE_ENUM)
#undef WFC_MODE_ENUM
    WFC_MODE_COUNT,
    WFC_MODE_INVALID = -1
} WfcModeId;

const char *wfc_mode_name(WfcModeId id);
WfcModeId wfc_mode_id_from_name(const char *name);
bool wfc_mode_id_valid(WfcModeId id);

#endif
