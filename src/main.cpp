#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LovyanGFX.hpp>

// ============================================================
// WIFI / TIME
// ============================================================

#define WIFI_SSID   "network"
#define WIFI_PASS   "password"

#define TZ_STRING   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER  "pool.ntp.org"

// ============================================================
// DISPLAY CONFIG
// ============================================================

class LGFX : public lgfx::LGFX_Device {

    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;

public:

    LGFX()
    {
        // SPI BUS
        {
            auto cfg = _bus.config();

            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;

            cfg.freq_write  = 40000000;
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

        // PANEL
        {
            auto cfg = _panel.config();

            cfg.pin_cs           = 10;
            cfg.pin_rst          = 8;
            cfg.pin_busy         = -1;

            cfg.panel_width      = 320;
            cfg.panel_height     = 480;

            cfg.memory_width     = 320;
            cfg.memory_height    = 480;

            cfg.offset_x         = 0;
            cfg.offset_y         = 0;

            cfg.readable         = true;

            cfg.invert           = false;
            cfg.rgb_order        = false;

            cfg.dlen_16bit       = false;

            cfg.bus_shared       = true;

            _panel.config(cfg);
        }

        // BACKLIGHT
        {
            auto cfg = _light.config();

            cfg.pin_bl      = 3;
            cfg.invert      = false;

            cfg.freq        = 5000;

            cfg.pwm_channel = 0;

            _light.config(cfg);

            _panel.setLight(&_light);
        }

        // TOUCH
        {
            auto cfg = _touch.config();

            cfg.x_min       = 0;
            cfg.x_max       = 319;

            cfg.y_min       = 0;
            cfg.y_max       = 479;

            cfg.pin_int     = 2;

            cfg.bus_shared  = true;

            cfg.spi_host    = SPI2_HOST;

            cfg.freq        = 2500000;

            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = 13;

            cfg.pin_cs      = 4;

            _touch.config(cfg);

            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};

static LGFX lcd;

// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);

// ============================================================
// COLORS
// ============================================================

static constexpr uint16_t C_BG      = 0xFFFF; // WHITE
static constexpr uint16_t C_TEXT    = 0x0000; // BLACK
static constexpr uint16_t C_BAR_BG  = 0x0000; // BLACK
static constexpr uint16_t C_BAR_TX  = 0xFFFF; // WHITE

// ============================================================
// SCREEN
// ============================================================

#define SCR_W 480
#define SCR_H 320

#define BAR_H 40
#define BAR_Y (SCR_H - BAR_H)

// ============================================================
// GLOBALS
// ============================================================

WebServer web(80);

char prevTime[16] = "";

// ============================================================
// STATIC UI
// ============================================================

void drawLayout()
{
    lcd.fillScreen(C_BG);

    lcd.fillRect(
        0,
        BAR_Y,
        SCR_W,
        BAR_H,
        C_BAR_BG
    );
}

// ============================================================
// OCR CLOCK
// ============================================================

void drawClock(const char* newTime)
{
    lcd.setFont(&fonts::Font7);

    //
    // Реальный размер для 480x320
    //
    lcd.setTextSize(2);

    lcd.setTextDatum(lgfx::MC_DATUM);

    //
    // Стираем старое время
    //
    lcd.setTextColor(C_BG, C_BG);

    lcd.drawString(
        prevTime,
        SCR_W / 2,
        110
    );

    //
    // Рисуем новое
    //
    lcd.setTextColor(C_TEXT, C_BG);

    lcd.drawString(
        newTime,
        SCR_W / 2,
        110
    );
}

// ============================================================
// STATUS BAR
// ============================================================

void drawBar()
{
    struct tm ti;

    if (!getLocalTime(&ti)) return;

    char dateBuf[32];

    const char *mo[] = {
        "Jan","Feb","Mar","Apr",
        "May","Jun","Jul","Aug",
        "Sep","Oct","Nov","Dec"
    };

    snprintf(
        dateBuf,
        sizeof(dateBuf),
        "%02d %s %04d",
        ti.tm_mday,
        mo[ti.tm_mon],
        ti.tm_year + 1900
    );

    String ip = WiFi.localIP().toString();

    lcd.fillRect(
        0,
        BAR_Y,
        SCR_W,
        BAR_H,
        C_BAR_BG
    );

    lcd.setFont(&fonts::Font2);

    lcd.setTextSize(1);

    lcd.setTextColor(C_BAR_TX, C_BAR_BG);

    int y = BAR_Y + BAR_H / 2;

    lcd.setTextDatum(lgfx::ML_DATUM);
    lcd.drawString(dateBuf, 8, y);

    lcd.setTextDatum(lgfx::MC_DATUM);
    lcd.drawString(ip, SCR_W / 2, y);

    lcd.setTextDatum(lgfx::MR_DATUM);
    lcd.drawString(WiFi.SSID(), SCR_W - 8, y);
}

// ============================================================
// UPDATE DISPLAY
// ============================================================

void updateDisplay()
{
    struct tm ti;

    if (!getLocalTime(&ti)) return;

    char timeBuf[16];

    //
    // OCR SAFE FORMAT
    // Вместо :
    // используем -
    //
    snprintf(
        timeBuf,
        sizeof(timeBuf),
        "%02d-%02d-%02d",
        ti.tm_hour,
        ti.tm_min,
        ti.tm_sec
    );

    if (strcmp(timeBuf, prevTime) != 0)
    {
        drawClock(timeBuf);

        strcpy(prevTime, timeBuf);

        drawBar();
    }
}

// ============================================================
// WEB PAGE
// ============================================================

void handleRoot()
{
    struct tm ti;

    char timeBuf[16] = "-- -- --";

    if (getLocalTime(&ti))
    {
        snprintf(
            timeBuf,
            sizeof(timeBuf),
            "%02d-%02d-%02d",
            ti.tm_hour,
            ti.tm_min,
            ti.tm_sec
        );
    }

    String html =
R"HTML(
<!DOCTYPE html>
<html>
<head>

<meta charset="utf-8">
<meta http-equiv="refresh" content="1">

<style>

body{
    background:#ffffff;
    color:#000000;
    font-family:monospace;

    display:flex;
    justify-content:center;
    align-items:center;

    width:100vw;
    height:100vh;

    margin:0;
}

.clock{
    font-size:120px;
    font-weight:bold;
}

</style>

</head>

<body>

<div class="clock">
)HTML"
+ String(timeBuf) +
R"HTML(
</div>

</body>
</html>
)HTML";

    server.send(
        200,
        "text/html",
        html
    );
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    lcd.init();

    lcd.setRotation(1);

    lcd.setBrightness(255);

    drawLayout();

    //
    // CONNECT SCREEN
    //
    lcd.setFont(&fonts::Font4);

    lcd.setTextColor(C_TEXT, C_BG);

    lcd.setTextDatum(lgfx::MC_DATUM);

    lcd.drawString(
        "CONNECTING...",
        SCR_W / 2,
        SCR_H / 2
    );

    //
    // WIFI
    //
    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );

    uint32_t t0 = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - t0 < 15000
    )
    {
        delay(250);
    }

    //
    // NTP
    //
    if (WiFi.status() == WL_CONNECTED)
    {
        configTzTime(
            TZ_STRING,
            NTP_SERVER
        );

        struct tm ti;

        t0 = millis();

        while (
            !getLocalTime(&ti) &&
            millis() - t0 < 5000
        )
        {
            delay(100);
        }
    }

    //
    // WEB
    //
    server.on("/", handleRoot);

    server.begin();

    drawLayout();

    updateDisplay();
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    server.handleClient();

    static uint32_t prev = 0;

    if (millis() - prev >= 1000)
    {
        prev = millis();

        updateDisplay();
    }
}
