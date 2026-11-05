#!/usr/bin/env python3
"""Render a full top-down map straight from a CMP/GRY pair.

The output approximates what the engine's world mesh looks like from directly
above: the highest opaque lid per cell, transparent decals composited on top,
and the same tile rotation convention as the engine.  Cells that only contain
decals borrow the nearest real surface texture, so the map does not get
invented blue/green holes.
"""
import sys
from collections import deque
from parse_formats import GRY, CMP, png

city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
CELL = int(sys.argv[2]) if len(sys.argv) > 2 else 8
cmpf = {'NYC': 'NYC.CMP', 'MIAMI': 'MIAMI.CMP', 'SANB': 'SANB.CMP'}[city]
style = {'NYC': 'STYLE001', 'MIAMI': 'STYLE002', 'SANB': 'STYLE003'}[city]
g = GRY(f'game_data/GTADATA/{style}.GRY')
c = CMP(f'game_data/GTADATA/{cmpf}')
pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
N = 256
STEP = max(1, 64 // CELL)
img = bytearray(N * CELL * N * CELL * 3)
water_px = bytearray([24, 64, 112] * CELL * CELL)


def tile_rgb(idx):
    data = g.tile('lid', idx)
    if len(data) < 4096:
        return None
    out = bytearray(CELL * CELL * 3)
    for y in range(CELL):
        sy = y * STEP
        for x in range(CELL):
            b = data[sy * 64 + x * STEP]
            o = (y * CELL + x) * 3
            out[o] = pal[b * 3]
            out[o + 1] = pal[b * 3 + 1]
            out[o + 2] = pal[b * 3 + 2]
    return out


def tile_alpha(idx):
    data = g.tile('lid', idx)
    if len(data) < 4096:
        return None
    out = bytearray(CELL * CELL)
    for y in range(CELL):
        sy = y * STEP
        for x in range(CELL):
            out[y * CELL + x] = 0 if data[sy * 64 + x * STEP] == 0 else 255
    return out


def rotate_cw(buf, channels):
    src = [buf[i * channels:(i + 1) * channels] for i in range(CELL * CELL)]
    out = bytearray(len(buf))
    for y in range(CELL):
        for x in range(CELL):
            s = y + (CELL - 1 - x) * CELL
            d = y * CELL + x
            out[d * channels:(d + 1) * channels] = src[s]
    return out


def oriented(buf, channels, rotation):
    for _ in range((rotation - 1) & 3):
        buf = rotate_cw(buf, channels)
    return buf


cache = {}


def get_tile(idx, rotation, alpha=False):
    key = (idx, rotation, alpha)
    if key not in cache:
        raw = tile_alpha(idx) if alpha else tile_rgb(idx)
        cache[key] = oriented(raw, 1 if alpha else 3, rotation) if raw else None
    return cache[key]


# Pass 1: highest opaque lid per cell.
base_lid = [[0] * N for _ in range(N)]
base_rot = [[0] * N for _ in range(N)]
block_counts = [[0] * N for _ in range(N)]
for y in range(N):
    for x in range(N):
        b = c.base[x * 256 + y] // 2
        n = c.column[b]
        block_counts[y][x] = n
        for z in range(n - 1, -1, -1):
            bi = c.column[b + n - z]
            if not bi:
                continue
            tm, ext, left, right, top_, bottom, lid = c.blocks[bi]
            if lid and not (tm & 128):
                base_lid[y][x] = lid
                base_rot[y][x] = (tm >> 14) & 3
                break

# Pass 1b: propagate the nearest surface into decal-only and empty cells.
q = deque()
for y in range(N):
    for x in range(N):
        if base_lid[y][x]:
            q.append((x, y))
while q:
    x, y = q.popleft()
    for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
        if 0 <= nx < N and 0 <= ny < N and not base_lid[ny][nx]:
            base_lid[ny][nx] = base_lid[y][x]
            base_rot[ny][nx] = base_rot[y][x]
            q.append((nx, ny))

# Pass 2: compose.
for y in range(N):
    for x in range(N):
        b = c.base[x * 256 + y] // 2
        n = block_counts[y][x]
        if n <= 0:
            px = water_px
        else:
            lid = base_lid[y][x]
            px = get_tile(lid, base_rot[y][x]) if lid else water_px
            if px is None:
                px = water_px
            # Composite transparent decals (flat blocks) over the surface.
            for z in range(n):
                bi = c.column[b + n - z]
                if not bi:
                    continue
                tm, ext, left, right, top_, bottom, dlid = c.blocks[bi]
                if not dlid or not (tm & 128):
                    continue
                rot = (tm >> 14) & 3
                dp = get_tile(dlid, rot)
                da = get_tile(dlid, rot, alpha=True)
                if dp is None or da is None:
                    continue
                for i in range(CELL * CELL):
                    if da[i]:
                        px[i * 3:i * 3 + 3] = dp[i * 3:i * 3 + 3]

        for yy in range(CELL):
            dst = ((y * CELL + yy) * N * CELL + x * CELL) * 3
            img[dst:dst + CELL * 3] = px[yy * CELL * 3:(yy + 1) * CELL * 3]

path = f'tools/preview_{city}.png'
png(path, N * CELL, N * CELL, img)
print('wrote', path, N * CELL, 'x', N * CELL)
