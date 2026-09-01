#include "wfc_mode.h"

#include <stddef.h>
#include <string.h>

static const char *const WFC_MODE_NAMES[WFC_MODE_COUNT] = {
#define WFC_MODE_NAME(id, name) [WFC_MODE_##id] = name,
    WFC_MODE_LIST(WFC_MODE_NAME)
#undef WFC_MODE_NAME
};

const char *wfc_mode_name(WfcModeId id) {
    return wfc_mode_id_valid(id) ? WFC_MODE_NAMES[id] : NULL;
}

WfcModeId wfc_mode_id_from_name(const char *name) {
    if (!name || !*name) return WFC_MODE_INVALID;
    for (int i = 0; i < WFC_MODE_COUNT; i++)
        if (!strcmp(name, WFC_MODE_NAMES[i])) return (WfcModeId)i;
    return WFC_MODE_INVALID;
}

bool wfc_mode_id_valid(WfcModeId id) {
    return id >= 0 && id < WFC_MODE_COUNT;
}
