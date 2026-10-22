/*
 * Distort Me - image-driven distortion core.
 * SPDX-License-Identifier: MIT
 */
#include "distortme.h"
#include "dm_threads.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */

dm_result dm_image_alloc(dm_image *img, int width, int height, int channels)
{
    if (!img || width <= 0 || height <= 0 || channels <= 0 || channels > DM_MAX_CHANNELS)
        return DM_ERR_ARGUMENT;
    img->data = (float *)calloc((size_t)width * height * channels, sizeof(float));
    if (!img->data)
        return DM_ERR_MEMORY;
    img->width    = width;
    img->height   = height;
    img->channels = channels;
    img->alpha    = -1;
    return DM_OK;
}

void dm_image_free(dm_image *img)
{
    if (!img)
        return;
    free(img->data);
    img->data     = NULL;
    img->width    = 0;
    img->height   = 0;
    img->channels = 0;
    img->alpha    = -1;
}

const char *dm_mode_name(dm_mode mode)
{
    switch (mode) {
    case DM_MODE_DISPLACE: return "displace";
    case DM_MODE_REFRACT:  return "refract";
    case DM_MODE_FLOW:     return "flow";
    case DM_MODE_SPLIT:    return "split";
    case DM_MODE_SLICE:    return "slice";
    case DM_MODE_BLOCKS:   return "blocks";
    }
    return "unknown";
}

