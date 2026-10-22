/*
 * Distort Me - Photoshop panel (UXP).
 *
 * The panel keeps a small copy of the source layer and of the map and
 * renders a live preview with the JavaScript port of the core while the
 * sliders move. "Apply" runs the native filter (DistortMe.8bf/.plugin)
 * with the same settings, so the result is full quality and can be a
 * Smart Filter.
 *
 * SPDX-License-Identifier: MIT
 */
'use strict';

const { app, core, action, imaging } = require('photoshop');
const { storage } = require('uxp');
const DM = require('./js/distortme.js');

const fs = storage.localFileSystem;

/* The filter's event: its PiPL ScopeString, with the 4-char code as fallback. */
const FILTER_EVENTS = ['8f3b6c2e-5d1a-4e7b-9a40-3c6f2b1d7e95', '$dsMe'];
const PREVIEW_MAX = 512; /* longest side of the preview render */
const MAP_PREVIEW_MAX = 512;
const STB_FORMATS = ['png', 'jpg', 'jpeg', 'bmp', 'tga', 'gif', 'psd', 'ppm', 'pgm'];
const SRGB = 'sRGB IEC61966-2.1';

const state = {
    p: DM.defaults(DM.MODE.DISPLACE),
    smart: true,
    showOrig: false, /* preview shows the untouched source */
    view: { z: 1, cx: 0, cy: 0 },        /* zoom (1 = fit) and centre in document pixels */
    shown: { base: null, detail: null }, /* document rectangles of the preview images */
    detail: null,   /* sharper crop of the source while zoomed: { img, scale, bounds, docId, layerId } */
    src: null,      /* { img, half, scale, docId, layerId, name, docW, docH } */
    map: null,      /* { img, prepared, scale, name, path, w, h } */
    eventIndex: 0,
    dragging: false,
    pending: false,
    rendering: false
};

/* ---------------------------------------------------------------- */
/* Sliders                                                           */
/* ---------------------------------------------------------------- */

const SLIDERS = [
    { key: 'strength', label: p => p.mode === 5 ? 'MAX BLOCK' : 'STRENGTH', min: 0, max: 300, step: 1, unit: 'PX', glitch: true },
    { key: 'cell', label: 'BAND HEIGHT', min: 1, max: 200, step: 1, unit: 'PX', show: p => p.mode === 4 },
    { key: 'mapBlur', label: 'SMOOTH MAP', min: 0, max: 20, step: 0.5, unit: 'PX' },
    { key: 'angle', label: 'ANGLE', min: 0, max: 360, step: 1, unit: '°', show: p => (p.mode === 0 && p.useLuminance) || p.mode === 3 },
    { key: 'ior', label: 'GLASS (IOR)', min: 1, max: 2.5, step: 0.01, unit: '', show: p => p.mode === 1 },
    { key: 'flowSteps', label: 'FLOW STEPS', min: 4, max: 128, step: 1, unit: '', show: p => p.mode === 2 },
    { key: 'mapScale', label: 'MAP SCALE', min: 0.1, max: 4, step: 0.05, unit: '×', show: p => p.mapFit === 1 },
    { key: 'mapOffsetX', label: 'MAP OFFSET X', min: -1000, max: 1000, step: 1, unit: 'PX' },
    { key: 'mapOffsetY', label: 'MAP OFFSET Y', min: -1000, max: 1000, step: 1, unit: 'PX' }
];
function fmt(v, step) {
    return step < 1 ? v.toFixed(step < 0.1 ? 2 : 1) : String(Math.round(v));
}

function buildSliders() {
    const box = document.getElementById('sliders');
    box.innerHTML = '';
    SLIDERS.forEach(s => {
        const el = document.createElement('div');
        el.className = 'slider';
        el.innerHTML = '<div class="top"><span class="name"></span><span class="val"></span></div>' +
                       '<div class="bar"></div>';
        s.el = el;
        s.name = el.querySelector('.name');
        s.bar = el.querySelector('.bar');
        s.val = el.querySelector('.val');

        const setFromX = clientX => {
            const r = s.bar.getBoundingClientRect();
            let t = (clientX - r.left) / Math.max(1, r.width);
            t = Math.min(1, Math.max(0, t));
            let v = s.min + t * (s.max - s.min);
            v = Math.round(v / s.step) * s.step;
            state.p[s.key] = +v.toFixed(4);
            updateSlider(s);
            schedule();
        };
        const move = e => setFromX(e.clientX);
        const up = e => {
            document.removeEventListener('mousemove', move);
            document.removeEventListener('mouseup', up);
            state.dragging = false;
            setFromX(e.clientX);
        };
        s.bar.addEventListener('mousedown', e => {
            state.dragging = true;
            document.addEventListener('mousemove', move);
            document.addEventListener('mouseup', up);
            setFromX(e.clientX);
        });
        /* double-click the value: back to the mode's default */
        s.val.addEventListener('dblclick', () => {
            state.p[s.key] = DM.defaults(state.p.mode)[s.key];
            updateSlider(s);
            schedule();
        });
        box.appendChild(el);
    });
}

