#!/usr/bin/env python3
"""Generate the built-in map library of Distort Me.

Every map is made from scratch here (no photos, no third-party data), so the
results are ours to give away: they are released under CC0 1.0, like the
rest of panel/maps. All maps tile seamlessly, because they are built from
periodic noise (FFT-filtered white noise), integer wave counts and a
toroidal Voronoi.

    python3 tools/maps/make_maps.py [out_dir] [--size 2048]

Writes <name>.png (8-bit grey, or 16-bit with --16) and thumbs/<name>.png
(96 px) per map, plus index.json with names and descriptions for the panel.
8 bits are plenty here: a 40 px shift spread over 256 levels moves in
steps of 0.16 px, and 16-bit noise does not compress (90 MB vs 28 MB).
"""
import json
import os
import sys

import numpy as np
from PIL import Image

TAU = 2.0 * np.pi


class Gen:
    def __init__(self, n, seed):
        self.n = n
        self.rng = np.random.default_rng(seed)
        f = np.fft.fftfreq(n) * n  # integer frequencies
        self.kx, self.ky = np.meshgrid(f, f)
        c = (np.arange(n) + 0.5) / n
        self.u, self.v = np.meshgrid(c, c)  # 0..1, x right, y down

    # -- periodic noise -------------------------------------------------
    def noise(self, beta=2.0, kmin=1.0, kmax=None, ax=1.0, ay=1.0):
        """Gaussian noise with a 1/k^beta spectrum, zero mean, unit std."""
        n = self.n
        kmax = kmax or n / 2
        k = np.sqrt((self.kx * ax) ** 2 + (self.ky * ay) ** 2)
        filt = np.where((k >= kmin) & (k <= kmax), 1.0 / np.maximum(k, 1e-6) ** (beta / 2), 0.0)
        # soft roll-off at the top so nothing aliases
        filt *= np.exp(-(k / kmax) ** 4)
        w = self.rng.standard_normal((n, n))
        out = np.real(np.fft.ifft2(np.fft.fft2(w) * filt))
        out -= out.mean()
        return out / (out.std() + 1e-12)

    def sample(self, img, x, y):
        """Bilinear lookup with wrap-around; x, y in pixels."""
        n = img.shape[0]
        x0 = np.floor(x).astype(np.int64)
        y0 = np.floor(y).astype(np.int64)
        fx = x - x0
        fy = y - y0
        x0 %= n
        y0 %= n
        x1 = (x0 + 1) % n
        y1 = (y0 + 1) % n
        a = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
        b = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
        return a * (1 - fy) + b * fy

    def warp(self, img, dx, dy, amount):
        """Displace img by (dx, dy) * amount pixels."""
        n = self.n
        ix, iy = np.meshgrid(np.arange(n, dtype=np.float64), np.arange(n, dtype=np.float64))
        return self.sample(img, ix + dx * amount, iy + dy * amount)

    # -- toroidal Voronoi on a jittered grid ----------------------------
    def voronoi(self, cells, u=None, v=None, jitter=0.9):
        """F1, F2 (in cell units) and nearest cell id at (u, v) in 0..1."""
        u = self.u if u is None else u
        v = self.v if v is None else v
        g = cells
        jx = self.rng.random((g, g)) * jitter + (1 - jitter) / 2
        jy = self.rng.random((g, g)) * jitter + (1 - jitter) / 2
        px = (u % 1.0) * g
        py = (v % 1.0) * g
        cx = np.floor(px).astype(np.int64)
        cy = np.floor(py).astype(np.int64)
        f1 = np.full(px.shape, 1e9)
        f2 = np.full(px.shape, 1e9)
        ident = np.zeros(px.shape, dtype=np.int64)
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                ncx = cx + ox
                ncy = cy + oy
                wx = ncx % g
                wy = ncy % g
                qx = ncx + jx[wy, wx]
                qy = ncy + jy[wy, wx]
                d = np.hypot(px - qx, py - qy)
                closer = d < f1
                f2 = np.where(closer, f1, np.minimum(f2, d))
                ident = np.where(closer, wy * g + wx, ident)
                f1 = np.where(closer, d, f1)
        return f1, f2, ident, (jx, jy)


