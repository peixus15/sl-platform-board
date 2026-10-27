# Stockholm Platform Board

ESP32 firmware for a two-row amber-style Stockholm metro departure display. The prototype targets an ESP32-DevKitC ESP-32U and two chained 64x32 HUB75 RGB panels (128x32 total).

## Current scope

- SL Transport live departures for any Stockholm metro station (default: Sankt Eriksplan, site ID `9116`).
- The setup page lists every metro station. After you pick one, the device asks SL which metro platforms are in use there and lists each with its lines and destinations, plus an optional single-line filter. Changes to station, platform, line, rows and brightness apply immediately, without a restart. Only Wi-Fi changes restart the device.
- Two-row Stockholm theme: line and destination left, SL's `display` value (for example `Nu` or `2 min`) right.
- Local setup page for Wi-Fi, site ID, platform, visible rows, and brightness. Settings persist in ESP32 Preferences.
- Polls the SL API every 25 seconds, and immediately whenever Wi-Fi (re)connects. Cancelled and already-passed departures are skipped.
- If the API or Wi-Fi fails, the last good departures remain visible for up to 3 minutes; the second row indicates loss of connection. After that the board clears to `INGEN KONTAKT MED SL` and the stop name.
- Destinations too long for the space between the line number and the time scroll right to left in a loop. The line number and time stay fixed, and the scroll pauses for 2 s at the start of each pass. Only those text windows are redrawn while scrolling.
- Panel text is always Swedish. The setup page has a language selector (Svenska / English / Español), remembered in the browser, and a short explanation under each setting.
- The setup page shows the last SL error, if any.

SL Transport replaced SL Departures v4 and does not require an API key. API documentation: https://www.trafiklab.se/api/other-apis/sl/transport/.

## Simulator (no hardware needed)

`simulator/index.html` renders the 128x32 panel in a browser. It uses the same Adafruit GFX 5x7 font bitmap, layout, SL filtering and timing rules as the firmware, with live SL data. Open the file directly in a browser, or serve it locally:

```
python -m http.server 8765 --directory simulator
```

It also has buttons to simulate a Wi-Fi outage and to jump the clock forward 3 minutes, so you can check the offline and stale-data screens. If you change the display logic in `StockholmTheme.cpp` or `SlTransportAdapter.cpp`, update the matching functions in the simulator too.

## Build and flash

1. Install PlatformIO Core or the PlatformIO IDE extension.
2. Open this project in PlatformIO and connect the ESP32 over USB.
3. Build with `pio run` and upload with `pio run -t upload`.
4. Open the serial monitor at 115200 baud.
5. Join the `SL-Board-Setup` Wi-Fi network (password `SLBoard9116`) and visit `http://192.168.4.1`.
6. Enter your Wi-Fi details and save. The device restarts and keeps its setup access point available for later changes.

## HUB75 wiring

The firmware's default ESP32 GPIO mapping is:

| HUB75 signal | ESP32 GPIO |
| --- | ---: |
| R1, G1, B1 | 25, 26, 27 |
| R2, G2, B2 | 14, 13, 23 |
| A, B, C, D | 32, 33, 17, 16 |
| CLK, LAT, OE | 22, 21, 4 |

Connect the ESP32 ground to the panel supply ground. Power the LED panels from a regulated 5 V supply sized from the panel manufacturer's maximum-current specification; never power the panels from the ESP32. Verify the panel's input/output chain order and pinout before connecting. The code assumes two 64x32 panels are chained horizontally, with the first panel at the top-left.

## Important prototype limitations

- The station list is a snapshot of SL's metro sites, embedded in the setup page. The firmware adapter and theme are Stockholm-only. Other cities need their own transport adapter and display theme.
- HTTPS certificate verification is disabled in the prototype to keep the ESP32 build self-contained. The SL endpoint is public and no API key is sent, but production firmware should validate the server certificate.
- The setup access point uses a shared default password and the web page has no authentication. Change the AP credentials before exposing the device to an untrusted network.
- The render uses the matrix library's built-in 5x7 bitmap font in CP437 mode, so Å, Ä and Ö (plus É and Ü) are drawn as real Swedish letters; other non-ASCII characters are dropped. The theme adjusts the font's Swedish letters: the dots of Ä and Ö are lifted one pixel off the letter, and Å is lifted one pixel onto the common baseline. It reproduces the photo's amber-on-black, two-row layout, not SL's proprietary font or exact hardware resolution.
- GPIO assignments and the HUB75 driver must be checked against the exact DevKitC and panel revision. If the board resets during boot, disconnect the panel while flashing and verify the panel's input-side pin mapping.
