# Battery Display 480×480 for Battery Emulator

A 4-inch touch dashboard for the [Battery Emulator](https://github.com/dalathegreat/Battery-Emulator), built specifically for the ESP32-4848S040C-I / Guition 480×480 display.

**[Open the 480×480 web installer](https://sort282-rgb.github.io/battery-display-esp32-4848s040c/installer/)** (Chrome or Edge, USB data cable required). The [device chooser](https://sort282-rgb.github.io/battery-display-esp32-4848s040c/) also links to the separate LILYGO T-Display-S3 installer.

> Firmware: `v15.7 Fixed Backlight Menu`. It is based on the verified working v15.5 project, fixes the backlight at 100%, removes brightness controls and uses an evenly spaced 2x4 Display Menu grid.

## Highlights

- Live SOC, voltage, power, current, cell voltage and temperature dashboard
- Direct ESP-NOW read-only mode without a router
- Wi-Fi mode with Battery Emulator web data and guarded control commands
- Full-width 96-cell monitor with min/max cell identification
- Events and DTC/fault screens
- Battery contactor, charge/discharge limit, HVIL, BMS and isolation information
- Four screen orientations with corresponding touch transformation
- Phone-based setup, screen orientation and connection diagnostics
- Update and factory web-installer flows

## Screen gallery

| Main dashboard | Cell monitor |
| --- | --- |
| ![Main Battery Display dashboard](docs/images/main-screen.png) | ![96-cell voltage monitor](docs/images/cell-monitor.png) |

| Display menu | Battery information |
| --- | --- |
| ![Display Menu](docs/images/display-menu.png) | ![Battery information and contactor status](docs/images/battery-info.png) |

| Events | DTC and faults |
| --- | --- |
| ![Events list](docs/images/events.png) | ![DTC and fault controls](docs/images/dtc-faults.png) |

| System information |
| --- |
| ![System information and connection status](docs/images/system.png) |

## Hardware

- ESP32-4848S040C-I / Guition 4-inch board
- 480×480 RGB panel with capacitive touch
- ESP32-S3, 16 MB flash and 8 MB OPI PSRAM
- USB data cable

The installer is intentionally hardware-locked. The LILYGO T-Display-S3 uses a separate project, manifest and firmware binaries; the chooser only links the two isolated installers.

## Build

Install PlatformIO, then run:

```text
pio run -e esp32-4848s040c
```

The project-specific board definition is in `boards/esp32-4848s040c.json`. Owner-specific defaults may be placed in `include/local_config.h`; that file is ignored by Git and never belongs in a release archive.

Run `python scripts/package_release.py` to validate the manifests, privacy guard and the locked SHA-256 checksums of the hardware-tested installer binaries. A clean source build is checked separately and does not replace those release files.

## Installation modes

- **Update** preserves NVS settings.
- **Factory install** erases the whole flash before installation.

After a factory install, select Wi-Fi + web or direct ESP-NOW on the display. Destructive Battery Emulator commands require a two-second hold. In ESP-NOW mode all web-only controls are visibly disabled.

## Documentation

- [Installation](docs/installation.md)
- [Connection modes](docs/connection-modes.md)
- [Screens and controls](docs/screens.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Release checklist](docs/release-checklist.md)

## License

GPL-3.0-only. See [LICENSE](LICENSE). Inter font assets retain their OFL license in `src/fonts/INTER-OFL-LICENSE.txt`.
