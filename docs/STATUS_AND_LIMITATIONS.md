# NIMRODCORE MAKCU v0.7 — current status

**Experimental source only. NOT a validated flash release.**

This implementation is an independent firmware rewrite using the MAKCU pinout and dual-MCU architecture documented in [Oso-Cute/makcu-controller-fw](https://github.com/Oso-Cute/makcu-controller-fw). It does not copy or redistribute their controller passthrough source or firmware.

## Wiring and data flow
- **RIGHT ESP32-S3**: USB3 host, UART1 TX GPIO1 / RX GPIO2, 5 Mbaud to LEFT.
- **LEFT ESP32-S3**: UART1 TX GPIO2 / RX GPIO1, UART0 TX GPIO43 / RX GPIO44 to USB2 CH343 at 4 Mbaud.
- Both: **4 MB DIO**.
- PC application reads serial packets; existing NIMRODCORE mouse curves and ViGEmBus controller output are a separate integration step.
- USB1 is **not** emulating a mouse or controller in this firmware.
- Windows Raw Input and Interception are not used to capture HID input.

## Current supported input
- Boot-protocol USB mice: buttons, relative 8-bit X/Y, wheel.
- Boot-protocol USB keyboards: modifiers and six-key rollover.
- Advanced gaming mouse report descriptors, composite interfaces, NKRO, 16-bit motion, and polling rates above the ESP32-S3 host's USB Full Speed capabilities are **not yet implemented/validated**.
- A USB hub is required for simultaneous keyboard and mouse on USB3; powered hub may be necessary.

## Build
The GitHub Actions workflow compiles both ESP-IDF projects, merges each image at offset 0x0, and publishes artifacts **only if compilation succeeds**.

## Testing and safety
1. First inspect the GitHub Actions result and correct compiler/API errors.
2. Back up **the complete flash of both ESP32-S3 chips**. Vendor app-only binaries are not full backups.
3. Validate hardware VBUS and bootloader ports; USB2 CH343 is not a ROM flash port.
4. Only then flash **both** matching images.
5. Test a basic boot mouse before any gaming mouse.
6. Add the Windows host adapter and verify events before claiming a working remapper.

**No successful build or hardware validation is claimed here.**
