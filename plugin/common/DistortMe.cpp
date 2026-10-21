/*
 * Distort Me - Photoshop filter plug-in entry point.
 *
 * Photoshop calls PluginMain several times per run, each time with a
 * "selector" saying what to do: show the about box, ask for settings,
 * prepare memory, then start/continue/finish the actual filtering.
 *
 * The distortion itself lives in the portable core (distortme.h). This
 * file only moves pixels between Photoshop and the core.
 *
 * SPDX-License-Identifier: MIT
 */
#include "PIFilter.h"
#include "PIAbout.h"

#include "distortme.h"
#include "DistortMeMap.h"
#include "DistortMeParams.h"
#include "DistortMeScripting.h"
#include "DistortMeUI.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define DM_EXPORT extern "C" __declspec(dllexport)
#else
#define DM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/* Set in filterSelectorParameters so Start doesn't ask a second time. */
static bool gDialogShownThisRun = false;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void *parent_window(void *platformData)
{
#ifdef _WIN32
    return platformData ? (void *)((PlatformData *)platformData)->hwnd : NULL;
#else
    (void)platformData;
    return NULL;
#endif
}

/* Hand a message to Photoshop; it shows it after we return. */
static int16 report_error(FilterRecord *fr, const char *msg)
{
    if (fr->errorString) {
        size_t n = strlen(msg);
        if (n > 255)
            n = 255;
        (*fr->errorString)[0] = (unsigned char)n;
        memcpy(&(*fr->errorString)[1], msg, n);
        return errReportString;
    }
    return filterBadParameters;
}

/* Photoshop keeps the last settings in fr->parameters between runs
 * (that's what makes Ctrl+F repeat the filter). */
static void load_settings(FilterRecord *fr, DMSettings *s)
{
    DMSettingsDefault(s);
    if (!fr->parameters)
        return;
    if (fr->handleProcs->getSizeProc(fr->parameters) != (int32)sizeof(DMSettings))
        return;
    Ptr p = fr->handleProcs->lockProc(fr->parameters, false);
    if (p && ((const DMSettings *)p)->version == DM_SETTINGS_VERSION)
        memcpy(s, p, sizeof(DMSettings));
    fr->handleProcs->unlockProc(fr->parameters);
}

static void save_settings(FilterRecord *fr, const DMSettings *s)
{
    if (fr->parameters && fr->handleProcs->getSizeProc(fr->parameters) != (int32)sizeof(DMSettings)) {
        fr->handleProcs->disposeProc(fr->parameters);
        fr->parameters = NULL;
    }
    if (!fr->parameters)
        fr->parameters = fr->handleProcs->newProc((int32)sizeof(DMSettings));
    if (!fr->parameters)
        return;
    Ptr p = fr->handleProcs->lockProc(fr->parameters, false);
    if (p)
        memcpy(p, s, sizeof(DMSettings));
    fr->handleProcs->unlockProc(fr->parameters);
}

/* Photoshop has 16-bit and 32-bit coordinate fields; documents larger
 * than 30000 px only work with the 32-bit ones. */
static VRect get_filter_rect(FilterRecord *fr)
{
    if (fr->bigDocumentData)
        return fr->bigDocumentData->filterRect32;
    VRect r = { fr->filterRect.top, fr->filterRect.left,
                fr->filterRect.bottom, fr->filterRect.right };
    return r;
}

static VPoint get_image_size(FilterRecord *fr)
{
    if (fr->bigDocumentData)
        return fr->bigDocumentData->imageSize32;
    VPoint p = { fr->imageSize.v, fr->imageSize.h };
    return p;
}

static Rect to_rect16(VRect r)
{
    Rect o;
    o.top    = (int16)r.top;
    o.left   = (int16)r.left;
    o.bottom = (int16)r.bottom;
    o.right  = (int16)r.right;
    return o;
}

static void set_in_rect(FilterRecord *fr, VRect r)
{
    if (fr->bigDocumentData)
        fr->bigDocumentData->inRect32 = r;
    else
        fr->inRect = to_rect16(r);
}

