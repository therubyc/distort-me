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
 * a float image covering the whole document, at most `max` pixels on
 * its longest side. Transparent outside the layer's bounds. */
async function readPixels(doc, layerID, max) {
    const docW = Math.round(doc.width), docH = Math.round(doc.height);
    const opts = {
        documentID: doc.id,
        sourceBounds: { left: 0, top: 0, right: docW, bottom: docH },
        targetSize: fitSize(docW, docH, max),
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
        const want = fitSize(docW, docH, max);
        const lvl = res.level || 0, sbw = sb.right - sb.left;
        let got, oxL, oyL; /* returned pixels per document pixel, offset in returned pixels */
        if (lvl > 0 && Math.abs(sbw - id.width) <= 1) {
            got = Math.pow(2, -lvl);
            oxL = sb.left;
            oyL = sb.top;
        } else {
            got = id.width / Math.max(1, sbw);
            oxL = sb.left * got;
            oyL = sb.top * got;
        }
        const step = Math.max(1, Math.floor(got / (want.width / docW)));
        const scale = got / step;
        const W = Math.max(1, Math.round(docW * scale)), H = Math.max(1, Math.round(docH * scale));
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
                let r = 0, g = 0, b = 0, a = 0;
                for (let j = 0; j < step; j++) {
                    let s = ((y * step + j) * id.width + x * step) * n;
                    for (let i = 0; i < step; i++, s += n) {
                        const al = n === 4 ? bytes[s + 3] : 255;
                        r += bytes[s] * al / 255;
                        g += bytes[s + 1] * al / 255;
                        b += bytes[s + 2] * al / 255;
                        a += al;
                    }
                }
                const d = (ty * W + tx) * 4;
                /* averaged premultiplied, stored straight */
                const am = a * k;
                img.data[d] = a > 0 ? r / a : 0;
                img.data[d + 1] = a > 0 ? g / a : 0;
                img.data[d + 2] = a > 0 ? b / a : 0;
                img.data[d + 3] = am;
            }
        }
        return { img: img, scale: scale, docW: docW, docH: docH };
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

/* The preview box grows with the panel. UXP's resize event is unreliable,
 * so check its size now and then and re-fit the image when it changed. */
function watchPreviewSize() {
    const box = document.getElementById('previewBox');
    const img = document.getElementById('preview');
    let last = '';
    setInterval(() => {
        const r = box.getBoundingClientRect();
        const key = Math.round(r.width) + 'x' + Math.round(r.height);
        if (key === last)
            return;
        last = key;
        if (img.dataset.w)
            fitInBox(img, +img.dataset.w, +img.dataset.h);
        SLIDERS.forEach(s => { if (s.glitch && s.fill) updateSlider(s); });
    }, 400);
}

/* Float RGBA -> 8-bit RGB over a dark checkerboard, then JPEG for <img>. */
async function showImage(img, target) {
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
        const jpeg = await imaging.encodeImageData({ imageData: id, base64: true });
        target.src = 'data:image/jpeg;base64,' + jpeg;
        target.dataset.w = W;
        target.dataset.h = H;
        fitInBox(target, W, H);
    } finally {
        id.dispose();
    }
}

/* ---------------------------------------------------------------- */
/* Preview                                                           */
/* ---------------------------------------------------------------- */

function schedule() {
    state.pending = true;
    if (!state.rendering)
        setTimeout(renderPreview, 0);
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
    if (!map) {
        badge('NO MAP');
        await showImage(src.img, document.getElementById('preview'));
        status('PICK A MAP');
        return;
    }
    state.rendering = true;
    if (!state.dragging) badge('RENDER', 'busy');
    try {
        const fast = state.dragging && src.half;
        const img = fast ? src.half : src.img;
        const s = src.scale * (fast ? 0.5 : 1);
        const q = DM.scaleParams(state.p, s, map.scale);
        if (!map.prepared || map.prepared.blur !== q.mapBlur || map.prepared.edge !== q.mapEdge)
            map.prepared = DM.prepareMap(map.img, q);
        const t0 = Date.now();
        const out = DM.apply(img, map.img, q, null, 0, 0, map.prepared);
        const ms = Date.now() - t0;
        await showImage(out, document.getElementById('preview'));
        badge('');
        status(Math.round(s * 100) + '% · ' + ms + 'MS');
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
        state.src = {
            img: r.img, half: halve(r.img), scale: r.scale, docW: r.docW, docH: r.docH,
            docId: doc.id, layerId: layer.id, name: layer.name
        };
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
    watchPreviewSize();
    document.getElementById('smartToggle').addEventListener('click', () => {
        state.smart = !state.smart;
        syncSegments();
    });
    if (app.activeDocument)
        loadSource();
}

init();
