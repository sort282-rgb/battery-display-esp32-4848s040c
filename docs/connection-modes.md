# Connection modes

## Wi-Fi + web

The display connects to the home network and reads Battery Emulator web endpoints. Supported control commands are available with hold-to-confirm protection where appropriate.

## Direct ESP-NOW

The display receives broadcast telemetry directly without a router. This mode is strictly read-only. Buttons that require HTTP are dimmed, struck through and labelled **WEB ONLY**.

Changing modes is transactional: settings are written and verified first, then the radio mode is applied after a clean restart.
