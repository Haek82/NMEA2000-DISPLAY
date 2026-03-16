# NMEA2000 Auxiliary Navigation Display

Wireless cockpit display for the [NMEA2000 WiFi Gateway](https://github.com/Haek82/NMEA2000-CAN). Runs on a **LilyGo T-Display-S3 Touch** and shows live navigation data received over WiFi from the gateway's WebSocket. Touch the screen to cycle through 10 pages of instrument data.

```
T-CAN485 Gateway                    T-Display-S3 Touch
┌─────────────────┐                 ┌──────────────────────────────────┐
│ WiFi AP         │◄── WiFi STA ───►│  OVERVIEW          ●●○●●●●●●●   │
│ WebSocket /ws   │── JSON ────────►│  60°10.1940'N  024°56.3040'E    │
└─────────────────┘                 │  COG 245° SOG 7.2 HDG 243 D14m │
                                    └──────────────────────────────────┘
                                      ◄ touch left    touch right ►
```

## Features

- **Touch navigation** — Touch left/right half of screen to cycle through 10 pages
- **Overview page** — Position + 4-column instrument grid (COG, SOG, HDG, Depth)
- **Position detail** — Large lat/lon with fix quality info
- **Instrument detail pages** — Each instrument shown extra large: COG, SOG, HDG, Depth, STW, AWS, AWA, Water Temp
- **Stale data warning** — Values turn red if no update for 5 seconds
- **Connection status** — Shows WiFi and WebSocket state with page dot indicators
- **Flicker-free** — Full-screen sprite buffer, only pushes on change
- **Dim button** — Left hardware button toggles between full and low brightness
- **Auto-reconnect** — Reconnects WiFi and WebSocket automatically

## Hardware

### LilyGo T-Display-S3 Touch

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3R8 (240 MHz dual-core, 16MB flash, 8MB PSRAM) |
| Display | 1.9" IPS LCD, 170x320 pixels, ST7789 controller, 8-bit parallel interface |
| Touch | CST816S capacitive touch controller (I2C) |
| Buttons | GPIO 0 (left), GPIO 14 (right) |
| Backlight | GPIO 38 |
| Touch I2C | SDA=18, SCL=17, INT=16, RST=21 |
| USB | USB-C (native USB on ESP32-S3) |
| Power | USB-C or battery via JST connector |

Purchase: https://lilygo.cc/products/t-display-s3

## Display Pages

The display runs in landscape mode (320x170 pixels) with 10 touch-navigable pages:

### Page 0 — Overview

```
┌──────────────────────────────────────────┐
│ OVERVIEW        ●○○○○○○○○○           [●] │
│  60°10.1940'N              2D 12Sat      │
│  024°56.3040'E             H0.8          │
│──────────────────────────────────────────│
│  COG     │  SOG     │  HDG     │ DEPTH   │
│ 245.3°T  │  7.2 kn  │ 243.1°M │  14.2 m │
└──────────┴──────────┴──────────┴─────────┘
```

### Page 1 — Position Detail

```
┌──────────────────────────────────────────┐
│ POSITION        ○●○○○○○○○○           [●] │
│                 POSITION                 │
│ <      60°10.1940'N                  >   │
│        024°56.3040'E                     │
│          2D  12Sat  HDOP 0.8             │
└──────────────────────────────────────────┘
```

### Pages 2–9 — Instrument Detail

```
┌──────────────────────────────────────────┐
│ COG             ○○●○○○○○○○           [●] │
│                   COG                    │
│ <              247.3                 >   │
│                   °T                     │
└──────────────────────────────────────────┘
```

Available instruments: **COG**, **SOG**, **HDG**, **DEPTH**, **STW**, **AWS**, **AWA**, **W.TEMP**

### Color Coding

| Condition | Position Color | Value Color |
|-----------|---------------|-------------|
| Normal (data < 5s old) | Green | Instrument color |
| Stale (no update > 5s) | Red | Red |
| No data received yet | Gray | Gray |
| Shallow depth (< 3m) | — | Red |

### Status Indicator (top-right corner)

| Indicator | Meaning |
|-----------|---------|
| Green dot | Connected and receiving data |
| `NO WIFI` (red) | Not connected to gateway WiFi |
| `NO DATA` (orange) | WiFi connected, WebSocket disconnected |
| `STALE` (red) | WebSocket connected but no recent data |

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- The [NMEA2000 WiFi Gateway](https://github.com/Haek82/NMEA2000-CAN) running on a T-CAN485
- USB-C cable for the T-Display-S3

### Build and Flash

```bash
cd NMEA2000-Display

# Build
pio run

# Upload to T-Display-S3
pio run -t upload

# Serial monitor
pio device monitor
```

### First Boot

1. Flash the firmware to the T-Display-S3
2. Power on the T-CAN485 gateway (creates the `Boat_Gateway` WiFi network)
3. Power on the T-Display-S3 — it shows "Connecting to Boat_Gateway..."
4. Once connected, the overview page appears with navigation data
5. Touch the right half of the screen to cycle through detail pages

### Serial Monitor Output

```
=== NMEA2000 Nav Display ===
Board: LilyGo T-Display-S3 Touch
[TOUCH] Initialized
[WiFi] Connecting to Boat_Gateway...
[WiFi] Connected, IP: 192.168.4.2
[WS] Connected
```

## Controls

| Input | Action |
|-------|--------|
| Touch right half | Next page |
| Touch left half | Previous page |
| Left button (GPIO 0) | Toggle brightness (full / dim) |

Pages wrap around: touching right on the last page returns to the overview.

## Configuration

Edit the top of `src/main.cpp`:

```cpp
static const char* GATEWAY_SSID = "Boat_Gateway";   // WiFi network name
static const char* GATEWAY_PASS = "";                // Password (empty = open)
static const char* GATEWAY_HOST = "192.168.4.1";     // Gateway IP
static const uint16_t GATEWAY_WS_PORT = 80;          // WebSocket port
```

If you changed the WiFi SSID or password on the gateway, update these values to match.

## How It Works

1. The T-Display-S3 connects to the gateway's WiFi AP as a station (client)
2. It opens a WebSocket connection to `ws://192.168.4.1/ws`
3. The gateway pushes JSON messages containing navigation data:
   ```json
   {
     "type": "nav",
     "lat": 60.169900,
     "lon": 24.938400,
     "cog": 245.3,
     "sog": 7.2,
     "stw": 5.9,
     "hdg": 243.1,
     "hdgTrue": false,
     "depth": 14.2,
     "aws": 12.4,
     "awa": 42.0,
     "wTemp": 18.2,
     "fix": 2,
     "sats": 12,
     "hdop": 0.8
   }
   ```
4. The display parses the JSON and renders it on the LCD using a full-screen sprite (no flicker)
5. Touch input cycles through overview, position detail, and 8 instrument detail pages
6. If the connection drops, it automatically reconnects

No changes are needed on the gateway — the display uses the same WebSocket that the browser dashboard uses.

## Project Structure

```
NMEA2000-Display/
├── platformio.ini      # Board config, TFT_eSPI pin definitions, libraries
├── mockup.html         # Interactive browser mockup of the display
├── README.md           # This file
└── src/
    └── main.cpp        # Everything: WiFi, WebSocket, JSON, touch, TFT rendering
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | ^2.5.43 | ST7789 LCD driver (8-bit parallel) |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.3.0 | Parse WebSocket JSON |
| [WebSockets](https://github.com/Links2004/arduinoWebSockets) | ^2.6.1 | WebSocket client |
| [CST816S](https://github.com/fbiego/CST816S) | ^1.3.0 | Capacitive touch controller |

TFT_eSPI pin configuration is done entirely through build flags in `platformio.ini` — no need to edit library header files.

## Troubleshooting

### Screen stays black
- GPIO 38 must be HIGH for the backlight. The firmware does this automatically, but if you see nothing at all, check USB power delivery.
- Try pressing the left button in case brightness was set to minimum.

### Touch not responding
- The CST816S uses I2C on GPIO 18 (SDA) and GPIO 17 (SCL). Check that you have the Touch version of the T-Display-S3 (non-touch version has no touch IC).
- Check serial output for `[TOUCH] Initialized` on boot.

### "NO WIFI" shown
- Make sure the T-CAN485 gateway is powered on and broadcasting `Boat_Gateway`.
- Verify SSID and password match between the two devices.
- The display retries WiFi every 5 seconds automatically.

### "NO DATA" shown
- WiFi is connected but WebSocket failed. Check that the gateway web server is running (try `http://192.168.4.1` from a phone on the same network).
- WebSocket reconnects every 2 seconds automatically.

### Values show "--.-"
- No navigation data received yet. The gateway needs to be connected to an active NMEA 2000 bus with instruments transmitting PGNs.

### Values turn red
- Data is stale (no update in 5+ seconds). Check the CAN bus connection on the gateway, or if the gateway has lost power.

### Build errors about TFT pins
- All TFT_eSPI configuration is in `platformio.ini` build flags. Do not use a separate `User_Setup.h` file — it will conflict.
- The Touch version uses 8-bit parallel interface, not SPI. Make sure your `platformio.ini` includes `-DTFT_PARALLEL_8_BIT=1` and the D0–D7 data pins.

## Power Options

The T-Display-S3 can be powered by:
- **USB-C** — from a chart table USB port or power bank
- **LiPo battery** — via the JST connector on the board (includes charging circuit)

For cockpit mounting, a small waterproof enclosure with a clear window over the display works well.

## Related

- [NMEA2000 WiFi Gateway](https://github.com/Haek82/NMEA2000-CAN) — the T-CAN485 gateway that this display connects to
