#include "modules/neopixel_controller.h"
#include "config.h"
#include <Arduino.h>

// ============================================================================
// WS2812-family CUSTOM DRIVER (MbedOS-safe: saves/restores PRIMASK)
// Replaces Adafruit NeoPixel library which corrupts RTOS state on Apollo3.
// Supports 3-byte GRB (WS2812B/SK6812) and 4-byte GRBW (SK6805) via config.
// ============================================================================
#define BYTES_PER_LED NEOPIXEL_BYTES_PER_LED

#define NOP1  __asm volatile("nop")
#define NOP2  NOP1; NOP1
#define NOP4  NOP2; NOP2
#define NOP5  NOP4; NOP1
#define NOP8  NOP4; NOP4
#define NOP10 NOP8; NOP2

static uint8_t ledBuffer[NEOPIXEL_COUNT * BYTES_PER_LED];
static uint8_t brightness = NEOPIXEL_BRIGHTNESS;
static unsigned long lastRefresh = 0;

static LEDMode currentMode = LED_MODE_STANDBY;

// Lua command state
static uint8_t  luaPattern     = LUA_PATTERN_OFF;
static uint32_t luaColor       = 0;
static uint16_t luaSpeedMs     = 1000;
static unsigned long luaLastCmdTime = 0;

// ============================================================================
// LOW-LEVEL DRIVER
// ============================================================================
static void ws_setPixelRaw(uint16_t n, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (n >= NEOPIXEL_COUNT) return;
    uint16_t offset = n * BYTES_PER_LED;
    // SK6812 uses GRBW order (same as WS2812B for RGB channels)
    ledBuffer[offset]     = ((uint16_t)g * brightness) >> 8;
    ledBuffer[offset + 1] = ((uint16_t)r * brightness) >> 8;
    ledBuffer[offset + 2] = ((uint16_t)b * brightness) >> 8;
#if BYTES_PER_LED == 4
    ledBuffer[offset + 3] = ((uint16_t)w * brightness) >> 8;
#endif
    (void)w;
}

static void ws_show() {
    uint8_t* ptr = ledBuffer;
    uint16_t numBytes = NEOPIXEL_COUNT * BYTES_PER_LED;

    uint32_t savedPrimask = __get_PRIMASK();
    __disable_irq();

    // Hold data line LOW for a full reset period before sending new data.
    am_hal_gpio_output_clear(NEOPIXEL_PIN);
    delayMicroseconds(80);

    // SK6812 timing at 48 MHz (1 NOP ≈ 20.8 ns):
    //   T1H 450-750 ns  → 28 NOPs ≈ 583 ns
    //   T1L 450-750 ns  → 20 NOPs + loop overhead ≈ 600 ns
    //   T0H 150-450 ns  → 10 NOPs ≈ 208 ns
    //   T0L 750-1050 ns → 35 NOPs + loop overhead ≈ 910 ns

    // Send one dummy pixel (all zero bits) to absorb first-pixel
    // signal glitches. The first physical LED latches this as black.
    for (uint8_t dummy = 0; dummy < (8 * BYTES_PER_LED); dummy++) {
        am_hal_gpio_output_set(NEOPIXEL_PIN);
        NOP10;
        am_hal_gpio_output_clear(NEOPIXEL_PIN);
        NOP10; NOP10; NOP10; NOP5;
    }

    while (numBytes--) {
        uint8_t b = *ptr++;
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            if (b & mask) {
                am_hal_gpio_output_set(NEOPIXEL_PIN);
                NOP10; NOP10; NOP8;
                am_hal_gpio_output_clear(NEOPIXEL_PIN);
                NOP10; NOP10;
            } else {
                am_hal_gpio_output_set(NEOPIXEL_PIN);
                NOP10;
                am_hal_gpio_output_clear(NEOPIXEL_PIN);
                NOP10; NOP10; NOP10; NOP5;
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

static void setAllOff() {
    memset(ledBuffer, 0, sizeof(ledBuffer));
}

// ============================================================================
// ANIMATION PATTERNS
// ============================================================================

// Spinning light: one bright head with a fading tail sweeping around the strip
static void spinPattern(uint32_t color, uint16_t stepMs) {
    setAllOff();
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    uint16_t head = (millis() / stepMs) % NEOPIXEL_COUNT;
    uint16_t tailLen = 8;
    for (uint16_t i = 0; i < tailLen; i++) {
        uint16_t pos = (head + NEOPIXEL_COUNT - i) % NEOPIXEL_COUNT;
        float fade = 1.0f - (i * (1.0f / tailLen));
        ws_setPixelRaw(pos, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade), 0);
    }
}

// Pulsing: all LEDs breathe in and out using triangle wave with
// gamma correction for perceptually smooth brightness transitions.
static void pulsePattern(uint32_t color, uint16_t periodMs, bool useWhiteLED = false) {
    uint16_t phase = millis() % (unsigned long)periodMs;
    // Triangle wave: ramp 0→255→0 over one period
    uint16_t half = periodMs / 2;
    uint8_t linear = (phase < half)
        ? (uint16_t)phase * 255 / half
        : (uint16_t)(periodMs - phase) * 255 / half;
    // Gamma 2.2 approximation for perceptually smooth fading
    uint8_t level = (uint8_t)(((uint32_t)linear * linear) >> 8);

    if (useWhiteLED) {
        for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++)
            ws_setPixelRaw(i, 0, 0, 0, level);
    } else {
        uint8_t r, g, b;
        getColorRGB(color, &r, &g, &b);
        uint8_t rr = ((uint16_t)r * level) >> 8;
        uint8_t gg = ((uint16_t)g * level) >> 8;
        uint8_t bb = ((uint16_t)b * level) >> 8;
        for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++)
            ws_setPixelRaw(i, rr, gg, bb, 0);
    }
}

