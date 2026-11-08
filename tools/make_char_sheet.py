#!/usr/bin/env python3
"""Render the 8 selectable characters (body window + clothing remap) to one
contact-sheet PNG for visual verification.  Each character is shown walking
(3 consecutive frames), recoloured through its GRY remap table.

Usage: python3 tools/make_char_sheet.py [city] [out.png]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from parse_formats import GRY
from parse_formats import png as write_png

DATA = os.path.join(os.path.dirname(__file__), '..', 'game_data', 'GTADATA')

# Mirrors the engine: first 8 detected windows = the 8 characters, and the
# per-character clothing remap (game.cpp kCharacterRemaps).
CHAR_WINDOWS = {
    'NYC': [620, 628, 639, 678, 691, 701, 719, 727],
    'SANB': [622, 630, 641, 680, 693, 703, 721, 729],
    'MIAMI': [635, 643, 654, 693, 706, 716, 734, 742],
}
CHAR_REMAPS = [131, 136, 140, 139, 165, 156, 143, 133]


def load(city):
    style = {'NYC': 1, 'SANB': 2, 'MIAMI': 3}[city]
    g = GRY(os.path.join(DATA, f'STYLE00{style}.GRY'))
    pal = bytes(((v << 2) | (v >> 4)) for v in g.pal)
    (ver, side, lid, aux, anim, palSz, remSz, remIdx, objSz, carSz,
     sprSz, gfxSz, numSz) = struct.unpack_from('<13I', g.d, 0)
    tiles = side + lid + aux
    trail = ((4 - ((tiles // 4096) % 4)) % 4) * 4096
    remap = g.d[52 + tiles + trail + anim + 768:
                52 + tiles + trail + anim + 768 + remSz]
    return g, pal, remap


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


def sprite_rgba(g, recs, pal, idx, remap_table=None):
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
            if remap_table is not None:
                i = remap_table[i]
            out += bytes((pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2],
                          0 if i == 0 else 255))
    return w, h, bytes(out)


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    out = sys.argv[2] if len(sys.argv) > 2 else f'char_sheet_{city}.png'
    g, pal, remap = load(city)
    recs = records(g)
    SCALE = 8
    PAD = 16
    GAP = 10
    wins = CHAR_WINDOWS[city]
    FRAMES = 3

    # Pre-decode each character's frames.
    chars = []
    for n in range(8):
        rem = remap[CHAR_REMAPS[n] * 256:(CHAR_REMAPS[n] + 1) * 256]
        frames = [sprite_rgba(g, recs, pal, wins[n] + f, rem)
                  for f in range(FRAMES)]
        chars.append(frames)

    cell_w = max(sum(f[0] for f in c) for c in chars) * SCALE + 2 * GAP + 2 * PAD
    cell_h = max(max(f[1] for f in c) for c in chars) * SCALE + 2 * PAD
    total_w = 8 * cell_w + PAD
    total_h = cell_h + PAD

    sheet = bytearray(b'\x14\x16\x1c' * (total_w * total_h) +
                      b'\xff' * (total_w * total_h))

    def blit(dx, dy, sprite):
        w, h, rgba = sprite
        for y in range(h):
            for x in range(w):
                si = (y * w + x) * 4
                if rgba[si + 3] == 0:
                    continue
                di = ((dy + y) * total_w + (dx + x)) * 4
                sheet[di] = rgba[si]
                sheet[di + 1] = rgba[si + 1]
                sheet[di + 2] = rgba[si + 2]
                sheet[di + 3] = 255

    for n in range(8):
        frames = chars[n]
        # draw the 3 frames side by side, bottom-aligned, scaled
        base_w = sum(f[0] for f in frames) * SCALE + 2 * GAP
        ox = PAD + n * cell_w + (cell_w - base_w) // 2
        bottom = total_h - PAD
        fx = ox
        for f in frames:
            fw, fh, rgba = f
            for sy in range(fh):
                for sx in range(fw):
                    si = (sy * fw + sx) * 4
                    if rgba[si + 3] == 0:
                        continue
                    for zz in range(SCALE):
                        for xx in range(SCALE):
                            di = ((bottom - fh * SCALE + sy * SCALE + zz) *
                                  total_w + (fx + sx * SCALE + xx)) * 4
                            sheet[di] = rgba[si]
                            sheet[di + 1] = rgba[si + 1]
                            sheet[di + 2] = rgba[si + 2]
                            sheet[di + 3] = 255
            fx += fw * SCALE + GAP

    write_png(out, total_w, total_h, bytes(sheet))
    print('wrote', out, f'{total_w}x{total_h}')
    print('char remaps:', CHAR_REMAPS)
    print('char windows:', wins)


if __name__ == '__main__':
    main()
