# Stockholm Platform Board Instructions

- Use PlatformIO with the Arduino framework and the `esp32dev` target.
- Keep transport parsing in `SlTransportAdapter` and city-specific pixel layout in the theme module.
- The initial SL Transport adapter uses site `9116` and filters the selected metro platform; preserve the API's `display` text such as `Nu` and `2 min`.
- Store user configuration in ESP32 Preferences. Never commit Wi-Fi credentials or other secrets.
- Keep display updates event-driven; do not clear and redraw the HUB75 frame continuously.
- Update `README.md` whenever wiring, setup access, API behavior, or build steps change.
- The current TLS client skips certificate verification for prototyping; treat this as a production hardening item.