// Blink: on for onMs, off for remainder of periodMs.
// fullBright bypasses the global brightness cap (for recovery beacon).
static void blinkPattern(uint16_t periodMs, uint16_t onMs, bool useWhiteLED = false,
                         uint32_t color = 0xFFFFFF, bool fullBright = false) {
    unsigned long phase = millis() % (unsigned long)periodMs;
    if (phase < onMs) {
        if (fullBright) {
            for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
                uint16_t off = i * BYTES_PER_LED;
                if (useWhiteLED) {
                    ledBuffer[off] = 0; ledBuffer[off+1] = 0;
                    ledBuffer[off+2] = 0; ledBuffer[off+3] = 255;
                } else {
                    uint8_t r, g, b;
                    getColorRGB(color, &r, &g, &b);
                    ledBuffer[off] = g; ledBuffer[off+1] = r;
                    ledBuffer[off+2] = b; ledBuffer[off+3] = 0;
                }
            }
        } else if (useWhiteLED) {
            for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++)
                ws_setPixelRaw(i, 0, 0, 0, 255);
        } else {
            uint8_t r, g, b;
            getColorRGB(color, &r, &g, &b);
            for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++)
                ws_setPixelRaw(i, r, g, b, 0);
        }
    } else {
        setAllOff();
    }
}

// Rainbow sweep
static void rainbowPattern(uint16_t periodMs) {
    uint16_t hueOffset = (millis() % (unsigned long)periodMs) * 65536UL / periodMs;
    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        uint16_t hue = (i * 65536UL / NEOPIXEL_COUNT + hueOffset) & 0xFFFF;
        uint8_t r, g, b;
        hsvToRGB(hue, 255, 255, &r, &g, &b);
        ws_setPixelRaw(i, r, g, b, 0);
    }
}

// Chase animation
static void chasePattern(uint32_t color, uint16_t stepMs) {
    setAllOff();
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    uint16_t head = (millis() / stepMs) % NEOPIXEL_COUNT;
    uint16_t tailLen = 5;
    for (uint16_t i = 0; i < tailLen; i++) {
        uint16_t pos = (head + NEOPIXEL_COUNT - i) % NEOPIXEL_COUNT;
        float fade = 1.0f - (i * (1.0f / tailLen));
        ws_setPixelRaw(pos, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade), 0);
    }
}

// Solid color on all LEDs
static void solidPattern(uint32_t color) {
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        ws_setPixelRaw(i, r, g, b, 0);
    }
}

// ============================================================================
// LUA-COMMANDED DISPLAY
// ============================================================================
static void updateLuaPattern() {
    switch (luaPattern) {
        case LUA_PATTERN_OFF:     setAllOff();                                    break;
        case LUA_PATTERN_SOLID:   solidPattern(luaColor);                         break;
        case LUA_PATTERN_PULSE:   pulsePattern(luaColor, luaSpeedMs);             break;
        case LUA_PATTERN_CHASE:   chasePattern(luaColor, luaSpeedMs / NEOPIXEL_COUNT); break;
        case LUA_PATTERN_STROBE:  blinkPattern(luaSpeedMs, luaSpeedMs / 10, false, luaColor); break;
        case LUA_PATTERN_RAINBOW: rainbowPattern(luaSpeedMs);                     break;
        default:                  setAllOff();                                    break;
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
    currentMode = LED_MODE_STANDBY;
    DebugPrintln(F("NeoPixel: SK6805 RGBW custom driver initialized"));
}

void NeoPixelController_update() {
    unsigned long now = millis();
    if (now - lastRefresh < 20) return;
    lastRefresh = now;

    switch (currentMode) {
        case LED_MODE_STANDBY:
            blinkPattern(2000, 1000, true);             // 1s on / 1s off white
            break;

        case LED_MODE_READY:
            blinkPattern(1500, 750, false, 0x00FF00);   // 0.75s on / 0.75s off green
            break;

        case LED_MODE_ERROR:
            blinkPattern(800, 400, false, 0xFF0000);    // 0.4s on / 0.4s off red
            break;

        case LED_MODE_DIVING:
            setAllOff();
            break;

        case LED_MODE_LUA:
            updateLuaPattern();
            break;

        case LED_MODE_RECOVERY:
            blinkPattern(RECOVERY_STROBE_PERIOD_MS, RECOVERY_STROBE_ON_MS, true, 0xFFFFFF, true);
            break;
    }

    // LED 0 is sacrificial: the first pixel on the data line picks up
    // glitches from ADC/GPIO activity. Force it dark so it never
    // shows a false color to the user.
    ledBuffer[0] = 0; ledBuffer[1] = 0; ledBuffer[2] = 0; ledBuffer[3] = 0;

    ws_show();
}

void NeoPixelController_setMode(LEDMode mode) {
    currentMode = mode;
}

void NeoPixelController_setLuaCommand(uint8_t pattern, uint32_t color,
                                      uint16_t speedMs, uint8_t brt) {
    luaPattern = pattern;
    luaColor   = color;
    luaSpeedMs = speedMs > 0 ? speedMs : 1000;
    if (brt > 0) brightness = brt;
    luaLastCmdTime = millis();
}

bool NeoPixelController_isLuaActive() {
    if (luaLastCmdTime == 0) return false;
    return (millis() - luaLastCmdTime) < LUA_COMMAND_TIMEOUT_MS;
}

void NeoPixelController_setBrightness(uint8_t b) {
    brightness = b;
}

void NeoPixelController_clear() {
    memset(ledBuffer, 0, sizeof(ledBuffer));
    ws_show();
}
