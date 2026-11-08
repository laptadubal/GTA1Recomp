#!/usr/bin/env python3
"""Match F_PLAY1..8 to (PED block, remap131..187) via coarse color signature.

Usage: python3 tools/match_portraits2.py [city] [blocks]
"""
import sys, os, struct, colorsys, collections
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import read
from analyze_ped_frames import load, records

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

def ckey(r, g, b):
    """Coarse perceptual key: hue bin 6, sat/bri level 3. Ignores near-gray."""
    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    if s < 0.2:
        return 'g' + str(int(v * 3))   # gray by level
    if v < 0.15:
        return 'd'                     # too dark
    return str(int(h * 6)) + str(int(s * 2)) + str(int(v * 3))

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    nblk = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    g = load(city)
    d = g.d
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    (ver, side, lid, aux, anim, palSz, remSz, remIdx, objSz, carSz,
     sprSz, gfxSz, numSz) = struct.unpack_from('<13I', d, 0)
    tiles = side + lid + aux
    trail = ((4 - ((tiles // 4096) % 4)) % 4) * 4096
    off = 52 + tiles + trail + anim + 768
    remap = d[off:off + remSz]

    fpal = read(os.path.join(DATA, 'F_PAL.RAW'))
    recs = records(g)
    ped_a = sum(g.spriteNumbers[0:7])

    def frame_idx(idx):
        w, h, dc, fl, ptrv = recs[idx]
        page = ptrv >> 16
        xo = (ptrv & 0xFFFF) & 255
        yo = (ptrv & 0xFFFF) >> 8
        gfx = g.spriteGfx
        out = []
        for y in range(h):
            for x in range(w):
                out.append(gfx[page * 65536 + (yo + y) * 256 + xo + x])
        return out

    def color_key_counter(ids, palette, skip_top_frac=0.25):
        cnt = collections.Counter()
        for i in ids:
            if i == 0:
                continue
            r, gg, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
            cnt[ckey(r, gg, b)] += 1
        total = sum(cnt.values())
        if skip_top_frac:
            for c, k in cnt.most_common(2):
                cnt[c] -= min(k, max(0, int(total * skip_top_frac)))
        return cnt

    # portraits (F_PAL), torso only (skip top 25%)
    port = []
    for n in range(1, 9):
        rat = read(os.path.join(DATA, f'F_PLAY{n}.RAT'))
        w, h = 102, 141
        ids = [rat[y * w + x] for y in range(h // 4, h) for x in range(w)]
        palettes = []
        for i in set(ids):
            palettes.append(fpal[i * 3:i * 3 + 3])
        # build palette array
        full = bytes()
        for i in range(256):
            full += fpal[i * 3:i * 3 + 3]
        port.append(color_key_counter(ids, full))

    for n in range(8):
        cnt = port[n]
        best = []
        for b in range(nblk):
            allc = []
            for f in range(8):
                allc.extend(frame_idx(ped_a + b * 8 + f))
            for r in range(131, 188):
                t = remap[r * 256:(r + 1) * 256]
                cc = collections.Counter()
                for i in allc:
                    j = t[i]
                    if j == 0:
                        continue
                    rr, gg, bb = pal[j * 3], pal[j * 3 + 1], pal[j * 3 + 2]
                    cc[ckey(rr, gg, bb)] += 1
                sc = sum(min(k, cnt[c]) for c, k in cc.items() if c in cnt)
                best.append((sc, b, r))
        best.sort(reverse=True)
        print(f'F_PLAY{n+1}: ' + ', '.join(f'{c}x{k}' for c, k in cnt.most_common(6)))
        for sc, b, r in best[:5]:
            print(f'   score {sc:4.0f}  block {b} (idx {ped_a + b*8}..+7)  remap {r}')

if __name__ == '__main__':
    main()
