/**
 * ESP32-S3 + ILI9488 3.5"  —  Clock Display + Web Page
 *
 * Пины (нижний ряд: 3V3 3V3 RST 4 5 6 15 16 17 18 8 3 46 9 10 11 12 13 14 5V GND)
 * Пины (верхний ряд: GND TX RX 1 2 42 41 40 39 38 37 36 35 0 45 48 47 21 20 19 GND GND)
 *
 *   TFT_MOSI = 11,  TFT_MISO = 13,  TFT_SCLK = 12
 *   TFT_CS   = 10,  TFT_DC   = 9,   TFT_RST  = 8
 *   BL       = 3
 *   T_CS     = 4,   T_IRQ    = 2
 *
 * Web-страница: http://<ip>/
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LovyanGFX.hpp>

// ══════════════════════════════════════════════════════════════════
//  НАСТРОЙКИ
// ══════════════════════════════════════════════════════════════════
#define WIFI_SSID   "network"
#define WIFI_PASS   "password"
#define TZ_STRING   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER  "pool.ntp.org"

// ══════════════════════════════════════════════════════════════════
//  КОНФИГ ДИСПЛЕЯ
//  Весь конфиг здесь — не нужны никакие build_flags для дисплея.
//  LovyanGFX нативно поддерживает IDF 5.x / Arduino-ESP32 3.x.
// ══════════════════════════════════════════════════════════════════
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;

public:
    LGFX() {
        // SPI шина
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
        // Панель ILI9488
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
            cfg.bus_shared    = true;   // шина shared с тачем
            _panel.config(cfg);
        }
        // Подсветка (GPIO3, PWM канал 0)
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = 3;
            cfg.invert      = false;
            cfg.freq        = 5000;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        // Touch XPT2046 (T_CS = 4, T_IRQ = 2)
        {
            auto cfg         = _touch.config();
            cfg.x_min        = 0;
            cfg.x_max        = 319;
            cfg.y_min        = 0;
            cfg.y_max        = 479;
            cfg.pin_int      = 2;
            cfg.bus_shared   = true;
            cfg.spi_host     = SPI2_HOST;
            cfg.freq         = 2500000;
            cfg.pin_sclk     = 12;
            cfg.pin_mosi     = 11;
            cfg.pin_miso     = 13;
            cfg.pin_cs       = 4;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

static LGFX lcd;

// ══════════════════════════════════════════════════════════════════
//  ГЛОБАЛЬНЫЕ
// ══════════════════════════════════════════════════════════════════
WebServer server(80);

// Предыдущие значения — перерисовываем только при изменении
char prevTime[9]  = "";
char prevDate[14] = "";
char prevIP[16]   = "";
char prevSSID[33] = "";

// ══════════════════════════════════════════════════════════════════
//  КОНСТАНТЫ ЦВЕТОВ  (RGB565)
// ══════════════════════════════════════════════════════════════════
static const uint32_t C_BG      = 0xFFFFFF;   // белый фон
static const uint32_t C_CLOCK   = 0x111111;   // почти чёрные цифры
static const uint32_t C_BAR_BG  = 0x2B2B3A;   // тёмная полоска внизу
static const uint32_t C_IP      = 0x44FF88;   // зелёный IP
static const uint32_t C_DATE    = 0xBBBBCC;   // серая дата
static const uint32_t C_SSID    = 0xFFE040;   // жёлтый SSID

// Экран в landscape: 480 x 320
#define SCR_W    480
#define SCR_H    320
#define CX       240              // центр по X
#define BAR_Y    285              // Y начала нижней полоски
#define BAR_H    (SCR_H - BAR_Y) // = 35px
#define CLOCK_Y  (BAR_Y / 2)     // вертикальный центр часов = 142

// ══════════════════════════════════════════════════════════════════
//  ОТРИСОВКА ЭКРАНА
// ══════════════════════════════════════════════════════════════════
//
//  ┌─────────────────────────────────────────────────────────────┐
//  │                                                             │
//  │                      HH:MM:SS         белый фон, ~90%       │
//  │                                                             │
//  ├─────────────────────────────────────────────────────────────┤ Y=285
//  │  17 May 2026          192.168.1.100          SkyNet         │ тёмная полоска 35px
//  └─────────────────────────────────────────────────────────────┘

void drawLayout() {
    lcd.fillRect(0, 0,     SCR_W, BAR_Y, C_BG);
    lcd.fillRect(0, BAR_Y, SCR_W, BAR_H, C_BAR_BG);
}

void updateDisplay() {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    char buf[16];

    // ── Время ────────────────────────────────────────────────────
    //
    // FreeSansBold24pt7b — чистый геометрический sans-serif,
    // хорошо читается как OCR/цифровой дисплей.
    //
    // setTextSize(sx, sy) — независимый масштаб по X и Y.
    // Подбор: FreeSansBold24pt7b "HH:MM:SS" при size(1) ≈ 175×33px
    //   sy = 285 / 33  ≈ 8.6  → заполняет высоту белой зоны
    //   sx = 460 / 175 ≈ 2.6  → вписывается в 480px с небольшим отступом
    // Если всё равно обрезает → уменьши CLOCK_SX.
    // Если слишком узко      → увеличь CLOCK_SX.
    //
    static constexpr float CLOCK_SX = 2.6f;
    static constexpr float CLOCK_SY = 6.1f;

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    if (strcmp(buf, prevTime) != 0) {
        strcpy(prevTime, buf);
        lcd.setFont(&fonts::FreeSansBold24pt7b);
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

    bool barChanged = strcmp(buf,          prevDate) != 0 ||
                      strcmp(ip.c_str(),   prevIP)   != 0 ||
                      strcmp(ssid.c_str(), prevSSID)  != 0;

    if (barChanged) {
        strncpy(prevDate, buf,          sizeof(prevDate)  - 1);
        strncpy(prevIP,   ip.c_str(),   sizeof(prevIP)    - 1);
        strncpy(prevSSID, ssid.c_str(), sizeof(prevSSID)  - 1);

        lcd.fillRect(0, BAR_Y, SCR_W, BAR_H, C_BAR_BG);
        lcd.setFont(&fonts::Font2);
        int barMid = BAR_Y + BAR_H / 2;

        lcd.setTextColor(C_DATE, C_BAR_BG);
        lcd.setTextDatum(lgfx::ML_DATUM);
        lcd.drawString(buf, 12, barMid);

        lcd.setTextColor(C_IP, C_BAR_BG);
        lcd.setTextDatum(lgfx::MC_DATUM);
        lcd.drawString(ip, CX, barMid);

        lcd.setTextColor(C_SSID, C_BAR_BG);
        lcd.setTextDatum(lgfx::MR_DATUM);
        lcd.drawString(ssid, SCR_W - 12, barMid);
    }
}

// ══════════════════════════════════════════════════════════════════
//  WEB-СТРАНИЦА
// ══════════════════════════════════════════════════════════════════
void handleRoot() {
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

    String ip   = WiFi.localIP().toString();
    String ssid = WiFi.SSID();
    int    rssi = WiFi.RSSI();

    String html = R"html(<!DOCTYPE html><html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="1">
<title>ESP32-S3 Clock</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: #080818;
    color: #c9d1d9;
    font-family: 'Courier New', monospace;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    padding: 24px;
  }
  .card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 16px;
    padding: 32px 40px;
    text-align: center;
    width: 100%;
    max-width: 420px;
  }
  .title {
    font-size: .75em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .15em;
    margin-bottom: 20px;
  }
  .clock {
    font-size: 4em;
    color: #00e5ff;
    letter-spacing: .08em;
    line-height: 1;
    margin-bottom: 10px;
  }
  .date {
    font-size: 1.1em;
    color: #e6edf3;
    margin-bottom: 28px;
  }
  hr { border: none; border-top: 1px solid #21262d; margin: 20px 0; }
  .row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 0;
    font-size: .85em;
  }
  .label { color: #484f58; }
  .value { color: #58a6ff; }
  .green { color: #3fb950; }
  .yellow { color: #d29922; }
  .footer {
    margin-top: 20px;
    font-size: .65em;
    color: #30363d;
  }
</style>
</head>
<body>
<div class="card">
  <div class="title">⚡ ESP32-S3 Display</div>
  <div class="clock">)html" + String(timeBuf) + R"html(</div>
  <div class="date">)html" + String(dateBuf) + R"html(</div>
  <hr>
  <div class="row">
    <span class="label">IP Address</span>
    <span class="green">)html" + ip + R"html(</span>
  </div>
  <div class="row">
    <span class="label">Network</span>
    <span class="value">)html" + ssid + R"html(</span>
  </div>
  <div class="row">
    <span class="label">Signal</span>
    <span class="yellow">)html" + String(rssi) + R"html( dBm</span>
  </div>
  <div class="row">
    <span class="label">Free heap</span>
    <span class="value">)html" + String(ESP.getFreeHeap() / 1024) + R"html( KB</span>
  </div>
  <hr>
  <div class="footer">Auto-refresh every second</div>
</div>
</body></html>)html";

    server.send(200, "text/html", html);
}

// ══════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n== ESP32-S3 Clock ==");

    // ── Дисплей ─────────────────────────────────────────────────
    lcd.init();
    lcd.setRotation(1);          // landscape: 480 × 320
    lcd.setBrightness(200);
    lcd.fillScreen(C_BG);

    // Заставка пока грузится WiFi
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.setTextDatum(lgfx::MC_DATUM);
    lcd.drawString("Connecting...", CX, SCR_H / 2);
    Serial.println("Display: OK");

    // ── WiFi ─────────────────────────────────────────────────────
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_STRING, NTP_SERVER);
        Serial.println("WiFi: " + WiFi.localIP().toString());
        // Ждём NTP (макс 5 сек)
        struct tm ti;
        t0 = millis();
        while (!getLocalTime(&ti) && millis() - t0 < 5000) delay(200);
    } else {
        Serial.println("WiFi: timeout");
    }

    // ── HTTP сервер ──────────────────────────────────────────────
    server.on("/", handleRoot);
    server.begin();
    Serial.println("HTTP: started");

    // ── Начальная отрисовка ──────────────────────────────────────
    drawLayout();
}

// ══════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();

    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate >= 1000) {
        lastUpdate = millis();
        updateDisplay();
    }
}