function updateSlider(s) {
    const v = state.p[s.key];
    const t = (v - s.min) / (s.max - s.min);
    /* Two clipped runs of block characters: the bar always spans the full
     * width, whatever the font and panel size (no measuring needed). */
    if (!s.fill) {
        s.bar.innerHTML = '';
        s.fill = document.createElement('div');
        s.fill.className = 'fill';
        s.fill.textContent = '█'.repeat(120);
        s.rest = document.createElement('div');
        s.rest.className = 'rest';
        s.rest.textContent = '█'.repeat(120); /* dim full blocks: ░ is missing in some UI fonts */
        s.bar.appendChild(s.rest);
        s.bar.appendChild(s.fill); /* on top of the track */
    }
    const pct = Math.round(Math.min(1, Math.max(0, t)) * 1000) / 10;
    s.fill.style.width = pct + '%';
    s.fill.style.display = pct > 0 ? 'block' : 'none';
    s.val.textContent = fmt(v, s.step) + s.unit;
    s.name.textContent = typeof s.label === 'function' ? s.label(state.p) : s.label;
    s.el.classList.toggle('hidden', !!s.show && !s.show(state.p));
    if (s.glitch)
        glitchBar(s, pct);
}

/* A few broken fragments around the end of the bar: notches in the fill,
 * stray blocks after it and a cyan/magenta misregistration. They are
 * seeded by the value, so they jump around while the slider moves. */
const GLITCH_COLORS = { fill: '#c6ff3d', notch: '#1b1b1b', cyan: '#2ee6ff', magenta: '#ff3dc8' };
function glitchBar(s, pct) {
    if (!s.gl) {
        s.gl = document.createElement('div');
        s.bar.appendChild(s.gl);
    }
    s.gl.innerHTML = '';
    const W = s.bar.getBoundingClientRect().width;
    if (!W || pct <= 0)
        return;
    const end = W * pct / 100;
    let seed = (Math.round(pct * 10) * 2654435761) >>> 0;
    const rnd = () => {
        seed ^= seed << 13; seed >>>= 0;
        seed ^= seed >>> 17;
        seed ^= seed << 5; seed >>>= 0;
        return seed / 4294967296;
    };
    const frag = (x, w, color, dy, h) => {
        x = Math.max(0, Math.min(W - 1, x));
        w = Math.max(1, Math.min(W - x, w));
        const d = document.createElement('div');
        d.className = 'gl';
        d.style.left = Math.round(x) + 'px';
        d.style.width = Math.round(w) + 'px';
        d.style.backgroundColor = color;
        if (dy) d.style.top = (3 + dy) + 'px';
        if (h) d.style.height = h + 'px';
        s.gl.appendChild(d);
    };
    /* misregistration behind the end */
    frag(end - 3, 5, GLITCH_COLORS.cyan, -2, 0);
    frag(end + 1, 3, GLITCH_COLORS.magenta, 2, 0);
    /* notches cut into the fill */
    for (let i = 0; i < 2; i++)
        if (end > 30) frag(end - 6 - rnd() * 40, 1 + rnd() * 3, GLITCH_COLORS.notch, 0, 0);
    /* stray pieces after the end, thinner the further out */
    let x = end + 4;
    for (let i = 0; i < 3 && x < W; i++) {
        x += rnd() * 10;
        const h = Math.round(11 - i * 3 - rnd() * 2);
        frag(x, 2 + rnd() * 6, GLITCH_COLORS.fill, Math.round((11 - h) * rnd()), h);
        x += 6;
    }
}

/* Segmented switches: data-key names the parameter. */
function syncSegments() {
    document.querySelectorAll('.seg').forEach(seg => {
        const key = seg.dataset.key;
        const v = +state.p[key];
        seg.querySelectorAll('.opt').forEach(o => o.classList.toggle('on', +o.dataset.v === v));
    });
    document.querySelectorAll('.only-0').forEach(e => e.classList.toggle('hidden', state.p.mode !== 0));
    document.querySelectorAll('.only-2').forEach(e => e.classList.toggle('hidden', state.p.mode !== 2));
    SLIDERS.forEach(updateSlider);
    const smart = document.getElementById('smartToggle');
    smart.textContent = (state.smart ? '[x]' : '[ ]') + ' SMART';
    smart.classList.toggle('on', state.smart);
}

function bindSegments() {
    document.querySelectorAll('.seg').forEach(seg => {
        const key = seg.dataset.key;
        seg.querySelectorAll('.opt').forEach(o => o.addEventListener('click', () => {
            const v = +o.dataset.v;
            if (key === 'mode') {
                /* a new mode brings its own defaults for strength and smoothing */
                const d = DM.defaults(v);
                state.p.mode = v;
                state.p.strength = d.strength;
                state.p.mapBlur = d.mapBlur;
                state.p.cell = d.cell;
            } else if (key === 'useLuminance') {
                state.p.useLuminance = !!v;
            } else {
                state.p[key] = v;
            }
            syncSegments();
            schedule();
        }));
    });
}

/* ---------------------------------------------------------------- */
/* Status line                                                       */
/* ---------------------------------------------------------------- */

function log(text, isError) {
    const el = document.getElementById('log');
    el.textContent = '> ' + text.toUpperCase() + '_';
    el.classList.toggle('error', !!isError);
}

/* Small label in the preview's corner; empty text hides it. */
function badge(text, cls) {
    const b = document.getElementById('badge');
    b.textContent = text || '';
    b.className = 'badge' + (cls ? ' ' + cls : '') + (text ? '' : ' hidden');
}

