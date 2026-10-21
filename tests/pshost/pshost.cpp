/*
 * Distort Me - a tiny fake Photoshop for testing the Windows plug-in.
 *
 * Loads DistortMe.8bf, feeds it an image through the same FilterRecord
 * protocol Photoshop uses (Prepare, Start, Continue, Finish), replays the
 * settings like an action would (no dialog) and saves the result. Runs on
 * Windows, or on Linux/macOS under Wine.
 *
 *   pshost DistortMe.8bf input.png map.png out.png <mode 0-2> <strength>
 *          [--depth 8|16] [--no-alpha] [--select L T R B] [--dialog]
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "PIFilter.h"
#include "PIActions.h"
#include "PIAbout.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ---- handles ------------------------------------------------------- */

struct FakeHandle {
    int32 size;
    char *data;
};

static Handle new_handle(int32 size)
{
    FakeHandle *h = new FakeHandle;
    h->size = size;
    h->data = (char *)calloc(size > 0 ? size : 1, 1);
    return (Handle)h;
}
static void dispose_handle(Handle h)
{
    if (!h) return;
    FakeHandle *f = (FakeHandle *)h;
    free(f->data);
    delete f;
}
static int32 handle_size(Handle h) { return h ? ((FakeHandle *)h)->size : 0; }
static Ptr lock_handle(Handle h, Boolean) { return h ? (Ptr)((FakeHandle *)h)->data : NULL; }
static void unlock_handle(Handle) {}

/* ---- descriptors ----------------------------------------------------- */

struct Item {
    DescriptorKeyID key;
    DescriptorTypeID type;
    double f;
    int32 i;
    std::vector<char> text;
};
struct Desc {
    std::vector<Item> items;
    size_t pos;
};

/* A descriptor handle just points at a Desc. */
static Handle desc_handle(Desc *d)
{
    Handle h = new_handle(sizeof(Desc *));
    memcpy(((FakeHandle *)h)->data, &d, sizeof(Desc *));
    return h;
}
static Desc *desc_of(Handle h)
{
    Desc *d;
    memcpy(&d, ((FakeHandle *)h)->data, sizeof(Desc *));
    return d;
}

static PIReadDescriptor rd_open(PIDescriptorHandle h, DescriptorKeyIDArray)
{
    Desc *d = desc_of((Handle)h);
    d->pos = 0;
    return (PIReadDescriptor)d;
}
static OSErr rd_close(PIReadDescriptor) { return noErr; }
static Boolean rd_key(PIReadDescriptor t, DescriptorKeyID *k, DescriptorTypeID *ty, int32 *fl)
{
    Desc *d = (Desc *)t;
    if (d->pos >= d->items.size()) return false;
    *k = d->items[d->pos].key;
    *ty = d->items[d->pos].type;
    *fl = 0;
    d->pos++;
    return true;
}
static Item &cur(PIReadDescriptor t) { Desc *d = (Desc *)t; return d->items[d->pos - 1]; }
static OSErr rd_int(PIReadDescriptor t, int32 *v) { *v = cur(t).i; return noErr; }
static OSErr rd_float(PIReadDescriptor t, real64 *v) { *v = cur(t).f; return noErr; }
static OSErr rd_bool(PIReadDescriptor t, Boolean *v) { *v = cur(t).i != 0; return noErr; }
static OSErr rd_text(PIReadDescriptor t, Handle *v)
{
    Item &it = cur(t);
    *v = new_handle((int32)it.text.size());
    memcpy(((FakeHandle *)*v)->data, it.text.data(), it.text.size());
    return noErr;
}

static Desc gWritten;
static PIWriteDescriptor wr_open(void) { gWritten.items.clear(); return (PIWriteDescriptor)&gWritten; }
static OSErr wr_close(PIWriteDescriptor, PIDescriptorHandle *h) { *h = (PIDescriptorHandle)desc_handle(&gWritten); return noErr; }
static OSErr wr_int(PIWriteDescriptor, DescriptorKeyID k, int32 v) { Item it; it.key = k; it.type = 'long'; it.i = v; it.f = 0; gWritten.items.push_back(it); return noErr; }
static OSErr wr_float(PIWriteDescriptor, DescriptorKeyID k, const real64 *v) { Item it; it.key = k; it.type = 'doub'; it.f = *v; it.i = 0; gWritten.items.push_back(it); return noErr; }
static OSErr wr_bool(PIWriteDescriptor, DescriptorKeyID k, Boolean v) { Item it; it.key = k; it.type = 'bool'; it.i = v; it.f = 0; gWritten.items.push_back(it); return noErr; }
static OSErr wr_text(PIWriteDescriptor, DescriptorKeyID k, Handle h)
{
    Item it; it.key = k; it.type = 'TEXT'; it.i = 0; it.f = 0;
    it.text.assign(((FakeHandle *)h)->data, ((FakeHandle *)h)->data + ((FakeHandle *)h)->size);
    gWritten.items.push_back(it);
    return noErr;
}

