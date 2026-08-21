#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct ConnectionSettings {
  String ssid;
  String password;
  String emulatorHost;
  bool directMode = false;
  uint8_t espNowChannel = 1;
};

bool storeConnectionSettings(Preferences &preferences,
                             const ConnectionSettings &settings,
                             SemaphoreHandle_t mutex = nullptr);
