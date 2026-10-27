#pragma once

#include <Arduino.h>
#include <vector>

struct Departure {
  String line;
  String destination;
  String displayTime;
  String platform;
};

struct PlatformInfo {
  String designation;
  std::vector<String> lines;
  std::vector<String> destinations;
};

struct SlStopData {
  std::vector<Departure> departures;
  String stopName;
  String notice;
};
