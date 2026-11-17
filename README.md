# NIMRODCORE MAKCU firmware — development repository

**Status: experimental source; do not flash until both ESP32-S3 builds pass and the hardware is checked.**

This project is an independently written USB-HID-to-serial input bridge for NIMRODCORE Remapper. It uses the confirmed MAKCM/MAKCU wiring reference from [Oso-Cute/makcu-controller-fw](https://github.com/Oso-Cute/makcu-controller-fw) (USB1 LEFT, USB2 CH343, USB3 RIGHT). No vendor firmware is redistributed.

- RIGHT: USB host for a physical mouse/keyboard, UART1 5 Mbps to LEFT.
- LEFT: UART1 receive, UART0 CH343 4 Mbps to PC.
- PC: NIMRODCORE remapper handles curves and ViGEmBus virtual Xbox 360 output.
- No Interception and no Windows Raw Input capture.
- Target: ESP32-S3, **4 MB flash each**, DIO.
- Flashing both matching images is required. Stock firmware is incompatible.
- The two matching firmware images are built via GitHub Actions and are not hardware validated.

## Important
Do **not** flash this while it is untested. A mouse/keyboard connected to USB3 may require a powered USB hub and USB-HID compatibility verification. USB1 is not an active mouse/keyboard passthrough in this build. The MAKCU USB2 serial port is not the ROM flashing port.

Source is new work and uses Espressif ESP-IDF HID Host APIs. Hardware pin assignments were cross-checked against the linked reference; firmware from that repository is not included.

