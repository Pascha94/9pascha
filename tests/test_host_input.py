import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from makcu_input import PacketDecoder, InputState, MouseEvent, KeyEvent, crc16


def packet(kind, seq, payload, timestamp=123):
    header = bytes([1, kind, len(payload)]) + struct.pack("<HI", seq, timestamp)
    body = header + payload
    return b"NC" + body + struct.pack("<H", crc16(body))


class TestMakcuHost(unittest.TestCase):
    def test_crc(self):
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_fragmented_mouse(self):
        d = PacketDecoder()
        raw = packet(1, 65535, struct.pack("<Bhhbbb", 3, -15, 100, -1, 0, 0))
        self.assertEqual(d.feed(raw[:8]), [])
        frames = d.feed(raw[8:])
        self.assertEqual(len(frames), 1)
        events = InputState().interpret(frames[0])
        self.assertEqual(events, [MouseEvent(3, -15, 100, -1, 0, 0, 123, 65535)])

    def test_corrupt_frame_recovers(self):
        d = PacketDecoder()
        bad = bytearray(packet(1, 1, b"12345678"))
        bad[12] ^= 1
        frames = d.feed(bytes(bad) + packet(3, 2, b"\x01\x00\x00\x00\x00"))
        self.assertEqual(len(frames), 1)
        self.assertGreater(d.bad_frames, 0)

    def test_keyboard_modifier_press_release(self):
        s = InputState()
        p = bytes([1]) + bytes([4, 5] + [0]*12)
        events = s.interpret((2, 1, 42, p))
        self.assertEqual([(e.usage, e.pressed) for e in events], [(4, True), (5, True), (224, True)])
        events = s.interpret((2, 2, 43, bytes(15)))
        self.assertEqual([(e.usage, e.pressed) for e in events], [(4, False), (5, False), (224, False)])

    def test_close_releases_all(self):
        s = InputState()
        s.interpret((2, 1, 0, bytes([0, 4] + [0]*13)))
        s.interpret((1, 2, 0, struct.pack("<Bhhbbb", 1, 0, 0, 0, 0, 0)))
        events = s.release_all()
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0], KeyEvent(4, False, 0, 0))
        self.assertEqual(events[1].buttons, 0)

    def test_sequence_wrap(self):
        d = PacketDecoder()
        d.feed(packet(3, 65535, bytes(5)))
        d.feed(packet(3, 0, bytes(5)))
        self.assertEqual(d.sequence_gaps, 0)

if __name__ == "__main__":
    unittest.main()
