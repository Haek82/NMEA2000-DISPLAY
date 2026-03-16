# NMEA2000 Auxiliary Navigation Display

Wireless cockpit display for the [NMEA2000 WiFi Gateway](../NMEA2000-CAN/). Runs on a **LilyGo T-Display-S3** and shows live navigation data received over WiFi from the gateway's WebSocket.

```
T-CAN485 Gateway                    T-Display-S3
┌─────────────────┐                 ┌──────────────────────────────────┐
│ WiFi AP         │◄── WiFi STA ───►│  60°10.1940'N  024°56.3040'E    │
│ WebSocket /ws   │── JSON ────────►│  COG 245° SOG 7.2 HDG 243 D14m │
└─────────────────┘                 └──────────────────────────────────┘
```

## Features

- **Live coordinates** — Latitude and longitude in large font, updated at ~4 Hz
- **Instrument strip** — COG, SOG, Heading, Depth in a 4-column layout
- **Stale data warning** — Values turn red if no update for 5 seconds
- **Connection status** — Shows WiFi and WebSocket state in the corner
- **Flicker-free** — Full-screen sprite buffer, only pushes on change
- **Dim button** — Left button toggles between full and low brightness
- **Auto-reconnect** — Reconnects WiFi and WebSocket automatically

## Hardware

### LilyGo T-Display-S3

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3R8 (240 MHz dual-core, 16MB flash, 8MB PSRAM) |
| Display | 1.9" IPS LCD, 170x320 pixels, ST7789 controller |
| Buttons | GPIO 0 (left), GPIO 14 (right) |
| Backlight | GPIO 15 |
| USB | USB-C (native USB on ESP32-S3) |
| Power | USB-C or battery via JST connector |

Purchase: https://lilygo.cc/products/t-display-s3

## Display Layout

The display runs in landscape mode (320x170 pixels):

```
┌──────────────────────────────────────────┐
│ POSITION                             [●] │  ← Status dot
│  60°10.1940'N              2D 12Sat      │  ← Large lat/lon
│  024°56.3040'E             H0.8          │
│──────────────────────────────────────────│
│  COG     │  SOG     │  HDG     │ DEPTH   │  ← 4-column grid
│ 245.3°T  │  7.2 kn  │ 243.1°M │  14.2 m │
└──────────┴──────────┴──────────┴─────────┘
```

### Color Coding

| Condition | Position Color | Value Color |
|-----------|---------------|-------------|
| Normal (data < 5s old) | Green | White |
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
- The [NMEA2000 WiFi Gateway](../NMEA2000-CAN/) running on a T-CAN485
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
4. Once connected, navigation data appears on screen within seconds

### Serial Monitor Output

```
=== NMEA2000 Nav Display ===
Board: LilyGo T-Display-S3
[WiFi] Connecting to Boat_Gateway...
[WiFi] Connected, IP: 192.168.4.2
[WS] Connected
```

## Buttons

| Button | Action |
|--------|--------|
| Left (GPIO 0) | Toggle brightness (full / dim) |
| Right (GPIO 14) | Reserved for future use (page cycling) |

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
     "hdg": 243.1,
     "hdgTrue": false,
     "depth": 14.2,
     "fix": 2,
     "sats": 12,
     "hdop": 0.8
   }
   ```
4. The display parses the JSON and renders it on the LCD using a full-screen sprite (no flicker)
5. If the connection drops, it automatically reconnects

No changes are needed on the gateway — the display uses the same WebSocket that the browser dashboard uses.

## Project Structure

```
NMEA2000-Display/
├── platformio.ini      # Board config, TFT_eSPI pin definitions, libraries
├── README.md           # This file
└── src/
    └── main.cpp        # Everything: WiFi, WebSocket, JSON, TFT rendering
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | ^2.5.43 | ST7789 LCD driver |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.3.0 | Parse WebSocket JSON |
| [WebSockets](https://github.com/Links2004/arduinoWebSockets) | ^2.6.1 | WebSocket client |

TFT_eSPI pin configuration is done entirely through build flags in `platformio.ini` — no need to edit library header files.

## Troubleshooting

### Screen stays black
- GPIO 15 must be HIGH for the backlight. The firmware does this automatically, but if you see nothing at all, check USB power delivery.
- Try pressing the left button in case brightness was set to minimum.

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

## Power Options

The T-Display-S3 can be powered by:
- **USB-C** — from a chart table USB port or power bank
- **LiPo battery** — via the JST connector on the board (includes charging circuit)

For cockpit mounting, a small waterproof enclosure with a clear window over the display works well.

## Related

- [NMEA2000 WiFi Gateway](../NMEA2000-CAN/) — the T-CAN485 gateway that this display connects to