/* Hovering a control explains it in the INFO line. */
function bindInfo() {
    const info = document.getElementById('info');
    const idle = info.textContent;
    document.addEventListener('mouseover', e => {
        let el = e.target;
        while (el && el !== document.body && !(el.dataset && el.dataset.info))
            el = el.parentNode;
        info.textContent = el && el.dataset && el.dataset.info ? 'INFO > ' + el.dataset.info : idle;
    });
}

/* ---------------------------------------------------------------- */
/* Pixels                                                            */
/* ---------------------------------------------------------------- */

function fitSize(w, h, max) {
    const s = Math.min(1, max / Math.max(w, h));
    return { width: Math.max(1, Math.round(w * s)), height: Math.max(1, Math.round(h * s)) };
}

/* Reads pixels of a layer (or the composite when layerID is null) into
 * a float image covering `bounds` of the document (default: all of it),
 * at most `max` pixels on its longest side. Transparent outside the
 * layer's bounds. */
async function readPixels(doc, layerID, max, bounds) {
    const docW = Math.round(doc.width), docH = Math.round(doc.height);
    const b = bounds || { left: 0, top: 0, right: docW, bottom: docH };
    const bw = b.right - b.left, bh = b.bottom - b.top;
    const want = fitSize(bw, bh, max);
    const opts = {
        documentID: doc.id,
        sourceBounds: { left: b.left, top: b.top, right: b.right, bottom: b.bottom },
        targetSize: want,
        componentSize: 8,
        applyAlpha: false,
        colorSpace: 'RGB',
        colorProfile: SRGB
    };
    if (layerID !== null)
        opts.layerID = layerID;
    const res = await imaging.getPixels(opts);
    const id = res.imageData;
    try {
        const bytes = await id.getData({ chunky: true });
        const sb = res.sourceBounds;
        /* With targetSize Photoshop returns a cached pyramid level (res.level,
         * 1/2^level of full size), and then sourceBounds are in that level's
         * pixels. Without a cached level it may return full size. Either way,
         * shrink further here by averaging step×step blocks. */
        const lvl = res.level || 0, sbw = sb.right - sb.left;
        let got, oxL, oyL; /* returned pixels per document pixel, offset in returned pixels */
        if (lvl > 0 && Math.abs(sbw - id.width) <= 1) {
            got = Math.pow(2, -lvl);
            oxL = sb.left - b.left * got;
            oyL = sb.top - b.top * got;
        } else {
            got = id.width / Math.max(1, sbw);
            oxL = (sb.left - b.left) * got;
            oyL = (sb.top - b.top) * got;
        }
        const step = Math.max(1, Math.floor(got / (want.width / bw)));
        const scale = got / step;
        const W = Math.max(1, Math.round(bw * scale)), H = Math.max(1, Math.round(bh * scale));
        const img = DM.image(W, H, 4);
        img.alpha = 3;
        const n = id.components, ox = Math.round(oxL / step), oy = Math.round(oyL / step);
        const sw = Math.floor(id.width / step), sh = Math.floor(id.height / step), k = 1 / (255 * step * step);
        for (let y = 0; y < sh; y++) {
            const ty = y + oy;
            if (ty < 0 || ty >= H) continue;
            for (let x = 0; x < sw; x++) {
                const tx = x + ox;
                if (tx < 0 || tx >= W) continue;
                let r = 0, g = 0, bl = 0, a = 0;
                for (let j = 0; j < step; j++) {
                    let s = ((y * step + j) * id.width + x * step) * n;
                    for (let i = 0; i < step; i++, s += n) {
                        const al = n === 4 ? bytes[s + 3] : 255;
                        r += bytes[s] * al / 255;
                        g += bytes[s + 1] * al / 255;
                        bl += bytes[s + 2] * al / 255;
                        a += al;
                    }
                }
                const d = (ty * W + tx) * 4;
                /* averaged premultiplied, stored straight */
                img.data[d] = a > 0 ? r / a : 0;
                img.data[d + 1] = a > 0 ? g / a : 0;
                img.data[d + 2] = a > 0 ? bl / a : 0;
                img.data[d + 3] = a * k;
            }
        }
        return { img: img, scale: scale, docW: docW, docH: docH, bounds: b };
    } finally {
        id.dispose();
    }
}

/* Half-size copy for fast previews while a slider is dragged. */
function halve(img) {
    const W = Math.max(1, img.width >> 1), H = Math.max(1, img.height >> 1), n = img.channels;
    const out = DM.image(W, H, n);
    out.alpha = img.alpha;
    for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++)
            for (let c = 0; c < n; c++) {
                const a = ((2 * y) * img.width + 2 * x) * n + c;
                const b = a + img.width * n;
                const right = 2 * x + 1 < img.width ? n : 0;
                const down = 2 * y + 1 < img.height ? b : a;
                out.data[(y * W + x) * n + c] =
                    0.25 * (img.data[a] + img.data[a + right] + img.data[down] + img.data[down + right]);
            }
    return out;
}

/* UXP has no object-fit: size and centre the <img> in its box by hand. */
function fitInBox(target, W, H) {
    const box = target.parentNode.getBoundingClientRect();
    if (!box.width || !box.height)
        return;
    const k = Math.min(box.width / W, box.height / H);
    const w = Math.round(W * k), h = Math.round(H * k);
    target.style.width = w + 'px';
    target.style.height = h + 'px';
    target.style.left = Math.round((box.width - w) / 2) + 'px';
    target.style.top = Math.round((box.height - h) / 2) + 'px';
}

