/*
 * Distort Me - Photoshop filter: loading map images from disk.
 * SPDX-License-Identifier: MIT
 */
#include "DistortMeMap.h"

#include <stdio.h>

#ifdef _WIN32
#define STBI_WINDOWS_UTF8 /* stbi_load takes UTF-8 paths on Windows too */
#endif
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool DMLoadMap(const char *path, dm_image *map, char *error, int errorSize)
{
    int w, h, n;

    if (!path || !path[0]) {
        snprintf(error, errorSize, "No map image chosen.");
        return false;
    }

    if (stbi_is_16_bit(path)) {
        stbi_us *px = stbi_load_16(path, &w, &h, &n, 4);
        if (!px)
            goto fail;
        if (dm_image_alloc(map, w, h, 4) != DM_OK) {
            stbi_image_free(px);
            snprintf(error, errorSize, "Not enough memory for the map.");
            return false;
        }
        for (size_t i = 0; i < (size_t)w * h * 4; i++)
            map->data[i] = px[i] / 65535.0f;
        stbi_image_free(px);
    } else {
        stbi_uc *px = stbi_load(path, &w, &h, &n, 4);
        if (!px)
            goto fail;
        if (dm_image_alloc(map, w, h, 4) != DM_OK) {
            stbi_image_free(px);
            snprintf(error, errorSize, "Not enough memory for the map.");
            return false;
        }
        for (size_t i = 0; i < (size_t)w * h * 4; i++)
            map->data[i] = px[i] / 255.0f;
        stbi_image_free(px);
    }
    map->alpha = 3;
    return true;

fail:
    snprintf(error, errorSize, "Can't read the map image (%s).", stbi_failure_reason());
    return false;
}
