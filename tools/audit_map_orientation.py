#!/usr/bin/env python3
"""Decide which way round the CMP base grid is read.

`base` is a 256x256 uint32 grid.  The engine used to index it as
`base[x * kSize + y]`; this script compares that with `base[y * kSize + x]`
against two independent data sources that live in the same file/frame:

  1. NYC MISSION.INI payphones (absolute world coordinates). A payphone stands
     on pavement at city floor level, never inside a building or in the road.
  2. The CMP route network (`routeSize` bytes after the blocks and objects).
     Type 255 records are police routes; types 0..253 are roadblock routes.
     These authored coordinates help check the map frame, but are not routes
     for ordinary civilian traffic.

Run it from the repository root:

    python3 tools/audit_map_orientation.py [NYC]

Requires only the standard library plus tools/parse_formats.py.
"""
import re
import struct
import sys
from collections import Counter

sys.path.insert(0, 'tools')
from parse_formats import CMP, GRY  # noqa: E402

CITY_STYLE = {'NYC': 'STYLE001', 'SANB': 'STYLE002', 'MIAMI': 'STYLE003'}


def road_lids(gry):
    """Same grey-asphalt heuristic as AssetManager::classifyRoadLids."""
    pal = bytes(((v << 2) | (v >> 4)) for v in gry.pal)
    road = set()
    for lid in range(1, gry.nlid + 1):
        data = gry.tile('lid', lid)
        if len(data) < 4096:
            continue
        bright = red = green = blue = gr = bl = 0
        for idx in data:
            r, g, b = pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]
            bright += (r + g + b) // 3
            red += r
            green += g
            blue += b
            if g > r + 15 and g > b + 15:
                gr += 1
            if b > r + 20 and b > g + 10:
                bl += 1
        mean, mr, mg, mb = bright // 4096, red // 4096, green // 4096, blue // 4096
        spread = max(mr, mg, mb) - min(mr, mg, mb)
        greenish = gr > 900 and gr > bl * 2 and mg > mr
        blueish = bl > 1200 and bl > gr + 150
        neutral = abs(mr - mg) <= 10 and mg - mb <= 25 and mb <= mg + 5
        if not greenish and not blueish and neutral and 45 <= mean < 93 and spread < 26:
            road.add(lid)
    return road


def make_top(cmp_map):
    """Returns top(x, y) -> (surface height, top lid) in parse_formats' frame."""
    def top(x, y):
        if not (0 <= x < 256 and 0 <= y < 256):
            return 0, 0
        n = cmp_map.num_blocks(x, y)
        height, lid = 0, 0
        for z in range(n):
            block = cmp_map.block_at(x, y, z)
            if block is None:
                continue
            tm, ext, l, r, t, b, li = block
            if tm == 0 and ext == 0 and l == 0 and r == 0 and t == 0 and b == 0 and li == 0:
                continue
            if tm & 128 or li == 0:      # flat decal / no visible top
                continue
            height, lid = z + 1, li
        return height, lid
    return top


def make_road_lookup(cmp_map):
    """Return a predicate for the authored type-2 CMP road surface."""
    def is_road(x, y):
        n = cmp_map.num_blocks(x, y)
        for z in range(n - 1, -1, -1):
            block = cmp_map.block_at(x, y, z)
            if block is None:
                continue
            tm, ext, left, right, upper, lower, lid = block
            if tm == 0 and ext == 0 and not (left or right or upper or lower or lid):
                continue
            if tm & 128 or lid == 0:
                continue
            above = cmp_map.block_at(x, y, z + 1) if z + 1 < n else None
            lid_type = (tm >> 4) & 7
            above_type = ((above[0] >> 4) & 7) if above else 0
            return (above_type if above_type != 0 else lid_type) == 2
        return False
    return is_road


def payphones(path, section=1):
    out, sec = [], None
    for line in open(path, encoding='latin-1'):
        m = re.match(r'^\[(\d+)\]', line)
        if m:
            sec = int(m.group(1))
            continue
        if sec != section:
            continue
        m = re.search(r'\((\d+),(\d+),(\d+)\)\s+TELEPHONE', line)
        if m:
            out.append(tuple(int(v) for v in m.groups()))
    return out


def routes(cmp_map):
    """[(type, [(x, y, z), ...]), ...] -- vertices stored (x, y, z)."""
    d, out = cmp_map.d, []
    pos, end = cmp_map.routeOff, cmp_map.routeOff + cmp_map.routeSize
    while pos < end:
        n, rtype = d[pos], d[pos + 1]
        pos += 2
        verts = [tuple(d[pos + 3 * i:pos + 3 * i + 3]) for i in range(n)]
        pos += 3 * n
        out.append((rtype, verts))
    return out


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else 'NYC'
    cmp_map = CMP('game_data/GTADATA/%s.CMP' % city)
    gry = GRY('game_data/GTADATA/%s.GRY' % CITY_STYLE[city])
    road = road_lids(gry)
    # parse_formats indexes base[x * 256 + y]; the engine now uses base[y * 256 + x],
    # which equals parse_formats' lookup with the arguments swapped.
    top = make_top(cmp_map)
    is_road = make_road_lookup(cmp_map)
    print('%s: %d road lid(s)' % (city, len(road)))

    if city == 'NYC':
        phones = payphones('game_data/GTADATA/MISSION.INI')
        print('\nNYC MISSION.INI payphones (%d):' % len(phones))
        for label, swap in (('old  base[x*256+y]', False),
                            ('fixed base[y*256+x]', True)):
            floor = raised = water = on_road = 0
            for px, py, _pz in phones:
                u, v = (py, px) if swap else (px, py)
                height, lid = top(u, v)
                if height == 0:
                    water += 1
                elif height <= 2:
                    floor += 1
                else:
                    raised += 1
                if lid in road:
                    on_road += 1
            print('  %-20s floor=%3d raised(building)=%3d none=%d  onRoadLid=%3d'
                  % (label, floor, raised, water, on_road))
    else:
        print('\nMISSION.INI payphones skipped (coordinates belong to NYC missions).')

    print('\nCMP route network:')
    for label, swap in (('old  base[x*256+y]', False),
                        ('fixed base[y*256+x]', True)):
        stats = Counter()
        total = Counter()
        for rtype, verts in routes(cmp_map):
            if rtype == 255:
                kind = 'police(255)'
            elif rtype <= 253:
                kind = 'roadblock(0..253)'
            else:
                kind = 'unclassified(254)'
            for (a, b, _z) in verts:
                if (a, b, _z) == (0, 0, 0):
                    continue          # terminator
                u, v = (b, a) if swap else (a, b)
                total[kind] += 1
                if is_road(u, v):
                    stats[kind] += 1
        detail = '  '.join('%s %d/%d (%.0f%%)' % (k, stats[k], total[k],
                                                  100.0 * stats[k] / max(total[k], 1))
                           for k in sorted(total))
        print('  %-20s %s' % (label, detail))

    print('\nRead the grid as base[y*256+x] when this city\'s authored route '
          'coordinates align with its CMP-tagged road network; NYC payphones provide an '
          'additional check for NYC only.\n'
          'Route data supports the coordinate frame but does not establish the '
          'behavior of ordinary civilian traffic; type 254 remains unclassified.')


if __name__ == '__main__':
    main()
