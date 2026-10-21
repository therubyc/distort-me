/*
 * Distort Me - Photoshop filter: user settings.
 * SPDX-License-Identifier: MIT
 */
#include "DistortMeParams.h"

#include <string.h>

void DMSettingsDefault(DMSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->version = DM_SETTINGS_VERSION;
    dm_params_default(&s->core, DM_MODE_DISPLACE);
}

void DMSettingsSetMode(DMSettings *s, dm_mode mode)
{
    dm_params old = s->core;
    dm_params_default(&s->core, mode);

    /* things that don't depend on the mode survive the switch */
    s->core.map_fit      = old.map_fit;
    s->core.map_scale    = old.map_scale;
    s->core.map_offset_x = old.map_offset_x;
    s->core.map_offset_y = old.map_offset_y;
    s->core.map_edge     = old.map_edge;
    s->core.src_edge     = old.src_edge;
}
