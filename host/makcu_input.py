"""NIMRODCORE MAKCU serial input adapter.

Physical HID events arrive from USB3 through RIGHT -> LEFT -> CH343 USB2.
No Interception and no Windows Raw Input capture. This module does NOT
emulate a gamepad; the NIMRODCORE engine/ViGEmBus must consume its events.
"""
from __future__ import annotations

from dataclasses import dataclass
import struct
import time
from typing import Callable, Optional

BAUD = 4_000_000
MAX_PAYLOAD = 64
FRAME_MOUSE, FRAME_KEYBOARD, FRAME_STATUS = 1, 2, 3
MODIFIER_USAGES = tuple(range(0xE0, 0xE8))


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


@dataclass(frozen=True)
class MouseEvent:
    buttons: int
    dx: int
    dy: int
    wheel: int
    pan: int
    tilt: int
    timestamp_us: int
    sequence: int


@dataclass(frozen=True)
class KeyEvent:
    usage: int
    pressed: bool
    timestamp_us: int
    sequence: int


@dataclass(frozen=True)
class StatusEvent:
    connected_devices: int
    dropped_events: int
    timestamp_us: int
    sequence: int


class PacketDecoder:
    """Incremental, CRC-checked stream decoder with corruption resync."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad_frames = 0
        self.sequence_gaps = 0
        self._previous_sequence: Optional[int] = None

    def feed(self, data: bytes) -> list[tuple[int, int, int, bytes]]:
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 5:
            if self.buffer[:2] != b"NC" or self.buffer[2] != 1 or self.buffer[4] > MAX_PAYLOAD or self.buffer[3] not in (1, 2, 3):
                del self.buffer[0]
                self.bad_frames += 1
                continue
            size = 13 + self.buffer[4]
            if len(self.buffer) < size:
                break
            expected = int.from_bytes(self.buffer[size - 2:size], "little")
            if crc16(self.buffer[2:size - 2]) != expected:
                del self.buffer[0]
                self.bad_frames += 1
                continue
            typ = self.buffer[3]
            seq = int.from_bytes(self.buffer[5:7], "little")
            ts = int.from_bytes(self.buffer[7:11], "little")
            payload = bytes(self.buffer[11:size - 2])
            del self.buffer[:size]
            if self._previous_sequence is not None and (seq - self._previous_sequence) & 0xFFFF != 1:
                self.sequence_gaps += 1
            self._previous_sequence = seq
            frames.append((typ, seq, ts, payload))
        if len(self.buffer) > 8192:
            self.buffer.clear()
            self.bad_frames += 1
        return frames


class InputState:
    """Convert HID snapshots to press/release events and mouse deltas."""

    def __init__(self) -> None:
        self.pressed: set[int] = set()
        self.buttons = 0

    def release_all(self, timestamp_us: int = 0, sequence: int = 0) -> list[object]:
        events = [KeyEvent(k, False, timestamp_us, sequence) for k in sorted(self.pressed)]
        self.pressed.clear()
        if self.buttons:
            events.append(MouseEvent(0, 0, 0, 0, 0, 0, timestamp_us, sequence))
        self.buttons = 0
        return events

    def interpret(self, frame: tuple[int, int, int, bytes]) -> list[object]:
        typ, seq, ts, payload = frame
        if typ == FRAME_MOUSE and len(payload) == 8:
            buttons, dx, dy, wheel, pan, tilt = struct.unpack("<Bhhbbb", payload)
            self.buttons = buttons
            return [MouseEvent(buttons, dx, dy, wheel, pan, tilt, ts, seq)]
        if typ == FRAME_KEYBOARD and len(payload) == 15:
            mods = payload[0]
            current = {u for u in payload[1:] if u and u not in (1, 2, 3)}
            current.update(usage for bit, usage in enumerate(MODIFIER_USAGES) if mods & (1 << bit))
            events = [KeyEvent(k, False, ts, seq) for k in sorted(self.pressed - current)]
            events.extend(KeyEvent(k, True, ts, seq) for k in sorted(current - self.pressed))
            self.pressed = current
            return events
        if typ == FRAME_STATUS and len(payload) == 5:
            return [StatusEvent(payload[0], int.from_bytes(payload[1:], "little"), ts, seq)]
        return []


class MakcuSerialInput:
    """Synchronous poller; integrate poll() into an engine input worker.

    Calls on_event for MouseEvent/KeyEvent/StatusEvent.
    Call close() on stop to release all held keys/buttons.
    """

    def __init__(self, port: str, on_event: Callable[[object], None], baud: int = BAUD):
        self.port = port
        self.baud = baud
        self.on_event = on_event
        self.serial = None
        self.decoder = PacketDecoder()
        self.state = InputState()
        self.last_keepalive = 0.0

    def open(self) -> None:
        import serial  # pip install pyserial; only dependency for this adapter
        if self.serial is not None:
            return
        conn = serial.Serial(self.port, self.baud, timeout=0.02, write_timeout=1)
        try:
            conn.reset_input_buffer()
            conn.write(b"NC:HELLO\r\n")
            greeting = conn.readline().decode("ascii", "replace").strip()
            if not greeting.startswith("NC:READY protocol=1 role=LEFT"):
                raise RuntimeError(f"Unexpected MAKCU firmware handshake: {greeting!r}")
            conn.write(b"NC:START\r\n")
            response = conn.readline().decode("ascii", "replace").strip()
            if response != "NC:STARTED":
                raise RuntimeError(f"MAKCU stream start failed: {response!r}")
            self.serial = conn
            self.last_keepalive = time.monotonic()
        except BaseException:
            conn.close()
            raise

    def poll(self) -> int:
        if self.serial is None:
            raise RuntimeError("Call open() first")
        now = time.monotonic()
        if now - self.last_keepalive >= 1.0:
            self.serial.write(b"NC:KEEPALIVE\r\n")
            self.last_keepalive = now
        count = 0
        for frame in self.decoder.feed(self.serial.read(1024)):
            for event in self.state.interpret(frame):
                self.on_event(event)
                count += 1
        return count

    def close(self) -> None:
        conn, self.serial = self.serial, None
        if conn is not None:
            try:
                conn.write(b"NC:STOP\r\n")
            except Exception:
                pass
            finally:
                conn.close()
        for event in self.state.release_all():
            self.on_event(event)

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="MAKCU hardware HID input diagnostic")
    parser.add_argument("--port", default="COM3")
    args = parser.parse_args()
    def show(event: object) -> None:
        print(event)
    with MakcuSerialInput(args.port, show) as device:
        try:
            while True:
                device.poll()
        except KeyboardInterrupt:
            pass
