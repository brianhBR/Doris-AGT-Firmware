#include "modules/neopixel_controller.h"
#include "config.h"
#include <Arduino.h>

// ============================================================================
// SK6805 RGBW CUSTOM DRIVER (MbedOS-safe: saves/restores PRIMASK)
// Replaces Adafruit NeoPixel library which corrupts RTOS state on Apollo3.
// SK6805SIDE-FRGBW uses 4 bytes per pixel in GRBW order, 800kHz protocol.
// ============================================================================
#define BYTES_PER_LED 4  // GRBW

#define NOP1  __asm volatile("nop")
#define NOP2  NOP1; NOP1
#define NOP4  NOP2; NOP2
#define NOP5  NOP4; NOP1
#define NOP8  NOP4; NOP4
#define NOP10 NOP8; NOP2

static uint8_t ledBuffer[NEOPIXEL_COUNT * BYTES_PER_LED];
static uint8_t brightness = NEOPIXEL_BRIGHTNESS;
static uint16_t animationStep = 0;
static unsigned long lastAnimationUpdate = 0;
static LEDState currentState = LED_STATE_BOOT;

static void ws_setPixelRaw(uint16_t n, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (n >= NEOPIXEL_COUNT) return;
    uint16_t offset = n * BYTES_PER_LED;
    ledBuffer[offset]     = ((uint16_t)g * brightness) >> 8;
    ledBuffer[offset + 1] = ((uint16_t)r * brightness) >> 8;
    ledBuffer[offset + 2] = ((uint16_t)b * brightness) >> 8;
    ledBuffer[offset + 3] = ((uint16_t)w * brightness) >> 8;
}

static void ws_show() {
    uint8_t* ptr = ledBuffer;
    uint16_t numBytes = NEOPIXEL_COUNT * BYTES_PER_LED;

    uint32_t savedPrimask = __get_PRIMASK();
    __disable_irq();

    while (numBytes--) {
        uint8_t b = *ptr++;
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            if (b & mask) {
                am_hal_gpio_output_set(NEOPIXEL_PIN);
                NOP10; NOP10; NOP8;
                am_hal_gpio_output_clear(NEOPIXEL_PIN);
                NOP5;
            } else {
                am_hal_gpio_output_set(NEOPIXEL_PIN);
                NOP10;
                am_hal_gpio_output_clear(NEOPIXEL_PIN);
                NOP10; NOP10; NOP5;
            }
        }
    }

    __set_PRIMASK(savedPrimask);
    delayMicroseconds(80);
}

// ============================================================================
// COLOR HELPERS
// ============================================================================
static void getColorRGB(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;
}

// HSV to RGB (hue 0-65535, sat 0-255, val 0-255)
static void hsvToRGB(uint16_t hue, uint8_t sat, uint8_t val, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region = hue / 10923;  // 65536/6
    uint16_t remainder = (hue - (region * 10923)) * 6;

    uint8_t p = ((uint16_t)val * (255 - sat)) >> 8;
    uint8_t q = ((uint16_t)val * (255 - (((uint16_t)sat * remainder) >> 16))) >> 8;
    uint8_t t = ((uint16_t)val * (255 - (((uint16_t)sat * (65535 - remainder)) >> 16))) >> 8;

    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================
void NeoPixelController_init() {
    pinMode(NEOPIXEL_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_PIN, LOW);
    memset(ledBuffer, 0, sizeof(ledBuffer));
    ws_show();
    Serial.println(F("NeoPixel: SK6805 RGBW custom driver initialized"));
}

void NeoPixelController_setColor(uint32_t color) {
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        ws_setPixelRaw(i, r, g, b, 0);
    }
    ws_show();
}

void NeoPixelController_setPixel(uint16_t pixel, uint32_t color) {
    if (pixel >= NEOPIXEL_COUNT) return;
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    ws_setPixelRaw(pixel, r, g, b, 0);
    ws_show();
}

void NeoPixelController_clear() {
    memset(ledBuffer, 0, sizeof(ledBuffer));
    ws_show();
}

void NeoPixelController_setBrightness(uint8_t b) {
    brightness = b;
}

