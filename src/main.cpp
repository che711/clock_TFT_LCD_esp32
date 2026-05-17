/**
 * ESP32-S3 + ILI9488 3.5"  —  Clock Display + Web Dashboard
 *
 * Пины (нижний ряд: 3V3 3V3 RST 4 5 6 15 16 17 18 8 3 46 9 10 11 12 13 14 5V GND)
 * Пины (верхний ряд: GND TX RX 1 2 42 41 40 39 38 37 36 35 0 45 48 47 21 20 19 GND GND)
 *
 *   TFT_MOSI = 11,  TFT_MISO = 13,  TFT_SCLK = 12
 *   TFT_CS   = 10,  TFT_DC   = 9,   TFT_RST  = 8
 *   BL       = 3
 *   T_CS     = 4,   T_IRQ    = 2
 *
 * Структура:
 *   src/main.cpp    — логика, дисплей, HTTP
 *   src/webpage.h   — встроенный HTML дашборд (статичный, данные через /api/stats)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LovyanGFX.hpp>
#include "webpage.h"       // ← HTML вынесен сюда

// ══════════════════════════════════════════════════════════════════
//  НАСТРОЙКИ
// ══════════════════════════════════════════════════════════════════
#define WIFI_SSID   "network"
#define WIFI_PASS   "password"
#define TZ_STRING   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER  "pool.ntp.org"

#define SERIAL_INTERVAL_MS  10000   // лог в Serial каждые 10 сек

// ══════════════════════════════════════════════════════════════════
//  КОНФИГ ДИСПЛЕЯ (LovyanGFX — нативная поддержка IDF 5.x)
// ══════════════════════════════════════════════════════════════════
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 27000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = 13;
            cfg.pin_dc      = 9;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg          = _panel.config();
            cfg.pin_cs        = 10;
            cfg.pin_rst       = 8;
            cfg.pin_busy      = -1;
            cfg.panel_width   = 320;
            cfg.panel_height  = 480;
            cfg.memory_width  = 320;
            cfg.memory_height = 480;
            cfg.invert        = false;
            cfg.rgb_order     = false;
            cfg.dlen_16bit    = false;
            cfg.bus_shared    = true;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = 3;
            cfg.invert      = false;
            cfg.freq        = 5000;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg         = _touch.config();
            cfg.x_min        = 0;   cfg.x_max  = 319;
            cfg.y_min        = 0;   cfg.y_max  = 479;
            cfg.pin_int      = 2;
            cfg.bus_shared   = true;
            cfg.spi_host     = SPI2_HOST;
            cfg.freq         = 2500000;
            cfg.pin_sclk     = 12;  cfg.pin_mosi = 11;
            cfg.pin_miso     = 13;  cfg.pin_cs   = 4;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

static LGFX    lcd;
WebServer      server(80);

// ── Буферы предыдущих значений (перерисовка только при изменении) ─
char prevTime[9]  = "";
char prevDate[14] = "";
char prevIP[16]   = "";
char prevSSID[33] = "";

// ── Состояние дисплея (управляется через API) ────────────────────
bool displayOn  = true;
int  brightness = 200;   // 0-255

// ══════════════════════════════════════════════════════════════════
//  ЦВЕТА И ГЕОМЕТРИЯ
// ══════════════════════════════════════════════════════════════════
static const uint32_t C_BG     = 0xFFFFFF;
static const uint32_t C_CLOCK  = 0x111111;
static const uint32_t C_BAR_BG = 0x2B2B3A;
static const uint32_t C_IP     = 0x44FF88;
static const uint32_t C_DATE   = 0xBBBBCC;
static const uint32_t C_SSID   = 0xFFE040;

#define SCR_W    480
#define SCR_H    320
#define CX       240
#define BAR_Y    285
#define BAR_H    (SCR_H - BAR_Y)
#define CLOCK_Y  (BAR_Y / 2)

// Масштаб шрифта: подбери sx если текст выходит за края
// Уменьши CLOCK_SX если обрезает, увеличь если есть запас
static constexpr float CLOCK_SX = 2.6f;
static constexpr float CLOCK_SY = 6.1f;

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
    lcd.fillRect(0, 0,     SCR_W, BAR_Y, C_BG);
    lcd.fillRect(0, BAR_Y, SCR_W, BAR_H, C_BAR_BG);
}

void updateDisplay() {
    if (!displayOn) return;   // экран выключен — не трогаем
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    char buf[16];

    // ── Время — без fillRect: setTextColor(fg,bg) перекрывает ────
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    if (strcmp(buf, prevTime) != 0) {
        strcpy(prevTime, buf);
        lcd.setFont(&lgfx::fonts::Orbitron_Light_32);
        lcd.setTextSize(CLOCK_SX, CLOCK_SY);
        lcd.setTextColor(C_CLOCK, C_BG);
        lcd.setTextDatum(lgfx::MC_DATUM);
        lcd.drawString(buf, CX, CLOCK_Y);
        lcd.setTextSize(1.0f);
    }

    // ── Нижняя полоска: дата | IP | SSID ────────────────────────
    String ip   = WiFi.localIP().toString();
    String ssid = WiFi.SSID();

    const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                        "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(buf, sizeof(buf), "%02d %s %04d",
             ti.tm_mday, mo[ti.tm_mon], ti.tm_year + 1900);

    bool barChanged = strcmp(buf,          prevDate) != 0
                   || strcmp(ip.c_str(),   prevIP)   != 0
                   || strcmp(ssid.c_str(), prevSSID)  != 0;

    if (barChanged) {
        strncpy(prevDate, buf,          sizeof(prevDate)  - 1);
        strncpy(prevIP,   ip.c_str(),   sizeof(prevIP)    - 1);
        strncpy(prevSSID, ssid.c_str(), sizeof(prevSSID)  - 1);

        lcd.fillRect(0, BAR_Y, SCR_W, BAR_H, C_BAR_BG);
        lcd.setFont(&fonts::Font2);
        int cy = BAR_Y + BAR_H / 2;

        lcd.setTextColor(C_DATE,  C_BAR_BG); lcd.setTextDatum(lgfx::ML_DATUM);
        lcd.drawString(buf,  12, cy);

        lcd.setTextColor(C_IP,    C_BAR_BG); lcd.setTextDatum(lgfx::MC_DATUM);
        lcd.drawString(ip,   CX, cy);

        lcd.setTextColor(C_SSID,  C_BAR_BG); lcd.setTextDatum(lgfx::MR_DATUM);
        lcd.drawString(ssid, SCR_W - 12, cy);
    }
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — главная страница (статичный HTML из webpage.h)
// ══════════════════════════════════════════════════════════════════
void handleRoot() {
    server.send_P(200, "text/html", WEBPAGE);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/stats  (JSON, опрашивается страницей раз в секунду)
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
    json += "\"time\":\""         + String(timeBuf)                  + "\",";
    json += "\"date\":\""         + String(dateBuf)                  + "\",";
    json += "\"ip\":\""           + WiFi.localIP().toString()        + "\",";
    json += "\"ssid\":\""         + WiFi.SSID()                     + "\",";
    json += "\"rssi\":"           + String(WiFi.RSSI())              + ",";
    json += "\"uptime\":\""       + uptimeStr()                      + "\",";
    json += "\"temp\":"           + String(temp, 1)                  + ",";
    json += "\"chip\":\""         + String(ESP.getChipModel())       + "\",";
    json += "\"chip_rev\":"       + String(ESP.getChipRevision())    + ",";
    json += "\"cpu_mhz\":"        + String(ESP.getCpuFreqMHz())      + ",";
    json += "\"heap_free\":"      + String(heapFree)                 + ",";
    json += "\"heap_total\":"     + String(heapTotal)                + ",";
    json += "\"heap_min_free\":"  + String(heapMin)                  + ",";
    json += "\"reset_reason\":\"" + String(resetReasonStr())         + "\",";
    json += "\"display_on\":"     + String(displayOn ? "true":"false")+ ",";
    json += "\"brightness\":"     + String(brightness * 100 / 255);
    json += "}";

    server.send(200, "application/json", json);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/power?on=1|0
// ══════════════════════════════════════════════════════════════════
void handlePower() {
    if (!server.hasArg("on")) {
        server.send(400, "application/json", "{\"error\":\"missing ?on=1|0\"}");
        return;
    }
    displayOn = (server.arg("on") != "0");
    lcd.setBrightness(displayOn ? brightness : 0);

    // Сбрасываем буфер времени чтобы при включении сразу перерисовало
    if (displayOn) {
        prevTime[0] = '\0';
        drawLayout();
    }

    Serial.printf("[API] power → %s\n", displayOn ? "ON" : "OFF");
    server.send(200, "application/json",
        String("{\"display_on\":") + (displayOn ? "true" : "false") + "}");
}

// ══════════════════════════════════════════════════════════════════
//  HTTP — /api/brightness?value=0..100
// ══════════════════════════════════════════════════════════════════
void handleBrightness() {
    if (!server.hasArg("value")) {
        server.send(400, "application/json", "{\"error\":\"missing ?value=0..100\"}");
        return;
    }
    int pct    = constrain(server.arg("value").toInt(), 0, 100);
    brightness = pct * 255 / 100;
    if (displayOn) lcd.setBrightness(brightness);

    Serial.printf("[API] brightness → %d%% (%d/255)\n", pct, brightness);
    server.send(200, "application/json",
        String("{\"brightness\":") + pct + "}");
}

// ══════════════════════════════════════════════════════════════════
//  SERIAL MONITOR — краткая сводка каждые 10 секунд
// ══════════════════════════════════════════════════════════════════
void serialReport() {
    struct tm ti;
    bool ntpOk = getLocalTime(&ti);

    Serial.println("─────────────────────────────────");
    if (ntpOk) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d  %02d.%02d.%04d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec,
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        Serial.printf("  Time    : %s\n", buf);
    } else {
        Serial.println("  Time    : NTP not synced");
    }
    Serial.printf("  Uptime  : %s\n",    uptimeStr().c_str());
    Serial.printf("  WiFi    : %s  %s  %d dBm\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    Serial.printf("  Temp    : %.1f °C\n",   temperatureRead());
    Serial.printf("  Heap    : %u free / %u total  (min %u)\n",
                  ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap());
    Serial.printf("  Reset   : %s\n",    resetReasonStr());
    Serial.println("─────────────────────────────────");
}

// ══════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n═══ ESP32-S3 Clock starting ═══");

    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(200);
    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.setTextDatum(lgfx::MC_DATUM);
    lcd.drawString("Connecting...", CX, SCR_H / 2);
    Serial.println("Display : OK");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000)
        delay(300);

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
        Serial.println("WiFi    : timeout — no network");
    }

    server.on("/",               HTTP_GET, handleRoot);
    server.on("/api/stats",      HTTP_GET, handleStats);
    server.on("/api/power",      HTTP_GET, handlePower);
    server.on("/api/brightness", HTTP_GET, handleBrightness);
    server.begin();
    Serial.printf("HTTP    : started  http://%s/\n",
                  WiFi.localIP().toString().c_str());

    drawLayout();
    Serial.println("═══════════════════════════════════");
}

// ══════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();

    uint32_t now = millis();

    // Обновление дисплея — каждую секунду
    static uint32_t lastDisplay = 0;
    if (now - lastDisplay >= 1000) {
        lastDisplay = now;
        updateDisplay();
    }

    // Serial-отчёт — каждые SERIAL_INTERVAL_MS
    static uint32_t lastSerial = 0;
    if (now - lastSerial >= SERIAL_INTERVAL_MS) {
        lastSerial = now;
        serialReport();
    }
}