static void set_out_rect(FilterRecord *fr, VRect r)
{
    if (fr->bigDocumentData)
        fr->bigDocumentData->outRect32 = r;
    else
        fr->outRect = to_rect16(r);
}

static void set_mask_rect(FilterRecord *fr, VRect r)
{
    if (fr->bigDocumentData)
        fr->bigDocumentData->maskRect32 = r;
    else
        fr->maskRect = to_rect16(r);
}

static void clear_rects(FilterRecord *fr)
{
    VRect empty = { 0, 0, 0, 0 };
    set_in_rect(fr, empty);
    set_out_rect(fr, empty);
    set_mask_rect(fr, empty);
}

/* Which plane holds layer transparency, or -1. */
static int alpha_plane(FilterRecord *fr)
{
    if (fr->inTransparencyMask > 0 && fr->inLayerPlanes > 0 && fr->inLayerPlanes < fr->planes)
        return fr->inLayerPlanes;
    return -1;
}

/* Photoshop's 16-bit range is 0..32768, not 0..65535. */
static float read_sample(const uint8 *p, int32 depth)
{
    if (depth == 8)
        return *p / 255.0f;
    if (depth == 16)
        return *(const uint16 *)p / 32768.0f;
    return *(const float *)p;
}

static void write_sample(uint8 *p, int32 depth, float v)
{
    if (depth == 32) {
        *(float *)p = v;
        return;
    }
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    if (depth == 8)
        *p = (uint8)(v * 255.0f + 0.5f);
    else
        *(uint16 *)p = (uint16)(v * 32768.0f + 0.5f);
}

/* ------------------------------------------------------------------ */
/* Selectors                                                           */
/* ------------------------------------------------------------------ */

static void do_parameters(FilterRecord *fr, int16 *result)
{
    DMSettings s;
    bool show;
    load_settings(fr, &s);
    DMReadDescriptor(fr, &s, &show);

    if (!DMShowDialog(parent_window(fr->platformData), &s)) {
        *result = userCanceledErr;
        return;
    }
    save_settings(fr, &s);
    gDialogShownThisRun = true;
}

static void do_prepare(FilterRecord *fr, int16 *result)
{
    (void)result;
    if (fr->bigDocumentData)
        fr->bigDocumentData->PluginUsing32BitCoordinates = true;
    /* we allocate our own working memory, Photoshop keeps the rest */
    fr->bufferSpace = 0;
}

