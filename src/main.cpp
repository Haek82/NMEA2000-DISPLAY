#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <CST816S.h>

// --- Configuration ---
static const char* GATEWAY_SSID = "Boat_Gateway";
static const char* GATEWAY_PASS = "";               // Empty if open network
static const char* GATEWAY_HOST = "192.168.4.1";
static const uint16_t GATEWAY_WS_PORT = 80;
static const char* GATEWAY_WS_PATH = "/ws";

// T-Display-S3 Touch pins
static const int PIN_BACKLIGHT = 38;
static const int PIN_BTN_LEFT  = 0;
static const int PIN_BTN_RIGHT = 14;

// Colors (RGB565)
static const uint16_t COL_BG       = 0x0861;  // #0f172a
static const uint16_t COL_CARD     = 0x1149;  // #1e293b
static const uint16_t COL_TEXT     = 0xF7BE;  // #f1f5f9
static const uint16_t COL_LABEL    = 0x9492;  // #94a3b8
static const uint16_t COL_DIM      = 0x632C;  // #64748b
static const uint16_t COL_GREEN    = 0x2EE5;  // #22c55e
static const uint16_t COL_RED      = 0xF0A2;  // #ef4444
static const uint16_t COL_ORANGE   = 0xFCA0;  // #f59e0b
static const uint16_t COL_BLUE     = 0x3B1F;  // #3b82f6
static const uint16_t COL_CYAN     = 0x05F7;  // #06b6d4
static const uint16_t COL_PURPLE   = 0xA55F;  // #a78bfa

// --- Pages ---
// Page 0 = overview, Page 1 = position detail, Pages 2..11 = instrument detail
static const int NUM_PAGES = 12;  // overview + position + 10 instruments
static int currentPage = 0;

// --- State ---
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
WebSocketsClient ws;
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

struct NavState {
    double lat = 0, lon = 0;
    float cog = 0, sog = 0, stw = 0, heading = 0;
    float depth = 0, depthOffset = 0;
    float aws = 0, awa = 0;
    float tws = 0, twa = 0;
    float waterTemp = 0;
    int fixQuality = 0, sats = 0;
    float hdop = 0;
    bool headingTrue = false;
    unsigned long lastUpdate = 0;
};

static NavState nav;
static bool wsConnected = false;
static bool wifiConnected = false;
static uint8_t brightness = 255;
static bool needsRedraw = true;

// --- Instrument definitions ---
struct Instrument {
    const char* label;
    const char* unit;
    float* valuePtr;
    uint16_t color;
    int decimals;
};

// We store pointers so values stay current; built in setup after nav is initialized
static Instrument instruments[10];

static void buildInstruments() {
    instruments[0] = {"COG",     "\xB0T",  &nav.cog,       COL_ORANGE, 1};
    instruments[1] = {"SOG",     "kn",     &nav.sog,       COL_BLUE,   1};
    instruments[2] = {"HDG",     "\xB0M",  &nav.heading,   COL_CYAN,   1};
    instruments[3] = {"DEPTH",   "m",      &nav.depth,     COL_GREEN,  1};
    instruments[4] = {"STW",     "kn",     &nav.stw,       COL_BLUE,   1};
    instruments[5] = {"AWS",     "kn",     &nav.aws,       COL_CYAN,   1};
    instruments[6] = {"AWA",     "\xB0",   &nav.awa,       COL_CYAN,   1};
    instruments[7] = {"TWS",     "kn",     &nav.tws,       COL_PURPLE, 1};
    instruments[8] = {"TWA",     "\xB0",   &nav.twa,       COL_PURPLE, 1};
    instruments[9] = {"W.TEMP",  "\xB0" "C",  &nav.waterTemp, COL_GREEN,  1};
}

// --- Formatters ---
static String fmtLat(double v) {
    char ns = v >= 0 ? 'N' : 'S';
    v = fabs(v);
    int d = (int)v;
    double m = (v - d) * 60.0;
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d%c%07.4f'%c", d, 0xB0, m, ns);
    return String(buf);
}

static String fmtLon(double v) {
    char ew = v >= 0 ? 'E' : 'W';
    v = fabs(v);
    int d = (int)v;
    double m = (v - d) * 60.0;
    char buf[20];
    snprintf(buf, sizeof(buf), "%03d%c%07.4f'%c", d, 0xB0, m, ew);
    return String(buf);
}

