/**
 * ESP32-S3 + ILI9488 Clock
 * Orbitron_Light_32 + Плавное обновление ТОЛЬКО секунд
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
#define WIFI_SSID   "SkyNet"
#define WIFI_PASS   "password"
#define TZ_STRING   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER  "pool.ntp.org"

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
        auto bus_cfg = _bus.config();
        bus_cfg.spi_host = SPI2_HOST;
        bus_cfg.pin_sclk = 12;
        bus_cfg.pin_mosi = 11;
        bus_cfg.pin_miso = 13;
        bus_cfg.pin_dc   = 9;
        _bus.config(bus_cfg);
        _panel.setBus(&_bus);

        auto panel_cfg = _panel.config();
        panel_cfg.pin_cs  = 10;
        panel_cfg.pin_rst = 8;
        panel_cfg.panel_width  = 320;
        panel_cfg.panel_height = 480;
        _panel.config(panel_cfg);

        auto light_cfg = _light.config();
        light_cfg.pin_bl = 3;
        _light.config(light_cfg);
        _panel.setLight(&_light);

        auto touch_cfg = _touch.config();
        touch_cfg.pin_cs = 4;
        touch_cfg.pin_int = 2;
        touch_cfg.bus_shared = true;
        _touch.config(touch_cfg);
        _panel.setTouch(&_touch);

        setPanel(&_panel);
    }
};

static LGFX       lcd;
static LGFX_Sprite clockSprite(&lcd);
WebServer         server(80);

bool displayOn  = true;
int  brightness = 200;

static const uint32_t C_BG     = 0xFFFFFF;
static const uint32_t C_CLOCK  = 0x111111;
static const uint32_t C_BAR_BG = 0x2B2B3A;
static const uint32_t C_IP     = 0x44FF88;
static const uint32_t C_DATE   = 0xBBBBCC;
static const uint32_t C_SSID   = 0xFFE040;

#define SCR_W    480
#define CLOCK_X  15
#define CLOCK_Y  48
#define CLOCK_W  450
#define CLOCK_H  185

#define HHMM_X   195     // позиция HH:MM
#define SS_X     325    // позиция секунд

char prevHHMM[6] = "";

// ══════════════════════════════════════════════════════════════════
//  ДИСПЛЕЙ
// ══════════════════════════════════════════════════════════════════
void drawLayout() {
    lcd.fillRect(0, 0,   SCR_W, 280, C_BG);
    lcd.fillRect(0, 280, SCR_W,  40, C_BAR_BG);
}

void updateClock() {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d-%02d", ti.tm_hour, ti.tm_min);

    if (strcmp(hhmm, prevHHMM) != 0) {
        strcpy(prevHHMM, hhmm);
        clockSprite.fillSprite(C_BG);

        clockSprite.setFont(&lgfx::fonts::Orbitron_Light_32);
        clockSprite.setTextSize(2.40f, 5.30f);
        clockSprite.setTextColor(C_CLOCK);
        clockSprite.setTextDatum(lgfx::MC_DATUM);

        clockSprite.drawString(hhmm, 148, CLOCK_H/2 + 6);
    }

    // ==================== СЕКУНДЫ ====================
    char ss[3];
    snprintf(ss, sizeof(ss), "%02d", ti.tm_sec);

    // Увеличенная область очистки
    clockSprite.fillRect(280, 15, 190, CLOCK_H - 35, C_BG);

    clockSprite.setFont(&lgfx::fonts::Orbitron_Light_32);
    clockSprite.setTextSize(2.40f, 5.30f);
    clockSprite.setTextColor(C_CLOCK);
    clockSprite.setTextDatum(lgfx::MC_DATUM);

    clockSprite.drawString("-", 285, CLOCK_H/2 + 6);   // тире
        clockSprite.drawString(ss, 370, CLOCK_H/2 + 6);   // ← секунды правее

    clockSprite.pushSprite(CLOCK_X, CLOCK_Y);
}


void updateBottomBar() {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    String ip   = WiFi.localIP().toString();
    String ssid = WiFi.SSID();

    const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char dateBuf[14];
    snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d", ti.tm_mday, mo[ti.tm_mon], ti.tm_year + 1900);

    static char prevDate[14] = "", prevIP[16] = "", prevSSID[33] = "";

    if (strcmp(dateBuf, prevDate) == 0 && 
        strcmp(ip.c_str(), prevIP) == 0 && 
        strcmp(ssid.c_str(), prevSSID) == 0) return;

    strncpy(prevDate, dateBuf, sizeof(prevDate)-1);
    strncpy(prevIP,   ip.c_str(), sizeof(prevIP)-1);
    strncpy(prevSSID, ssid.c_str(), sizeof(prevSSID)-1);

    lcd.fillRect(0, 280, SCR_W, 40, C_BAR_BG);
    lcd.setFont(&fonts::Font2);
    int cy = 300;

    lcd.setTextColor(C_DATE, C_BAR_BG); lcd.setTextDatum(lgfx::ML_DATUM); lcd.drawString(dateBuf, 12, cy);
    lcd.setTextColor(C_IP,   C_BAR_BG); lcd.setTextDatum(lgfx::MC_DATUM); lcd.drawString(ip,   240, cy);
    lcd.setTextColor(C_SSID, C_BAR_BG); lcd.setTextDatum(lgfx::MR_DATUM); lcd.drawString(ssid, SCR_W-12, cy);
}

// ══════════════════════════════════════════════════════════════════
//  HTTP
// ══════════════════════════════════════════════════════════════════
void handleRoot() { server.send_P(200, "text/html", WEBPAGE); }

void handleStats() {
    struct tm ti; getLocalTime(&ti);
    char t[9]; snprintf(t, sizeof(t), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    String json = "{\"time\":\"" + String(t) + "\",\"ip\":\"" + WiFi.localIP().toString() + 
                  "\",\"ssid\":\"" + WiFi.SSID() + "\"}";
    server.send(200, "application/json", json);
}

void handlePower() {
    if (server.hasArg("on")) {
        displayOn = (server.arg("on") != "0");
        lcd.setBrightness(displayOn ? brightness : 0);
        if (displayOn) prevHHMM[0] = '\0';
    }
    server.send(200, "application/json", "{\"success\":true}");
}

void handleBrightness() {
    if (server.hasArg("value")) {
        int p = constrain(server.arg("value").toInt(), 0, 100);
        brightness = p * 255 / 100;
        if (displayOn) lcd.setBrightness(brightness);
    }
    server.send(200, "application/json", "{\"success\":true}");
}

// ══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n═══ Orbitron Clock - Only Seconds Update ═══");

    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(200);
    lcd.fillScreen(C_BG);

    if (!clockSprite.createSprite(CLOCK_W, CLOCK_H)) {
        Serial.println("Sprite creation failed!");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_STRING, NTP_SERVER);
    }

    server.on("/",               HTTP_GET, handleRoot);
    server.on("/api/stats",      HTTP_GET, handleStats);
    server.on("/api/power",      HTTP_GET, handlePower);
    server.on("/api/brightness", HTTP_GET, handleBrightness);
    server.begin();

    drawLayout();
    prevHHMM[0] = '\0';        // принудительное обновление при старте
    Serial.println("Started!");
}

void loop() {
    server.handleClient();

    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate >= 1000) {
        lastUpdate = millis();
        if (displayOn) {
            updateClock();
            updateBottomBar();
        }
    }
}