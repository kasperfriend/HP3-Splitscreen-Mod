# Read-only HP3 v129 package tables. No game data is included.
import struct
import pathlib

class Reader:

    def __init__(self, b, pos=0):
        self.b = b
        self.p = pos

    def read(self, n):
        if n < 0 or self.p < 0 or self.p + n > len(self.b):
            raise ValueError('truncated package data')
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def u8(self):
        return self.read(1)[0]

    def i32(self):
        return struct.unpack('<i', self.read(4))[0]

    def u32(self):
        return struct.unpack('<I', self.read(4))[0]

    def idx(self):
        b = self.u8()
        v = b & 63
        neg = b & 128
        shift = 6
        if b & 64:
            while True:
                if shift > 27:
                    raise ValueError('invalid compact index')
                b = self.u8()
                v |= (b & 127) << shift
                shift += 7
                if not b & 128:
                    break
        return -v if neg else v

    def string(self):
        n = self.idx()
        return self.read(abs(n) * (2 if n < 0 else 1)).decode('utf-16-le' if n < 0 else 'latin1').rstrip('\x00')

class Package:

    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.b = self.path.read_bytes()
        h = struct.unpack_from('<IHHIIIIIII', self.b)
        self.names = []
        self.exports = []
        self.imports = []
        if h[0] != 2653586369 or h[1:3] != (129, 0):
            raise ValueError('only uncompressed HP3 v129 packages supported')
        if any((v > len(self.b) for v in h[4:])):
            raise ValueError('invalid package header')
        r = Reader(self.b, h[5])
        for _ in range(h[4]):
            self.names.append(r.string())
            r.u32()
        r = Reader(self.b, h[9])
        for _ in range(h[8]):
            self.imports.append(dict(pkg=self.names[r.idx()], cls=self.names[r.idx()], outer=r.i32(), name=self.names[r.idx()]))
        r = Reader(self.b, h[7])
        for _ in range(h[6]):
            e = dict(cls=r.idx(), super=r.idx(), outer=r.i32(), name=self.names[r.idx()], flags=r.u32(), size=r.idx())
            e['offset'] = r.idx() if e['size'] else 0
            self.exports.append(e)

    def obj(self, i):
        return self.exports[i - 1] if i > 0 else self.imports[-i - 1] if i < 0 else None

    def full(self, i, seen=()):
        if i in seen or len(seen) > 100:
            raise ValueError('cyclic object outer chain')
        e = self.obj(i)
        if not e:
            return 'None'
        return (self.full(e['outer'], seen + (i,)) + '.' if e['outer'] else '') + e['name']

    def data(self, i):
        e = self.obj(i)
        return Reader(self.b, e['offset']).read(e['size'])

    def find(self, path):
        return next((i for i in range(1, len(self.exports) + 1) if self.full(i) == path), None)
