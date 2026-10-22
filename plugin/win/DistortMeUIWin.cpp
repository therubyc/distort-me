/*
 * Distort Me - Windows settings dialog (plain Win32, no MFC).
 * SPDX-License-Identifier: MIT
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "DistortMeUI.h"
#include "resource.h"

static HINSTANCE this_module(void)
{
    HMODULE m = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)&this_module, &m);
    return (HINSTANCE)m;
}

/* ------------------------------------------------------------------ */
/* Field helpers                                                       */
/* ------------------------------------------------------------------ */

static void set_float(HWND dlg, int id, float v)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%g", (double)v);
    SetDlgItemTextW(dlg, id, buf);
}

/* Accepts both "1.5" and "1,5" (German keyboards). */
static float get_float(HWND dlg, int id, float fallback)
{
    wchar_t buf[64];
    if (!GetDlgItemTextW(dlg, id, buf, 64))
        return fallback;
    for (wchar_t *p = buf; *p; p++)
        if (*p == L',')
            *p = L'.';
    wchar_t *end;
    double v = wcstod(buf, &end);
    return end == buf ? fallback : (float)v;
}

static void combo_fill(HWND dlg, int id, const wchar_t *const *items, int count, int selected)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(c, CB_SETCURSEL, (WPARAM)selected, 0);
}

static int combo_get(HWND dlg, int id)
{
    LRESULT r = SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return r == CB_ERR ? 0 : (int)r;
}

static void utf8_to_wide(const char *in, wchar_t *out, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, in, -1, out, cap))
        out[0] = 0;
}

static void wide_to_utf8(const wchar_t *in, char *out, int cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, in, -1, out, cap, NULL, NULL))
        out[0] = 0;
}

/* ------------------------------------------------------------------ */
/* Dialog                                                              */
/* ------------------------------------------------------------------ */

static const wchar_t *const kModes[] = { L"Displacement", L"Refraction", L"Gradient Flow",
                                          L"RGB Split", L"Slice", L"Blocks" };
static const wchar_t *const kFits[]  = { L"Stretch to image", L"Tile" };
static const wchar_t *const kEdges[] = { L"Clamp", L"Wrap (tile)", L"Mirror" };
static const wchar_t *const kFlow[]  = { L"Along contours", L"Uphill (to bright)" };

static void show_mode_controls(HWND dlg, const DMSettings *s)
{
    BOOL displace = s->core.mode == DM_MODE_DISPLACE;
    BOOL refract  = s->core.mode == DM_MODE_REFRACT;
    BOOL flow     = s->core.mode == DM_MODE_FLOW;
    BOOL angle    = (displace && s->core.use_luminance) || s->core.mode == DM_MODE_SPLIT;
    BOOL tile     = s->core.map_fit == DM_FIT_TILE;

    EnableWindow(GetDlgItem(dlg, IDC_LUMA), displace);
    EnableWindow(GetDlgItem(dlg, IDC_ANGLE), angle);
    EnableWindow(GetDlgItem(dlg, IDC_ANGLE_LBL), angle);
    EnableWindow(GetDlgItem(dlg, IDC_IOR), refract);
    EnableWindow(GetDlgItem(dlg, IDC_IOR_LBL), refract);
    EnableWindow(GetDlgItem(dlg, IDC_FLOWDIR), flow);
    EnableWindow(GetDlgItem(dlg, IDC_FLOWDIR_LBL), flow);
    EnableWindow(GetDlgItem(dlg, IDC_STEPS), flow);
    EnableWindow(GetDlgItem(dlg, IDC_STEPS_LBL), flow);
    EnableWindow(GetDlgItem(dlg, IDC_SCALE), tile);
    EnableWindow(GetDlgItem(dlg, IDC_SCALE_LBL), tile);
}

static void to_dialog(HWND dlg, const DMSettings *s)
{
    wchar_t path[DM_PATH_MAX];
    utf8_to_wide(s->mapPath, path, DM_PATH_MAX);
    SetDlgItemTextW(dlg, IDC_MAP_PATH, path);

    combo_fill(dlg, IDC_FIT, kFits, 2, (int)s->core.map_fit);
    set_float(dlg, IDC_SCALE, s->core.map_scale * 100.0f);
    set_float(dlg, IDC_OFFX, s->core.map_offset_x);
    set_float(dlg, IDC_OFFY, s->core.map_offset_y);
    set_float(dlg, IDC_BLUR, s->core.map_blur);
    combo_fill(dlg, IDC_MAPEDGE, kEdges, 3, (int)s->core.map_edge);

    combo_fill(dlg, IDC_MODE, kModes, DM_MODE_COUNT, (int)s->core.mode);
    set_float(dlg, IDC_STRENGTH, s->core.strength);
    CheckDlgButton(dlg, IDC_LUMA, s->core.use_luminance ? BST_CHECKED : BST_UNCHECKED);
    set_float(dlg, IDC_ANGLE, s->core.angle);
    set_float(dlg, IDC_IOR, s->core.ior);
    combo_fill(dlg, IDC_FLOWDIR, kFlow, 2, (int)s->core.flow_dir);
    set_float(dlg, IDC_STEPS, (float)s->core.flow_steps);
    combo_fill(dlg, IDC_SRCEDGE, kEdges, 3, (int)s->core.src_edge);

    show_mode_controls(dlg, s);
}

