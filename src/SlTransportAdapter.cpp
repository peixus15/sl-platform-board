#include "SlTransportAdapter.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>

namespace {

void addUnique(std::vector<String>& values, const String& value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

}  // namespace

bool SlTransportAdapter::request(uint16_t siteId, JsonDocument& document, String& error) {
  error = "";

  if (WiFi.status() != WL_CONNECTED) {
    error = "Wi-Fi frånkopplat";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const String url = "https://transport.integration.sl.se/v1/sites/" + String(siteId) +
                     "/departures";
  if (!http.begin(client, url)) {
    error = "Kunde inte starta HTTPS-förfrågan";
    return false;
  }

  // HTTP/1.0 avoids chunked transfer encoding, which getStream() does not decode.
  http.useHTTP10(true);
  http.setTimeout(8000);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = status < 0 ? "SL API " + HTTPClient::errorToString(status)
                       : "SL API HTTP " + String(status);
    http.end();
    return false;
  }

  // Keep only the fields we use; the full response can exceed the ESP32's free heap.
  JsonDocument filter;
  JsonObject departureFilter = filter["departures"][0].to<JsonObject>();
  departureFilter["destination"] = true;
  departureFilter["display"] = true;
  departureFilter["state"] = true;
  departureFilter["line"]["designation"] = true;
  departureFilter["line"]["transport_mode"] = true;
  departureFilter["stop_point"]["designation"] = true;
  departureFilter["stop_area"]["name"] = true;

  const DeserializationError jsonError = deserializeJson(
      document, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (jsonError) {
    error = "Ogiltig JSON från SL: " + String(jsonError.c_str());
    return false;
  }
  return true;
}

bool SlTransportAdapter::fetch(uint16_t siteId, const String& platform, const String& line,
                               SlStopData& result, String& error) {
  result.departures.clear();
  result.stopName = "";
  result.notice = "";

  JsonDocument document;
  if (!request(siteId, document, error)) return false;

  JsonArray departures = document["departures"].as<JsonArray>();
  for (JsonObject item : departures) {
    if (result.stopName.isEmpty()) result.stopName = item["stop_area"]["name"] | "";

    const String mode = item["line"]["transport_mode"] | "";
    const String itemPlatform = item["stop_point"]["designation"] | "";
    if (mode != "METRO" || itemPlatform != platform) continue;

    const String state = item["state"] | "";
    if (state == "CANCELLED" || state == "PASSED") continue;

    Departure departure;
    departure.line = item["line"]["designation"] | "?";
    if (!line.isEmpty() && departure.line != line) continue;
    departure.destination = item["destination"] | "Okänd";
    departure.displayTime = item["display"] | "";
    departure.platform = itemPlatform;
    if (departure.displayTime.isEmpty()) departure.displayTime = "-- min";
    result.departures.push_back(departure);
  }

  return true;
}

bool SlTransportAdapter::fetchPlatforms(uint16_t siteId, std::vector<PlatformInfo>& platforms,
                                        String& stopName, String& error) {
  platforms.clear();
  stopName = "";

  JsonDocument document;
  if (!request(siteId, document, error)) return false;

  JsonArray departures = document["departures"].as<JsonArray>();
  for (JsonObject item : departures) {
    if (stopName.isEmpty()) stopName = item["stop_area"]["name"] | "";

    const String mode = item["line"]["transport_mode"] | "";
    const String designation = item["stop_point"]["designation"] | "";
    if (mode != "METRO" || designation.isEmpty()) continue;

    auto platform = std::find_if(platforms.begin(), platforms.end(),
                                 [&](const PlatformInfo& p) { return p.designation == designation; });
    if (platform == platforms.end()) {
      platforms.push_back({designation, {}, {}});
      platform = platforms.end() - 1;
    }
    addUnique(platform->lines, item["line"]["designation"] | "?");
    addUnique(platform->destinations, item["destination"] | "");
  }

  std::sort(platforms.begin(), platforms.end(),
            [](const PlatformInfo& a, const PlatformInfo& b) { return a.designation < b.designation; });
  return true;
}
