#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  uint16_t siteId = 9116;
  String platform = "2";
  String line;  // Empty shows all lines at the platform.
  uint8_t visibleRows = 2;
  uint8_t brightness = 64;
};

inline void loadConfig(AppConfig& config) {
  Preferences preferences;
  preferences.begin("sl-board", true);
  config.wifiSsid = preferences.getString("ssid", "");
  config.wifiPassword = preferences.getString("wifi-pass", "");
  config.siteId = preferences.getUShort("site", 9116);
  config.platform = preferences.getString("platform", "2");
  config.line = preferences.getString("line", "");
  config.visibleRows = preferences.getUChar("rows", 2);
  config.brightness = preferences.getUChar("brightness", 64);
  preferences.end();

  if (config.visibleRows < 1 || config.visibleRows > 2) config.visibleRows = 2;
  if (config.brightness < 1) config.brightness = 1;
}

inline void saveConfig(const AppConfig& config) {
  Preferences preferences;
  preferences.begin("sl-board", false);
  preferences.putString("ssid", config.wifiSsid);
  preferences.putString("wifi-pass", config.wifiPassword);
  preferences.putUShort("site", config.siteId);
  preferences.putString("platform", config.platform);
  preferences.putString("line", config.line);
  preferences.putUChar("rows", config.visibleRows);
  preferences.putUChar("brightness", config.brightness);
  preferences.end();
}
