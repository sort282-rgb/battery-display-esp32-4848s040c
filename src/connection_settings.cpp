#include "connection_settings.h"

namespace {
class SettingsLock {
 public:
  explicit SettingsLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    locked_ = !mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(1500)) == pdTRUE;
  }
  ~SettingsLock() {
    if (mutex_ && locked_) xSemaphoreGive(mutex_);
  }
  bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  bool locked_ = false;
};
}  // namespace

bool storeConnectionSettings(Preferences &preferences,
                             const ConnectionSettings &settings,
                             SemaphoreHandle_t mutex) {
  SettingsLock lock(mutex);
  if (!lock.locked()) return false;

  const uint8_t mode = settings.directMode ? 1 : 0;
  preferences.putString("ssid", settings.ssid);
  preferences.putString("password", settings.password);
  preferences.putString("emu-host", settings.emulatorHost);
  preferences.putUChar("conn-mode", mode);
  preferences.putBool("direct", settings.directMode);  // legacy compatibility
  preferences.putUChar("esp-channel", settings.espNowChannel);

  return preferences.getString("ssid", "") == settings.ssid &&
         preferences.getString("password", "") == settings.password &&
         preferences.getString("emu-host", "") == settings.emulatorHost &&
         preferences.getUChar("conn-mode", mode ^ 1U) == mode &&
         preferences.getBool("direct", !settings.directMode) == settings.directMode &&
         preferences.getUChar("esp-channel", 0) == settings.espNowChannel;
}
