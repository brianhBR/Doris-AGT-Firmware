#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ARTEMIS GLOBAL TRACKER PIN DEFINITIONS
// ============================================================================
#define SPI_CS1              4   // D4 - RELAY_POWER_MGMT
#define GEOFENCE_PIN         10  // Input for the ZOE-M8Q's PIO14 (geofence) pin
#define BUS_VOLTAGE_PIN      13  // Bus voltage divided by 3 (Analog in)
#define IRIDIUM_SLEEP        17  // Iridium 9603N ON/OFF (sleep) pin
#define IRIDIUM_NA           18  // Input for the Iridium 9603N Network Available
#define LED_WHITE            19  // White LED (onboard)
#define IRIDIUM_PWR_EN       22  // ADM4210 ON: pull high to enable power for Iridium
#define GNSS_EN              26  // GNSS Enable: pull low to enable GNSS power
#define SUPERCAP_CHG_EN      27  // LTC3225 super capacitor charger enable
#define SUPERCAP_PGOOD       28  // LTC3225 PGOOD signal input
#define BUS_VOLTAGE_MON_EN   34  // Bus voltage monitor enable
#define SPI_CS2              35  // D35 - RELAY_TIMED_EVENT
#define IRIDIUM_RI           41  // Iridium 9603N Ring Indicator
#define GNSS_BCKP_BAT_CHG_EN 44  // GNSS backup battery charge enable

// Custom pin assignments
#define NEOPIXEL_PIN         32  // NeoPixel data pin (GPIO32/AD32)
#define RELAY_POWER_MGMT     4   // Relay 1: Power management (Navigator/Pi, Camera, Lights)
#define RELAY_TIMED_EVENT    35  // Relay 2: Drop weight release

// Blue Robotics PSM analog inputs
#define PSM_VOLTAGE_PIN      11  // GPIO11 (AD11) - PSM voltage analog output
#define PSM_CURRENT_PIN      12  // GPIO12 (AD12) - PSM current analog output

// Meshtastic Serial2 pins (using SPI header breakout pins)
#define MESHTASTIC_TX_PIN    6   // GPIO6 (MISO header, TX2 to RAK4603 RX)
#define MESHTASTIC_RX_PIN    7   // GPIO7 (MOSI header, RX2 from RAK4603 TX)

// ============================================================================
// SERIAL PORT DEFINITIONS
// ============================================================================
// Serial (USB) - Debug, Config, MAVLink to Navigator
// Serial1 - Iridium 9603N (pins 24/25: TX1/RX1)
// Serial2 - Meshtastic RAK4603 (needs to be configured)

#define MESHTASTIC_SERIAL    Serial1
#define IRIDIUM_SERIAL       Serial1
#define DEBUG_SERIAL         Serial
#define MAVLINK_SERIAL       Serial  // USB to Navigator

#define MESHTASTIC_BAUD      115200
#define IRIDIUM_BAUD         19200
#define DEBUG_BAUD           115200
#define MAVLINK_BAUD         57600

// ============================================================================
// TIMING CONFIGURATION
// ============================================================================
#define GPS_FIX_TIMEOUT_MS         180000  // 3 minutes
#define IRIDIUM_SEND_INTERVAL_MS   600000  // 10 minutes
#define MESHTASTIC_UPDATE_MS       30000   // 30 seconds
#define MAVLINK_UPDATE_MS          1000    // 1 Hz
#define PSM_UPDATE_MS              5000    // 5 seconds
#define NEOPIXEL_UPDATE_MS         100     // 10 Hz for animations

// ============================================================================
// NEOPIXEL CONFIGURATION
// ============================================================================
#define NEOPIXEL_COUNT       30
#define NEOPIXEL_BRIGHTNESS  50  // 0-255

// LED Status Colors (RGB)
#define COLOR_BOOT           0x0000FF  // Blue
#define COLOR_GPS_SEARCH     0xFFFF00  // Yellow
#define COLOR_GPS_FIX        0x00FF00  // Green
#define COLOR_IRIDIUM_TX     0xFF00FF  // Magenta
#define COLOR_ERROR          0xFF0000  // Red
#define COLOR_LOW_BATTERY    0xFF8000  // Orange
#define COLOR_STANDBY        0x001010  // Dim cyan
#define COLOR_OFF            0x000000  // Off

// ============================================================================
// BATTERY MONITORING
// ============================================================================
#define BATTERY_LOW_VOLTAGE      11.5  // Volts
#define BATTERY_CRITICAL_VOLTAGE 11.0  // Volts
#define BATTERY_FULL_VOLTAGE     14.8  // Volts (for 4S LiPo)

// ============================================================================
// RELAY CONFIGURATION
// ============================================================================
#define RELAY_ACTIVE_HIGH    true  // Set false if relay is active low

// ============================================================================
// IRIDIUM CONFIGURATION
// ============================================================================
#define IRIDIUM_SLEEP_ENABLED    true
#define MAX_IRIDIUM_RETRY        3
#define IRIDIUM_SIGNAL_TIMEOUT   180  // seconds

// ============================================================================
// GPS CONFIGURATION
// ============================================================================
#define GPS_UPDATE_RATE_HZ       1
#define GPS_MIN_SATS             4
#define GPS_DYNAMIC_MODEL        DYN_MODEL_PORTABLE  // or SEA, AIRBORNE1g, etc.

// ============================================================================
// MAVLINK CONFIGURATION
// ============================================================================
#define MAVLINK_SYSTEM_ID        1
#define MAVLINK_COMPONENT_ID     MAV_COMP_ID_GPS
#define MAVLINK_HEARTBEAT_MS     1000

// ============================================================================
// TIMED EVENT CONFIGURATION (stored in EEPROM/Flash)
// ============================================================================
struct TimedEventConfig {
    bool enabled;
    bool useAbsoluteTime;    // true = GMT time, false = delay from boot
    uint32_t triggerTime;    // Unix timestamp (GMT) or seconds from boot
    uint16_t durationMs;     // How long to activate relay (milliseconds)
};

// ============================================================================
// SYSTEM CONFIGURATION STRUCTURE
// ============================================================================
struct SystemConfig {
    // Intervals
    uint32_t iridiumInterval;
    uint32_t meshtasticInterval;
    uint32_t mavlinkInterval;

    // Features enabled
    bool enableIridium;
    bool enableMeshtastic;
    bool enableMAVLink;
    bool enablePSM;
    bool enableNeoPixels;

    // Timed events
    TimedEventConfig timedEvent;

    // Power management
    float powerSaveVoltage;  // Voltage to trigger nonessential shutdown

    // Checksum for validation
    uint32_t checksum;
};

// Default configuration
#define DEFAULT_IRIDIUM_INTERVAL     600000
#define DEFAULT_MESHTASTIC_INTERVAL  30000
#define DEFAULT_MAVLINK_INTERVAL     1000
#define DEFAULT_POWER_SAVE_VOLTAGE   11.5

#endif // CONFIG_H