// --- Draw status bar (shared top strip, y 0..14) ---
static void drawStatusBar(bool stale, bool noData) {
    // Page indicator on left
    sprite.setTextColor(COL_LABEL, COL_BG);
    sprite.setTextDatum(TL_DATUM);
    if (currentPage == 0) {
        sprite.drawString("OVERVIEW", 8, 2, 1);
    } else if (currentPage == 1) {
        sprite.drawString("POSITION", 8, 2, 1);
    } else {
        char pageBuf[20];
        snprintf(pageBuf, sizeof(pageBuf), "%s", instruments[currentPage - 2].label);
        sprite.drawString(pageBuf, 8, 2, 1);
    }

    // Page dots centered
    int dotSpacing = NUM_PAGES <= 10 ? 10 : 8;
    int dotStartX = 160 - (NUM_PAGES * dotSpacing / 2);
    for (int i = 0; i < NUM_PAGES; i++) {
        uint16_t dotCol = (i == currentPage) ? COL_TEXT : COL_DIM;
        sprite.fillCircle(dotStartX + i * dotSpacing, 7, 2, dotCol);
    }

    // Connection status on right
    if (!wifiConnected) {
        sprite.setTextColor(COL_RED, COL_BG);
        sprite.setTextDatum(TR_DATUM);
        sprite.drawString("NO WIFI", 312, 2, 1);
    } else if (!wsConnected) {
        sprite.setTextColor(COL_ORANGE, COL_BG);
        sprite.setTextDatum(TR_DATUM);
        sprite.drawString("NO DATA", 312, 2, 1);
    } else if (stale) {
        sprite.setTextColor(COL_RED, COL_BG);
        sprite.setTextDatum(TR_DATUM);
        sprite.drawString("STALE", 312, 2, 1);
    } else {
        sprite.fillCircle(308, 7, 3, COL_GREEN);
    }
    sprite.setTextDatum(TL_DATUM);

    // Separator
    sprite.drawFastHLine(4, 15, 312, COL_CARD);
}

// --- Draw overview page (page 0) ---
static void drawOverview() {
    bool stale = (millis() - nav.lastUpdate) > 5000;
    bool noData = nav.lastUpdate == 0;
    uint16_t posColor = noData ? COL_DIM : (stale ? COL_RED : COL_GREEN);
    uint16_t valColor = noData ? COL_DIM : (stale ? COL_RED : COL_TEXT);

    drawStatusBar(stale, noData);

    // --- Position section ---
    sprite.setTextColor(posColor, COL_BG);
    if (noData) {
        sprite.drawString("--\xB0--.----'N", 8, 20, 4);
        sprite.drawString("---\xB0--.----'E", 8, 46, 4);
    } else {
        sprite.drawString(fmtLat(nav.lat), 8, 20, 4);
        sprite.drawString(fmtLon(nav.lon), 8, 46, 4);
    }

    // Fix info
    sprite.setTextColor(COL_DIM, COL_BG);
    sprite.setTextDatum(TR_DATUM);
    if (!noData) {
        char fixBuf[16];
        snprintf(fixBuf, sizeof(fixBuf), "%dD %dSat", nav.fixQuality, nav.sats);
        sprite.drawString(fixBuf, 312, 20, 2);
        char hdopBuf[10];
        snprintf(hdopBuf, sizeof(hdopBuf), "H%.1f", nav.hdop);
        sprite.drawString(hdopBuf, 312, 38, 2);
    }
    sprite.setTextDatum(TL_DATUM);

    // Divider
    sprite.drawFastHLine(4, 74, 312, COL_CARD);

    // --- 4-column instrument grid (first 4 instruments) ---
    for (int i = 0; i < 4; i++) {
        int x = i * 80;
        int centerX = x + 40;
        Instrument& inst = instruments[i];

        // Dynamic color for depth
        uint16_t col = inst.color;
        if (i == 3 && !noData && nav.depth < 3.0f) col = COL_RED;

        // Label
        sprite.setTextColor(COL_LABEL, COL_BG);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString(inst.label, centerX, 78, 1);

        // Value
        sprite.setTextColor(noData ? COL_DIM : col, COL_BG);
        char valBuf[12];
        if (noData) {
            snprintf(valBuf, sizeof(valBuf), "--.-");
        } else {
            snprintf(valBuf, sizeof(valBuf), "%.*f", inst.decimals, *inst.valuePtr);
        }
        sprite.drawString(valBuf, centerX, 92, 6);

        // Unit
        sprite.setTextColor(COL_DIM, COL_BG);
        sprite.drawString(inst.unit, centerX, 138, 2);

        // Column divider
        if (i > 0) {
            sprite.drawFastVLine(x, 76, 90, COL_CARD);
        }
    }
    sprite.setTextDatum(TL_DATUM);
}

