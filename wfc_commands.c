#include "wfc_commands.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static const WfcCommand COMMANDS[] = {
    {WFC_COMMAND_HELP,       "help",       "show keyboard controls"},
    {WFC_COMMAND_NEW,        "new map",    "restart the current world"},
    {WFC_COMMAND_NEXT_MODE,  "next mode",  "switch to the next world"},
    {WFC_COMMAND_PICKER,      "pick world", "search all registered worlds"},
    {WFC_COMMAND_OBSERVATORY, "observatory", "inspect quality and solver state"},
    {WFC_COMMAND_HEATMAP,     "heatmap",    "toggle quality heatmap"},
    {WFC_COMMAND_EVOLUTION,   "evolution",  "rank deterministic seed variants"},
    {WFC_COMMAND_SESSIONS,    "sessions",   "resume, favorite, rename, or delete snapshots"},
    {WFC_COMMAND_SAVE,        "save image", "write the current map image"},
    {WFC_COMMAND_AUDIO,       "audio",      "toggle synthesized sound"},
    {WFC_COMMAND_FULLSCREEN,  "fullscreen", "fit the world to the terminal"},
    {WFC_COMMAND_QUIT,        "quit",       "exit wfc"},
};

int wfc_command_count(void) {
    return (int)(sizeof COMMANDS / sizeof *COMMANDS);
}

const WfcCommand *wfc_command_at(int index) {
    if (index < 0 || index >= wfc_command_count()) return NULL;
    return &COMMANDS[index];
}

static bool contains_folded(const char *haystack, const char *needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    size_t n = strlen(needle);
    for (const char *start = haystack; *start; start++) {
        size_t i = 0;
        while (i < n && start[i] &&
               tolower((unsigned char)start[i]) ==
               tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

bool wfc_command_matches(const WfcCommand *command, const char *query) {
    if (!command) return false;
    return contains_folded(command->name, query) ||
           contains_folded(command->description, query);
}