void dm_params_default(dm_params *p, dm_mode mode)
{
    memset(p, 0, sizeof(*p));
    p->mode          = mode;
    p->strength      = 20.0f;
    p->map_fit       = DM_FIT_STRETCH;
    p->map_scale     = 1.0f;
    p->map_edge      = DM_EDGE_WRAP;
    p->map_blur      = 0.0f;
    p->src_edge      = DM_EDGE_CLAMP;
    p->use_luminance = 0;
    p->angle         = 0.0f;
    p->ior           = 1.5f;
    p->flow_dir      = DM_FLOW_ALONG_CONTOURS;
    p->flow_steps    = 32;
    p->cell          = 12.0f;
    p->threads       = 0;

    if (mode == DM_MODE_REFRACT) {
        p->strength = 40.0f;
        p->map_blur = 1.0f;
    } else if (mode == DM_MODE_FLOW) {
        p->strength = 30.0f;
        p->map_blur = 2.0f;
    } else if (mode == DM_MODE_SPLIT) {
        p->strength = 12.0f;
    } else if (mode == DM_MODE_SLICE) {
        p->strength = 40.0f;
    } else if (mode == DM_MODE_BLOCKS) {
        p->strength = 32.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Sampling helpers                                                    */
/* ------------------------------------------------------------------ */

/* Map an integer coordinate into 0..n-1 according to the edge mode. */
static int edge_index(int i, int n, dm_edge edge)
{
    if (i >= 0 && i < n)
        return i;
    switch (edge) {
    case DM_EDGE_WRAP:
        i %= n;
        return i < 0 ? i + n : i;
    case DM_EDGE_MIRROR: {
        int period = 2 * n;
        i %= period;
        if (i < 0)
            i += period;
        return i < n ? i : period - 1 - i;
    }
    case DM_EDGE_CLAMP:
    default:
        return i < 0 ? 0 : n - 1;
    }
}

/* Bilinear lookup in a single-channel float plane. */
static float plane_sample(const float *plane, int w, int h,
                          float x, float y, dm_edge edge)
{
    float fx = floorf(x), fy = floorf(y);
    float tx = x - fx, ty = y - fy;
    int x0 = edge_index((int)fx, w, edge), x1 = edge_index((int)fx + 1, w, edge);
    int y0 = edge_index((int)fy, h, edge), y1 = edge_index((int)fy + 1, h, edge);

    float a = plane[y0 * w + x0], b = plane[y0 * w + x1];
    float c = plane[y1 * w + x0], d = plane[y1 * w + x1];
    float top = a + (b - a) * tx;
    float bot = c + (d - c) * tx;
    return top + (bot - top) * ty;
}

/* Bilinear lookup of all channels in the source image. With an alpha
 * channel, colours are interpolated premultiplied so transparent pixels
 * don't bleed dark fringes. */
static void image_sample(const dm_image *img, float x, float y,
                         dm_edge edge, float *out)
{
    /* pixel centres sit at +0.5 */
    x -= 0.5f;
    y -= 0.5f;

    float fx = floorf(x), fy = floorf(y);
    float tx = x - fx, ty = y - fy;
    int x0 = edge_index((int)fx, img->width, edge);
    int x1 = edge_index((int)fx + 1, img->width, edge);
    int y0 = edge_index((int)fy, img->height, edge);
    int y1 = edge_index((int)fy + 1, img->height, edge);

    int n = img->channels, ai = img->alpha;
    const float *p00 = img->data + ((size_t)y0 * img->width + x0) * n;
    const float *p10 = img->data + ((size_t)y0 * img->width + x1) * n;
    const float *p01 = img->data + ((size_t)y1 * img->width + x0) * n;
    const float *p11 = img->data + ((size_t)y1 * img->width + x1) * n;

    float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty);
    float w01 = (1 - tx) * ty,       w11 = tx * ty;

    if (ai < 0) {
        for (int c = 0; c < n; c++)
            out[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
        return;
    }

    float a = p00[ai] * w00 + p10[ai] * w10 + p01[ai] * w01 + p11[ai] * w11;
    for (int c = 0; c < n; c++) {
        if (c == ai)
            continue;
        float v = p00[c] * p00[ai] * w00 + p10[c] * p10[ai] * w10
                + p01[c] * p01[ai] * w01 + p11[c] * p11[ai] * w11;
        out[c] = a > 1e-6f ? v / a : 0.0f;
    }
    out[ai] = a;
}

/* ------------------------------------------------------------------ */
/* Map preparation                                                     */
/* ------------------------------------------------------------------ */

/* The map, split into the planes the modes need. All planes have the
 * map's size and are already blurred. Gradients are per map pixel. */
typedef struct prepared_map {
    int    w, h;
    float *r, *g, *lum, *gx, *gy;
} prepared_map;

static void prepared_map_free(prepared_map *m)
{
    free(m->r);
    free(m->g);
    free(m->lum);
    free(m->gx);
    free(m->gy);
    memset(m, 0, sizeof(*m));
}

/* Separable box blur in place, `radius` in pixels (fractional allowed:
 * the outermost tap gets a partial weight). */
static int box_blur(float *plane, int w, int h, float radius, dm_edge edge)
{
    if (radius <= 0.0f)
        return 1;

    int   r    = (int)ceilf(radius);
    float frac = radius - (float)(r - 1); /* weight of the outer taps */
    float norm = 1.0f / (2.0f * (r - 1) + 1.0f + 2.0f * frac);
    int   n    = w > h ? w : h;
    float *tmp = (float *)malloc(sizeof(float) * n);
    if (!tmp)
        return 0;

    for (int y = 0; y < h; y++) {           /* horizontal */
        float *row = plane + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            float s = 0.0f;
            for (int k = -r; k <= r; k++) {
                float wk = (k == -r || k == r) ? frac : 1.0f;
                s += row[edge_index(x + k, w, edge)] * wk;
            }
            tmp[x] = s * norm;
        }
        memcpy(row, tmp, sizeof(float) * w);
    }
    for (int x = 0; x < w; x++) {           /* vertical */
        for (int y = 0; y < h; y++) {
            float s = 0.0f;
            for (int k = -r; k <= r; k++) {
                float wk = (k == -r || k == r) ? frac : 1.0f;
                s += plane[(size_t)edge_index(y + k, h, edge) * w + x] * wk;
            }
            tmp[y] = s * norm;
        }
        for (int y = 0; y < h; y++)
            plane[(size_t)y * w + x] = tmp[y];
    }
    free(tmp);
    return 1;
}

static dm_result prepare_map(const dm_image *map, const dm_params *p,
                             prepared_map *m)
{
    int    w = map->width, h = map->height;
    size_t n = (size_t)w * h;

    memset(m, 0, sizeof(*m));
    m->w   = w;
    m->h   = h;
    m->r   = (float *)malloc(sizeof(float) * n);
    m->g   = (float *)malloc(sizeof(float) * n);
    m->lum = (float *)malloc(sizeof(float) * n);
    m->gx  = (float *)malloc(sizeof(float) * n);
    m->gy  = (float *)malloc(sizeof(float) * n);
    if (!m->r || !m->g || !m->lum || !m->gx || !m->gy) {
        prepared_map_free(m);
        return DM_ERR_MEMORY;
    }

    int ch = map->channels;
    for (size_t i = 0; i < n; i++) {
        const float *px = map->data + i * ch;
        /* 1-2 channels: grey (+ alpha), 3-4 channels: RGB (+ alpha).
         * Transparent map pixels count as neutral grey. */
        float r = px[0], g = ch >= 3 ? px[1] : px[0], b = ch >= 3 ? px[2] : px[0];
        float a = ch == 2 ? px[1] : (ch >= 4 ? px[3] : 1.0f);
        r = r * a + 0.5f * (1 - a);
        g = g * a + 0.5f * (1 - a);
        b = b * a + 0.5f * (1 - a);
        m->r[i]   = r;
        m->g[i]   = g;
        m->lum[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b; /* Rec. 709 */
    }

    if (!box_blur(m->r, w, h, p->map_blur, p->map_edge) ||
        !box_blur(m->g, w, h, p->map_blur, p->map_edge) ||
        !box_blur(m->lum, w, h, p->map_blur, p->map_edge)) {
        prepared_map_free(m);
        return DM_ERR_MEMORY;
    }

    /* central differences on the (blurred) luminance */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xl = edge_index(x - 1, w, p->map_edge);
            int xr = edge_index(x + 1, w, p->map_edge);
            int yu = edge_index(y - 1, h, p->map_edge);
            int yd = edge_index(y + 1, h, p->map_edge);
            m->gx[(size_t)y * w + x] = 0.5f * (m->lum[(size_t)y * w + xr] - m->lum[(size_t)y * w + xl]);
            m->gy[(size_t)y * w + x] = 0.5f * (m->lum[(size_t)yd * w + x] - m->lum[(size_t)yu * w + x]);
        }
    }
    return DM_OK;
}

