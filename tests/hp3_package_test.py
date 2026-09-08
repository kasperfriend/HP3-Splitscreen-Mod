"""Synthetic reader tests; no copyrighted package fixture is required."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools/hp3'))
from package import Reader
from properties import props
from bytecode import Dis


class MockPackage:
    names = ['None', 'Hidden', 'Radius']

    @staticmethod
    def full(index):
        return 'Object' + str(index)


class ReaderTest(unittest.TestCase):
    def test_signed_compact_indices(self):
        for raw, number in [(b'\0', 0), (b'\x3f', 63), (b'\x40\x01', 64),
                            (b'\xbf', -63), (b'\xff\x01', -127)]:
            self.assertEqual(Reader(raw).idx(), number)

    def test_bounds_and_truncation(self):
        for raw in [b'', b'\x40', b'\x40\x80\x80\x80\x80\x00']:
            with self.assertRaises(ValueError):
                Reader(raw).idx()
        with self.assertRaises(ValueError):
            Reader(b'abc', -1).read(1)
        with self.assertRaises(ValueError):
            Reader(b'abc').read(4)

    def test_strings(self):
        self.assertEqual(Reader(b'\x04abc\0').string(), 'abc')
        self.assertEqual(Reader(b'\x83a\0b\0\0\0').string(), 'ab')

    def test_bool_tag_has_no_payload(self):
        raw = b'\x01\x83\x02\x24' + struct.pack('<f', 60) + b'\0'
        items, end = props(MockPackage(), raw)
        self.assertEqual(items, [('Hidden', 0, '', True), ('Radius', 0, '', 60.0)])
        self.assertEqual(end, len(raw))

    def test_meta_cast_consumes_class_reference(self):
        # Crucial for reading HasSpell(class<baseSpell>(targetVulnerableClass)).
        d = Dis(MockPackage(), b'\x13\x01\x00\x02')
        self.assertEqual(d.expr(), 'class<Object1>local:Object2')
        self.assertEqual(d.r.p, 4)

    def test_unknown_token_fails(self):
        with self.assertRaisesRegex(Exception, 'unknown'):
            Dis(MockPackage(), b'\x03').expr()


if __name__ == '__main__':
    unittest.main()
