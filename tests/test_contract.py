"""Minimal deterministic wire protocol tests; run with python3 -m unittest discover -s tests."""
import unittest
from pathlib import Path

def crc(data):
    x=0xffff
    for b in data:
        x ^= b<<8
        for _ in range(8):
            x=((x<<1)^(0x1021 if x&0x8000 else 0))&0xffff
    return x

class TestProtocol(unittest.TestCase):
    def test_crc_reference(self):
        self.assertEqual(crc(b'123456789'),0x29b1)
    def test_configuration(self):
        root=Path(__file__).resolve().parents[1]
        for chip in ('LEFT','RIGHT'):
            self.assertIn('CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y',(root/chip/'sdkconfig.defaults').read_text())
    def test_no_windows_rawinput_capture(self):
        root=Path(__file__).resolve().parents[1]
        for p in [root/'LEFT/main/main.c',root/'RIGHT/main/main.c']:
            s=p.read_text()
            self.assertNotIn('GetRawInputData(',s)
            self.assertNotIn('RegisterRawInputDevices(',s)
if __name__=='__main__':unittest.main()
