/*
 * Distort Me - Photoshop filter: actions, Smart Filter and scripting.
 *
 * Photoshop stores a filter's settings in a "descriptor" so actions can
 * replay it and Smart Filters can re-open it. The keys below must match
 * the Terminology section in PiPL.json.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DISTORTME_SCRIPTING_H
#define DISTORTME_SCRIPTING_H

#include "DistortMeParams.h"
#include "PIFilter.h"

#define DM_KEY_MODE        'dmMd'
#define DM_KEY_STRENGTH    'dmSt'
#define DM_KEY_BLUR        'dmBl'
#define DM_KEY_FIT         'dmFt'
#define DM_KEY_SCALE       'dmSc'
#define DM_KEY_OFFSET_X    'dmOx'
#define DM_KEY_OFFSET_Y    'dmOy'
#define DM_KEY_MAP_EDGE    'dmME'
#define DM_KEY_SRC_EDGE    'dmSE'
#define DM_KEY_LUMINANCE   'dmLu'
#define DM_KEY_ANGLE       'dmAn'
#define DM_KEY_IOR         'dmIr'
#define DM_KEY_FLOW_DIR    'dmFD'
#define DM_KEY_FLOW_STEPS  'dmFS'
#define DM_KEY_MAP_PATH    'dmMP'

/* Fill `s` from the descriptor Photoshop handed us, if any. Sets
 * *showDialog to whether the user (or the action) wants the dialog.
 * Returns true if a descriptor was read. */
bool DMReadDescriptor(FilterRecord *fr, DMSettings *s, bool *showDialog);

/* Hand the current settings back to Photoshop for actions/Smart Filters. */
OSErr DMWriteDescriptor(FilterRecord *fr, const DMSettings *s);

#endif /* DISTORTME_SCRIPTING_H */