void NeoPixelController_pulse(uint32_t color, uint16_t speed) {
    float bright = (sin(animationStep * 0.1f) + 1.0f) / 2.0f;
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    uint8_t pr = (uint8_t)(r * bright);
    uint8_t pg = (uint8_t)(g * bright);
    uint8_t pb = (uint8_t)(b * bright);

    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        ws_setPixelRaw(i, pr, pg, pb, 0);
    }
    ws_show();
    animationStep++;
}

void NeoPixelController_chase(uint32_t color, uint16_t speed) {
    memset(ledBuffer, 0, sizeof(ledBuffer));

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    for (int i = 0; i < 5; i++) {
        int pos = (animationStep + i) % NEOPIXEL_COUNT;
        float fade = 1.0f - (i * 0.2f);
        ws_setPixelRaw(pos, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade), 0);
    }

    ws_show();
    animationStep++;
    if (animationStep >= NEOPIXEL_COUNT) animationStep = 0;
}

void NeoPixelController_rainbow(uint16_t speed) {
    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        uint16_t hue = ((i * 65536L / NEOPIXEL_COUNT) + (animationStep * 256)) & 0xFFFF;
        uint8_t r, g, b;
        hsvToRGB(hue, 255, 255, &r, &g, &b);
        ws_setPixelRaw(i, r, g, b, 0);
    }
    ws_show();
    animationStep++;
}

void NeoPixelController_strobe(uint32_t color) {
    static bool on = false;
    on = !on;
    if (on) {
        uint8_t r, g, b;
        getColorRGB(color, &r, &g, &b);
        // Use white channel for white strobe (brighter + true white on RGBW)
        uint8_t w = (r == 0xFF && g == 0xFF && b == 0xFF) ? 0xFF : 0;
        for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
            ws_setPixelRaw(i, r, g, b, w);
        }
    } else {
        memset(ledBuffer, 0, sizeof(ledBuffer));
    }
    ws_show();
}

void NeoPixelController_progressBar(uint8_t percent, uint32_t color) {
    if (percent > 100) percent = 100;
    memset(ledBuffer, 0, sizeof(ledBuffer));

    uint16_t numLit = (NEOPIXEL_COUNT * percent) / 100;
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    for (uint16_t i = 0; i < numLit; i++) {
        ws_setPixelRaw(i, r, g, b, 0);
    }
    ws_show();
}

void NeoPixelController_update(int systemState) {
    currentState = (LEDState)systemState;
    unsigned long now = millis();

    switch (currentState) {
        case LED_STATE_BOOT:
            if (now - lastAnimationUpdate > 50) {
                NeoPixelController_rainbow(50);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_RECOVERY_STROBE:
            if (now - lastAnimationUpdate > 100) {
                NeoPixelController_strobe(0xFFFFFF);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_GPS_SEARCH:
            if (now - lastAnimationUpdate > 20) {
                NeoPixelController_pulse(COLOR_GPS_SEARCH, 20);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_GPS_FIX:
            if (now - lastAnimationUpdate > 50) {
                NeoPixelController_pulse(COLOR_GPS_FIX, 50);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_IRIDIUM_TX:
            if (now - lastAnimationUpdate > 30) {
                NeoPixelController_chase(COLOR_IRIDIUM_TX, 30);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_PRE_MISSION:
        case LED_STATE_SELF_TEST:
        case LED_STATE_MISSION:
            if (now - lastAnimationUpdate > 50) {
                NeoPixelController_pulse(COLOR_STANDBY, 50);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_ERROR:
            if (now - lastAnimationUpdate > 250) {
                if (animationStep % 2 == 0) {
                    NeoPixelController_setColor(COLOR_ERROR);
                } else {
                    NeoPixelController_clear();
                }
                animationStep++;
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_LOW_BATTERY:
            if (now - lastAnimationUpdate > 30) {
                NeoPixelController_pulse(COLOR_LOW_BATTERY, 30);
                lastAnimationUpdate = now;
            }
            break;

        case LED_STATE_STANDBY:
            NeoPixelController_setColor(COLOR_STANDBY);
            break;

        default:
            NeoPixelController_clear();
            break;
    }
}