/* ------------------------------------------------------------------ */
/* Per-pixel effects                                                   */
/* ------------------------------------------------------------------ */

typedef struct job {
    const dm_image    *src;
    const prepared_map *map;
    const dm_params   *p;
    dm_image          *dst;
    int left, top;     /* position of dst inside src */
    float sx, sy;      /* map pixels per source pixel */
    float cos_a, sin_a;
} job;

/* Source pixel position -> map pixel position. */
static void to_map(const job *j, float x, float y, float *u, float *v)
{
    *u = (x - j->p->map_offset_x) * j->sx - 0.5f;
    *v = (y - j->p->map_offset_y) * j->sy - 0.5f;
}

/* Luminance gradient at a source position, in "brightness per source
 * pixel" so strengths don't depend on the map's resolution. */
static void gradient_at(const job *j, float x, float y, float *gx, float *gy)
{
    const prepared_map *m = j->map;
    float u, v;
    to_map(j, x, y, &u, &v);
    *gx = plane_sample(m->gx, m->w, m->h, u, v, j->p->map_edge) * j->sx;
    *gy = plane_sample(m->gy, m->w, m->h, u, v, j->p->map_edge) * j->sy;
}

static void shade_displace(const job *j, float x, float y, float *ox, float *oy)
{
    const prepared_map *m = j->map;
    float u, v;
    to_map(j, x, y, &u, &v);

    if (j->p->use_luminance) {
        float l = plane_sample(m->lum, m->w, m->h, u, v, j->p->map_edge);
        float d = (l - 0.5f) * 2.0f * j->p->strength;
        *ox = x + d * j->cos_a;
        *oy = y + d * j->sin_a;
    } else {
        /* Photoshop convention: 50% grey = no shift */
        float r = plane_sample(m->r, m->w, m->h, u, v, j->p->map_edge);
        float g = plane_sample(m->g, m->w, m->h, u, v, j->p->map_edge);
        *ox = x + (r - 0.5f) * 2.0f * j->p->strength;
        *oy = y + (g - 0.5f) * 2.0f * j->p->strength;
    }
}

