#include "modules/neopixel_controller.h"
#include "config.h"
#include <Arduino.h>

static Adafruit_NeoPixel* pixelsPtr = nullptr;
static uint16_t animationStep = 0;
static unsigned long lastAnimationUpdate = 0;
static LEDState currentLEDState = LED_STATE_BOOT;

// Helper function to create color
uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
    if (pixelsPtr != nullptr) {
        return pixelsPtr->Color(r, g, b);
    }
    return 0;
}

// Helper function to extract RGB from color
void getColorRGB(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;
}

void NeoPixelController_init(Adafruit_NeoPixel* strip) {
    pixelsPtr = strip;

    if (pixelsPtr == nullptr) {
        return;
    }

    pixelsPtr->begin();
    pixelsPtr->setBrightness(NEOPIXEL_BRIGHTNESS);
    pixelsPtr->clear();
    pixelsPtr->show();

    Serial.println(F("NeoPixel: Controller initialized"));
}

void NeoPixelController_update(int systemState) {
    if (pixelsPtr == nullptr) {
        return;
    }

    currentLEDState = (LEDState)systemState;
    unsigned long currentMillis = millis();

    // Different animations based on system state
    switch (currentLEDState) {
        case LED_STATE_BOOT:
            // Rainbow animation during boot
            if (currentMillis - lastAnimationUpdate > 50) {
                NeoPixelController_rainbow(50);
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_GPS_SEARCH:
            // Pulsing yellow during GPS search
            if (currentMillis - lastAnimationUpdate > 20) {
                NeoPixelController_pulse(COLOR_GPS_SEARCH, 20);
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_GPS_FIX:
            // Solid green with slow pulse
            if (currentMillis - lastAnimationUpdate > 50) {
                NeoPixelController_pulse(COLOR_GPS_FIX, 50);
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_IRIDIUM_TX:
            // Chase pattern during Iridium transmission
            if (currentMillis - lastAnimationUpdate > 30) {
                NeoPixelController_chase(COLOR_IRIDIUM_TX, 30);
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_ERROR:
            // Fast red blink
            if (currentMillis - lastAnimationUpdate > 250) {
                if (animationStep % 2 == 0) {
                    NeoPixelController_setColor(COLOR_ERROR);
                } else {
                    NeoPixelController_clear();
                }
                animationStep++;
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_LOW_BATTERY:
            // Orange warning pulse
            if (currentMillis - lastAnimationUpdate > 30) {
                NeoPixelController_pulse(COLOR_LOW_BATTERY, 30);
                lastAnimationUpdate = currentMillis;
            }
            break;

        case LED_STATE_STANDBY:
            // Dim cyan standby
            NeoPixelController_setColor(COLOR_STANDBY);
            break;

        default:
            NeoPixelController_clear();
            break;
    }
}

void NeoPixelController_setColor(uint32_t color) {
    if (pixelsPtr == nullptr) {
        return;
    }

    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        pixelsPtr->setPixelColor(i, color);
    }
    pixelsPtr->show();
}

void NeoPixelController_setPixel(uint16_t pixel, uint32_t color) {
    if (pixelsPtr == nullptr || pixel >= NEOPIXEL_COUNT) {
        return;
    }

    pixelsPtr->setPixelColor(pixel, color);
    pixelsPtr->show();
}

void NeoPixelController_clear() {
    if (pixelsPtr == nullptr) {
        return;
    }

    pixelsPtr->clear();
    pixelsPtr->show();
}

void NeoPixelController_setBrightness(uint8_t brightness) {
    if (pixelsPtr == nullptr) {
        return;
    }

    pixelsPtr->setBrightness(brightness);
    pixelsPtr->show();
}

void NeoPixelController_pulse(uint32_t color, uint16_t speed) {
    if (pixelsPtr == nullptr) {
        return;
    }

    // Sine wave pulse
    float brightness = (sin(animationStep * 0.1) + 1.0) / 2.0;  // 0.0 to 1.0
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    uint32_t pulsedColor = Color(r * brightness, g * brightness, b * brightness);

    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        pixelsPtr->setPixelColor(i, pulsedColor);
    }
    pixelsPtr->show();

    animationStep++;
}

void NeoPixelController_chase(uint32_t color, uint16_t speed) {
    if (pixelsPtr == nullptr) {
        return;
    }

    pixelsPtr->clear();

    // Create a chase pattern with 5 LEDs
    for (int i = 0; i < 5; i++) {
        int pos = (animationStep + i) % NEOPIXEL_COUNT;
        uint8_t r, g, b;
        getColorRGB(color, &r, &g, &b);

        // Fade trail
        float fade = 1.0 - (i * 0.2);
        uint32_t fadedColor = Color(r * fade, g * fade, b * fade);
        pixelsPtr->setPixelColor(pos, fadedColor);
    }

    pixelsPtr->show();
    animationStep++;

    if (animationStep >= NEOPIXEL_COUNT) {
        animationStep = 0;
    }
}

void NeoPixelController_rainbow(uint16_t speed) {
    if (pixelsPtr == nullptr) {
        return;
    }

    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        // Calculate hue based on position and animation step
        uint16_t hue = ((i * 65536L / NEOPIXEL_COUNT) + (animationStep * 256)) & 0xFFFF;
        pixelsPtr->setPixelColor(i, pixelsPtr->gamma32(pixelsPtr->ColorHSV(hue)));
    }

    pixelsPtr->show();
    animationStep++;
}

void NeoPixelController_progressBar(uint8_t percent, uint32_t color) {
    if (pixelsPtr == nullptr) {
        return;
    }

    if (percent > 100) {
        percent = 100;
    }

    pixelsPtr->clear();

    uint16_t numLit = (NEOPIXEL_COUNT * percent) / 100;

    for (uint16_t i = 0; i < numLit; i++) {
        pixelsPtr->setPixelColor(i, color);
    }

    pixelsPtr->show();
}
