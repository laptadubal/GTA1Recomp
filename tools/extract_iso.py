#!/usr/bin/env python3
"""Extract files from the GTA DOS CloneCD image (2448-byte raw sectors)."""
import os, struct, sys

SS = 2448        # 2352 raw + 96 subchannel
DATA_OFF = 16    # skip sync/header
DSZ = 2048

src = sys.argv[1] if len(sys.argv) > 1 else 'iso/GrandTheftAuto.mdf'
dst = sys.argv[2] if len(sys.argv) > 2 else 'game_data'

f = open(src, 'rb')

def sector(lba):
    f.seek(lba * SS + DATA_OFF)
    return f.read(DSZ)

pvd = sector(16)
if pvd[1:6] != b'CD001':
    sys.exit('not an ISO9660 image')

root = pvd[156:190]
root_lba = struct.unpack('<I', root[2:6])[0]
root_len = struct.unpack('<I', root[10:14])[0]

def norm(name):
    if ';' in name:
        name = name.split(';', 1)[0]
    return name

count = 0
def walk(lba, size, out):
    global count
    data = b''.join(sector(lba + i) for i in range((size + DSZ - 1) // DSZ))[:size]
    i = 0
    while i < size:
        ln = data[i]
        if ln == 0:
            i = (i // DSZ + 1) * DSZ
            continue
        rec = data[i:i + ln]
        ext = struct.unpack('<I', rec[2:6])[0]
        sz = struct.unpack('<I', rec[10:14])[0]
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen].decode('latin-1')
        i += ln
        if name in ('\x00', '\x01'):
            continue
        name = norm(name)
        path = os.path.join(out, name)
        if flags & 2:
            os.makedirs(path, exist_ok=True)
            walk(ext, sz, path)
        else:
            os.makedirs(out, exist_ok=True)
            with open(path, 'wb') as w:
                for s in range((sz + DSZ - 1) // DSZ):
                    w.write(sector(ext + s))
                w.truncate(sz)
            count += 1

os.makedirs(dst, exist_ok=True)
walk(root_lba, root_len, dst)
print(f'extracted {count} files to {dst}')