/* The map is a sheet of glass whose thickness follows its brightness.
 * A ray looking straight down bends at the surface (Snell's law) and
 * travels `strength` pixels deep before it hits the image. */
static void shade_refract(const job *j, float x, float y, float *ox, float *oy)
{
    float gx, gy;
    gradient_at(j, x, y, &gx, &gy);

    /* surface height = lum * strength, so slope = gradient * strength */
    float sx = gx * j->p->strength, sy = gy * j->p->strength;
    float inv = 1.0f / sqrtf(sx * sx + sy * sy + 1.0f);
    float nx = -sx * inv, ny = -sy * inv, nz = inv;     /* surface normal */

    float eta  = 1.0f / (j->p->ior > 1e-3f ? j->p->ior : 1e-3f);
    float cosi = nz;                                    /* -dot(n, (0,0,-1)) */
    float k    = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k < 0.0f) {                                     /* total reflection */
        *ox = x;
        *oy = y;
        return;
    }
    float f  = eta * cosi - sqrtf(k);
    float tx = f * nx, ty = f * ny, tz = -eta + f * nz; /* refracted ray */
    float depth = j->p->strength / (tz < -1e-3f ? -tz : 1e-3f);

    *ox = x + tx * depth;
    *oy = y + ty * depth;
}

static void flow_dir(const job *j, float x, float y, float *dx, float *dy)
{
    float gx, gy;
    gradient_at(j, x, y, &gx, &gy);
    float len = sqrtf(gx * gx + gy * gy);
    if (len < 1e-7f) {
        *dx = *dy = 0.0f;
        return;
    }
    /* fade out on almost flat areas instead of amplifying noise */
    float fade = len / (len + 1e-3f);
    if (j->p->flow_dir == DM_FLOW_UPHILL) {
        *dx = gx / len * fade;
        *dy = gy / len * fade;
    } else {
        *dx = -gy / len * fade;
        *dy =  gx / len * fade;
    }
}

/* Follow the direction field for `strength` pixels (midpoint method). */
static void shade_flow(const job *j, float x, float y, float *ox, float *oy)
{
    int   steps = j->p->flow_steps > 0 ? j->p->flow_steps : 1;
    float h     = j->p->strength / (float)steps;
    float px = x, py = y;

    for (int i = 0; i < steps; i++) {
        float d1x, d1y, d2x, d2y;
        flow_dir(j, px, py, &d1x, &d1y);
        flow_dir(j, px + d1x * h * 0.5f, py + d1y * h * 0.5f, &d2x, &d2y);
        px += d2x * h;
        py += d2y * h;
    }
    *ox = px;
    *oy = py;
}

/* Map brightness at a source position. */
static float lum_at(const job *j, float x, float y)
{
    float u, v;
    to_map(j, x, y, &u, &v);
    return plane_sample(j->map->lum, j->map->w, j->map->h, u, v, j->p->map_edge);
}

/* Bands `cell` pixels high; each row of a band shifts by the map's
 * brightness at the band's centre line. */
static void shade_slice(const job *j, float x, float y, float *ox, float *oy)
{
    float cell = j->p->cell > 1.0f ? j->p->cell : 1.0f;
    float yb   = (floorf(y / cell) + 0.5f) * cell;
    *ox = x + (lum_at(j, x, yb) - 0.5f) * 2.0f * j->p->strength;
    *oy = y;
}

/* Mosaic whose block size follows the map: black = untouched, white =
 * blocks of `strength` pixels. Each pixel shows its block's centre. */
static void shade_blocks(const job *j, float x, float y, float *ox, float *oy)
{
    float size = floorf(lum_at(j, x, y) * j->p->strength + 0.5f);
    if (size <= 1.0f) {
        *ox = x;
        *oy = y;
        return;
    }
    *ox = (floorf(x / size) + 0.5f) * size;
    *oy = (floorf(y / size) + 0.5f) * size;
}

/* Colour channels sampled at different offsets along `angle`, spread
 * by the map's brightness: first channel forwards, last backwards. */