/* ---------------------------------------------------------------- */
/* Preview box: size, zoom and pan                                   */
/* ---------------------------------------------------------------- */

/* The preview takes whatever height the panel has left over (at least
 * PREVIEW_MIN_H). Done in script because UXP's flex layout and resize
 * event are unreliable; a timer checks the panel size a few times a
 * second and only touches the layout when something changed. */
const PREVIEW_MIN_H = 200;
const ZOOM_MAX_PX = 8; /* screen pixels per document pixel at most */

function viewportHeight() {
    return window.innerHeight || document.documentElement.clientHeight || document.body.clientHeight || 0;
}

function fitPreviewHeight() {
    const box = document.getElementById('previewBox');
    const app = document.getElementById('app');
    const vh = viewportHeight();
    if (!vh)
        return false;
    const boxH = box.getBoundingClientRect().height;
    const rest = app.getBoundingClientRect().height - boxH;
    const h = Math.max(PREVIEW_MIN_H, Math.floor(vh - rest));
    if (Math.abs(h - boxH) < 2)
        return false;
    box.style.height = h + 'px';
    return true;
}

function watchLayout() {
    const box = document.getElementById('previewBox');
    let last = '';
    const tick = () => {
        fitPreviewHeight();
        const r = box.getBoundingClientRect();
        const key = Math.round(r.width) + 'x' + Math.round(r.height);
        if (key === last)
            return;
        last = key;
        layoutPreview();
        SLIDERS.forEach(s => { if (s.glitch && s.fill) updateSlider(s); });
        if (state.view.z > 1)
            scheduleDetail();
    };
    tick();
    setInterval(tick, 250);
}

/* The view: zoom z (1 = whole image fits) around the document point
 * (cx, cy). Images in the box carry the document rectangle they show. */
function boxSize() {
    const r = document.getElementById('previewBox').getBoundingClientRect();
    return { w: r.width, h: r.height };
}

function docSize() {
    return state.src ? { w: state.src.docW, h: state.src.docH } : null;
}

/* screen pixels per document pixel */
function viewScale() {
    const d = docSize(), b = boxSize();
    if (!d || !b.w || !b.h)
        return 0;
    return Math.min(b.w / d.w, b.h / d.h) * state.view.z;
}

function clampView() {
    const d = docSize(), b = boxSize(), k = viewScale();
    if (!d || !k)
        return;
    const v = state.view;
    const halfW = b.w / (2 * k), halfH = b.h / (2 * k);
    v.cx = d.w <= 2 * halfW ? d.w / 2 : Math.min(d.w - halfW, Math.max(halfW, v.cx));
    v.cy = d.h <= 2 * halfH ? d.h / 2 : Math.min(d.h - halfH, Math.max(halfH, v.cy));
}

/* Places an image that shows document rectangle `r` according to the view. */
function placeImage(img, r) {
    const b = boxSize(), k = viewScale();
    if (!r || !k) {
        img.style.display = 'none';
        return;
    }
    img.style.display = 'block';
    img.style.left = Math.round(b.w / 2 + (r.left - state.view.cx) * k) + 'px';
    img.style.top = Math.round(b.h / 2 + (r.top - state.view.cy) * k) + 'px';
    img.style.width = Math.max(1, Math.round((r.right - r.left) * k)) + 'px';
    img.style.height = Math.max(1, Math.round((r.bottom - r.top) * k)) + 'px';
}

function layoutPreview() {
    clampView();
    placeImage(document.getElementById('preview'), state.shown.base);
    placeImage(document.getElementById('detail'), state.view.z > 1 ? state.shown.detail : null);
    const k = viewScale();
    document.getElementById('zoomInfo').textContent = k ? Math.round(k * 100) + '%' : '';
    document.getElementById('fitBtn').classList.toggle('on', state.view.z === 1);
}

/* Zooms by `f` keeping the document point under (sx, sy) in place. */
function zoomAt(f, sx, sy) {
    const d = docSize();
    if (!d)
        return;
    const b = boxSize(), v = state.view;
    const k0 = viewScale();
    if (!k0)
        return;
    const fitK = k0 / v.z;
    const zMax = Math.max(1, ZOOM_MAX_PX / fitK);
    const z = Math.min(zMax, Math.max(1, v.z * f));
    if (z === v.z)
        return;
    /* the document point under the cursor stays put */
    const px = v.cx + (sx - b.w / 2) / k0, py = v.cy + (sy - b.h / 2) / k0;
    v.z = z;
    const k1 = fitK * z;
    v.cx = px - (sx - b.w / 2) / k1;
    v.cy = py - (sy - b.h / 2) / k1;
    layoutPreview();
    scheduleDetail();
}

function fitView() {
    const d = docSize();
    state.view.z = 1;
    if (d) {
        state.view.cx = d.w / 2;
        state.view.cy = d.h / 2;
    }
    layoutPreview();
}

/* Mouse on the preview, as in Make Me Analog: left click zooms in,
 * right click zooms out, the wheel zooms, double click fits, dragging
 * pans. */
