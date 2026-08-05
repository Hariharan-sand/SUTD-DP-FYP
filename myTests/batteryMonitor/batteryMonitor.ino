#include "boards.h"

void setup()
{
    initBoard();
    
    display.setFont(&FreeMonoBold12pt7b);
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(10, 30);
    display.print("Battery Monitor");
    display.update();
    
    pinMode(BAT_ADC_PIN, INPUT);
}

void loop()
{
    // Read the battery ADC pin
    // The battery voltage is divided by 2 via a resistor divider before going to the ADC
    uint32_t adc_mv = 0;
    
    // Take an average of 10 readings for stability
    for (int i = 0; i < 10; i++) {
        adc_mv += analogReadMilliVolts(BAT_ADC_PIN);
        delay(10);
    }
    adc_mv /= 10;
    
    // Calculate actual battery voltage
    float voltage = (adc_mv * 2.0) / 1000.0;
    
    // Estimate percentage (Assuming standard LiPo 3.3V - 4.2V range)
    float percentage = (voltage - 3.3) / (4.2 - 3.3) * 100.0;
    if (percentage > 100) percentage = 100.0;
    if (percentage < 0) percentage = 0.0;
    
    display.fillScreen(GxEPD_WHITE);
    
    display.setCursor(10, 30);
    display.print("Battery Info");
    
    display.setCursor(10, 70);
    display.print("V: ");
    display.print(voltage, 2);
    display.print(" V");
    
    display.setCursor(10, 110);
    display.print("C: ");
    display.print((int)percentage);
    display.print(" %");

    display.update();
    
    // Update every 10 seconds. Note: E-paper full update takes a few seconds.
    // Avoid updating too rapidly to prevent screen damage.
    delay(10000);
}
