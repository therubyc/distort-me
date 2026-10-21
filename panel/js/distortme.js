/*
 * Distort Me - JavaScript port of the core (src/distortme.c) for the
 * panel's live preview. Same algorithms and defaults as the C core, so
 * the preview matches what the filter renders. Keep both in sync.
 *
 * Works in Photoshop's UXP, in browsers and in Node (module.exports).
 *
 * SPDX-License-Identifier: MIT
 */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.DistortMe = factory();
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MODE = { DISPLACE: 0, REFRACT: 1, FLOW: 2 };
    var EDGE = { CLAMP: 0, WRAP: 1, MIRROR: 2 };
    var FIT = { STRETCH: 0, TILE: 1 };
    var FLOW = { ALONG_CONTOURS: 0, UPHILL: 1 };

    /* An image: { width, height, channels, alpha (-1 = none), data: Float32Array } */
    function image(width, height, channels) {
        return { width: width, height: height, channels: channels, alpha: -1,
                 data: new Float32Array(width * height * channels) };
    }

    function defaults(mode) {
        var p = {
            mode: mode || MODE.DISPLACE, strength: 20, mapFit: FIT.STRETCH, mapScale: 1,
            mapOffsetX: 0, mapOffsetY: 0, mapEdge: EDGE.WRAP, mapBlur: 0,
            srcEdge: EDGE.CLAMP, useLuminance: false, angle: 0, ior: 1.5,
            flowDir: FLOW.ALONG_CONTOURS, flowSteps: 32
        };
        if (p.mode === MODE.REFRACT) { p.strength = 40; p.mapBlur = 1; }
        else if (p.mode === MODE.FLOW) { p.strength = 30; p.mapBlur = 2; }
        return p;
    }

    /* Settings for a preview rendered at `s` times the document size
     * (e.g. 0.25). Everything measured in pixels shrinks with it. The map
     * is assumed to be shrunk by `mapS` (defaults to `s`). */
    function scaleParams(p, s, mapS) {
        if (mapS === undefined) mapS = s;
        var q = Object.assign({}, p);
        q.strength = p.strength * s;
        q.mapOffsetX = p.mapOffsetX * s;
        q.mapOffsetY = p.mapOffsetY * s;
        q.mapBlur = p.mapBlur * mapS;
        if (p.mapFit === FIT.TILE) q.mapScale = p.mapScale * s / mapS;
        return q;
    }

    function edgeIndex(i, n, edge) {
        if (i >= 0 && i < n) return i;
        if (edge === EDGE.WRAP) { i %= n; return i < 0 ? i + n : i; }
        if (edge === EDGE.MIRROR) {
            var period = 2 * n;
            i %= period;
            if (i < 0) i += period;
            return i < n ? i : period - 1 - i;
        }
        return i < 0 ? 0 : n - 1;
    }

    function planeSample(plane, w, h, x, y, edge) {
        var fx = Math.floor(x), fy = Math.floor(y);
        var tx = x - fx, ty = y - fy;
        var x0 = edgeIndex(fx, w, edge), x1 = edgeIndex(fx + 1, w, edge);
        var y0 = edgeIndex(fy, h, edge), y1 = edgeIndex(fy + 1, h, edge);
        var a = plane[y0 * w + x0], b = plane[y0 * w + x1];
        var c = plane[y1 * w + x0], d = plane[y1 * w + x1];
        var top = a + (b - a) * tx, bot = c + (d - c) * tx;
        return top + (bot - top) * ty;
    }

    function imageSample(img, x, y, edge, out, o) {
        x -= 0.5; y -= 0.5;
        var fx = Math.floor(x), fy = Math.floor(y);
        var tx = x - fx, ty = y - fy;
        var W = img.width, H = img.height, n = img.channels, ai = img.alpha, d = img.data;
        var x0 = edgeIndex(fx, W, edge), x1 = edgeIndex(fx + 1, W, edge);
        var y0 = edgeIndex(fy, H, edge), y1 = edgeIndex(fy + 1, H, edge);
        var p00 = (y0 * W + x0) * n, p10 = (y0 * W + x1) * n;
        var p01 = (y1 * W + x0) * n, p11 = (y1 * W + x1) * n;
        var w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty);
        var w01 = (1 - tx) * ty, w11 = tx * ty;
        var c;
        if (ai < 0) {
            for (c = 0; c < n; c++)
                out[o + c] = d[p00 + c] * w00 + d[p10 + c] * w10 + d[p01 + c] * w01 + d[p11 + c] * w11;
            return;
        }
        var a00 = d[p00 + ai], a10 = d[p10 + ai], a01 = d[p01 + ai], a11 = d[p11 + ai];
        var a = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
        for (c = 0; c < n; c++) {
            if (c === ai) continue;
            var v = d[p00 + c] * a00 * w00 + d[p10 + c] * a10 * w10
                  + d[p01 + c] * a01 * w01 + d[p11 + c] * a11 * w11;
            out[o + c] = a > 1e-6 ? v / a : 0;
        }
        out[o + ai] = a;
    }

    function boxBlur(plane, w, h, radius, edge) {
        if (radius <= 0) return;
        var r = Math.ceil(radius);
        var frac = radius - (r - 1);
        var norm = 1 / (2 * (r - 1) + 1 + 2 * frac);
        var tmp = new Float32Array(Math.max(w, h));
        var x, y, k, s;
        for (y = 0; y < h; y++) {
            var row = y * w;
            for (x = 0; x < w; x++) {
                s = 0;
                for (k = -r; k <= r; k++)
                    s += plane[row + edgeIndex(x + k, w, edge)] * ((k === -r || k === r) ? frac : 1);
                tmp[x] = s * norm;
            }
            for (x = 0; x < w; x++) plane[row + x] = tmp[x];
        }
        for (x = 0; x < w; x++) {
            for (y = 0; y < h; y++) {
                s = 0;
                for (k = -r; k <= r; k++)
                    s += plane[edgeIndex(y + k, h, edge) * w + x] * ((k === -r || k === r) ? frac : 1);
                tmp[y] = s * norm;
            }
            for (y = 0; y < h; y++) plane[y * w + x] = tmp[y];
        }
    }

    /* Splits the map into the planes the modes need. Depends only on the
     * map, mapBlur and mapEdge, so the panel can cache it while other
     * sliders move. */
    function prepareMap(map, p) {
        var w = map.width, h = map.height, n = w * h, ch = map.channels, d = map.data;
        var m = { w: w, h: h, blur: p.mapBlur, edge: p.mapEdge,
                  r: new Float32Array(n), g: new Float32Array(n), lum: new Float32Array(n),
                  gx: new Float32Array(n), gy: new Float32Array(n) };
        for (var i = 0; i < n; i++) {
            var px = i * ch;
            var r = d[px], g = ch >= 3 ? d[px + 1] : d[px], b = ch >= 3 ? d[px + 2] : d[px];
            var a = ch === 2 ? d[px + 1] : (ch >= 4 ? d[px + 3] : 1);
            r = r * a + 0.5 * (1 - a);
            g = g * a + 0.5 * (1 - a);
            b = b * a + 0.5 * (1 - a);
            m.r[i] = r;
            m.g[i] = g;
            m.lum[i] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        }
        boxBlur(m.r, w, h, p.mapBlur, p.mapEdge);
        boxBlur(m.g, w, h, p.mapBlur, p.mapEdge);
        boxBlur(m.lum, w, h, p.mapBlur, p.mapEdge);
        for (var y = 0; y < h; y++) {
            for (var x = 0; x < w; x++) {
                var xl = edgeIndex(x - 1, w, p.mapEdge), xr = edgeIndex(x + 1, w, p.mapEdge);
                var yu = edgeIndex(y - 1, h, p.mapEdge), yd = edgeIndex(y + 1, h, p.mapEdge);
                m.gx[y * w + x] = 0.5 * (m.lum[y * w + xr] - m.lum[y * w + xl]);
                m.gy[y * w + x] = 0.5 * (m.lum[yd * w + x] - m.lum[yu * w + x]);
            }
        }
        return m;
    }

    /* Renders `dst` (a window of `src` at left/top, or the whole of it).
     * `prepared` is optional: pass prepareMap()'s result to reuse it. */
    function apply(src, map, p, dst, left, top, prepared) {
        left = left || 0; top = top || 0;
        if (!dst) { dst = image(src.width, src.height, src.channels); dst.alpha = src.alpha; }
        if (dst.channels !== src.channels || left + dst.width > src.width || top + dst.height > src.height)
            throw new Error('distortme: bad arguments');

        var m = prepared && prepared.blur === p.mapBlur && prepared.edge === p.mapEdge
            ? prepared : prepareMap(map, p);
        var sx, sy;
        if (p.mapFit === FIT.TILE) {
            sx = sy = 1 / Math.max(p.mapScale, 1e-4);
        } else {
            sx = map.width / src.width;
            sy = map.height / src.height;
        }
        var mw = m.w, mh = m.h, me = p.mapEdge, ox = p.mapOffsetX, oy = p.mapOffsetY;
        var strength = p.strength;
        var cosA = Math.cos(p.angle * Math.PI / 180), sinA = Math.sin(p.angle * Math.PI / 180);
        var eta = 1 / Math.max(p.ior, 1e-3);
        var steps = p.flowSteps > 0 ? p.flowSteps : 1, hstep = strength / steps;
        var uphill = p.flowDir === FLOW.UPHILL, lumMode = !!p.useLuminance;
        var res = [0, 0];

        function grad(x, y) {
            var u = (x - ox) * sx - 0.5, v = (y - oy) * sy - 0.5;
            res[0] = planeSample(m.gx, mw, mh, u, v, me) * sx;
            res[1] = planeSample(m.gy, mw, mh, u, v, me) * sy;
        }
        function flowDir(x, y) {
            grad(x, y);
            var gx = res[0], gy = res[1], len = Math.sqrt(gx * gx + gy * gy);
            if (len < 1e-7) { res[0] = res[1] = 0; return; }
            var fade = len / (len + 1e-3);
            if (uphill) { res[0] = gx / len * fade; res[1] = gy / len * fade; }
            else { res[0] = -gy / len * fade; res[1] = gx / len * fade; }
        }
        function shade(x, y) {
            var u, v;
            if (p.mode === MODE.REFRACT) {
                grad(x, y);
                var ssx = res[0] * strength, ssy = res[1] * strength;
                var inv = 1 / Math.sqrt(ssx * ssx + ssy * ssy + 1);
                var nx = -ssx * inv, ny = -ssy * inv, nz = inv;
                var k = 1 - eta * eta * (1 - nz * nz);
                if (k < 0) { res[0] = x; res[1] = y; return; }
                var f = eta * nz - Math.sqrt(k);
                var tz = -eta + f * nz;
                var depth = strength / (tz < -1e-3 ? -tz : 1e-3);
                res[0] = x + f * nx * depth;
                res[1] = y + f * ny * depth;
            } else if (p.mode === MODE.FLOW) {
                var px = x, py = y;
                for (var i = 0; i < steps; i++) {
                    flowDir(px, py);
                    var d1x = res[0], d1y = res[1];
                    flowDir(px + d1x * hstep * 0.5, py + d1y * hstep * 0.5);
                    px += res[0] * hstep;
                    py += res[1] * hstep;
                }
                res[0] = px; res[1] = py;
            } else {
                u = (x - ox) * sx - 0.5; v = (y - oy) * sy - 0.5;
                if (lumMode) {
                    var dd = (planeSample(m.lum, mw, mh, u, v, me) - 0.5) * 2 * strength;
                    res[0] = x + dd * cosA; res[1] = y + dd * sinA;
                } else {
                    res[0] = x + (planeSample(m.r, mw, mh, u, v, me) - 0.5) * 2 * strength;
                    res[1] = y + (planeSample(m.g, mw, mh, u, v, me) - 0.5) * 2 * strength;
                }
            }
        }

        var W = dst.width, n = dst.channels;
        for (var y = 0; y < dst.height; y++) {
            for (var x = 0; x < W; x++) {
                shade(x + left + 0.5, y + top + 0.5);
                imageSample(src, res[0], res[1], p.srcEdge, dst.data, (y * W + x) * n);
            }
        }
        return dst;
    }

    /* 8-bit interleaved buffer (e.g. from getPixels / ImageData) -> image */
    function fromBytes(bytes, width, height, channels, alphaIndex) {
        var img = image(width, height, channels);
        img.alpha = alphaIndex === undefined ? (channels === 4 || channels === 2 ? channels - 1 : -1) : alphaIndex;
        for (var i = 0; i < img.data.length; i++) img.data[i] = bytes[i] / 255;
        return img;
    }

    function toBytes(img, out) {
        out = out || new Uint8Array(img.data.length);
        for (var i = 0; i < img.data.length; i++) {
            var v = img.data[i] * 255 + 0.5;
            out[i] = v < 0 ? 0 : v > 255 ? 255 : v;
        }
        return out;
    }

    return { MODE: MODE, EDGE: EDGE, FIT: FIT, FLOW: FLOW, image: image, defaults: defaults,
             scaleParams: scaleParams, prepareMap: prepareMap, apply: apply,
             fromBytes: fromBytes, toBytes: toBytes };
});
