#ifndef NEOPIXEL_CONTROLLER_H
#define NEOPIXEL_CONTROLLER_H

#include <Arduino.h>

enum LEDState {
    LED_STATE_BOOT,
    LED_STATE_PRE_MISSION,
    LED_STATE_SELF_TEST,
    LED_STATE_MISSION,
    LED_STATE_RECOVERY_STROBE,
    LED_STATE_GPS_SEARCH,
    LED_STATE_GPS_FIX,
    LED_STATE_IRIDIUM_TX,
    LED_STATE_ERROR,
    LED_STATE_LOW_BATTERY,
    LED_STATE_STANDBY
};

// Initialize custom SK6805 RGBW driver (no library needed)
void NeoPixelController_init();

// Update LED animation (call in main loop)
void NeoPixelController_update(int systemState);

// Set solid color for all LEDs (0xRRGGBB)
void NeoPixelController_setColor(uint32_t color);

// Set individual LED color
void NeoPixelController_setPixel(uint16_t pixel, uint32_t color);

// Clear all LEDs
void NeoPixelController_clear();

// Set brightness (0-255)
void NeoPixelController_setBrightness(uint8_t brightness);

// Animation patterns
void NeoPixelController_pulse(uint32_t color, uint16_t speed);
void NeoPixelController_chase(uint32_t color, uint16_t speed);
void NeoPixelController_rainbow(uint16_t speed);
void NeoPixelController_progressBar(uint8_t percent, uint32_t color);
void NeoPixelController_strobe(uint32_t color);

#endif // NEOPIXEL_CONTROLLER_H
