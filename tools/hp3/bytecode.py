# Partial token reader for the supplied HP3 packages. Unknown tokens fail explicitly.
from package import Package, Reader
import struct
from pathlib import Path
import sys
native = {}

def load_natives(directory):
    for file in ['Core.u', 'Engine.u', 'kwGame.u', 'HGame.u']:
        p = Package(Path(directory) / file)
        for i, e in enumerate(p.exports, 1):
            if p.full(e['cls']) == 'Core.Function':
                b = p.data(i)
                n = struct.unpack_from('<H', b, len(b) - 7)[0]
                if n:
                    native[n] = p.full(i)

class Dis:

    def __init__(self, p, b):
        self.pkg = p
        self.r = Reader(b)

    def obj(self):
        return self.pkg.full(self.r.idx())

    def name(self):
        return self.pkg.names[self.r.idx()]

    def u16(self):
        return struct.unpack('<H', self.r.read(2))[0]

    def args(self):
        args = []
        while self.r.b[self.r.p] != 22:
            args.append(self.expr())
        self.r.u8()
        return '(' + ', '.join(args) + ')'

    def expr(self):
        r = self.r
        t = r.u8()
        if t >= 96:
            n = t if t >= 112 else (t - 96) * 256 + r.u8()
            return native.get(n, f'native{n:X}') + self.args()
        if t in [0, 1, 2]:
            return ['local:', 'self:', 'default:'][t] + self.obj()
        if t == 4:
            return 'return ' + self.expr()
        if t == 6:
            return 'jump ' + hex(self.u16())
        if t == 7:
            j = self.u16()
            return 'jumpIfNot ' + hex(j) + ' ' + self.expr()
        if t == 8:
            return 'stop'
        if t == 11:
            return 'Nothing'
        if t == 13:
            return 'gotoLabel ' + self.expr()
        if t == 15 or t == 20:
            return self.expr() + ' = ' + self.expr()
        if t in [16, 26]:
            index = self.expr()
            arr = self.expr()
            return arr + '[' + index + ']'
        if t == 17:
            return 'new' + ''.join((' {' + self.expr() + '}' for _ in range(4)))
        if t in [18, 25]:
            o = self.expr()
            skip = self.u16()
            size = r.u8()
            return o + '.' + self.expr()
        if t == 19:
            return 'class<' + self.obj() + '>' + self.expr()
        if t == 21:
            return 'endlabeltable'
        if t == 22:
            return ')'
        if t == 23:
            return 'self'
        if t == 24:
            n = self.u16()
            return self.expr()
        if t == 27:
            return self.name() + self.args()
        if t == 28:
            return self.obj() + self.args()
        if t == 29:
            return str(r.i32())
        if t == 30:
            return str(struct.unpack('<f', r.read(4))[0])
        if t == 31:
            b = b''
            while (c := r.read(1)) != b'\x00':
                b += c
            return repr(b.decode('latin1'))
        if t == 32:
            return '@' + self.obj()
        if t == 33:
            return "'" + self.name() + "'"
        if t == 34:
            return str(struct.unpack('<iii', r.read(12)))
        if t == 35:
            return str(struct.unpack('<fff', r.read(12)))
        if t in [36, 44]:
            return str(r.u8())
        if t in [37, 38, 39, 40, 42]:
            return {37: '0', 38: '1', 39: 'true', 40: 'false', 42: 'None'}[t]
        if t == 45:
            return 'bool:' + self.expr()
        if t == 46:
            return self.obj() + '(' + self.expr() + ')'
        if t == 47:
            e = self.expr()
            j = self.u16()
            return 'foreach ' + e + ' end=' + hex(j)
        if t == 48:
            return 'iteratorpop'
        if t == 49:
            return 'iteratornext'
        if t == 54:
            member = self.obj()
            return self.expr() + '.' + member
        if t == 55:
            return self.expr() + '.Length'
        if t == 56:
            return 'global ' + self.name() + self.args()
        if t == 57:
            return 'cast' + hex(r.u8()) + '(' + self.expr() + ')'
        raise Exception(f'unknown {t:02x} at {r.p - 1:x}')

def script(p, i):
    b = p.data(i)
    r = Reader(b)
    assert r.idx() == 0
    fields = [r.idx() for _ in range(6)]
    line = r.i32()
    pos = r.i32()
    size = r.i32()
    return (b[r.p:-7], size)
if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Partial HP3 function-bytecode inspector, NOT an UnrealScript decompiler. Left offsets are serialized byte offsets; branch destinations are VM offsets (different units).')
    ap.add_argument('package')
    ap.add_argument('filter')
    args = ap.parse_args()
    load_natives(Path(args.package).parent)
    p = Package(args.package)
    matched = False
    failed = False
    print('Left offsets: SERIALIZED bytes. Jump operands: VM offsets; do not equate them.')
    for i, e in enumerate(p.exports, 1):
        if args.filter.lower() in p.full(i).lower() and p.full(e['cls']) == 'Core.Function':
            matched = True
            print('\n###', p.full(i))
            b, size = script(p, i)
            d = Dis(p, b)
            try:
                while d.r.p < len(b):
                    pos = d.r.p
                    expr = d.expr()
                    print(f'{pos:04x}: {expr}')
            except Exception as e:
                failed = True
                print('UNSUPPORTED/INVALID:', e)
    if not matched:
        ap.error('no matching functions')
    if failed:
        raise SystemExit(1)
