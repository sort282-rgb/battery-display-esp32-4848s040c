# Changelog

## v15.7

- Remove brightness controls from the System screen and web settings.
- Fix the ESP32-4848S040C-I backlight at a stable 100% duty cycle.
- Disable target-display dimming behavior.
- Arrange Display Menu as an evenly spaced 2x4 button grid.
- Move Screen Orientation into the lower-right grid position.

## v15.6.1

- Raise the measured safe brightness floor to 90% for this panel revision.
- Apply the 90% floor to the touch slider, web settings, stored settings and automatic dimming.

## v15.6

- Remove the brightness cycle button that could make the panel appear black.
- Add a vertical brightness slider to the right side of the System screen.
- Apply brightness continuously while dragging and save it when released.
- Initially enforce a 35% minimum for touch, web and automatic dimming controls.

## v15.5

- Show the amber dashboard notification only when Events contains a WARN item.
- Open the Events page when the amber notification is tapped.
- Change the notification action text to `TAP FOR EVENT`.
- Display the SOC percentage in blue while the battery is discharging.

## v1.0.0-rc.1

- Added the native 480×480 touch interface and four orientations.
- Added Wi-Fi + web and direct ESP-NOW connection modes.
- Added main dashboard, Cell Monitor, Events, Battery Info, DTC/Faults, Web Control and System screens.
- Added guarded contactor/BMS commands and explicit ESP-NOW read-only controls.
- Added transactional connection-mode persistence and clean-restart switching.
- Added hardware-specific board metadata, installer device checks and release validation.
