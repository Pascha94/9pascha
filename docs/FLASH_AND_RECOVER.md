# NIMRODCORE MAKCU v0.7 — backup, flash and rollback (EXPERIMENTAL)

**DO NOT flash this build until you have a complete backup of BOTH MCUs.** The GitHub Actions build passes, but USB HID and UART operation have not been tested on the physical board. Compiling does not validate hardware operation.

## Ports

The [Oso-Cute MAKCM hardware guide](https://github.com/Oso-Cute/makcu-controller-fw/blob/main/FLASHING.md) describes:

- USB1 (LEFT ESP32-S3) — hold BOOT next to USB1 while connecting to Windows to enter ROM download mode.
- USB2 (middle CH343) — UART commands and streaming, **not** ROM flashing.
- USB3 (RIGHT ESP32-S3) — hold BOOT next to USB3 while connecting to Windows to enter ROM download mode if necessary.
- Connect **only one MCU's ROM flash port at a time**. Identify the actual COM number in Device Manager. ROM bootloader often identifies as VID 303A, PID 0009.

## Full 4MB backups FIRST

Use a recent esptool executable. If using the Python package, install `esptool` and run `python -m esptool`; otherwise substitute the standalone esptool executable.

With only USB1 (LEFT) in ROM boot mode:

```powershell
python -m esptool --chip esp32s3 --port COM4 --baud 921600 read_flash 0x0 0x400000 stock_LEFT_FULL_4MB.bin
```

Disconnect LEFT; connect USB3 (RIGHT) in ROM boot mode and replace COM port:

```powershell
python -m esptool --chip esp32s3 --port COM5 --baud 921600 read_flash 0x0 0x400000 stock_RIGHT_FULL_4MB.bin
```

**Replace COM4/COM5 with the actual ROM ports**; COM3 may be USB2 CH343 and must NOT be used to flash. Each full backup should be exactly 4,194,304 bytes. Keep these files somewhere safe. Vendor V3.2_LEFT / V3.6_RIGHT app-only BIN files are **not** full flash backups.

If read fails, stop. Do not erase flash until both backups have been verified.

## Firmware images

GitHub Actions artifact `NIMRODCORE-v0.7-UNTESTED-LEFT-RIGHT` includes:

- `NIMRODCORE_LEFT_MERGED.bin`
- `NIMRODCORE_RIGHT_MERGED.bin`
- `SHA256SUMS.txt`

Both merged images contain bootloader at 0x0, partition table at 0x8000 and application at 0x10000. Both target 4MB DIO flash.

## Flash — only after backups and hardware checks

Flash LEFT using USB1 in ROM download mode:

```powershell
python -m esptool --chip esp32s3 --port COM4 --baud 921600 write_flash 0x0 NIMRODCORE_LEFT_MERGED.bin
```

Flash RIGHT using USB3 in ROM download mode:

```powershell
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash 0x0 NIMRODCORE_RIGHT_MERGED.bin
```

Or use Espressif Flash Download Tool GUI with chip ESP32-S3, DIO, 80 MHz and image offset **0x0**. Do not confuse USB2 CH343 with either ROM port.

After both, unplug ALL connections, wait, and reconnect USB2 CH343 to Windows. This firmware is **not** the stock km.* protocol.

## Initial diagnostics

Use `host/monitor.py` with Python + pyserial on USB2 CH343 at 4,000,000 baud:

```powershell
python -m pip install pyserial
python host/monitor.py --port COM3
```

Expected: `NC:READY protocol=1 ...`, `NC:STARTED`. Attach a **basic USB boot mouse** to USB3 (with USB3 NOT plugged into the PC), move it, and look for `MOUSE` lines. For mouse and keyboard together, a USB hub is required. Not all gaming mice are supported yet.

**Do not assume that moving the mouse will move the Windows cursor.** USB1 passthrough is not implemented in this firmware. NIMRODCORE PC host integration is still required.

## Restore stock firmware

Use the full backups you created above, matching the physical MCU:

```powershell
python -m esptool --chip esp32s3 --port COM4 --baud 921600 write_flash 0x0 stock_LEFT_FULL_4MB.bin
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash 0x0 stock_RIGHT_FULL_4MB.bin
```

Run each command with only the corresponding MCU connected to its ROM port, then power-cycle.

## Current limitations

- Build has passed in GitHub Actions; **not tested on MAKCU hardware**.
- USB3 host VBUS, USB hub, composite gaming mice, NKRO keyboards and high-resolution HID reports require further validation.
- NIMRODCORE PC adapter is not yet integrated with the production remapper.
- No guarantees for performance, compatibility, or safety.