/* ---- pixels ------------------------------------------------------------ */

static int gW, gH, gPlanes, gBytes;
static std::vector<unsigned char> gIn, gOut;
static FilterRecord gFR;
static BigDocumentStruct gBig;

static OSErr advance_state(void)
{
    VRect in = gBig.inRect32, out = gBig.outRect32;
    size_t rowBytes = (size_t)gW * gPlanes * gBytes;
    gFR.inRowBytes = (int32)rowBytes;
    gFR.outRowBytes = (int32)rowBytes;
    gFR.inData = in.right > in.left ? &gIn[(size_t)in.top * rowBytes + (size_t)in.left * gPlanes * gBytes] : NULL;
    gFR.outData = out.right > out.left ? &gOut[(size_t)out.top * rowBytes + (size_t)out.left * gPlanes * gBytes] : NULL;
    if ((gFR.inLoPlane != 0 || gFR.inHiPlane != gPlanes - 1) && gFR.inData) {
        fprintf(stderr, "host: plug-in asked for planes %d..%d, test host only serves all\n", gFR.inLoPlane, gFR.inHiPlane);
        return filterBadParameters;
    }
    return noErr;
}
static void progress(int32, int32) {}
static Boolean test_abort(void) { return false; }

typedef void (*PluginMainFn)(const int16, FilterRecordPtr, intptr_t *, int16 *);

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: pshost plugin.8bf in.png map.png out.png mode strength [--depth 8|16] [--no-alpha] [--select L T R B]\n");
        return 2;
    }
    int depth = 8, alpha = 1, dialog = 0;
    VRect sel = { 0, 0, 0, 0 };
    bool haveSel = false;
    for (int i = 7; i < argc; i++) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-alpha")) alpha = 0;
        else if (!strcmp(argv[i], "--dialog")) dialog = 1;
        else if (!strcmp(argv[i], "--select") && i + 4 < argc) {
            sel.left = atoi(argv[++i]); sel.top = atoi(argv[++i]);
            sel.right = atoi(argv[++i]); sel.bottom = atoi(argv[++i]);
            haveSel = true;
        }
    }

    HMODULE lib = LoadLibraryA(argv[1]);
    if (!lib) { fprintf(stderr, "can't load %s\n", argv[1]); return 1; }
    PluginMainFn entry = (PluginMainFn)GetProcAddress(lib, "PluginMain");
    if (!entry) { fprintf(stderr, "no PluginMain export\n"); return 1; }
    HRSRC res = FindResourceA(lib, "JSON_PIPL", "JSON");
    printf("host: PiPL resource %s\n", res ? "found" : "MISSING");

    int n;
    unsigned char *px = stbi_load(argv[2], &gW, &gH, &n, 4);
    if (!px) { fprintf(stderr, "can't read %s\n", argv[2]); return 1; }
    gPlanes = alpha ? 4 : 3;
    gBytes = depth / 8;
    gIn.resize((size_t)gW * gH * gPlanes * gBytes);
    for (size_t p = 0; p < (size_t)gW * gH; p++)
        for (int c = 0; c < gPlanes; c++) {
            unsigned char v = px[p * 4 + c];
            if (depth == 8) gIn[p * gPlanes + c] = v;
            else ((uint16_t *)gIn.data())[p * gPlanes + c] = (uint16_t)((v * 32768 + 127) / 255);
        }
    stbi_image_free(px);
    gOut = gIn; /* Photoshop pre-fills the destination with the source */

    HandleProcs hp; memset(&hp, 0, sizeof(hp));
    hp.handleProcsVersion = 1;
    hp.numHandleProcs = 5;
    hp.newProc = new_handle; hp.disposeProc = dispose_handle; hp.getSizeProc = handle_size;
    hp.lockProc = lock_handle; hp.unlockProc = unlock_handle;

    ReadDescriptorProcs rp; memset(&rp, 0, sizeof(rp));
    rp.openReadDescriptorProc = rd_open; rp.closeReadDescriptorProc = rd_close; rp.getKeyProc = rd_key;
    rp.getIntegerProc = rd_int; rp.getFloatProc = rd_float; rp.getBooleanProc = rd_bool; rp.getTextProc = rd_text;
    WriteDescriptorProcs wp; memset(&wp, 0, sizeof(wp));
    wp.openWriteDescriptorProc = wr_open; wp.closeWriteDescriptorProc = wr_close;
    wp.putIntegerProc = wr_int; wp.putFloatProc = wr_float; wp.putBooleanProc = wr_bool; wp.putTextProc = wr_text;

    /* the settings, as an action would replay them */
    Desc in;
    Item it;
    it.key = 'dmMd'; it.type = 'long'; it.i = atoi(argv[5]); it.f = 0; in.items.push_back(it);
    it.key = 'dmSt'; it.type = 'doub'; it.f = atof(argv[6]); it.i = 0; in.items.push_back(it);
    it.key = 'dmMP'; it.type = 'TEXT'; it.text.assign(argv[3], argv[3] + strlen(argv[3])); in.items.push_back(it);

    PIDescriptorParameters dp; memset(&dp, 0, sizeof(dp));
    dp.descriptorParametersVersion = 0;
    dp.playInfo = plugInDialogDontDisplay;
    dp.descriptor = (PIDescriptorHandle)desc_handle(&in);
    dp.readDescriptorProcs = &rp;
    dp.writeDescriptorProcs = &wp;

    static Str255 errorString;
    PlatformData pd; pd.hwnd = 0;

    memset(&gFR, 0, sizeof(gFR));
    memset(&gBig, 0, sizeof(gBig));
    gBig.imageSize32.h = gW; gBig.imageSize32.v = gH;
    gBig.filterRect32 = haveSel ? sel : VRect{ 0, 0, gH, gW };
    gBig.wholeSize32 = gBig.imageSize32;
    gFR.bigDocumentData = &gBig;
    gFR.imageSize.h = (int16)gW; gFR.imageSize.v = (int16)gH;
    gFR.planes = (int16)gPlanes;
    gFR.depth = depth;
    gFR.imageMode = depth == 16 ? plugInModeRGB48 : plugInModeRGBColor;
    gFR.inLayerPlanes = 3;
    gFR.inTransparencyMask = alpha ? 1 : 0;
    gFR.handleProcs = &hp;
    gFR.advanceState = advance_state;
    gFR.progressProc = progress;
    gFR.abortProc = test_abort;
    gFR.descriptorParameters = &dp;
    gFR.errorString = &errorString;
    gFR.platformData = &pd;
    gFR.maxSpace = 1 << 30;

    intptr_t data = 0;
    int16 result = noErr;
    if (dialog) {
        /* like choosing the menu item: settings come from the dialog */
        entry(filterSelectorParameters, &gFR, &data, &result);
        if (result != noErr) {
            fprintf(stderr, "host: parameters returned %d\n", result);
            return 1;
        }
    }
    const int16 selectors[] = { filterSelectorPrepare, filterSelectorStart, filterSelectorContinue, filterSelectorFinish };
    const char *names[] = { "prepare", "start", "continue", "finish" };
    for (int s = 0; s < 4; s++) {
        entry(selectors[s], &gFR, &data, &result);
        if (result != noErr) {
            fprintf(stderr, "host: %s returned %d", names[s], result);
            if (result == errReportString)
                fprintf(stderr, ": %.*s", errorString[0], (const char *)&errorString[1]);
            fprintf(stderr, "\n");
            return 1;
        }
    }

    printf("host: plug-in wrote %d settings back:", (int)gWritten.items.size());
    for (size_t i = 0; i < gWritten.items.size(); i++) {
        DescriptorKeyID k = gWritten.items[i].key;
        printf(" %c%c%c%c", (char)(k >> 24), (char)(k >> 16), (char)(k >> 8), (char)k);
    }
    printf("\n");

    std::vector<unsigned char> rgba((size_t)gW * gH * 4, 255);
    for (size_t p = 0; p < (size_t)gW * gH; p++)
        for (int c = 0; c < gPlanes; c++) {
            unsigned v = depth == 8 ? gOut[p * gPlanes + c]
                                    : (((uint16_t *)gOut.data())[p * gPlanes + c] * 255u + 16384u) / 32768u;
            rgba[p * 4 + c] = (unsigned char)(v > 255 ? 255 : v);
        }
    if (!stbi_write_png(argv[4], gW, gH, 4, rgba.data(), gW * 4)) { fprintf(stderr, "can't write\n"); return 1; }
    printf("host: ok, wrote %s\n", argv[4]);
    return 0;
}
