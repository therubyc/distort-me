/*
 * Distort Me - command line test tool.
 *
 *   distortme <input> <map> <output.png> [options]
 *
 * SPDX-License-Identifier: MIT
 */
#include "distortme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void usage(void)
{
    printf(
        "Distort Me %d.%d.%d - image-driven distortion\n"
        "\n"
        "usage: distortme <input> <map> <output.png> [options]\n"
        "\n"
        "  --mode displace|refract|flow   effect (default displace)\n"
        "  --strength N                   max shift in pixels\n"
        "  --blur N                       blur the map first (map pixels)\n"
        "  --fit stretch|tile             how the map covers the image\n"
        "  --scale N                      map scale for --fit tile\n"
        "  --offset X Y                   move the map (pixels)\n"
        "  --map-edge clamp|wrap|mirror   map tiling (default wrap)\n"
        "  --edge clamp|wrap|mirror       source edges (default clamp)\n"
        "  --luminance                    displace: brightness pushes along --angle\n"
        "  --angle DEG                    direction for --luminance\n"
        "  --ior N                        refract: index of refraction (1.5)\n"
        "  --uphill                       flow: move towards bright areas\n"
        "  --steps N                      flow: integration steps (32)\n"
        "  --threads N                    worker threads (0 = all cores)\n",
        DM_VERSION_MAJOR, DM_VERSION_MINOR, DM_VERSION_PATCH);
}

static int load(const char *path, dm_image *img)
{
    int w, h, n;
    /* 16-bit files keep their precision, everything else loads as 8-bit */
    if (stbi_is_16_bit(path)) {
        stbi_us *px = stbi_load_16(path, &w, &h, &n, 4);
        if (!px)
            return 0;
        if (dm_image_alloc(img, w, h) != DM_OK) {
            stbi_image_free(px);
            return 0;
        }
        for (size_t i = 0; i < (size_t)w * h * 4; i++)
            img->data[i] = px[i] / 65535.0f;
        stbi_image_free(px);
    } else {
        stbi_uc *px = stbi_load(path, &w, &h, &n, 4);
        if (!px)
            return 0;
        if (dm_image_alloc(img, w, h) != DM_OK) {
            stbi_image_free(px);
            return 0;
        }
        for (size_t i = 0; i < (size_t)w * h * 4; i++)
            img->data[i] = px[i] / 255.0f;
        stbi_image_free(px);
    }
    return 1;
}

static int save_png(const char *path, const dm_image *img)
{
    size_t n = (size_t)img->width * img->height * 4;
    unsigned char *px = (unsigned char *)malloc(n);
    if (!px)
        return 0;
    for (size_t i = 0; i < n; i++) {
        float v = img->data[i];
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        px[i] = (unsigned char)(v * 255.0f + 0.5f);
    }
    int ok = stbi_write_png(path, img->width, img->height, 4, px, img->width * 4);
    free(px);
    return ok;
}

static int parse_edge(const char *s, dm_edge *out)
{
    if (!strcmp(s, "clamp"))  { *out = DM_EDGE_CLAMP;  return 1; }
    if (!strcmp(s, "wrap"))   { *out = DM_EDGE_WRAP;   return 1; }
    if (!strcmp(s, "mirror")) { *out = DM_EDGE_MIRROR; return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        usage();
        return argc == 1 ? 0 : 1;
    }

    /* the mode decides the defaults, so find it first */
    dm_mode mode = DM_MODE_DISPLACE;
    for (int i = 4; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--mode")) {
            const char *m = argv[i + 1];
            if (!strcmp(m, "displace"))     mode = DM_MODE_DISPLACE;
            else if (!strcmp(m, "refract")) mode = DM_MODE_REFRACT;
            else if (!strcmp(m, "flow"))    mode = DM_MODE_FLOW;
            else {
                fprintf(stderr, "unknown mode: %s\n", m);
                return 1;
            }
        }
    }

    dm_params p;
    dm_params_default(&p, mode);

    for (int i = 4; i < argc; i++) {
        const char *a = argv[i];
        int more = argc - 1 - i; /* arguments left after this one */

        if (!strcmp(a, "--mode") && more >= 1)          i++;
        else if (!strcmp(a, "--strength") && more >= 1) p.strength = (float)atof(argv[++i]);
        else if (!strcmp(a, "--blur") && more >= 1)     p.map_blur = (float)atof(argv[++i]);
        else if (!strcmp(a, "--scale") && more >= 1)    p.map_scale = (float)atof(argv[++i]);
        else if (!strcmp(a, "--angle") && more >= 1)    p.angle = (float)atof(argv[++i]);
        else if (!strcmp(a, "--ior") && more >= 1)      p.ior = (float)atof(argv[++i]);
        else if (!strcmp(a, "--steps") && more >= 1)    p.flow_steps = atoi(argv[++i]);
        else if (!strcmp(a, "--threads") && more >= 1)  p.threads = atoi(argv[++i]);
        else if (!strcmp(a, "--luminance"))             p.use_luminance = 1;
        else if (!strcmp(a, "--uphill"))                p.flow_dir = DM_FLOW_UPHILL;
        else if (!strcmp(a, "--offset") && more >= 2) {
            p.map_offset_x = (float)atof(argv[++i]);
            p.map_offset_y = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--fit") && more >= 1) {
            const char *f = argv[++i];
            if (!strcmp(f, "stretch"))   p.map_fit = DM_FIT_STRETCH;
            else if (!strcmp(f, "tile")) p.map_fit = DM_FIT_TILE;
            else { fprintf(stderr, "unknown fit: %s\n", f); return 1; }
        } else if (!strcmp(a, "--map-edge") && more >= 1) {
            if (!parse_edge(argv[++i], &p.map_edge)) { fprintf(stderr, "unknown edge: %s\n", argv[i]); return 1; }
        } else if (!strcmp(a, "--edge") && more >= 1) {
            if (!parse_edge(argv[++i], &p.src_edge)) { fprintf(stderr, "unknown edge: %s\n", argv[i]); return 1; }
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", a);
            return 1;
        }
    }

    dm_image src = {0}, map = {0}, dst = {0};
    if (!load(argv[1], &src)) {
        fprintf(stderr, "can't read input %s: %s\n", argv[1], stbi_failure_reason());
        return 1;
    }
    if (!load(argv[2], &map)) {
        fprintf(stderr, "can't read map %s: %s\n", argv[2], stbi_failure_reason());
        dm_image_free(&src);
        return 1;
    }
    if (dm_image_alloc(&dst, src.width, src.height) != DM_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    clock_t t0 = clock();
    dm_result r = dm_apply(&src, &map, &p, &dst);
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (r != DM_OK) {
        fprintf(stderr, "distortion failed (%d)\n", (int)r);
        return 1;
    }

    if (!save_png(argv[3], &dst)) {
        fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    printf("%s: %dx%d, map %dx%d, %.0f ms cpu\n", dm_mode_name(p.mode),
           src.width, src.height, map.width, map.height, ms);

    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
    return 0;
}
