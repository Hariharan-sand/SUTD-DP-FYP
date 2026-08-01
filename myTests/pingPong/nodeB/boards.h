#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "utilities.h"
#include <WiFi.h>

#ifdef HAS_SDCARD
#include <SD.h>
#include <FS.h>
#endif
SPIClass SDSPI(HSPI);

#include "Adafruit_GFX.h"
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h> // 2.13" b/w form DKE GROUP

#include GxEPD_BitmapExamples
GxIO_Class io(SDSPI, EDP_CS_PIN, EDP_DC_PIN, EDP_RSET_PIN);
GxEPD_Class display(io, EDP_RSET_PIN, EDP_BUSY_PIN);

void EPD_init(void)
{
    display.init();
    display.setTextColor(GxEPD_BLACK);
    delay(10);
    display.setRotation(2);
    delay(10);
    display.fillScreen(GxEPD_WHITE);
    display.update();
}

void initBoard()
{
    Serial.begin(115200);
    Serial.println("initBoard");
    pinMode(RADIO_POW_PIN, OUTPUT);
    digitalWrite(RADIO_POW_PIN, LED_ON);

    SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);

#ifdef BOARD_LED
    pinMode(BOARD_LED, OUTPUT);
    digitalWrite(BOARD_LED, LED_ON);
#endif

    pinMode(SDCARD_MISO, INPUT_PULLUP);
    SDSPI.begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);

    EPD_init();
}
