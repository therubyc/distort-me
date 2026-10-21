/*
 * Distort Me - Photoshop filter: actions, Smart Filter and scripting.
 * SPDX-License-Identifier: MIT
 */
#include "DistortMeScripting.h"
#include "PIActions.h"

#include <math.h>
#include <string.h>

/* The map path is stored as text. Photoshop's text type is not UTF-8,
 * so bytes outside plain ASCII (and '%') are percent-encoded. */
static size_t encode_path(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if (*p >= 0x20 && *p < 0x7F && *p != '%') {
            if (n + 1 >= cap)
                break;
            out[n++] = (char)*p;
        } else {
            if (n + 3 >= cap)
                break;
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    out[n] = 0;
    return n;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void decode_path(const char *in, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < cap; i++) {
        int hi, lo;
        if (in[i] == '%' && i + 2 < len && (hi = hex_value(in[i + 1])) >= 0 &&
            (lo = hex_value(in[i + 2])) >= 0) {
            out[n++] = (char)(hi * 16 + lo);
            i += 2;
        } else {
            out[n++] = in[i];
        }
    }
    out[n] = 0;
}

/* Scripts (UXP batchPlay, ExtendScript) may send a whole number where a
 * float is expected, or the other way round. Accept both. */
static bool read_real(ReadDescriptorProcs *rp, PIReadDescriptor token,
                      DescriptorTypeID type, real64 *out)
{
    if (type == typeInteger) {
        int32 i;
        if (rp->getIntegerProc(token, &i))
            return false;
        *out = (real64)i;
        return true;
    }
    return !rp->getFloatProc(token, out);
}

static bool read_int(ReadDescriptorProcs *rp, PIReadDescriptor token,
                     DescriptorTypeID type, int32 *out)
{
    if (type == typeFloat) {
        real64 f;
        if (rp->getFloatProc(token, &f))
            return false;
        *out = (int32)floor(f + 0.5);
        return true;
    }
    return !rp->getIntegerProc(token, out);
}

bool DMReadDescriptor(FilterRecord *fr, DMSettings *s, bool *showDialog)
{
    *showDialog = true;

    PIDescriptorParameters *dp = fr->descriptorParameters;
    if (!dp || !dp->readDescriptorProcs || !dp->descriptor)
        return false;

    ReadDescriptorProcs *rp = dp->readDescriptorProcs;
    DescriptorKeyIDArray keys = { 0 };
    PIReadDescriptor token = rp->openReadDescriptorProc(dp->descriptor, keys);
    if (!token)
        return false;

    /* the mode sets the defaults, everything else overrides them */
    DMSettings in = *s;
    DescriptorKeyID key;
    DescriptorTypeID type;
    int32 flags;
    int32 i32;
    real64 f64;
    Boolean b;
    Handle text;

    while (rp->getKeyProc(token, &key, &type, &flags)) {
        switch (key) {
        case DM_KEY_MODE:
            if (read_int(rp, token, type, &i32) && i32 >= 0 && i32 <= 2)
                DMSettingsSetMode(&in, (dm_mode)i32);
            break;
        case DM_KEY_STRENGTH:
            if (read_real(rp, token, type, &f64)) in.core.strength = (float)f64;
            break;
        case DM_KEY_BLUR:
            if (read_real(rp, token, type, &f64)) in.core.map_blur = (float)f64;
            break;
        case DM_KEY_FIT:
            if (read_int(rp, token, type, &i32)) in.core.map_fit = (dm_map_fit)(i32 ? 1 : 0);
            break;
        case DM_KEY_SCALE:
            if (read_real(rp, token, type, &f64)) in.core.map_scale = (float)f64;
            break;
        case DM_KEY_OFFSET_X:
            if (read_real(rp, token, type, &f64)) in.core.map_offset_x = (float)f64;
            break;
        case DM_KEY_OFFSET_Y:
            if (read_real(rp, token, type, &f64)) in.core.map_offset_y = (float)f64;
            break;
        case DM_KEY_MAP_EDGE:
            if (read_int(rp, token, type, &i32) && i32 >= 0 && i32 <= 2) in.core.map_edge = (dm_edge)i32;
            break;
        case DM_KEY_SRC_EDGE:
            if (read_int(rp, token, type, &i32) && i32 >= 0 && i32 <= 2) in.core.src_edge = (dm_edge)i32;
            break;
        case DM_KEY_LUMINANCE:
            if (!rp->getBooleanProc(token, &b)) in.core.use_luminance = b ? 1 : 0;
            break;
        case DM_KEY_ANGLE:
            if (read_real(rp, token, type, &f64)) in.core.angle = (float)f64;
            break;
        case DM_KEY_IOR:
            if (read_real(rp, token, type, &f64)) in.core.ior = (float)f64;
            break;
        case DM_KEY_FLOW_DIR:
            if (read_int(rp, token, type, &i32)) in.core.flow_dir = (dm_flow_dir)(i32 ? 1 : 0);
            break;
        case DM_KEY_FLOW_STEPS:
            if (read_int(rp, token, type, &i32) && i32 > 0 && i32 <= 1024) in.core.flow_steps = i32;
            break;
        case DM_KEY_MAP_PATH:
            if (!rp->getTextProc(token, &text) && text) {
                int32 len = fr->handleProcs->getSizeProc(text);
                Ptr p = fr->handleProcs->lockProc(text, false);
                if (p)
                    decode_path(p, (size_t)len, in.mapPath, sizeof(in.mapPath));
                fr->handleProcs->unlockProc(text);
                fr->handleProcs->disposeProc(text);
            }
            break;
        default:
            break;
        }
    }
    rp->closeReadDescriptorProc(token);
    fr->handleProcs->disposeProc(dp->descriptor);
    dp->descriptor = NULL;

    *s = in;
    *showDialog = dp->playInfo == plugInDialogDisplay;
    return true;
}

OSErr DMWriteDescriptor(FilterRecord *fr, const DMSettings *s)
{
    PIDescriptorParameters *dp = fr->descriptorParameters;
    if (!dp || !dp->writeDescriptorProcs)
        return noErr;

    WriteDescriptorProcs *wp = dp->writeDescriptorProcs;
    PIWriteDescriptor token = wp->openWriteDescriptorProc();
    if (!token)
        return errMissingParameter;

    const dm_params *c = &s->core;
    real64 f;

    wp->putIntegerProc(token, DM_KEY_MODE, (int32)c->mode);
    f = c->strength;     wp->putFloatProc(token, DM_KEY_STRENGTH, &f);
    f = c->map_blur;     wp->putFloatProc(token, DM_KEY_BLUR, &f);
    wp->putIntegerProc(token, DM_KEY_FIT, (int32)c->map_fit);
    f = c->map_scale;    wp->putFloatProc(token, DM_KEY_SCALE, &f);
    f = c->map_offset_x; wp->putFloatProc(token, DM_KEY_OFFSET_X, &f);
    f = c->map_offset_y; wp->putFloatProc(token, DM_KEY_OFFSET_Y, &f);
    wp->putIntegerProc(token, DM_KEY_MAP_EDGE, (int32)c->map_edge);
    wp->putIntegerProc(token, DM_KEY_SRC_EDGE, (int32)c->src_edge);
    wp->putBooleanProc(token, DM_KEY_LUMINANCE, c->use_luminance ? true : false);
    f = c->angle;        wp->putFloatProc(token, DM_KEY_ANGLE, &f);
    f = c->ior;          wp->putFloatProc(token, DM_KEY_IOR, &f);
    wp->putIntegerProc(token, DM_KEY_FLOW_DIR, (int32)c->flow_dir);
    wp->putIntegerProc(token, DM_KEY_FLOW_STEPS, (int32)c->flow_steps);

    char encoded[DM_PATH_MAX * 3];
    size_t len = encode_path(s->mapPath, encoded, sizeof(encoded));
    Handle text = fr->handleProcs->newProc((int32)len);
    if (text) {
        Ptr p = fr->handleProcs->lockProc(text, false);
        if (p)
            memcpy(p, encoded, len);
        fr->handleProcs->unlockProc(text);
        /* The SDK doesn't say whether Photoshop copies or adopts this
         * handle, so we don't dispose it: a few bytes leaked per run are
         * better than a double free. */
        wp->putTextProc(token, DM_KEY_MAP_PATH, text);
    }

    if (dp->descriptor)
        fr->handleProcs->disposeProc(dp->descriptor);
    PIDescriptorHandle h = NULL;
    OSErr err = wp->closeWriteDescriptorProc(token, &h);
    dp->descriptor = h;
    dp->recordInfo = plugInDialogOptional;
    return err;
}
