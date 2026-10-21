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
    dm_image_alloc(img, w, h);
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
    dm_image_alloc(img, w, h);
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
    dm_image_alloc(&dst, 64, 48);

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
    dm_image_alloc(&dst, 101, 11);

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
    dm_image_alloc(&map, 32, 32);
    srand(1);
    for (size_t i = 0; i < 32 * 32 * 4; i++)
        map.data[i] = (float)rand() / RAND_MAX;
    dm_image_alloc(&a, 200, 150);
    dm_image_alloc(&b, 200, 150);

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
    dm_image_alloc(&dst, 4, 4);
    dm_params p;
    dm_params_default(&p, DM_MODE_DISPLACE);
    CHECK(dm_apply(&src, &map, &p, &dst) == DM_ERR_ARGUMENT, "size mismatch");
    CHECK(dm_apply(&src, &map, &p, &src) == DM_ERR_ARGUMENT, "in-place");
    CHECK(dm_apply(NULL, &map, &p, &dst) == DM_ERR_ARGUMENT, "null");
    dm_image_free(&src);
    dm_image_free(&map);
    dm_image_free(&dst);
}

int main(void)
{
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