def stretch(a, lo=0.3, hi=99.7):
    """Map the lo..hi percentiles to 0..1."""
    p0, p1 = np.percentile(a, [lo, hi])
    return np.clip((a - p0) / (p1 - p0 + 1e-12), 0.0, 1.0)


def blur(g, img, radius):
    """Gaussian blur through the FFT (periodic, so tiling stays seamless)."""
    s = radius
    k2 = (g.kx ** 2 + g.ky ** 2) / g.n ** 2
    filt = np.exp(-2 * (np.pi ** 2) * (s ** 2) * k2)
    return np.real(np.fft.ifft2(np.fft.fft2(img) * filt))


# ---------------------------------------------------------------------------
# the maps


def marble(g):
    n = g.n
    qx = g.noise(beta=3.4, kmin=1, kmax=30)
    qy = g.noise(beta=3.4, kmin=1, kmax=30)
    r1 = g.warp(g.noise(beta=2.8, kmin=2, kmax=60), qx, qy, n * 0.03)
    r2 = g.warp(g.noise(beta=2.6, kmin=4, kmax=200), qy, qx, n * 0.03)
    veins = np.exp(-np.abs(r1) * 5)           # ridged noise: thin, branching veins
    hair = np.exp(-np.abs(r2) * 22)
    base = 0.6 + 0.12 * g.noise(beta=3.4, kmin=1, kmax=60)
    return stretch(base - 0.5 * veins - 0.08 * hair + 0.01 * g.noise(beta=1.0, kmin=300))


def wood(g):
    w = g.noise(beta=3.0, kmin=1, kmax=60, ax=6.0, ay=1.0)
    fine = g.noise(beta=1.6, kmin=8, kmax=900, ax=12.0, ay=1.0)
    rings = np.sin(TAU * (14 * g.v + 0.8 * w))
    late = np.abs(rings) ** 3
    return stretch(0.5 + 0.35 * rings + 0.25 * late + 0.07 * fine)


def caustics(g):
    dx = g.noise(beta=3.0, kmin=1, kmax=40)
    dy = g.noise(beta=3.0, kmin=1, kmax=40)
    u = g.u + 0.035 * dx
    v = g.v + 0.035 * dy
    f1, f2, _, _ = g.voronoi(9, u, v)
    web = np.exp(-(f2 - f1) * 9.0)
    f1b, f2b, _, _ = g.voronoi(15, u + 0.2, v + 0.37)
    web2 = np.exp(-(f2b - f1b) * 12.0)
    return stretch(web + 0.55 * web2, 1, 99.8) ** 1.4


def crumpled_paper(g):
    h = np.zeros((g.n, g.n))
    for cells, amp in ((5, 1.0), (11, 0.55), (23, 0.3)):
        dx = g.noise(beta=3.2, kmin=1, kmax=30)
        dy = g.noise(beta=3.2, kmin=1, kmax=30)
        u = g.u + 0.02 * dx
        v = g.v + 0.02 * dy
        f1, f2, ident, (jx, jy) = g.voronoi(cells, u, v)
        # F2 - F1 is zero on the cell borders and rises to the middle:
        # continuous, nearly flat facets that meet in sharp creases
        h += amp * (f2 - f1) / cells
    shade = blur(g, h, 1.2)
    # light from the top left turns the facets into a paper surface
    sx = np.roll(shade, -1, axis=1) - np.roll(shade, 1, axis=1)
    sy = np.roll(shade, -1, axis=0) - np.roll(shade, 1, axis=0)
    lit = -(sx + sy) * g.n
    fibre = g.noise(beta=1.2, kmin=300)
    return stretch(lit + 0.02 * fibre, 0.5, 99.5)


def brushed_metal(g):
    streak = g.noise(beta=1.5, kmin=2, kmax=1000, ax=40.0, ay=1.0)
    long = g.noise(beta=3.0, kmin=1, kmax=20, ax=8.0, ay=1.0)
    grain = g.noise(beta=0.5, kmin=400)
    return stretch(0.5 + 0.30 * streak + 0.18 * long + 0.05 * grain)


