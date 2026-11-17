# Connecting MAKCU events to NIMRODCORE Remapper

**Current state:** host/makcu_input.py is an input adapter and diagnostic poller, not yet wired into the user's production NIMRODCORE remapper. No complete mouse-to-Xbox mapping is claimed.

## Setup
- Both MCU firmware images must be the matching pair.
- Connect USB2 CH343 to PC; its port may be COM3 but verify Device Manager.
- Plug a basic boot-protocol USB mouse into USB3. For mouse + keyboard use a supported USB hub.
- `python -m pip install pyserial` on the Windows machine running the engine.

## Event API
`from host.makcu_input import MakcuSerialInput, MouseEvent, KeyEvent, StatusEvent`

```python
from host.makcu_input import MakcuSerialInput, MouseEvent, KeyEvent, StatusEvent

def handle(event):
    if isinstance(event, MouseEvent):
        # event.dx, event.dy are physical HID relative movement.
        # Feed these into the EXISTING HIP/ADS curve engine.
        # event.buttons is a full button-state snapshot, not a delta.
        pass
    elif isinstance(event, KeyEvent):
        # event.usage is a USB HID usage code, not a Windows VK code.
        # event.pressed tells you press/release. Map usages to engine keys.
        pass
    elif isinstance(event, StatusEvent):
        # Monitor connected_devices and dropped_events.
        pass

with MakcuSerialInput("COM3", handle) as device:
    while True:
        device.poll()
```

## Integration requirements
1. Preserve the current NIMRODCORE input-to-stick mapping, curves, HIP/ADS sensitivity, deadzones, smoothing, scripts and hotkeys. Only replace the physical input source.
2. Convert USB HID keyboard usages to the key IDs expected by the existing engine; do not treat HID usages as Windows virtual-key codes.
3. Convert mouse button-state snapshots to button edges; the adapter emits a snapshot per mouse report.
4. Feed signed dx/dy into the existing movement accumulation before stick curve processing; do not add an additional acceleration curve.
5. Handle device disconnection and missing heartbeat by releasing all keys and mouse buttons; avoid stuck virtual controller inputs.
6. Do not run the existing Interception input reader at the same time as the MAKCU adapter, or duplicate input will occur.
7. Continue using ViGEmBus for the virtual Xbox 360 controller. MAKCU firmware itself does not create it.
8. Run the host reader in a dedicated worker, not in the UI thread. Use a bounded event queue and observe dropped-event telemetry.
9. The current 8-bit boot-mouse input is not yet equivalent to high-resolution gaming mouse HID. Test basic mouse first.
10. Do not claim guaranteed 1000 Hz end-to-end input; measure real report frequency and latency after physical tests.

## Safety
This integration must not silently install or use Windows Raw Input or Interception. The physical capture source is the USB3 HID host inside MAKCU. It is okay for a future explicit PC passthrough implementation to synthesize mouse output, but that is separate from capturing input.

## Remaining dependency
To patch the actual NIMRODCORE engine, its current source or exact repository is required. This repo currently contains the standalone firmware and host adapter, not the full remapper engine.
