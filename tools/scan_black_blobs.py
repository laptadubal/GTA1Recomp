#!/usr/bin/env python3
"""Scan a game screenshot for solid black blobs (candidate 'missing tile' regions).

Usage: python3 tools/scan_black_blobs.py /tmp/base_NYC.png [more.png ...]

Masks out HUD: top 60px, bottom 60px, minimap circle (bottom-left) and the
right-bottom status strip. Reports connected near-black regions > min area.
"""
import sys
from collections import deque
from PIL import Image

def scan(path, min_area=150, thresh=20):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    # mask function: True = ignore (HUD)
    import math
    def masked(x, y):
        if y < 64 or y > h - 64 or x < 8 or x > w - 8:
            return True
        # minimap: bottom-left circle
        if math.hypot(x - 210, y - (h - 210)) < 260:
            return True
        # bottom-right / status strip
        if x > w - 520 and y > h - 120:
            return True
        return False
    seen = [[False] * w for _ in range(h)]
    blobs = []
    for y in range(64, h - 64):
        for x in range(8, w - 8):
            if seen[y][x] or masked(x, y):
                continue
            r, g, b = px[x, y]
            if r >= thresh or g >= thresh or b >= thresh:
                continue
            # flood fill
            dq = deque([(x, y)])
            seen[y][x] = True
            xs, ys, n = [x], [y], 0
            while dq:
                cx, cy = dq.popleft()
                n += 1
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx]:
                            rr, gg, bb = px[nx, ny]
                            if rr < thresh and gg < thresh and bb < thresh and not masked(nx, ny):
                                seen[ny][nx] = True
                                dq.append((nx, ny))
                                xs.append(nx); ys.append(ny)
            if n >= min_area:
                blobs.append((min(xs), min(ys), max(xs), max(ys), n))
    blobs.sort(key=lambda t: -t[4])
    print(f'{path}: {w}x{h}, black blobs (>= {min_area}px): {len(blobs)}')
    for b in blobs[:12]:
        cx = (b[0] + b[2]) // 2; cy = (b[1] + b[3]) // 2
        print(f'  x{b[0]}-{b[2]} y{b[1]}-{b[3]} area={b[4]} center=({cx},{cy})')
    return blobs

if __name__ == '__main__':
    for p in sys.argv[1:]:
        scan(p)
