/*
 * Distort Me - Photoshop filter: loading map images from disk.
 * SPDX-License-Identifier: MIT
 */
#ifndef DISTORTME_MAP_H
#define DISTORTME_MAP_H

#include "distortme.h"

/* Load PNG, JPEG, TGA, BMP, PSD (composite) or GIF as an RGBA map.
 * `path` is UTF-8. 16-bit PNGs keep their precision. Returns false and
 * fills `error` on failure. */
bool DMLoadMap(const char *path, dm_image *map, char *error, int errorSize);

#endif /* DISTORTME_MAP_H */
