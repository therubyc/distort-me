/*
 * Distort Me - Photoshop filter: platform dialogs.
 * Implemented in win/DistortMeUIWin.cpp and mac/DistortMeUIMac.mm.
 * SPDX-License-Identifier: MIT
 */
#ifndef DISTORTME_UI_H
#define DISTORTME_UI_H

#include "DistortMeParams.h"

/* Show the settings dialog. `parentWindow` is Photoshop's HWND on
 * Windows and NULL on macOS. Returns true if the user pressed OK; only
 * then is `s` changed. */
bool DMShowDialog(void *parentWindow, DMSettings *s);

void DMShowAbout(void *parentWindow);

#endif /* DISTORTME_UI_H */
