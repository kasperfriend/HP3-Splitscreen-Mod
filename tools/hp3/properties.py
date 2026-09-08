# Read-only property tag inspector; array payloads remain raw hex.
from package import Package, Reader
import struct
import sys

def props(p, b, start=0):
    r = Reader(b, start)
    items = []
    while True:
        at = r.p
        n = p.names[r.idx()]
        if n == 'None':
            break
        info = r.u8()
        ty = info & 15
        sc = info >> 4 & 7
        st = p.names[r.idx()] if ty == 10 else ''
        size = [1, 2, 4, 12, 16, 0, 0, 0][sc]
        if sc == 5:
            size = r.u8()
        if sc == 6:
            size = struct.unpack('<H', r.read(2))[0]
        if sc == 7:
            size = r.u32()
        index = 0
        if ty != 3 and info & 128:
            index = r.u8()
            if index & 128:
                if index & 64:
                    index = (index & 63) << 24 | int.from_bytes(r.read(3), 'big')
                else:
                    index = (index & 127) << 8 | r.u8()
        value = r.read(size) if ty != 3 else b''
        rr = Reader(value)
        if ty == 3:
            v = bool(info & 128)
        elif ty == 1:
            v = value[0]
        elif ty == 2:
            v = rr.i32()
        elif ty == 4:
            v = struct.unpack('<f', value)[0]
        elif ty in [5, 8]:
            v = p.full(rr.idx())
        elif ty == 6:
            v = p.names[rr.idx()]
        elif ty == 10:
            if st in ['Vector', 'Rotator']:
                v = struct.unpack('<fff' if st == 'Vector' else '<iii', value)
            elif st == 'Color':
                v = tuple(value)
            else:
                try:
                    v = props(p, value)[0]
                except Exception:
                    v = value.hex(' ')
        else:
            v = value.hex(' ')
        items.append((n, index, st, v))
    return (items, r.p)
if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Read HP3 tagged defaults; class defaults need --first (no UClass-layout guessing).')
    ap.add_argument('package')
    ap.add_argument('object')
    ap.add_argument('--first')
    args = ap.parse_args()
    p = Package(args.package)
    obj = p.find(args.object)
    if obj is None:
        ap.error('export not found')
    b = p.data(obj)
    if args.first:
        matches = []
        for start in range(len(b)):
            try:
                r = Reader(b, start)
                if p.names[r.idx()] != args.first:
                    continue
                items, end = props(p, b, start)
                if end == len(b):
                    matches.append((start, items))
            except (ValueError, IndexError, struct.error):
                pass
        if len(matches) != 1:
            ap.error('expected one unambiguous defaults block, found ' + str(len(matches)))
        start, items = matches[0]
        print('serialized defaults offset:', start)
    else:
        items, end = props(p, b)
        if end != len(b):
            ap.error('trailing object data; this is not a standalone tagged-defaults object')
    print(*items, sep='\n')
