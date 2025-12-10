#ifndef NEOPIXEL_CONTROLLER_H
#define NEOPIXEL_CONTROLLER_H

#include <Adafruit_NeoPixel.h>

// System states for LED indication
enum LEDState {
    LED_STATE_BOOT,
    LED_STATE_EMERGENCY,
    LED_STATE_GPS_SEARCH,
    LED_STATE_GPS_FIX,
    LED_STATE_IRIDIUM_TX,
    LED_STATE_ERROR,
    LED_STATE_LOW_BATTERY,
    LED_STATE_STANDBY
};

// Initialize NeoPixel controller
void NeoPixelController_init(Adafruit_NeoPixel* strip);

// Update LED animation (call in main loop)
void NeoPixelController_update(int systemState);

// Set solid color for all LEDs
void NeoPixelController_setColor(uint32_t color);

// Set individual LED color
void NeoPixelController_setPixel(uint16_t pixel, uint32_t color);

// Clear all LEDs
void NeoPixelController_clear();

// Set brightness (0-255)
void NeoPixelController_setBrightness(uint8_t brightness);

// Custom patterns
void NeoPixelController_pulse(uint32_t color, uint16_t speed);
void NeoPixelController_chase(uint32_t color, uint16_t speed);
void NeoPixelController_rainbow(uint16_t speed);
void NeoPixelController_progressBar(uint8_t percent, uint32_t color);

#endif // NEOPIXEL_CONTROLLER_H
