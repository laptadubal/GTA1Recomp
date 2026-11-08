#!/usr/bin/env python3
"""ASCII-dump one sprite with several GRY remap tables applied, plus a cop frame.

Usage: python3 tools/check_remaps.py [city] [idx]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    idx = int(sys.argv[2]) if len(sys.argv) > 2 else 620
    style = {'NYC': 1, 'SANB': 2, 'MIAMI': 3}[city]
    g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
    d = g.d
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    (ver, side, lid, aux, anim, palSz, remSz, remIdx, objSz, carSz,
     sprSz, gfxSz, numSz) = struct.unpack_from('<13I', d, 0)
    off = g.objInfoOff
    # recompute remap offset: 52 + tiles + trail + anim + 768
    tiles = side + lid + aux
    trail = ((4 - (((side + lid + aux) // 4096) % 4)) % 4) * 4096
    remap = d[52 + tiles + trail + anim + 768: 52 + tiles + trail + anim + 768 + remSz]
    # per-city stats of which remaps are non-identity
    non_id = [k for k in range(256)
              if sum(1 for i in range(256) if remap[k * 256 + i] != i) > 0]
    print(f'{city}: non-identity remaps: {non_id[:60]}')
    # decode idx raw
    p = g.objInfoOff + g.objectInfoSize + g.carInfoSize
    end = p + g.spriteInfoSize
    recs = []
    while p + 10 <= end:
        w, h, dc = d[p], d[p + 1], d[p + 2]
        ptr = struct.unpack_from('<I', d, p + 6)[0]
        recs.append((w, h, dc, ptr))
        p += 10 + dc * 6
    w, h, dc, ptr = recs[idx]
    page = ptr >> 16
    xo = (ptr & 0xFFFF) & 255
    yo = (ptr & 0xFFFF) >> 8
    gfx = g.spriteGfx
    ids = [gfx[page * 65536 + (yo + y) * 256 + xo + x] for y in range(h)
           for x in range(w)]
    tables = [0, 131, 140, 150, 160, 170, 175, 180, 185, 186, 187]
    for t in tables:
        tbl = remap[t * 256:(t + 1) * 256]
        lines = []
        for y in range(h):
            line = ''
            for x in range(w):
                i = tbl[ids[y * w + x]]
                if i == 0:
                    line += ' '
                    continue
                r, gg, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
                if r > 200 and gg > 200 and b > 200: line += 'W'
                elif r > 180 and gg > 140 and b < 120: line += 's'
                elif max(r, gg, b) < 50: line += '#'
                elif b > r and b > gg: line += 'b'
                elif r > 140 and gg < 90 and b < 90: line += 'R'
                else: line += '.'
            lines.append(line)
        print(f'=== remap {t} (idx {idx}) ===')
        for l in lines[:min(h, 30)]:
            print('  ' + l)

if __name__ == '__main__':
    main()
