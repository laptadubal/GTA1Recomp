#!/usr/bin/env python3
import sys
from parse_formats import GRY, CMP, png
from collections import Counter

city = sys.argv[1] if len(sys.argv)>1 else 'NYC'
cmpf = {'NYC':'NYC.CMP','MIAMI':'MIAMI.CMP','SANB':'SANB.CMP'}[city]
style = {'NYC':'STYLE001','MIAMI':'STYLE002','SANB':'STYLE003'}[city]
g = GRY(f'game_data/GTADATA/{style}.GRY')
c = CMP(f'game_data/GTADATA/{cmpf}')
print('style',c.style)
# pick tile -> RGB cache
def tile_rgbarr(which, idx, size, step):
    data = g.tile(which, idx)
    out = bytearray(size*size*3)
    pal = bytes(((v<<2)|(v>>4)) for v in g.pal)
    for y in range(size):
        sy = y*step
        for x in range(size):
            b = data[sy*64 + x*step]
            o=(y*size+x)*3
            out[o]=pal[b*3]; out[o+1]=pal[b*3+1]; out[o+2]=pal[b*3+2]
    return out

CELL=8; STEP=8; N=256
img = bytearray(N*CELL*N*CELL*3)
heights = Counter()
for y in range(N):
    for x in range(N):
        b = c.base[x*256+y]//2
        n = c.column[b]
        # bottom->top blocks are column[b+n] .. column[b+1]
        top = 0; topidx=0; height=0
        for z in range(n):
            bi = c.column[b+n-z]
            if bi:
                height=z+1
                tm,ext,left,right,top_,bottom,lid = c.blocks[bi]
                if lid: top=lid
        heights[height]+=1
        # draw top tile if any; else checker
        if top:
            px = tile_rgbarr('lid', top, CELL, STEP)
        else:
            px = bytes([40,40,40])*(CELL*CELL)
        for yy in range(CELL):
            dst=((y*CELL+yy)*N*CELL+x*CELL)*3
            img[dst:dst+CELL*3]=px[yy*CELL*3:(yy+1)*CELL*3]
png(f'tools/preview_{city}.png', N*CELL, N*CELL, img)
print('heights',heights)
