#ifndef WFC_COMMANDS_H
#define WFC_COMMANDS_H

#include <stdbool.h>

typedef enum {
    WFC_COMMAND_HELP = 0,
    WFC_COMMAND_NEW,
    WFC_COMMAND_NEXT_MODE,
    WFC_COMMAND_PICKER,
    WFC_COMMAND_OBSERVATORY,
    WFC_COMMAND_HEATMAP,
    WFC_COMMAND_EVOLUTION,
    WFC_COMMAND_SESSIONS,
    WFC_COMMAND_SAVE,
    WFC_COMMAND_AUDIO,
    WFC_COMMAND_FULLSCREEN,
    WFC_COMMAND_QUIT,
    WFC_COMMAND_COUNT
} WfcCommandId;

typedef struct {
    WfcCommandId id;
    const char *name;
    const char *description;
} WfcCommand;

int wfc_command_count(void);
const WfcCommand *wfc_command_at(int index);
bool wfc_command_matches(const WfcCommand *command, const char *query);

#endif