function bindPreviewMouse() {
    const box = document.getElementById('previewBox');
    let down = null, clickTimer = null;
    const local = e => {
        const r = box.getBoundingClientRect();
        return { x: e.clientX - r.left, y: e.clientY - r.top };
    };
    box.addEventListener('mousedown', e => {
        down = { x: e.clientX, y: e.clientY, cx: state.view.cx, cy: state.view.cy, moved: false, button: e.button };
        e.preventDefault();
    });
    document.addEventListener('mousemove', e => {
        if (!down || down.button !== 0)
            return;
        const dx = e.clientX - down.x, dy = e.clientY - down.y;
        if (!down.moved && Math.abs(dx) + Math.abs(dy) < 4)
            return;
        down.moved = true;
        const k = viewScale();
        if (!k || state.view.z === 1)
            return;
        state.view.cx = down.cx - dx / k;
        state.view.cy = down.cy - dy / k;
        layoutPreview();
    });
    document.addEventListener('mouseup', e => {
        if (!down)
            return;
        const d = down;
        down = null;
        if (d.moved) {
            scheduleDetail();
            return;
        }
        const p = local(e);
        if (d.button === 2) {
            zoomAt(0.5, p.x, p.y);
        } else if (d.button === 0) {
            /* wait a moment: a double click fits instead */
            clearTimeout(clickTimer);
            clickTimer = setTimeout(() => zoomAt(2, p.x, p.y), 230);
        }
    });
    box.addEventListener('dblclick', e => {
        clearTimeout(clickTimer);
        fitView();
        e.preventDefault();
    });
    box.addEventListener('contextmenu', e => e.preventDefault());
    box.addEventListener('wheel', e => {
        const p = local(e);
        zoomAt(e.deltaY < 0 ? 1.25 : 0.8, p.x, p.y);
        e.preventDefault();
    });
    document.getElementById('fitBtn').addEventListener('click', fitView);
}

/* ---------------------------------------------------------------- */
/* Detail: a sharper crop of the source while zoomed in              */
/* ---------------------------------------------------------------- */

let detailTimer = null;
function scheduleDetail() {
    clearTimeout(detailTimer);
    detailTimer = setTimeout(loadDetail, 200);
}

/* The document rectangle in view, grown by `pad` document pixels. */
function visibleRect(pad) {
    const d = docSize(), b = boxSize(), k = viewScale(), v = state.view;
    const hw = b.w / (2 * k) + pad, hh = b.h / (2 * k) + pad;
    return {
        left: Math.max(0, Math.floor(v.cx - hw)), top: Math.max(0, Math.floor(v.cy - hh)),
        right: Math.min(d.w, Math.ceil(v.cx + hw)), bottom: Math.min(d.h, Math.ceil(v.cy + hh))
    };
}

function contains(a, b) {
    return a && b && a.left <= b.left && a.top <= b.top && a.right >= b.right && a.bottom >= b.bottom;
}

async function loadDetail() {
    const src = state.src, k = viewScale();
    if (!src || !k || state.view.z === 1)
        return;
    const want = Math.min(1, k); /* never more than the document's own pixels */
    if (want <= src.scale * 1.15) {
        /* the whole-image preview is sharp enough */
        state.detail = null;
        state.shown.detail = null;
        layoutPreview();
        return;
    }
    const det = state.detail;
    const needed = visibleRect(0);
    if (det && det.docId === src.docId && det.layerId === src.layerId &&
        det.scale >= want * 0.95 && contains(det.bounds, needed)) {
        if (state.shown.detail !== det.bounds)
            schedule();
        return;
    }
    /* read a margin around the view: displaced pixels come from outside it,
     * and a small pan should not need a new read */
    const pad = Math.max(32, state.p.strength * 1.5) +
                0.25 * Math.max(needed.right - needed.left, needed.bottom - needed.top);
    const r = visibleRect(pad);
    const doc = app.documents.find(d => d.id === src.docId);
    if (!doc)
        return;
    const max = Math.ceil(Math.max(r.right - r.left, r.bottom - r.top) * want);
    try {
        const res = await core.executeAsModal(() => readPixels(doc, src.layerId, max, r),
                                               { commandName: 'Distort Me: read detail' });
        if (state.src !== src)
            return;
        state.detail = { img: res.img, scale: res.scale, bounds: r, docId: src.docId, layerId: src.layerId };
        schedule();
    } catch (e) {
        log('can\'t read detail: ' + (e.message || e), true);
    }
}

/* Float RGBA -> 8-bit RGB over a dark checkerboard, then JPEG for <img>. */
async function encodeImage(img) {
    const W = img.width, H = img.height;
    const rgb = new Uint8Array(W * H * 3);
    for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++) {
            const s = (y * W + x) * 4, d = (y * W + x) * 3;
            const a = img.alpha >= 0 ? img.data[s + 3] : 1;
            const bg = ((x >> 3) + (y >> 3)) & 1 ? 0.13 : 0.09;
            for (let c = 0; c < 3; c++) {
                const v = (img.data[s + c] * a + bg * (1 - a)) * 255 + 0.5;
                rgb[d + c] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
        }
    const id = await imaging.createImageDataFromBuffer(rgb, {
        width: W, height: H, components: 3, chunky: true, colorSpace: 'RGB', colorProfile: SRGB
    });
    try {
        return 'data:image/jpeg;base64,' + await imaging.encodeImageData({ imageData: id, base64: true });
    } finally {
        id.dispose();
    }
}

