/*
 * Distort Me - Photoshop filter: user settings.
 * SPDX-License-Identifier: MIT
 */
#ifndef DISTORTME_PARAMS_H
#define DISTORTME_PARAMS_H

#include "distortme.h"

#define DM_PATH_MAX 2048

/* Everything the dialog edits. Kept as plain data so it can live in a
 * Photoshop handle between runs. Bump `version` when the layout changes. */
typedef struct DMSettings {
    int       version;
    dm_params core;
    char      mapPath[DM_PATH_MAX]; /* UTF-8 */
} DMSettings;

#define DM_SETTINGS_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

void DMSettingsDefault(DMSettings *s);

/* Switch mode while keeping map, placement and edges. */
void DMSettingsSetMode(DMSettings *s, dm_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* DISTORTME_PARAMS_H */
