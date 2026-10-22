/*
 * Distort Me - core tests. Plain asserts, no framework.
 * SPDX-License-Identifier: MIT
 */
#include "distortme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* Gradient source: red = x / w, green = y / h, opaque. */
static void make_ramp(dm_image *img, int w, int h)
{
    dm_image_alloc(img, w, h, 4);
    img->alpha = 3;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float *p = img->data + ((size_t)y * w + x) * 4;
            p[0] = (float)x / (w - 1);
            p[1] = (float)y / (h - 1);
            p[2] = 0.25f;
            p[3] = 1.0f;
        }
}

static void fill(dm_image *img, int w, int h, float r, float g, float b)
{
    dm_image_alloc(img, w, h, 4);
    img->alpha = 3;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        img->data[i * 4 + 0] = r;
        img->data[i * 4 + 1] = g;
        img->data[i * 4 + 2] = b;
        img->data[i * 4 + 3] = 1.0f;
    }
}

static float max_diff(const dm_image *a, const dm_image *b)
{
    float m = 0.0f;
    for (size_t i = 0; i < (size_t)a->width * a->height * 4; i++) {
        float d = fabsf(a->data[i] - b->data[i]);
        if (d > m)
            m = d;
    }
    return m;
}

static void test_flat_map_is_identity(void)
{
    dm_image src, map, dst;
    make_ramp(&src, 64, 48);
    fill(&map, 7, 5, 0.5f, 0.5f, 0.5f);
    dm_image_alloc(&dst, 64, 48, 4);

    for (int m = 0; m <= 2; m++) {
        dm_params p;
        dm_params_default(&p, (dm_mode)m);
        CHECK(dm_apply(&src, &map, &p, &dst) == DM_OK, "apply");
        CHECK(max_diff(&src, &dst) < 1e-5f, "flat grey map must not move pixels");
    }
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
}

static void test_displace_shifts_by_strength(void)
{
    dm_image src, map, dst;
    make_ramp(&src, 101, 11);
    fill(&map, 4, 4, 1.0f, 0.5f, 0.5f); /* full right, no vertical */
    dm_image_alloc(&dst, 101, 11, 4);

    dm_params p;
    dm_params_default(&p, DM_MODE_DISPLACE);
    p.strength = 10.0f;
    dm_apply(&src, &map, &p, &dst);

    /* pixel x shows source pixel x + 10, red = x / 100 */
    float got  = dst.data[(5 * 101 + 20) * 4 + 0];
    float want = 30.0f / 100.0f;
    CHECK(fabsf(got - want) < 1e-4f, "displace by +10 px");

    /* past the right edge the border pixel repeats */
    got = dst.data[(5 * 101 + 95) * 4 + 0];
    CHECK(fabsf(got - 1.0f) < 1e-4f, "clamped edge");

    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
}

static void test_threads_match_single_thread(void)
{
    dm_image src, map, a, b;
    make_ramp(&src, 200, 150);
    dm_image_alloc(&map, 32, 32, 4);
    srand(1);
    for (size_t i = 0; i < 32 * 32 * 4; i++)
        map.data[i] = (float)rand() / RAND_MAX;
    dm_image_alloc(&a, 200, 150, 4);
    dm_image_alloc(&b, 200, 150, 4);

    for (int m = 0; m <= 2; m++) {
        dm_params p;
        dm_params_default(&p, (dm_mode)m);
        p.threads = 1;
        dm_apply(&src, &map, &p, &a);
        p.threads = 8;
        dm_apply(&src, &map, &p, &b);
        CHECK(max_diff(&a, &b) == 0.0f, "threaded result differs");
        CHECK(max_diff(&src, &a) > 0.01f, "random map should distort");
    }
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&a);
    dm_image_free(&b);
}

static void test_bad_arguments(void)
{
    dm_image src, map, dst;
    make_ramp(&src, 8, 8);
    fill(&map, 2, 2, 0.5f, 0.5f, 0.5f);
    dm_image_alloc(&dst, 4, 4, 4);
    dm_params p;
    dm_params_default(&p, DM_MODE_DISPLACE);
    CHECK(dm_apply(&src, &map, &p, &dst) == DM_ERR_ARGUMENT, "size mismatch");
    CHECK(dm_apply(&src, &map, &p, &src) == DM_ERR_ARGUMENT, "in-place");
    CHECK(dm_apply(NULL, &map, &p, &dst) == DM_ERR_ARGUMENT, "null");
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
}

/* A band computed with dm_apply_rect must equal the same rows and
 * columns of a full dm_apply. */