/* Shows a whole image fitted into its box (the map thumbnail). */
async function showImage(img, target) {
    target.src = await encodeImage(img);
    fitInBox(target, img.width, img.height);
}

/* Shows a preview image that covers document rectangle `rect`. */
async function showPreview(img, which, rect) {
    const el = document.getElementById(which === 'detail' ? 'detail' : 'preview');
    el.src = await encodeImage(img);
    state.shown[which] = rect;
    layoutPreview();
}

/* ---------------------------------------------------------------- */
/* Preview                                                           */
/* ---------------------------------------------------------------- */

function schedule() {
    state.pending = true;
    if (!state.rendering)
        setTimeout(renderPreview, 0);
}

/* The effect on a preview image rendered at `scale` times the document
 * size. For a crop, origin and full give its place in the whole image. */
function render(srcImg, scale, origin, full) {
    const map = state.map;
    if (state.showOrig || !map)
        return srcImg;
    const q = DM.scaleParams(state.p, scale, map.scale);
    if (origin) {
        q.originX = origin.x;
        q.originY = origin.y;
        q.fullWidth = full.w;
        q.fullHeight = full.h;
    }
    if (!map.prepared || map.prepared.blur !== q.mapBlur || map.prepared.edge !== q.mapEdge)
        map.prepared = DM.prepareMap(map.img, q);
    return DM.apply(srcImg, map.img, q, null, 0, 0, map.prepared);
}

async function renderPreview() {
    if (state.rendering || !state.pending)
        return;
    state.pending = false;
    const src = state.src, map = state.map;
    if (!src) {
        badge('NO SOURCE');
        return;
    }
    state.rendering = true;
    if (!state.dragging) badge('RENDER', 'busy');
    try {
        const whole = { left: 0, top: 0, right: src.docW, bottom: src.docH };
        const fast = state.dragging && src.half;
        const t0 = Date.now();
        const out = render(fast ? src.half : src.img, src.scale * (fast ? 0.5 : 1), null, null);
        await showPreview(out, 'base', whole);
        /* zoomed in: the sharp crop on top */
        const det = state.detail;
        if (state.view.z > 1 && det && det.docId === src.docId) {
            const b = det.bounds;
            const outD = render(det.img, det.scale, { x: b.left * det.scale, y: b.top * det.scale },
                                { w: src.docW * det.scale, h: src.docH * det.scale });
            await showPreview(outD, 'detail', b);
        }
        const ms = Date.now() - t0;
        badge(map || state.showOrig ? '' : 'NO MAP');
        status(state.showOrig ? 'ORIGINAL' : map ? ms + 'MS' : 'PICK A MAP');
    } catch (e) {
        badge('ERROR');
        log(String(e.message || e), true);
    } finally {
        state.rendering = false;
    }
    if (state.pending)
        setTimeout(renderPreview, 0);
}

function status(text) {
    document.getElementById('status').textContent = text;
}

/* ---------------------------------------------------------------- */
/* Source                                                            */
/* ---------------------------------------------------------------- */

async function loadSource() {
    const doc = app.activeDocument;
    if (!doc) {
        log('open a document first', true);
        return;
    }
    const layer = doc.activeLayers[0];
    if (!layer) {
        log('select a layer', true);
        return;
    }
    log('reading layer');
    try {
        const r = await core.executeAsModal(() => readPixels(doc, layer.id, PREVIEW_MAX),
                                             { commandName: 'Distort Me: read layer' });
        const same = state.src && state.src.docId === doc.id && state.src.docW === r.docW && state.src.docH === r.docH;
        state.src = {
            img: r.img, half: halve(r.img), scale: r.scale, docW: r.docW, docH: r.docH,
            docId: doc.id, layerId: layer.id, name: layer.name
        };
        state.detail = null;
        state.shown.detail = null;
        if (same && state.view.z > 1)
            scheduleDetail(); /* reload: keep the view */
        else
            fitView();
        document.getElementById('srcInfo').textContent =
            ('PREVIEW OF > ' + doc.title + ' · ' + layer.name).toUpperCase();
        log('layer loaded');
        schedule();
    } catch (e) {
        log('can\'t read this layer: ' + (e.message || e), true);
    }
}

async function openImage() {
    const file = await fs.getFileForOpening({ types: storage.fileTypes.images });
    if (!file)
        return;
    try {
        await core.executeAsModal(() => app.open(file), { commandName: 'Distort Me: open image' });
        await loadSource();
    } catch (e) {
        log('can\'t open: ' + (e.message || e), true);
    }
}

/* ---------------------------------------------------------------- */
/* Map                                                               */
/* ---------------------------------------------------------------- */

function extOf(name) {
    const i = name.lastIndexOf('.');
    return i < 0 ? '' : name.slice(i + 1).toLowerCase();
}

/* Full-resolution 16-bit copy of a document or layer as a binary PPM,
 * which the native filter can read. Transparent pixels become neutral
 * grey, which is what the core does with them too. */
