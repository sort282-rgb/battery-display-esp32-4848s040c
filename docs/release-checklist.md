# Release checklist

- Clean PlatformIO build without warnings
- Verify 16 MB flash and 8 MB PSRAM on target hardware
- Factory erase/install and update install
- Wi-Fi ↔ ESP-NOW switching
- Wrong password, missing router and unavailable Battery Emulator
- All four orientations and touch transforms
- Charging, discharging, negative current and sub-kilowatt power
- Missing/stale cell data and warning/error events
- Hold-to-confirm commands; no command available in ESP-NOW mode
- Installer rejects incompatible hardware before erase/write
- Validate manifests, release files and SHA-256 checksums
- Keep the hardware-tested `firmware.bin` SHA-256 locked to `d38dc4df2224030ea14a250ecc23966ef5c7b0cc108ca572799e2239adb3bb94`
- Keep each device's manifests and binaries inside its own installer directory