static void from_dialog(HWND dlg, DMSettings *s)
{
    wchar_t path[DM_PATH_MAX];
    GetDlgItemTextW(dlg, IDC_MAP_PATH, path, DM_PATH_MAX);
    wide_to_utf8(path, s->mapPath, DM_PATH_MAX);

    dm_params *c = &s->core;
    c->map_fit      = (dm_map_fit)combo_get(dlg, IDC_FIT);
    c->map_scale    = get_float(dlg, IDC_SCALE, c->map_scale * 100.0f) / 100.0f;
    c->map_offset_x = get_float(dlg, IDC_OFFX, c->map_offset_x);
    c->map_offset_y = get_float(dlg, IDC_OFFY, c->map_offset_y);
    c->map_blur     = get_float(dlg, IDC_BLUR, c->map_blur);
    c->map_edge     = (dm_edge)combo_get(dlg, IDC_MAPEDGE);
    c->mode         = (dm_mode)combo_get(dlg, IDC_MODE);
    c->strength     = get_float(dlg, IDC_STRENGTH, c->strength);
    c->use_luminance = IsDlgButtonChecked(dlg, IDC_LUMA) == BST_CHECKED;
    c->angle        = get_float(dlg, IDC_ANGLE, c->angle);
    c->ior          = get_float(dlg, IDC_IOR, c->ior);
    c->flow_dir     = (dm_flow_dir)combo_get(dlg, IDC_FLOWDIR);
    c->flow_steps   = (int)get_float(dlg, IDC_STEPS, (float)c->flow_steps);
    c->src_edge     = (dm_edge)combo_get(dlg, IDC_SRCEDGE);

    /* keep values in ranges the core handles well */
    if (c->map_scale < 0.01f) c->map_scale = 0.01f;
    if (c->map_blur < 0.0f)   c->map_blur = 0.0f;
    if (c->map_blur > 200.0f) c->map_blur = 200.0f;
    if (c->ior < 1.0f)        c->ior = 1.0f;
    if (c->ior > 3.0f)        c->ior = 3.0f;
    if (c->flow_steps < 1)    c->flow_steps = 1;
    if (c->flow_steps > 256)  c->flow_steps = 256;
}

static void browse(HWND dlg)
{
    wchar_t file[MAX_PATH * 4] = L"";
    GetDlgItemTextW(dlg, IDC_MAP_PATH, file, MAX_PATH * 4);

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = dlg;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif)\0"
                      L"*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif\0"
                      L"All files\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH * 4;
    ofn.lpstrTitle  = L"Choose a map image";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        SetDlgItemTextW(dlg, IDC_MAP_PATH, file);
}

static INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    DMSettings *s = (DMSettings *)GetWindowLongPtrW(dlg, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        to_dialog(dlg, (DMSettings *)lp);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            browse(dlg);
            return TRUE;
        case IDC_MODE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                /* a new mode brings its own sensible strength etc. */
                from_dialog(dlg, s);
                DMSettingsSetMode(s, s->core.mode);
                to_dialog(dlg, s);
            }
            return TRUE;
        case IDC_FIT:
        case IDC_LUMA:
            from_dialog(dlg, s);
            show_mode_controls(dlg, s);
            return TRUE;
        case IDOK: {
            from_dialog(dlg, s);
            if (!s->mapPath[0]) {
                MessageBoxW(dlg, L"Please choose a map image first.", L"Distort Me",
                            MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool DMShowDialog(void *parentWindow, DMSettings *s)
{
    DMSettings work = *s; /* only commit on OK */
    INT_PTR r = DialogBoxParamW(this_module(), MAKEINTRESOURCEW(IDD_DISTORTME),
                                (HWND)parentWindow, dialog_proc, (LPARAM)&work);
    if (r != IDOK)
        return false;
    *s = work;
    return true;
}

void DMShowAbout(void *parentWindow)
{
    wchar_t text[256];
    swprintf(text, 256,
             L"Distort Me %d.%d.%d\n\n"
             L"Free, image-driven distortion for Photoshop.\n"
             L"github.com/therubyc/distort-me\n\nMIT License",
             DM_VERSION_MAJOR, DM_VERSION_MINOR, DM_VERSION_PATCH);
    MessageBoxW((HWND)parentWindow, text, L"About Distort Me", MB_OK | MB_ICONINFORMATION);
}
