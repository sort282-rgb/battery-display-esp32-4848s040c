# Troubleshooting

- **Stays in ESP-NOW after choosing Wi-Fi:** install `v1.0.0-rc.1` or newer, save the Wi-Fi mode again and allow the display to restart.
- **Wrong password or router unavailable:** reopen Connection Setup and correct the network. ESP-NOW remains available as a router-free fallback.
- **Battery Emulator unavailable:** confirm its IP address, power status and that the display is on the same network.
- **No cell data:** check Battery Emulator cell endpoints or the ESP-NOW transmitter configuration; stale data age is shown on System.
- **Touch is rotated:** cycle Screen Orientation; factory installation also starts the touch/orientation setup path.
- **Port not offered by installer:** use a data cable and put the display into bootloader mode before reconnecting.