async function writeMapFile(doc, layerID, baseName) {
    const opts = { documentID: doc.id, componentSize: 16, applyAlpha: false, colorSpace: 'RGB', colorProfile: SRGB,
                   sourceBounds: { left: 0, top: 0, right: Math.round(doc.width), bottom: Math.round(doc.height) } };
    if (layerID !== null)
        opts.layerID = layerID;
    const res = await imaging.getPixels(opts);
    const id = res.imageData;
    let buf;
    try {
        const px = await id.getData({ chunky: true });
        const W = id.width, H = id.height, n = id.components;
        const header = 'P6\n' + W + ' ' + H + '\n65535\n';
        buf = new Uint8Array(header.length + W * H * 6);
        for (let i = 0; i < header.length; i++)
            buf[i] = header.charCodeAt(i);
        let o = header.length;
        for (let i = 0; i < W * H; i++) {
            const a = n === 4 ? px[i * 4 + 3] / 32768 : 1;
            for (let c = 0; c < 3; c++) {
                /* Photoshop's 16-bit range is 0..32768 */
                const v = px[i * n + c] / 32768 * a + 0.5 * (1 - a);
                const u = Math.min(65535, Math.max(0, Math.round(v * 65535)));
                buf[o++] = u >> 8;
                buf[o++] = u & 255;
            }
        }
    } finally {
        id.dispose();
    }
    const data = await fs.getDataFolder();
    let folder;
    try {
        folder = await data.getEntry('maps');
    } catch (e) {
        folder = await data.createFolder('maps');
    }
    const safe = baseName.replace(/[^A-Za-z0-9_-]+/g, '_').slice(0, 40);
    const file = await folder.createFile(safe + '-' + Date.now() + '.ppm', { overwrite: true });
    await file.write(buf.buffer, { format: storage.formats.binary });
    return file.nativePath;
}

function setMap(r, name, path, info) {
    state.map = { img: r.img, prepared: null, scale: r.scale, name: name, path: path, w: r.docW, h: r.docH };
    document.getElementById('mapName').textContent = name.toUpperCase();
    document.getElementById('mapInfo').textContent = r.docW + '×' + r.docH + (info ? ' · ' + info : '');
    showImage(r.img, document.getElementById('mapThumb'));
    schedule();
}

async function pickMapFile() {
    const file = await fs.getFileForOpening({ types: storage.fileTypes.images.concat(['tif', 'tiff', 'ppm', 'pgm']) });
    if (file)
        await loadMapFile(file, file.name, extOf(file.name).toUpperCase());
}

async function loadMapFile(file, name, info) {
    log('reading map');
    const prev = app.activeDocument;
    try {
        let r, path = file.nativePath;
        await core.executeAsModal(async () => {
            /* Photoshop decodes the file, so every format it opens works */
            const doc = await app.open(file);
            try {
                r = await readPixels(doc, null, MAP_PREVIEW_MAX);
                if (STB_FORMATS.indexOf(extOf(file.name)) < 0)
                    path = await writeMapFile(doc, null, file.name);
            } finally {
                await doc.closeWithoutSaving();
                if (prev)
                    app.activeDocument = prev;
            }
        }, { commandName: 'Distort Me: read map' });
        setMap(r, name, path, info);
        log('map loaded');
    } catch (e) {
        log('can\'t read map: ' + (e.message || e), true);
    }
}

/* The built-in library: panel/maps, listed in maps/index.json. */
async function toggleLibrary() {
    const box = document.getElementById('libPick');
    if (!box.classList.contains('hidden')) {
        box.classList.add('hidden');
        return;
    }
    document.getElementById('layerPick').classList.add('hidden');
    if (!box.firstChild) {
        try {
            const folder = await fs.getPluginFolder();
            const index = JSON.parse(await (await folder.getEntry('maps/index.json')).read());
            let row = null;
            index.maps.forEach((m, i) => {
                if (i % 4 === 0) {
                    row = document.createElement('div');
                    row.className = 'lrow';
                    box.appendChild(row);
                }
                const cell = document.createElement('div');
                cell.className = 'cell';
                cell.dataset.info = m.name + ': ' + m.info + '. 2048×2048, tiles seamlessly, CC0.';
                const img = document.createElement('img');
                img.src = 'maps/thumbs/' + m.file;
                const label = document.createElement('div');
                label.className = 'lname';
                label.textContent = m.name.toUpperCase();
                cell.appendChild(img);
                cell.appendChild(label);
                cell.addEventListener('click', async () => {
                    box.classList.add('hidden');
                    try {
                        const file = await folder.getEntry('maps/' + m.file);
                        await loadMapFile(file, m.name, 'LIBRARY');
                    } catch (e) {
                        log('map missing: ' + m.file, true);
                    }
                });
                row.appendChild(cell);
            });
        } catch (e) {
            log('no map library found', true);
            return;
        }
    }
    box.classList.remove('hidden');
}

function allLayers(layers, out, depth) {
    layers.forEach(l => {
        out.push({ layer: l, depth: depth });
        if (l.layers)
            allLayers(l.layers, out, depth + 1);
    });
    return out;
}