static void split_pixel(const job *j, float x, float y, float *out)
{
    const dm_image *src = j->src;
    int   n = src->channels, ai = src->alpha;
    int   colours = ai >= 0 ? n - 1 : n;
    float amount = lum_at(j, x, y) * j->p->strength;
    float tmp[DM_MAX_CHANNELS];
    float alpha = 0.0f;
    int   k = 0;

    for (int c = 0; c < n; c++) {
        if (c == ai)
            continue;
        float t = colours > 1 ? 1.0f - 2.0f * (float)k / (float)(colours - 1) : 0.0f;
        image_sample(src, x + t * amount * j->cos_a, y + t * amount * j->sin_a,
                     j->p->src_edge, tmp);
        out[c] = tmp[c];
        if (ai >= 0 && tmp[ai] > alpha)
            alpha = tmp[ai]; /* keep the widest coverage: no holes at edges */
        k++;
    }
    if (ai >= 0)
        out[ai] = alpha;
}

static void run_rows(void *arg, int y_begin, int y_end)
{
    const job *j = (const job *)arg;
    int w = j->dst->width, n = j->dst->channels;

    for (int y = y_begin; y < y_end; y++) {
        for (int x = 0; x < w; x++) {
            float cx = x + j->left + 0.5f, cy = y + j->top + 0.5f, sx, sy;
            if (j->p->mode == DM_MODE_SPLIT) {
                split_pixel(j, cx, cy, j->dst->data + ((size_t)y * w + x) * n);
                continue;
            }
            switch (j->p->mode) {
            case DM_MODE_REFRACT: shade_refract(j, cx, cy, &sx, &sy); break;
            case DM_MODE_FLOW:    shade_flow(j, cx, cy, &sx, &sy);    break;
            case DM_MODE_SLICE:   shade_slice(j, cx, cy, &sx, &sy);   break;
            case DM_MODE_BLOCKS:  shade_blocks(j, cx, cy, &sx, &sy);  break;
            case DM_MODE_DISPLACE:
            default:              shade_displace(j, cx, cy, &sx, &sy); break;
            }
            image_sample(j->src, sx, sy, j->p->src_edge,
                         j->dst->data + ((size_t)y * w + x) * n);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

dm_result dm_apply(const dm_image *src, const dm_image *map,
                   const dm_params *p, dm_image *dst)
{
    if (!src || !dst || src->width != dst->width || src->height != dst->height)
        return DM_ERR_ARGUMENT;
    return dm_apply_rect(src, map, p, 0, 0, dst);
}

dm_result dm_apply_rect(const dm_image *src, const dm_image *map,
                        const dm_params *p, int left, int top,
                        dm_image *dst)
{
    if (!src || !map || !p || !dst || !src->data || !map->data || !dst->data)
        return DM_ERR_ARGUMENT;
    if (src == dst || src->data == dst->data || src->channels != dst->channels)
        return DM_ERR_ARGUMENT;
    if (src->channels <= 0 || src->channels > DM_MAX_CHANNELS || src->alpha >= src->channels)
        return DM_ERR_ARGUMENT;
    if (left < 0 || top < 0 || left + dst->width > src->width || top + dst->height > src->height)
        return DM_ERR_ARGUMENT;
    if (map->width <= 0 || map->height <= 0 || map->channels <= 0)
        return DM_ERR_ARGUMENT;

    prepared_map m;
    dm_result res = prepare_map(map, p, &m);
    if (res != DM_OK)
        return res;

    job j;
    j.src = src;
    j.map = &m;
    j.p   = p;
    j.dst = dst;
    j.left = left;
    j.top  = top;
    if (p->map_fit == DM_FIT_TILE) {
        float s = p->map_scale > 1e-4f ? p->map_scale : 1e-4f;
        j.sx = j.sy = 1.0f / s;
    } else {
        j.sx = (float)map->width / (float)src->width;
        j.sy = (float)map->height / (float)src->height;
    }
    j.cos_a = cosf(p->angle * (float)M_PI / 180.0f);
    j.sin_a = sinf(p->angle * (float)M_PI / 180.0f);

    dm_parallel_rows(dst->height, p->threads, run_rows, &j);

    prepared_map_free(&m);
    return DM_OK;
}
