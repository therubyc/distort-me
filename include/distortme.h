/*
 * Distort Me - image-driven distortion core.
 *
 * Plain C99, no dependencies, knows nothing about Photoshop.
 * Every effect reads a "map" image and decides per pixel where to
 * sample the source image from.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DISTORTME_H
#define DISTORTME_H

#ifdef __cplusplus
extern "C" {
#endif

#define DM_VERSION_MAJOR 0
#define DM_VERSION_MINOR 1
#define DM_VERSION_PATCH 0

/* An image with `channels` interleaved float channels, usually 0..1.
 * Pixels are stored row by row: data[(y * width + x) * channels + c].
 * `alpha` is the index of the transparency channel, or -1 if there is
 * none. With an alpha channel, colours are interpolated premultiplied
 * so transparent pixels don't bleed dark fringes. */
#define DM_MAX_CHANNELS 16

typedef struct dm_image {
    int    width;
    int    height;
    int    channels;
    int    alpha;
    float *data;
} dm_image;

typedef enum dm_mode {
    DM_MODE_DISPLACE = 0, /* red/green of the map shift x/y               */
    DM_MODE_REFRACT  = 1, /* map is a height field, light bends through it */
    DM_MODE_FLOW     = 2  /* pixels travel along the map's gradient lines  */
} dm_mode;

/* What happens when a lookup falls outside the source image. */
typedef enum dm_edge {
    DM_EDGE_CLAMP  = 0, /* repeat the border pixel   */
    DM_EDGE_WRAP   = 1, /* tile the image            */
    DM_EDGE_MIRROR = 2  /* tile with mirrored copies */
} dm_edge;

/* How the map is laid over the source image. */
typedef enum dm_map_fit {
    DM_FIT_STRETCH = 0, /* map is stretched to the source size        */
    DM_FIT_TILE    = 1  /* map keeps its pixel size (times map_scale) */
} dm_map_fit;

/* Which direction gradient flow follows. */
typedef enum dm_flow_dir {
    DM_FLOW_ALONG_CONTOURS = 0, /* follow lines of equal brightness (swirly) */
    DM_FLOW_UPHILL         = 1  /* move towards brighter areas               */
} dm_flow_dir;

typedef struct dm_params {
    dm_mode     mode;
    float       strength;     /* maximum shift in source pixels             */

    /* map placement */
    dm_map_fit  map_fit;
    float       map_scale;    /* 1 = original size (DM_FIT_TILE only)       */
    float       map_offset_x; /* in source pixels                           */
    float       map_offset_y;
    dm_edge     map_edge;     /* tiling of the map                          */
    float       map_blur;     /* box-blur radius in map pixels, 0 = off     */

    dm_edge     src_edge;     /* lookups outside the source image           */

    /* DM_MODE_DISPLACE */
    int         use_luminance; /* 1 = brightness pushes along `angle`        */
    float       angle;         /* degrees, used when use_luminance = 1       */

    /* DM_MODE_REFRACT */
    float       ior;          /* index of refraction, 1.0..2.0 (glass 1.5)  */

    /* DM_MODE_FLOW */
    dm_flow_dir flow_dir;
    int         flow_steps;   /* integration steps, more = smoother         */

    int         threads;      /* 0 = use all cores                          */
} dm_params;

typedef enum dm_result {
    DM_OK = 0,
    DM_ERR_ARGUMENT,
    DM_ERR_MEMORY
} dm_result;

/* Fill `p` with sensible defaults for the given mode. */
void dm_params_default(dm_params *p, dm_mode mode);

/* Allocate / free an image. dm_image_alloc zero-fills the pixels and
 * sets alpha to -1 (callers set it if the image has transparency). */
dm_result dm_image_alloc(dm_image *img, int width, int height, int channels);
void      dm_image_free(dm_image *img);

/* Run the distortion. `dst` must already be allocated with the same
 * size and channel count as `src`. `src` and `dst` must not be the same
 * image. The map may have any size; its RGB (and alpha, if it has 4
 * channels) are read, a 1- or 2-channel map counts as grey. */
dm_result dm_apply(const dm_image *src, const dm_image *map,
                   const dm_params *p, dm_image *dst);

/* Like dm_apply, but only computes a part of the result: `dst` is a
 * smaller image whose top-left pixel lies at (left, top) in `src`.
 * The map is still laid over the whole of `src`, so the result matches
 * dm_apply exactly. Photoshop uses this for selections and bands. */
dm_result dm_apply_rect(const dm_image *src, const dm_image *map,
                        const dm_params *p, int left, int top,
                        dm_image *dst);

const char *dm_mode_name(dm_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* DISTORTME_H */
