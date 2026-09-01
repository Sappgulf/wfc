#ifndef WFC_ARTIFACT_H
#define WFC_ARTIFACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Exporters, session metadata, and config files all use the same atomic
 * same-directory publish protocol.  Keeping it here makes the guarantee
 * independently testable instead of coupling it to image rendering. */
#define WFC_ARTIFACT_TEMP_CAP 544

FILE *wfc_artifact_open(const char *path, char *temp, size_t temp_cap);
void wfc_artifact_abort(FILE *fp, const char *temp);
bool wfc_artifact_commit(FILE *fp, const char *temp, const char *path);

#endif