// --- Draw position detail page (page 1) ---
static void drawPositionDetail() {
    bool stale = (millis() - nav.lastUpdate) > 5000;
    bool noData = nav.lastUpdate == 0;
    uint16_t posColor = noData ? COL_DIM : (stale ? COL_RED : COL_GREEN);

    drawStatusBar(stale, noData);

    // Label
    sprite.setTextColor(COL_LABEL, COL_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString("POSITION", 160, 20, 2);

    // Latitude - large
    sprite.setTextColor(posColor, COL_BG);
    if (noData) {
        sprite.drawString("--\xB0--.----'N", 160, 42, 6);
        sprite.drawString("---\xB0--.----'E", 160, 96, 6);
    } else {
        sprite.drawString(fmtLat(nav.lat), 160, 42, 6);
        sprite.drawString(fmtLon(nav.lon), 160, 96, 6);
    }

    // Fix info at bottom
    sprite.setTextColor(COL_DIM, COL_BG);
    if (!noData) {
        char fixBuf[24];
        snprintf(fixBuf, sizeof(fixBuf), "%dD  %dSat  HDOP %.1f", nav.fixQuality, nav.sats, nav.hdop);
        sprite.drawString(fixBuf, 160, 150, 2);
    }

    // Touch arrows
    sprite.setTextColor(COL_DIM, COL_BG);
    sprite.setTextDatum(ML_DATUM);
    sprite.drawString("<", 6, 85, 4);
    sprite.setTextDatum(MR_DATUM);
    sprite.drawString(">", 314, 85, 4);

    sprite.setTextDatum(TL_DATUM);
}

// --- Draw instrument detail page (pages 2..9) ---
static void drawDetail(int instrIndex) {
    bool stale = (millis() - nav.lastUpdate) > 5000;
    bool noData = nav.lastUpdate == 0;

    drawStatusBar(stale, noData);

    Instrument& inst = instruments[instrIndex];

    // Dynamic color for depth
    uint16_t col = inst.color;
    if (instrIndex == 3 && !noData && nav.depth < 3.0f) col = COL_RED;

    // Label - large
    sprite.setTextColor(COL_LABEL, COL_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString(inst.label, 160, 22, 4);

    // Value - very large, centered
    sprite.setTextColor(noData ? COL_DIM : col, COL_BG);
    char valBuf[12];
    if (noData) {
        snprintf(valBuf, sizeof(valBuf), "--.-");
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.*f", inst.decimals, *inst.valuePtr);
    }
    sprite.drawString(valBuf, 160, 58, 8);  // Font 8 = largest built-in (75px tall)

    // Unit
    sprite.setTextColor(COL_DIM, COL_BG);
    sprite.drawString(inst.unit, 160, 140, 4);

    // Left/right arrows as touch hints
    sprite.setTextColor(COL_DIM, COL_BG);
    sprite.setTextDatum(ML_DATUM);
    sprite.drawString("<", 6, 90, 4);
    sprite.setTextDatum(MR_DATUM);
    sprite.drawString(">", 314, 90, 4);

    sprite.setTextDatum(TL_DATUM);
}

// --- Main draw dispatcher ---
static void drawDisplay() {
    sprite.fillSprite(COL_BG);

    if (currentPage == 0) {
        drawOverview();
    } else if (currentPage == 1) {
        drawPositionDetail();
    } else {
        drawDetail(currentPage - 2);
    }

    sprite.pushSprite(0, 0);
}

// --- Touch handling ---
static void handleTouch() {
    static unsigned long lastTouch = 0;

    if (!touch.available()) return;

    // Debounce: ignore touches within 300ms
    if (millis() - lastTouch < 300) return;
    lastTouch = millis();

    // The touch coordinates are in the raw panel frame (170x320 portrait).
    // With rotation=1 (landscape), we need to map:
    //   screen_x = touch_y
    //   screen_y = 170 - touch_x
    int sx = touch.data.y;

    if (sx < 160) {
        // Left half: previous page
        currentPage--;
        if (currentPage < 0) currentPage = NUM_PAGES - 1;
    } else {
        // Right half: next page
        currentPage++;
        if (currentPage >= NUM_PAGES) currentPage = 0;
    }

    needsRedraw = true;
    Serial.printf("[TOUCH] Page: %d\n", currentPage);
}

// --- WebSocket handler ---
static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            wsConnected = true;
            needsRedraw = true;
            Serial.println("[WS] Connected");
            break;

        case WStype_DISCONNECTED:
            wsConnected = false;
            needsRedraw = true;
            Serial.println("[WS] Disconnected");
            break;

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) break;

            const char* msgType = doc["type"];
            if (!msgType) break;

            if (strcmp(msgType, "nav") == 0) {
                nav.lat = doc["lat"].as<double>();
                nav.lon = doc["lon"].as<double>();
                nav.cog = doc["cog"].as<float>();
                nav.sog = doc["sog"].as<float>();
                nav.stw = doc["stw"].as<float>();
                nav.heading = doc["hdg"].as<float>();
                nav.headingTrue = doc["hdgTrue"].as<bool>();
                nav.depth = doc["depth"].as<float>();
                nav.depthOffset = doc["depthOff"].as<float>();
                nav.aws = doc["aws"].as<float>();
                nav.awa = doc["awa"].as<float>();
                nav.tws = doc["tws"].as<float>();
                nav.twa = doc["twa"].as<float>();
                nav.waterTemp = doc["wTemp"].as<float>();
                nav.fixQuality = doc["fix"].as<int>();
                nav.sats = doc["sats"].as<int>();
                nav.hdop = doc["hdop"].as<float>();
                nav.lastUpdate = millis();
                needsRedraw = true;
            }
            break;
        }

        default:
            break;
    }
}

