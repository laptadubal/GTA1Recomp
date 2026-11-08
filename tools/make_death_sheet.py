#!/usr/bin/env python3
"""Render the death-effect sprites (lying corpse, ground pool, spurt, dead cop)
from the GRY archive to one contact-sheet PNG, so the chosen PED-group
offsets can be visually verified.

Mirrors the engine: corpse = pedBase+{29,30,31}; pool = pedBase+240..244;
spurt = pedBase+245..252; dead cop = copBase+46 (copBase = pedBase+191).

Usage: python3 tools/make_death_sheet.py [city] [out.png]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY
from parse_formats import png as write_png

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')
PED_BASE = {'NYC': 620, 'SANB': 622, 'MIAMI': 635}


def load(city):
    style = {'NYC': 1, 'SANB': 2, 'MIAMI': 3}[city]
    g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    return g, pal


def records(g):
    d = g.d
    p = g.objInfoOff + g.objectInfoSize + g.carInfoSize
    end = p + g.spriteInfoSize
    recs = []
    while p + 10 <= end:
        w, h, dc = d[p], d[p + 1], d[p + 2]
        ptr = struct.unpack_from('<I', d, p + 6)[0]
        recs.append((w, h, dc, ptr))
        p += 10 + dc * 6
    return recs


def sprite_rgba(g, recs, pal, idx):
    w, h, dc, ptr = recs[idx]
    page = ptr >> 16
    xo = (ptr & 0xFFFF) & 255
    yo = (ptr & 0xFFFF) >> 8
    base = page * 65536
    out = bytearray()
    for y in range(h):
        row = (yo + y) * 256
        for x in range(w):
            i = g.spriteGfx[base + row + xo + x]
            out += bytes((pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2],
                          0 if i == 0 else 255))
    return w, h, bytes(out)


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    out = sys.argv[2] if len(sys.argv) > 2 else f'death_sheet_{city}.png'
    g, pal = load(city)
    recs = records(g)
    ped = PED_BASE[city]
    cop = ped + 191

    SCALE = 8
    PAD = 14
    label_h = 20
    rows = [
        ('corpse +29 +30 +31', [ped + 29, ped + 30, ped + 31]),
        ('pool +240..+244', [ped + 240 + i for i in range(5)]),
        ('spurt +245..+252 (8f)', [ped + 245 + i for i in range(8)]),
        ('dead cop +237', [cop + 46]),
    ]

    # Layout: each row is a label row + sprites in a line.
    max_w = 1
    for _name, idxs in rows:
        w = sum(recs[i][0] for i in idxs) * SCALE + PAD * (len(idxs) + 1)
        max_w = max(max_w, w)
    total_h = PAD + sum(label_h + max(recs[i][1] for i in idxs) * SCALE + PAD
                        for _name, idxs in rows)
    sheet = bytearray(b'\x14\x16\x1c' * (max_w * total_h) +
                      b'\xff' * (max_w * total_h))

    def blit(dx, dy, idx):
        w, h, rgba = sprite_rgba(g, recs, pal, idx)
        for y in range(h):
            for x in range(w):
                si = (y * w + x) * 4
                if rgba[si + 3] == 0:
                    continue
                for zz in range(SCALE):
                    for xx in range(SCALE):
                        di = ((dy + y * SCALE + zz) * max_w +
                              (dx + x * SCALE + xx)) * 4
                        sheet[di] = rgba[si]
                        sheet[di + 1] = rgba[si + 1]
                        sheet[di + 2] = rgba[si + 2]
                        sheet[di + 3] = 255

    def text_row(dy, s):
        # crude: just leave a dark divider; label is printed to stdout
        pass

    y = PAD
    print('rows:')
    for name, idxs in rows:
        y += label_h
        maxh = max(recs[i][1] for i in idxs) * SCALE
        x = PAD
        print(f'  {name}: {idxs}')
        # print a small ascii of each for the record
        for idx in idxs:
            blit(x, y, idx)
            w, h = recs[idx][0], recs[idx][1]
            x += w * SCALE + PAD
        y += maxh + PAD

    write_png(out, max_w, total_h, bytes(sheet))
    print('wrote', out, f'{max_w}x{total_h}')


if __name__ == '__main__':
    main()