def woven_fabric(g):
    n_threads = 96
    tx = g.u * n_threads
    ty = g.v * n_threads
    ix = np.floor(tx).astype(int)
    iy = np.floor(ty).astype(int)
    fx = tx - ix
    fy = ty - iy
    prof_x = np.sin(np.pi * fx) ** 0.6  # round threads
    prof_y = np.sin(np.pi * fy) ** 0.6
    # 2/2 twill: diagonal over-under rhythm
    over = ((ix + iy) % 4) < 2
    # each thread rises and falls along its length
    bend_x = 0.5 + 0.5 * np.sin(np.pi * fy + np.pi * ((ix + iy) % 4 < 2))
    bend_y = 0.5 + 0.5 * np.sin(np.pi * fx + np.pi * ((ix + iy) % 4 >= 2))
    h = np.where(over, prof_x * (0.6 + 0.4 * bend_x), prof_y * (0.6 + 0.4 * bend_y))
    slub = g.noise(beta=2.0, kmin=4, kmax=300)
    fuzz = g.noise(beta=0.8, kmin=500)
    return stretch(h + 0.10 * slub + 0.06 * fuzz)


def ripples(g):
    h = np.zeros((g.n, g.n))
    dx = g.noise(beta=3.0, kmin=1, kmax=20)
    dy = g.noise(beta=3.0, kmin=1, kmax=20)
    u = g.u + 0.01 * dx
    v = g.v + 0.01 * dy
    for _ in range(14):
        ang = 0.4 + g.rng.normal(0, 0.35)  # waves pushed by one wind
        k = g.rng.uniform(5, 22)
        kx = round(np.cos(ang) * k)
        ky = round(np.sin(ang) * k)
        h += np.cos(TAU * (kx * u + ky * v) + g.rng.random() * TAU) / np.hypot(kx, ky) ** 0.5
    return stretch(h)


def cracked_earth(g):
    dx = g.noise(beta=3.0, kmin=1, kmax=60)
    dy = g.noise(beta=3.0, kmin=1, kmax=60)
    u = g.u + 0.012 * dx
    v = g.v + 0.012 * dy
    f1, f2, _, _ = g.voronoi(14, u, v)
    crack = np.clip((f2 - f1) * 14, 0, 1) ** 0.5
    f1s, f2s, _, _ = g.voronoi(37, u + 0.5, v + 0.5)
    small = np.clip((f2s - f1s) * 20, 0, 1) ** 0.4
    dome = 1 - f1 * 0.6
    dirt = g.noise(beta=1.8, kmin=8)
    return stretch(crack * (0.7 + 0.3 * small) * dome + 0.06 * dirt)


def clouds(g):
    return stretch(g.noise(beta=2.6, kmin=1, kmax=700))


def vhs_bands(g):
    n = g.n
    rows = np.zeros(n)
    rng = g.rng
    f = np.fft.fftfreq(n) * n
    w = rng.standard_normal(n)
    filt = np.where(np.abs(f) >= 1, 1 / np.maximum(np.abs(f), 1) ** 0.9, 0)
    rows = np.real(np.fft.ifft(np.fft.fft(w) * filt))
    rows /= rows.std()
    # hard bands: a few tracking errors with sharp edges
    for _ in range(18):
        y0 = rng.integers(0, n)
        h = int(rng.integers(3, 60))
        rows[np.arange(y0, y0 + h) % n] += rng.normal(0, 2.2)
    img = np.repeat(rows[:, None], n, axis=1)
    jitter = g.noise(beta=1.0, kmin=60, ax=0.02, ay=1.0)
    return stretch(img + 0.25 * jitter, 0.5, 99.5)


def reeded_glass(g):
    flutes = 48
    f = (g.u * flutes) % 1.0
    lens = np.sqrt(np.clip(1 - (2 * f - 1) ** 2, 0, 1))
    wobble = g.noise(beta=3.0, kmin=1, kmax=12, ax=4.0, ay=1.0)
    return stretch(lens + 0.04 * wobble, 0, 100)


