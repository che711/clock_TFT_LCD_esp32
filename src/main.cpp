/**
 * ESP32-S3 + ILI9488 Clock
 * Orbitron_Light_32 · Sprite · Плавное обновление секунд
 *
 * Структура:
 *   src/main.cpp   — логика, дисплей, HTTP
 *   src/webpage.h  — статичный HTML (данные через /api/stats)
 *
 * API:
 *   GET /                       → веб-дашборд
 *   GET /api/stats              → JSON со всеми данными
 *   GET /api/power?on=1|0       → вкл/выкл подсветку
 *   GET /api/brightness?value=N → яркость 0..100
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LovyanGFX.hpp>
#include "webpage.h"

// ══════════════════════════════════════════════════════════════════
//  НАСТРОЙКИ
// ══════════════════════════════════════════════════════════════════
#define WIFI_SSID          "SkyNet"
#define WIFI_PASS          "password"
#define TZ_STRING          "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER         "pool.ntp.org"
#define SERIAL_INTERVAL_MS  10000   // Serial-отчёт каждые N мс

// ══════════════════════════════════════════════════════════════════
//  ДИСПЛЕЙ
// ══════════════════════════════════════════════════════════════════
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX() {
        auto bus_cfg      = _bus.config();
        bus_cfg.spi_host  = SPI2_HOST;
        bus_cfg.pin_sclk  = 12;
        bus_cfg.pin_mosi  = 11;
        bus_cfg.pin_miso  = 13;
        bus_cfg.pin_dc    = 9;
        _bus.config(bus_cfg);
        _panel.setBus(&_bus);

        auto panel_cfg         = _panel.config();
        panel_cfg.pin_cs       = 10;
        panel_cfg.pin_rst      = 8;
        panel_cfg.panel_width  = 320;
        panel_cfg.panel_height = 480;
        panel_cfg.bus_shared   = true;
        _panel.config(panel_cfg);

        auto light_cfg    = _light.config();
        light_cfg.pin_bl  = 3;
        _light.config(light_cfg);
        _panel.setLight(&_light);

        auto touch_cfg         = _touch.config();
        touch_cfg.pin_cs       = 4;
        touch_cfg.pin_int      = 2;
        touch_cfg.bus_shared   = true;
        _touch.config(touch_cfg);
        _panel.setTouch(&_touch);

        setPanel(&_panel);
    }
};

static LGFX        lcd;
static LGFX_Sprite clockSprite(&lcd);
WebServer          server(80);

// ── Состояние дисплея ─────────────────────────────────────────────
bool displayOn      = true;
int  brightness     = 200;   // 0-255
bool forceBarRedraw = false; // флаг: перерисовать нижнюю полосу принудительно

// ── CPU load (два FreeRTOS-таска) ────────────────────────────────
// idleCountTask крутится на минимальном приоритете, считает итерации.
// cpuMonTask раз в секунду сравнивает с калиброванным максимумом.
static volatile uint32_t s_idleCount = 0;
static volatile int      s_cpuLoad   = 0;   // 0-100 %

static void idleCountTask(void*) {
    for (;;) s_idleCount++;   // pure busy-count at idle priority
}

static void cpuMonTask(void*) {
    uint32_t hi = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t c  = s_idleCount;
        s_idleCount = 0;
        if (c > hi) hi = c;                         // auto-calibrate max
        s_cpuLoad = hi ? constrain(100 - (int)((uint64_t)c * 100 / hi), 0, 100) : 0;
    }
}

// ── Цвета ─────────────────────────────────────────────────────────
static const uint32_t C_BG     = 0xFFFFFF;
static const uint32_t C_CLOCK  = 0x111111;
static const uint32_t C_BAR_BG = 0x2B2B3A;
static const uint32_t C_IP     = 0x44FF88;
static const uint32_t C_DATE   = 0xBBBBCC;
static const uint32_t C_SSID   = 0xFFE040;

// ── Геометрия ─────────────────────────────────────────────────────
#define SCR_W    480
#define CLOCK_X  15
#define CLOCK_Y  48
#define CLOCK_W  450
#define CLOCK_H  185

char prevHHMM[6] = "";

// ══════════════════════════════════════════════════════════════════
//  ВСПОМОГАТЕЛЬНЫЕ
// ══════════════════════════════════════════════════════════════════
String uptimeStr() {
    unsigned long s = millis() / 1000;
    char buf[28];
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus",
             s/86400, (s%86400)/3600, (s%3600)/60, s%60);
    return String(buf);
}

const char* resetReasonStr() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_SW:        return "Software";
        case ESP_RST_PANIC:     return "Panic";
        case ESP_RST_INT_WDT:   return "Int WDT";
        case ESP_RST_TASK_WDT:  return "Task WDT";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT:  return "Brownout";
        default:                return "Unknown";
    }
}

// ══════════════════════════════════════════════════════════════════
//  ДИСПЛЕЙ
// ══════════════════════════════════════════════════════════════════
void drawLayout() {
    lcd.fillRect(0, 0,   SCR_W, 280, C_BG);
    lcd.fillRect(0, 280, SCR_W,  40, C_BAR_BG);
}


void updateClock() {
    if (!displayOn) return;

    struct tm ti;
    if (!getLocalTime(&ti)) return;

    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d-%02d", ti.tm_hour, ti.tm_min);

    // ==================== ЧАСЫ И МИНУТЫ ====================
    if (strcmp(hhmm, prevHHMM) != 0) {
        strcpy(prevHHMM, hhmm);

        clockSprite.fillSprite(C_BG);

        clockSprite.setFont(&lgfx::fonts::Orbitron_Light_32);

        // Было: 2.40f, 5.30f
        // Уменьшено примерно на 5%
        clockSprite.setTextSize(2.28f, 5.03f);

        clockSprite.setTextColor(C_CLOCK);
        clockSprite.setTextDatum(lgfx::MC_DATUM);

        clockSprite.drawString(hhmm, 148, CLOCK_H / 2 + 6);
    }

    // ==================== СЕКУНДЫ ====================
    char ss[3];
    snprintf(ss, sizeof(ss), "%02d", ti.tm_sec);

    clockSprite.fillRect(280, 15, 190, CLOCK_H - 35, C_BG);

    clockSprite.setFont(&lgfx::fonts::Orbitron_Light_32);

    // Такой же scale для секунд
    clockSprite.setTextSize(2.28f, 5.03f);

    clockSprite.setTextColor(C_CLOCK);
    clockSprite.setTextDatum(lgfx::MC_DATUM);

    clockSprite.drawString("-", 300, CLOCK_H / 2 + 6);
    clockSprite.drawString(ss, 385, CLOCK_H / 2 + 6);

    clockSprite.pushSprite(CLOCK_X, CLOCK_Y);
}


void updateBottomBar() {
    if (!displayOn) return;

    struct tm ti;
    if (!getLocalTime(&ti)) return;

    String ip   = WiFi.localIP().toString();
    String ssid = WiFi.SSID();

    const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                        "Jul","Aug","Sep","Oct","Nov","Dec"};
    char dateBuf[14];
    snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d",
             ti.tm_mday, mo[ti.tm_mon], ti.tm_year + 1900);

    static char prevDate[14] = "", prevIP[16] = "", prevSSID[33] = "";
    if (!forceBarRedraw          &&
        strcmp(dateBuf,      prevDate) == 0 &&
        strcmp(ip.c_str(),   prevIP)   == 0 &&
        strcmp(ssid.c_str(), prevSSID) == 0) return;

    forceBarRedraw = false;

    strncpy(prevDate, dateBuf,       sizeof(prevDate)  - 1);
    strncpy(prevIP,   ip.c_str(),    sizeof(prevIP)    - 1);
    strncpy(prevSSID, ssid.c_str(),  sizeof(prevSSID)  - 1);

    lcd.fillRect(0, 280, SCR_W, 40, C_BAR_BG);
    lcd.setFont(&fonts::Font2);
    int cy = 300;

    lcd.setTextColor(C_DATE,  C_BAR_BG);
    lcd.setTextDatum(lgfx::ML_DATUM);
    lcd.drawString(dateBuf, 12, cy);

    lcd.setTextColor(C_IP,    C_BAR_BG);
    lcd.setTextDatum(lgfx::MC_DATUM);
    lcd.drawString(ip, 240, cy);

    lcd.setTextColor(C_SSID,  C_BAR_BG);
    lcd.setTextDatum(lgfx::MR_DATUM);
    lcd.drawString(ssid, SCR_W - 12, cy);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /   (статичный HTML)
// ══════════════════════════════════════════════════════════════════
void handleRoot() {
    server.send_P(200, "text/html", WEBPAGE);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/stats   (полный JSON)
// ══════════════════════════════════════════════════════════════════
void handleStats() {
    struct tm ti;
    bool ntpOk = getLocalTime(&ti);

    char timeBuf[9]  = "--:--:--";
    char dateBuf[20] = "---";
    if (ntpOk) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
        const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d",
                 ti.tm_mday, mo[ti.tm_mon], ti.tm_year + 1900);
    }

    uint32_t heapFree  = ESP.getFreeHeap();
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapMin   = ESP.getMinFreeHeap();
    float    temp      = temperatureRead();

    String json = "{";
    json += "\"time\":\""         + String(timeBuf)                    + "\",";
    json += "\"date\":\""         + String(dateBuf)                    + "\",";
    json += "\"ip\":\""           + WiFi.localIP().toString()          + "\",";
    json += "\"ssid\":\""         + WiFi.SSID()                       + "\",";
    json += "\"rssi\":"           + String(WiFi.RSSI())                + ",";
    json += "\"uptime\":\""       + uptimeStr()                        + "\",";
    json += "\"temp\":"           + String(temp, 1)                    + ",";
    json += "\"chip\":\""         + String(ESP.getChipModel())         + "\",";
    json += "\"chip_rev\":"       + String(ESP.getChipRevision())      + ",";
    json += "\"cpu_mhz\":"        + String(ESP.getCpuFreqMHz())        + ",";
    json += "\"heap_free\":"      + String(heapFree)                   + ",";
    json += "\"heap_total\":"     + String(heapTotal)                  + ",";
    json += "\"heap_min_free\":"  + String(heapMin)                    + ",";
    json += "\"reset_reason\":\"" + String(resetReasonStr())           + "\",";
    json += "\"display_on\":"     + String(displayOn ? "true":"false") + ",";
    json += "\"brightness\":"     + String(brightness * 100 / 255)     + ",";
    json += "\"cpu_load\":"       + String(s_cpuLoad);
    json += "}";

    server.send(200, "application/json", json);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/power?on=1|0
// ══════════════════════════════════════════════════════════════════
void handlePower() {
    if (server.hasArg("on")) {
        displayOn = (server.arg("on") != "0");
        lcd.setBrightness(displayOn ? brightness : 0);
        if (displayOn) {
            prevHHMM[0]   = '\0';    // принудительная перерисовка часов
            forceBarRedraw = true;   // принудительная перерисовка нижней полосы
            drawLayout();
        }
        Serial.printf("[API] power → %s\n", displayOn ? "ON" : "OFF");
    }
    server.send(200, "application/json",
        String("{\"display_on\":") + (displayOn ? "true" : "false") + "}");
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/brightness?value=0..100
// ══════════════════════════════════════════════════════════════════
void handleBrightness() {
    if (server.hasArg("value")) {
        int pct    = constrain(server.arg("value").toInt(), 0, 100);
        brightness = pct * 255 / 100;
        if (displayOn) lcd.setBrightness(brightness);
        Serial.printf("[API] brightness → %d%%\n", pct);
    }
    server.send(200, "application/json",
        String("{\"brightness\":") + String(brightness * 100 / 255) + "}");
}

// ══════════════════════════════════════════════════════════════════
//  SERIAL — сводка каждые SERIAL_INTERVAL_MS
// ══════════════════════════════════════════════════════════════════
void serialReport() {
    struct tm ti;
    bool ok = getLocalTime(&ti);

    Serial.println("─────────────────────────────────");
    if (ok) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d  %02d.%02d.%04d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec,
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        Serial.printf("  Time    : %s\n", buf);
    } else {
        Serial.println("  Time    : NTP not synced");
    }
    Serial.printf("  Uptime  : %s\n", uptimeStr().c_str());
    Serial.printf("  WiFi    : %s  %s  %d dBm\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    Serial.printf("  Temp    : %.1f C\n",   temperatureRead());
    Serial.printf("  CPU     : %d%%\n",     s_cpuLoad);
    Serial.printf("  Heap    : %u free / %u total  (min ever %u)\n",
                  ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap());
    Serial.printf("  Display : %s  brightness %d%%\n",
                  displayOn ? "ON" : "OFF", brightness * 100 / 255);
    Serial.println("─────────────────────────────────");
}

// ══════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n═══ ESP32-S3 Clock ═══");

    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(brightness);
    lcd.fillScreen(C_BG);

    if (!clockSprite.createSprite(CLOCK_W, CLOCK_H)) {
        Serial.println("[WARN] Sprite allocation failed");
    }

    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_BAR_BG, C_BG);
    lcd.setTextDatum(lgfx::MC_DATUM);
    lcd.drawString("Connecting...", 240, 160);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_STRING, NTP_SERVER);
        Serial.printf("WiFi    : %s  %s\n",
                      WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str());
        struct tm ti;
        t0 = millis();
        while (!getLocalTime(&ti) && millis() - t0 < 5000) delay(200);
        Serial.println("NTP     : synced");
    } else {
        Serial.println("WiFi    : timeout");
    }

    // CPU load tasks (Core 1, разные приоритеты)
    xTaskCreatePinnedToCore(idleCountTask, "idle", 1024, NULL, 1,               NULL, 1);
    xTaskCreatePinnedToCore(cpuMonTask,    "cpuM", 2048, NULL, tskIDLE_PRIORITY+2, NULL, 1);

    server.on("/",               HTTP_GET, handleRoot);
    server.on("/api/stats",      HTTP_GET, handleStats);
    server.on("/api/power",      HTTP_GET, handlePower);
    server.on("/api/brightness", HTTP_GET, handleBrightness);
    server.begin();
    Serial.printf("HTTP    : http://%s/\n", WiFi.localIP().toString().c_str());

    drawLayout();
    prevHHMM[0] = '\0';
    Serial.println("═══════════════════════");
}

// ══════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();

    uint32_t now = millis();

    static uint32_t lastClock = 0;
    if (now - lastClock >= 1000) {
        lastClock = now;
        updateClock();
        updateBottomBar();
    }

    static uint32_t lastSerial = 0;
    if (now - lastSerial >= SERIAL_INTERVAL_MS) {
        lastSerial = now;
        serialReport();
    }
}
