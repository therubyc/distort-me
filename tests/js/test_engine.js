/* Tests for the JavaScript port of the core (panel/js/distortme.js).
 * Run: node tests/js/test_engine.js
 * SPDX-License-Identifier: MIT */
'use strict';
const path = require('path');
const D = require(path.join(__dirname, '..', '..', 'panel', 'js', 'distortme.js'));

let failed = 0;
function check(name, ok) {
    console.log((ok ? 'ok   ' : 'FAIL ') + name);
    if (!ok) failed++;
}

function gradientImage(w, h) {
    const img = D.image(w, h, 4);
    img.alpha = 3;
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
            const o = (y * w + x) * 4;
            img.data[o] = x / (w - 1);
            img.data[o + 1] = y / (h - 1);
            img.data[o + 2] = ((x * 7 + y * 3) % 17) / 16;
            img.data[o + 3] = 1;
        }
    return img;
}
function flatMap(w, h, v) {
    const m = D.image(w, h, 3);
    m.data.fill(v);
    return m;
}
function maxDiff(a, b) {
    let d = 0;
    for (let i = 0; i < a.data.length; i++) d = Math.max(d, Math.abs(a.data[i] - b.data[i]));
    return d;
}

const src = gradientImage(40, 30);

for (const mode of [D.MODE.DISPLACE, D.MODE.REFRACT, D.MODE.FLOW]) {
    const out = D.apply(src, flatMap(16, 16, 0.5), D.defaults(mode));
    check('flat grey map leaves image unchanged, mode ' + mode, maxDiff(src, out) < 1e-5);
}

{   /* red 0.75 = shift by +strength/2 in x: pixel x shows source x + 5 */
    const map = flatMap(8, 8, 0.5);
    for (let i = 0; i < map.data.length; i += 3) map.data[i] = 0.75;
    const p = D.defaults(D.MODE.DISPLACE);
    p.strength = 10;
    const out = D.apply(src, map, p);
    let ok = true;
    for (let y = 0; y < 30; y++)
        for (let x = 0; x < 30; x++)
            ok = ok && Math.abs(out.data[(y * 40 + x) * 4] - src.data[(y * 40 + x + 5) * 4]) < 1e-5;
    check('displace shifts by strength', ok);
}

{   /* a window renders the same pixels as the full image */
    const map = gradientImage(13, 11);
    for (const mode of [D.MODE.DISPLACE, D.MODE.REFRACT, D.MODE.FLOW]) {
        const p = D.defaults(mode);
        const full = D.apply(src, map, p);
        const part = D.image(10, 8, 4);
        part.alpha = 3;
        D.apply(src, map, p, part, 12, 9);
        let d = 0;
        for (let y = 0; y < 8; y++)
            for (let x = 0; x < 10; x++)
                for (let c = 0; c < 4; c++)
                    d = Math.max(d, Math.abs(part.data[(y * 10 + x) * 4 + c] - full.data[((y + 9) * 40 + x + 12) * 4 + c]));
        check('window matches full render, mode ' + mode, d < 1e-6);
    }
}

{   /* reusing a prepared map gives the same result */
    const map = gradientImage(13, 11);
    const p = D.defaults(D.MODE.REFRACT);
    const prepared = D.prepareMap(map, p);
    check('prepared map reuse', maxDiff(D.apply(src, map, p), D.apply(src, map, p, null, 0, 0, prepared)) === 0);
}

{   /* half-size preview: shifts halve too */
    const p = D.defaults(D.MODE.DISPLACE);
    p.strength = 10;
    const q = D.scaleParams(p, 0.5);
    check('scaleParams halves strength', q.strength === 5 && p.strength === 10);
}

{
    const bytes = new Uint8Array([0, 128, 255, 255]);
    const img = D.fromBytes(bytes, 1, 1, 4);
    const back = D.toBytes(img);
    check('8-bit round trip', img.alpha === 3 && back.every((v, i) => v === bytes[i]));
}

if (failed) {
    console.log(failed + ' test(s) failed');
    process.exit(1);
}
console.log('all tests passed');