function toggleLayerPicker() {
    const box = document.getElementById('layerPick');
    if (!box.classList.contains('hidden')) {
        box.classList.add('hidden');
        return;
    }
    const doc = app.activeDocument;
    if (!doc) {
        log('open a document first', true);
        return;
    }
    document.getElementById('libPick').classList.add('hidden');
    box.innerHTML = '';
    allLayers(doc.layers, [], 0).forEach(({ layer, depth }) => {
        const it = document.createElement('div');
        it.className = 'item';
        it.textContent = '  '.repeat(depth) + (layer.layers ? '▸ ' : '■ ') + layer.name;
        it.addEventListener('click', () => {
            box.classList.add('hidden');
            useLayerAsMap(doc, layer);
        });
        box.appendChild(it);
    });
    box.classList.remove('hidden');
}

async function useLayerAsMap(doc, layer) {
    log('reading map layer');
    try {
        let r, path;
        await core.executeAsModal(async () => {
            r = await readPixels(doc, layer.id, MAP_PREVIEW_MAX);
            path = await writeMapFile(doc, layer.id, layer.name);
        }, { commandName: 'Distort Me: read map layer' });
        setMap(r, layer.name, path, 'LAYER');
        log('map layer loaded');
    } catch (e) {
        log('can\'t read layer: ' + (e.message || e), true);
    }
}

/* ---------------------------------------------------------------- */
/* Apply                                                             */
/* ---------------------------------------------------------------- */

function filterDescriptor(eventKey) {
    const p = state.p;
    return {
        _obj: eventKey,
        $dmMd: p.mode,
        $dmSt: p.strength,
        $dmBl: p.mapBlur,
        $dmFt: p.mapFit,
        $dmSc: p.mapScale,
        $dmOx: p.mapOffsetX,
        $dmOy: p.mapOffsetY,
        $dmME: p.mapEdge,
        $dmSE: p.srcEdge,
        $dmLu: !!p.useLuminance,
        $dmAn: p.angle,
        $dmIr: p.ior,
        $dmFD: p.flowDir,
        $dmFS: p.flowSteps,
        $dmCe: p.cell,
        /* the plug-in reads the path percent-encoded */
        $dmMP: encodeURIComponent(state.map.path),
        _options: { dialogOptions: 'dontDisplay' }
    };
}

async function apply() {
    if (!state.src || !state.map) {
        log(state.src ? 'pick a map first' : 'load a layer first', true);
        return;
    }
    const doc = app.documents.find(d => d.id === state.src.docId);
    if (!doc) {
        log('the source document is closed', true);
        return;
    }
    log('applying');
    try {
        await core.executeAsModal(async ctx => {
            app.activeDocument = doc;
            const suspension = await ctx.hostControl.suspendHistory({ documentID: doc.id, name: 'Distort Me' });
            try {
                await action.batchPlay([{ _obj: 'select', _target: [{ _ref: 'layer', _id: state.src.layerId }], makeVisible: false }], {});
                if (state.smart && doc.activeLayers[0].kind !== 'smartObject')
                    await action.batchPlay([{ _obj: 'newPlacedLayer' }], {});
                await runFilter();
            } finally {
                await ctx.hostControl.resumeHistory(suspension);
            }
        }, { commandName: 'Distort Me' });
        log('applied');
    } catch (e) {
        log('apply failed: ' + (e.message || e), true);
    }
}

/* Tries the filter's event IDs until Photoshop accepts one. */
async function runFilter() {
    let lastError = null;
    for (let i = 0; i < FILTER_EVENTS.length; i++) {
        const k = (state.eventIndex + i) % FILTER_EVENTS.length;
        const res = await action.batchPlay([filterDescriptor(FILTER_EVENTS[k])], {});
        const r = res && res[0];
        if (r && r._obj === 'error') {
            lastError = r.message || ('error ' + r.result);
            continue;
        }
        state.eventIndex = k;
        return;
    }
    throw new Error(lastError || 'filter not found. Is DistortMe.8bf installed?');
}

/* ---------------------------------------------------------------- */
/* Start                                                             */
/* ---------------------------------------------------------------- */

function reset() {
    const keepMode = state.p.mode;
    state.p = DM.defaults(keepMode);
    syncSegments();
    schedule();
}

function init() {
    /* show unexpected errors in the panel instead of failing silently */
    window.addEventListener('error', e => log('error: ' + (e.message || e), true));
    window.addEventListener('unhandledrejection', e => log('error: ' + ((e.reason && e.reason.message) || e.reason), true));
    buildSliders();
    bindSegments();
    bindInfo();
    syncSegments();
    document.getElementById('srcLayer').addEventListener('click', loadSource);
    document.getElementById('srcOpen').addEventListener('click', openImage);
    document.getElementById('mapFile').addEventListener('click', pickMapFile);
    document.getElementById('mapLayer').addEventListener('click', toggleLayerPicker);
    document.getElementById('apply').addEventListener('click', apply);
    document.getElementById('reset').addEventListener('click', reset);
    document.getElementById('mapLib').addEventListener('click', toggleLibrary);
    bindPreviewMouse();
    const setOrig = on => {
        state.showOrig = on;
        document.getElementById('viewOrig').classList.toggle('on', on);
        document.getElementById('viewResult').classList.toggle('on', !on);
        schedule();
    };
    document.getElementById('viewResult').addEventListener('click', () => setOrig(false));
    document.getElementById('viewOrig').addEventListener('click', () => setOrig(true));
    watchLayout();
    document.getElementById('smartToggle').addEventListener('click', () => {
        state.smart = !state.smart;
        syncSegments();
    });
    if (app.activeDocument)
        loadSource();
}

init();
