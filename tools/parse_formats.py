#!/usr/bin/env python3
import struct, zlib, sys, os

def read(path):
    with open(path,'rb') as f: return f.read()

def png(path, w, h, rgb):
    bpp = 4 if len(rgb) >= w*h*4 else 3
    ctype = 6 if bpp == 4 else 2
    raw = b''.join(b'\x00' + bytes(rgb[y*w*bpp:(y+1)*w*bpp]) for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path,'wb').write(data)

class GRY:
    def __init__(self, path):
        d = read(path); self.d = d
        (self.ver, self.sideSize, self.lidSize, self.auxSize, self.animSize,
         self.palSize, self.remapSize, self.remapIndexSize, self.objectInfoSize,
         self.carInfoSize, self.spriteInfoSize, self.spriteGraphicsSize,
         self.spriteNumberSize) = struct.unpack_from('<13I', d, 0)
        assert self.ver == 325, hex(self.ver)
        self.nside = self.sideSize//4096
        self.nlid = self.lidSize//4096
        self.naux = self.auxSize//4096
        off = 52
        self.side = d[off:off+self.sideSize]; off += self.sideSize
        self.lid = d[off:off+self.lidSize]; off += self.lidSize
        self.aux = d[off:off+self.auxSize]; off += self.auxSize
        trail = (4 - ((self.nside+self.nlid+self.naux) % 4)) % 4
        off += trail*4096
        self.animRaw = d[off:off+self.animSize]; off += self.animSize
        self.pal = d[off:off+768]; off += 768
        self.remap = d[off:off+65536]; off += 65536
        self.remapIndex = d[off:off+1024]; off += 1024
        self.objInfoOff = off
        off += self.objectInfoSize + self.carInfoSize + self.spriteInfoSize
        self.spriteGfx = d[off:off+self.spriteGraphicsSize]; off += self.spriteGraphicsSize
        self.spriteNumbers = struct.unpack_from('<21H', d, off)
    def tile(self, which, idx):
        # idx is 1-based in map (0 = none). The tile sections are stored back
        # to back and interleaved in groups of four (see docs/FORMATS.md).
        if idx <= 0:
            return b''
        if which == 'side':
            slot = idx
        elif which == 'lid':
            slot = self.nside + idx
        elif which == 'aux':
            slot = self.nside + self.nlid + idx
        else:
            raise KeyError(which)
        raw = self.side + self.lid + self.aux
        raw += b'\0' * (((len(raw) + 16383) // 16384) * 16384 - len(raw))
        off = (slot // 4) * 16384 + (slot % 4) * 64
        return b''.join(raw[off + y * 256:off + y * 256 + 64]
                        for y in range(64))
    def rgba(self, data):
        out = bytearray(4096*4)
        pal = bytes(((v<<2)|(v>>4)) for v in self.pal)
        for i,b in enumerate(data):
            out[i*4:i*4+4] = bytes((pal[b*3],pal[b*3+1],pal[b*3+2],0 if b==0 else 255))
        return out

class CMP:
    def __init__(self, path):
        d = read(path); self.d=d
        (self.ver, self.style, self.sample, self.reserved, self.routeSize,
         self.objSize, self.columnSize, self.blockSize, self.navSize) = struct.unpack_from('<IBBHIIIII', d, 0)
        assert self.ver == 331, hex(self.ver)
        self.base = struct.unpack_from('<65536I', d, 28)
        o = 28+262144
        self.column = struct.unpack_from('<%dH' % (self.columnSize//2), d, o); o += self.columnSize
        self.blocksRaw = d[o:o+self.blockSize]; o += self.blockSize
        self.numBlocks = self.blockSize//8
        self.blocks = []
        for i in range(self.numBlocks):
            tm, ext, left, right, top, bottom, lid = struct.unpack_from('<HBBBBBB', self.blocksRaw, i*8)
            self.blocks.append((tm,ext,left,right,top,bottom,lid))
        self.objOff = o; o += self.objSize
        self.routeOff=o; o += self.routeSize
        self.locOff=o; o += 36*3
        self.navOff=o
    def block_at(self,x,y,z):
        # z == 0 is the bottom block of the column.
        b = self.base[x*256+y]//2
        n = self.column[b]
        return self.blocks[self.column[b+n-z]]

if __name__ == '__main__':
    g = GRY('game_data/GTADATA/STYLE001.GRY')
    print('GRY side',g.nside,'lid',g.nlid,'aux',g.naux,'sprites',len(g.spriteGfx),'numbers',g.spriteNumbers[:8])
    print('pal', g.pal[:12].hex())
    # tile sheet side
    n=g.nside; w=16; h=(n+15)//16
    img=bytearray(w*64*h*64*4)
    for i in range(n):
        px=g.rgba(g.tile('side',i+1)); tx=(i%16)*64; ty=(i//16)*64
        for y in range(64):
            dst=((ty+y)*w*64+tx)*4
            img[dst:dst+64*4]=px[y*64*4:(y+1)*64*4]
    png('tools/preview_side.png',w*64,h*64,img)
    n=g.nlid; h=(n+15)//16
    img=bytearray(w*64*h*64*4)
    for i in range(n):
        px=g.rgba(g.tile('lid',i+1)); tx=(i%16)*64; ty=(i//16)*64
        for y in range(64):
            dst=((ty+y)*w*64+tx)*4
            img[dst:dst+64*4]=px[y*64*4:(y+1)*64*4]
    png('tools/preview_lid.png',w*64,h*64,img)
    print('wrote previews')

    c = CMP('game_data/GTADATA/NYC.CMP')
    print('CMP style',c.style,'objects',c.objSize//16,'columns',len(c.column),'blocks',c.numBlocks,'route',c.routeSize,'nav',c.navSize)
    # stats
    from collections import Counter
    cnt=Counter()
    for x in range(256):
        for y in range(256):
            b=c.base[x*256+y]//2
            n=c.column[b]
            cnt[n]+=1
    print('num-block histogram',cnt)
    for (x,y) in [(128,128),(100,100),(50,50),(200,200)]:
        b=c.base[x*256+y]//2; n=c.column[b]
        print((x,y),'base',c.base[x*256+y],'colbase',b,'n',n,'cols',c.column[b:b+6])
        for z in range(0, min(n,6)):
            print('   z',z,c.block_at(x,y,z))