// --- WiFi management ---
static void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        return;
    }

    wifiConnected = false;
    Serial.printf("[WiFi] Connecting to %s...\n", GATEWAY_SSID);
    WiFi.begin(GATEWAY_SSID, GATEWAY_PASS);
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== NMEA2000 Nav Display ===");
    Serial.println("Board: LilyGo T-Display-S3 Touch");

    // Backlight on
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);

    // Buttons (still usable for brightness)
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);

    // Init touch
    touch.begin();
    Serial.println("[TOUCH] Initialized");

    // Init display
    tft.init();
    tft.setRotation(1);  // Landscape: 320x170
    tft.fillScreen(COL_BG);

    // Create full-screen sprite for flicker-free drawing
    sprite.createSprite(320, 170);
    sprite.setSwapBytes(true);

    // Build instrument table
    buildInstruments();

    // Show boot screen
    sprite.fillSprite(COL_BG);
    sprite.setTextColor(COL_TEXT, COL_BG);
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("NMEA2000 Nav Display", 160, 70, 4);
    sprite.setTextColor(COL_DIM, COL_BG);
    sprite.drawString("Connecting to Boat_Gateway...", 160, 100, 2);
    sprite.pushSprite(0, 0);
    sprite.setTextDatum(TL_DATUM);

    // Connect WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(GATEWAY_SSID, GATEWAY_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Not connected yet, will retry...");
    }

    // WebSocket client
    ws.begin(GATEWAY_HOST, GATEWAY_WS_PORT, GATEWAY_WS_PATH);
    ws.onEvent(onWsEvent);
    ws.setReconnectInterval(2000);

    Serial.println("[SYS] Ready");
}

// --- Main loop ---
void loop() {
    // WiFi reconnection
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 5000) {
        lastWifiCheck = millis();
        bool prevWifi = wifiConnected;
        wifiConnected = (WiFi.status() == WL_CONNECTED);
        if (!wifiConnected) {
            connectWiFi();
        }
        if (prevWifi != wifiConnected) needsRedraw = true;
    }

    // WebSocket loop
    ws.loop();

    // Handle touch input
    handleTouch();

    // Handle buttons (brightness only)
    static unsigned long lastBtnCheck = 0;
    if (millis() - lastBtnCheck > 200) {
        lastBtnCheck = millis();
        if (digitalRead(PIN_BTN_LEFT) == LOW) {
            if (brightness > 50) brightness = 50;
            else brightness = 255;
            analogWrite(PIN_BACKLIGHT, brightness);
            Serial.printf("[BTN] Brightness: %d\n", brightness);
        }
    }

    // Redraw display at max ~5 Hz
    static unsigned long lastDraw = 0;
    if (needsRedraw && (millis() - lastDraw >= 200)) {
        lastDraw = millis();
        needsRedraw = false;
        drawDisplay();
    }

    // Mark stale if no update for 5 seconds
    static unsigned long lastStaleCheck = 0;
    if (millis() - lastStaleCheck >= 1000) {
        lastStaleCheck = millis();
        if (nav.lastUpdate > 0 && (millis() - nav.lastUpdate) > 5000) {
            needsRedraw = true;
        }
    }

    delay(1);
}