static void do_start(FilterRecord *fr, int16 *result)
{
    if (fr->bigDocumentData)
        fr->bigDocumentData->PluginUsing32BitCoordinates = true;

    /* settings: last run, then the action/Smart Filter descriptor */
    DMSettings s;
    bool show = false;
    load_settings(fr, &s);
    bool fromScript = DMReadDescriptor(fr, &s, &show);
    if ((!fromScript || show) && !gDialogShownThisRun) {
        if (!DMShowDialog(parent_window(fr->platformData), &s)) {
            *result = userCanceledErr;
            return;
        }
    }
    gDialogShownThisRun = false;
    save_settings(fr, &s);

    int32 depth = fr->depth;
    if (depth != 8 && depth != 16 && depth != 32) {
        *result = report_error(fr, "Distort Me supports 8, 16 and 32 bit images.");
        return;
    }

    dm_image map = { 0, 0, 0, -1, NULL };
    char error[256];
    if (!DMLoadMap(s.mapPath, &map, error, sizeof(error))) {
        *result = report_error(fr, error);
        return;
    }

    VRect fRect = get_filter_rect(fr);
    VPoint size = get_image_size(fr);
    int planes  = fr->planes;
    int bytes   = depth / 8;
    int outW    = fRect.right - fRect.left;
    int outH    = fRect.bottom - fRect.top;

    if (outW <= 0 || outH <= 0 || planes <= 0 || planes > DM_MAX_CHANNELS) {
        dm_image_free(&map);
        return;
    }

    /* The whole image is the source, so pixels can come from outside
     * the selection. Only the selected area is written. */
    VRect all = { 0, 0, size.v, size.h };
    set_in_rect(fr, all);
    fr->inLoPlane = 0;
    fr->inHiPlane = (int16)(planes - 1);
    set_out_rect(fr, fRect);
    fr->outLoPlane = 0;
    fr->outHiPlane = (int16)(planes - 1);
    if (fr->haveMask)
        set_mask_rect(fr, fRect);
    fr->inputRate = (int32)1 << 16;
    fr->maskRate  = (int32)1 << 16;

    *result = fr->advanceState();
    if (*result != noErr || !fr->inData || !fr->outData) {
        dm_image_free(&map);
        return;
    }

    dm_image src = { 0, 0, 0, -1, NULL };
    dm_image dst = { 0, 0, 0, -1, NULL };
    if (dm_image_alloc(&src, size.h, size.v, planes) != DM_OK ||
        dm_image_alloc(&dst, outW, outH, planes) != DM_OK) {
        dm_image_free(&map);
        dm_image_free(&src);
        *result = memFullErr;
        return;
    }
    src.alpha = dst.alpha = alpha_plane(fr);

    fr->progressProc(0, 3);

    /* Photoshop -> float, interleaved planes */
    const uint8 *in = (const uint8 *)fr->inData;
    for (int y = 0; y < size.v; y++) {
        const uint8 *row = in + (size_t)y * fr->inRowBytes;
        float *d = src.data + (size_t)y * size.h * planes;
        for (int x = 0; x < size.h * planes; x++)
            d[x] = read_sample(row + (size_t)x * bytes, depth);
    }
    fr->progressProc(1, 3);

    if (fr->abortProc()) {
        *result = userCanceledErr;
    } else {
        dm_result r = dm_apply_rect(&src, &map, &s.core, fRect.left, fRect.top, &dst);
        if (r == DM_ERR_MEMORY)
            *result = memFullErr;
        else if (r != DM_OK)
            *result = report_error(fr, "Distort Me: invalid settings.");
    }
    fr->progressProc(2, 3);

    /* float -> Photoshop */
    if (*result == noErr) {
        uint8 *out = (uint8 *)fr->outData;
        for (int y = 0; y < outH; y++) {
            uint8 *row = out + (size_t)y * fr->outRowBytes;
            const float *sp = dst.data + (size_t)y * outW * planes;
            for (int x = 0; x < outW * planes; x++)
                write_sample(row + (size_t)x * bytes, depth, sp[x]);
        }
    }
    fr->progressProc(3, 3);

    dm_image_free(&map);
    dm_image_free(&src);
    dm_image_free(&dst);

    /* everything is done in one go; Continue has nothing left to do */
    clear_rects(fr);
}

static void do_continue(FilterRecord *fr, int16 *result)
{
    (void)result;
    clear_rects(fr);
}

static void do_finish(FilterRecord *fr, int16 *result)
{
    DMSettings s;
    load_settings(fr, &s);
    *result = DMWriteDescriptor(fr, &s);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

DM_EXPORT MACPASCAL void PluginMain(const int16 selector, FilterRecordPtr fr,
                                    intptr_t *data, int16 *result)
{
    (void)data;
    *result = noErr;

    if (selector == filterSelectorAbout) {
        AboutRecord *about = (AboutRecord *)fr;
        DMShowAbout(parent_window(about ? about->platformData : NULL));
        return;
    }
    if (!fr)
        return;

    switch (selector) {
    case filterSelectorParameters: do_parameters(fr, result); break;
    case filterSelectorPrepare:    do_prepare(fr, result);    break;
    case filterSelectorStart:      do_start(fr, result);      break;
    case filterSelectorContinue:   do_continue(fr, result);   break;
    case filterSelectorFinish:     do_finish(fr, result);     break;
    default:                       *result = filterBadParameters; break;
    }
}
