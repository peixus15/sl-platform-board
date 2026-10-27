#pragma once

#include <ArduinoJson.h>

#include "Departure.h"

class SlTransportAdapter {
 public:
  // An empty `line` shows every metro line at the platform.
  bool fetch(uint16_t siteId, const String& platform, const String& line, SlStopData& result,
             String& error);
  // Lists the metro platforms currently served at a site, for the setup page.
  bool fetchPlatforms(uint16_t siteId, std::vector<PlatformInfo>& platforms, String& stopName,
                      String& error);

 private:
  bool request(uint16_t siteId, JsonDocument& document, String& error);
};