def frosted_glass(g):
    a = g.noise(beta=1.4, kmin=60, kmax=1000)
    b = g.noise(beta=2.6, kmin=2, kmax=60)
    return stretch(a + 0.35 * b)


def hammered_metal(g):
    h = np.zeros((g.n, g.n))
    for cells, amp in ((18, 1.0), (31, 0.5)):
        dx = g.noise(beta=3.0, kmin=1, kmax=40)
        dy = g.noise(beta=3.0, kmin=1, kmax=40)
        f1, _, _, _ = g.voronoi(cells, g.u + 0.006 * dx, g.v + 0.006 * dy, jitter=0.8)
        h += amp * f1 ** 2
    return stretch(h + 0.03 * g.noise(beta=1.0, kmin=300))


def liquid(g):
    n = g.n
    a = g.noise(beta=3.0, kmin=1, kmax=80)
    qx = g.noise(beta=3.2, kmin=1, kmax=40)
    qy = g.noise(beta=3.2, kmin=1, kmax=40)
    b = g.warp(a, qx, qy, n * 0.08)
    rx = g.warp(g.noise(beta=3.0, kmin=1, kmax=60), b, qx, n * 0.06)
    return stretch(np.sin(TAU * 1.5 * rx) * 0.5 + b * 0.5)


MAPS = [
    ("marble", "Marble", "Veined stone, soft gradients", marble),
    ("liquid", "Liquid", "Domain-warped swirls, like oil on water", liquid),
    ("caustics", "Caustics", "Light webs from a pool floor", caustics),
    ("ripples", "Ripples", "Interfering water waves", ripples),
    ("reeded_glass", "Reeded glass", "Vertical flutes, made for REFRACT", reeded_glass),
    ("frosted_glass", "Frosted glass", "Fine grain for a soft smear", frosted_glass),
    ("crumpled_paper", "Crumpled paper", "Facets and creases", crumpled_paper),
    ("woven_fabric", "Woven fabric", "Twill weave, 96 threads", woven_fabric),
    ("wood", "Wood", "Growth rings and fibres", wood),
    ("brushed_metal", "Brushed metal", "Long horizontal streaks", brushed_metal),
    ("hammered_metal", "Hammered metal", "Dents of two sizes", hammered_metal),
    ("cracked_earth", "Cracked earth", "Dried mud, cells and cracks", cracked_earth),
    ("clouds", "Clouds", "Plain fractal noise", clouds),
    ("vhs_bands", "VHS bands", "Tracking errors, made for SLICE", vhs_bands),
]


def main():
    args = sys.argv[1:]
    size = 2048
    deep = "--16" in args
    if deep:
        args.remove("--16")
    if "--size" in args:
        i = args.index("--size")
        size = int(args[i + 1])
        del args[i:i + 2]
    out = args[0] if args else os.path.join(os.path.dirname(__file__), "..", "..", "panel", "maps")
    only = args[1:]
    os.makedirs(os.path.join(out, "thumbs"), exist_ok=True)
    index = []
    for seed, (key, name, desc, fn) in enumerate(MAPS):
        index.append({"file": key + ".png", "name": name, "info": desc})
        if only and key not in only:
            continue
        img = fn(Gen(size, 1000 + seed))
        a8 = Image.fromarray(np.round(np.clip(img, 0, 1) * 255).astype(np.uint8))
        if deep:
            a16 = np.round(np.clip(img, 0, 1) * 65535).astype(np.uint16)
            Image.fromarray(a16).save(os.path.join(out, key + ".png"), optimize=True)
        else:
            a8.save(os.path.join(out, key + ".png"), optimize=True)
        a8.resize((96, 96), Image.LANCZOS).save(os.path.join(out, "thumbs", key + ".png"))
        print(key, "ok")
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"license": "CC0-1.0", "maps": index}, f, indent=1)


if __name__ == "__main__":
    main()
