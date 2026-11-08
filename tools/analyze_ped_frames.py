#!/usr/bin/env python3
"""Ped (sprite group 7 = PED) cycle analysis with CORRECT spriteGfx offset.

Usage: python3 tools/analyze_ped_frames.py [city]
Prints, for each 8-block of the PED group:
  - w/h per frame, solid% (single-index dominance), and
  - mirror score: how well frame k vs frame k+4 match on foot ink.
Then dumps a chosen block as ASCII.
"""
import sys, os, struct, collections
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

def load(city):
    style = {'NYC': 1, 'SANB': 2, 'MIAMI': 3}[city]
    return GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))

def records(g):
    d = g.d
    p = g.objInfoOff + g.objectInfoSize + g.carInfoSize
    end = p + g.spriteInfoSize
    recs = []
    while p + 10 <= end:
        w, h, dc, fl = d[p], d[p+1], d[p+2], d[p+3]
        ptr = struct.unpack_from('<I', d, p+6)[0]
        recs.append((w, h, dc, fl, ptr))
        p += 10 + dc * 6
    return recs

def sprite_rgba(g, recs, idx):
    w, h, dc, fl, ptr = recs[idx]
    page = ptr >> 16
    xo = (ptr & 0xFFFF) & 255
    yo = (ptr & 0xFFFF) >> 8
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    gfx = g.spriteGfx
    out = bytearray()
    for y in range(h):
        for x in range(w):
            i = gfx[page * 65536 + yo * 256 + xo + x + y * 256]
            out += bytes((pal[i*3], pal[i*3+1], pal[i*3+2], 0 if i == 0 else 255))
    return w, h, bytes(out)

def stats(g, recs):
    """Per sprite: ink count, single-color dominance, ink rows count."""
    out = []
    for idx in range(len(recs)):
        w, h, rgba = sprite_rgba(g, recs, idx)
        tot = w * h
        ink = sum(1 for i in range(0, len(rgba), 4) if rgba[i+3])
        cnt = collections.Counter(rgba[i] for i in range(0, len(rgba), 4) if rgba[i+3])
        top1 = cnt.most_common(1)[0][1] if cnt else 0
        dom = round(top1 / max(1, ink) * 100)
        # ink rows
        rows_with = 0
        for y in range(h):
            if any(rgba[(y*w+x)*4+3] for x in range(w)):
                rows_with += 1
        out.append((w, h, ink, dom, rows_with))
    return out

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    g = load(city)
    recs = records(g)
    ped_a = sum(g.spriteNumbers[0:7])  # PED group starts after first 7 groups
    ped_b = ped_a + g.spriteNumbers[7] - 1
    print(f'{city}: PED group {ped_a}..{ped_b} ({g.spriteNumbers[7]} sprites)')
    st = stats(g, recs)
    # print in 8-blocks, flagging uniform-height (likely real cycles)
    for a in range(ped_a, ped_b + 1, 8):
        seg = st[a:a+8]
        hs = [s[1] for s in seg]
        ws = [s[0] for s in seg]
        doms = [s[3] for s in seg]
        inks = [s[2] for s in seg]
        rows = [s[4] for s in seg]
        uniform = len(set(hs)) <= 2
        mark = ' <--' if uniform else ''
        print(f'  {a:4d}: ' +
              ' '.join(f'{s[0]}x{s[1]}' for s in seg) + mark)
        print(f'       dom% {doms} ink {inks} rows {rows}')

if __name__ == '__main__':
    main()
