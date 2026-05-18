# clock_TFT_LCD_esp32

# Description
Setup:
  - esp32-s3-N16R8
  - 3.5" TFT SPI
  - microcontroller contacts:: 
  - bottom row from left to right: 3V3, 3V3, RST, 4, 5, 6, 15, 16, 17, 18, 8, 3, 46, 9, 10, 11, 12, 13, 14, 5V, GND
  - top row from left to right: GND, TX, RX, 1, 2, 42, 41, 40, 39, 38, 37, 36, 35, 0, 45, 48, 47, 21, 20, 19, GND, GNDD
  - esp32-s3 is connected to the display by the following pins: 
    - bottom row first pin 3V3 + VCC
    - top row first pin GND + GND of the screen
    - bottom row pin 10 + CS
    - bottom row pin 8 + RESET
    - bottom row pin 9 + DC\RSy
    - bottom row pin 12 + SCK+T_CLK
    - bottom row pin 11 + SDI(MOSI)+T_DIN
    - bottom row of pin 3 + LED
    - bottom row of pins 13 + SDO(MISO)+T_DO
    - bottom row pin 4 + T_CS
    - top row pin 2 + T_IRQ
    - SD card pin (SD_CS, SD_MOSI, SD_MISO, SD_SCK) has not been connected yet.
    