static void test_rect_matches_full(void)
{
    dm_image src, map, full, part;
    make_ramp(&src, 120, 90);
    dm_image_alloc(&map, 16, 16, 3);
    srand(7);
    for (size_t i = 0; i < 16 * 16 * 3; i++)
        map.data[i] = (float)rand() / RAND_MAX;
    dm_image_alloc(&full, 120, 90, 4);
    dm_image_alloc(&part, 50, 30, 4);

    for (int m = 0; m < DM_MODE_COUNT; m++) {
        dm_params p;
        dm_params_default(&p, (dm_mode)m);
        dm_apply(&src, &map, &p, &full);
        CHECK(dm_apply_rect(&src, &map, &p, 40, 25, &part) == DM_OK, "apply_rect");
        float d = 0.0f;
        for (int y = 0; y < 30; y++)
            for (int x = 0; x < 50; x++)
                for (int c = 0; c < 4; c++) {
                    float e = fabsf(part.data[(y * 50 + x) * 4 + c]
                                  - full.data[((y + 25) * 120 + x + 40) * 4 + c]);
                    if (e > d)
                        d = e;
                }
        CHECK(d == 0.0f, "rect differs from full result");
    }
    CHECK(dm_apply_rect(&src, &map, NULL, 0, 0, &part) == DM_ERR_ARGUMENT, "null params");
    CHECK(dm_apply_rect(&src, &map, &(dm_params){0}, 80, 0, &part) == DM_ERR_ARGUMENT, "rect outside");
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&full);
    dm_image_free(&part);
}

/* Grey images without alpha (as Photoshop hands them over) work too. */
static void test_single_channel(void)
{
    dm_image src, map, dst;
    dm_image_alloc(&src, 101, 3, 1);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 101; x++)
            src.data[y * 101 + x] = x / 100.0f;
    dm_image_alloc(&map, 2, 2, 1);
    for (int i = 0; i < 4; i++)
        map.data[i] = 1.0f; /* grey map: brightness 1 */
    dm_image_alloc(&dst, 101, 3, 1);

    dm_params p;
    dm_params_default(&p, DM_MODE_DISPLACE);
    p.strength = 5.0f; /* grey map = red and green both 1: shift +5, +5 */
    CHECK(dm_apply(&src, &map, &p, &dst) == DM_OK, "apply 1 channel");
    CHECK(fabsf(dst.data[1 * 101 + 10] - 0.15f) < 1e-4f, "1 channel shift");
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
}

/* The new modes leave the image alone where their map says "nothing":
 * black for split and blocks, 50% grey for slice. */
static void test_glitch_modes_neutral(void)
{
    dm_image src, black, grey, dst;
    make_ramp(&src, 64, 48);
    fill(&black, 5, 5, 0.0f, 0.0f, 0.0f);
    fill(&grey, 5, 5, 0.5f, 0.5f, 0.5f);
    dm_image_alloc(&dst, 64, 48, 4);

    dm_params p;
    dm_params_default(&p, DM_MODE_SPLIT);
    CHECK(dm_apply(&src, &black, &p, &dst) == DM_OK && max_diff(&src, &dst) < 1e-5f, "split: black = identity");
    dm_params_default(&p, DM_MODE_BLOCKS);
    CHECK(dm_apply(&src, &black, &p, &dst) == DM_OK && max_diff(&src, &dst) < 1e-5f, "blocks: black = identity");
    dm_params_default(&p, DM_MODE_SLICE);
    CHECK(dm_apply(&src, &grey, &p, &dst) == DM_OK && max_diff(&src, &dst) < 1e-5f, "slice: grey = identity");

    /* split with a white map: red comes from +strength, blue from -strength */
    fill(&grey, 5, 5, 1.0f, 1.0f, 1.0f);
    dm_params_default(&p, DM_MODE_SPLIT);
    p.strength = 4.0f;
    dm_apply(&src, &grey, &p, &dst);
    float r = dst.data[(10 * 64 + 20) * 4 + 0], g = dst.data[(10 * 64 + 20) * 4 + 1];
    CHECK(fabsf(r - 24.0f / 63.0f) < 1e-4f, "split moves red");
    CHECK(fabsf(g - src.data[(10 * 64 + 20) * 4 + 1]) < 1e-4f, "split keeps green");

    dm_image_free(&src);
    dm_image_free(&black);
    dm_image_free(&grey);
    dm_image_free(&dst);
}

int main(void)
{
    test_glitch_modes_neutral();
    test_rect_matches_full();
    test_single_channel();
    test_flat_map_is_identity();
    test_displace_shifts_by_strength();
    test_threads_match_single_thread();
    test_bad_arguments();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
