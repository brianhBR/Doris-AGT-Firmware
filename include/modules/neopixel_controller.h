#ifndef NEOPIXEL_CONTROLLER_H
#define NEOPIXEL_CONTROLLER_H

#include <Arduino.h>

// ============================================================================
// LED DISPLAY MODES
// ============================================================================
// STANDBY:   Slow spinning white   — booting / waiting for systems
// READY:     Spinning green         — GPS + MAVLink + mission all good
// ERROR:     Pulsing red            — something is wrong / not ready
// DIVING:    LEDs off               — underwater, save power
// LUA:       Lua-commanded pattern  — Lua can override during dive if needed
// RECOVERY:  Flashing white beacon  — surface recovery strobe

enum LEDMode {
    LED_MODE_STANDBY,
    LED_MODE_READY,
    LED_MODE_ERROR,
    LED_MODE_DIVING,
    LED_MODE_LUA,
    LED_MODE_RECOVERY
};

// Patterns available via Lua LED commands
enum LuaPattern {
    LUA_PATTERN_OFF     = 0,
    LUA_PATTERN_SOLID   = 1,
    LUA_PATTERN_PULSE   = 2,
    LUA_PATTERN_CHASE   = 3,
    LUA_PATTERN_STROBE  = 4,
    LUA_PATTERN_RAINBOW = 5
};

void NeoPixelController_init();
void NeoPixelController_update();

void NeoPixelController_setMode(LEDMode mode);

// Lua LED command (called from MAVLink handler)
void NeoPixelController_setLuaCommand(uint8_t pattern, uint32_t color,
                                      uint16_t speedMs, uint8_t brightness);
bool NeoPixelController_isLuaActive();

void NeoPixelController_setBrightness(uint8_t brightness);
void NeoPixelController_clear();

#endif // NEOPIXEL_CONTROLLER_H
