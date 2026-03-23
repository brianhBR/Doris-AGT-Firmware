#ifndef CONFIG_H
#define CONFIG_H

// Suppress all Serial.print debug text so ASCII doesn't corrupt MAVLink framing.
// Comment out this line to re-enable debug output for direct-serial debugging.
#define SUPPRESS_DEBUG_TEXT

#ifdef SUPPRESS_DEBUG_TEXT
  #define DebugPrint(...)   ((void)0)
  #define DebugPrintln(...) ((void)0)
#else
  #define DebugPrint(...)   Serial.print(__VA_ARGS__)
  #define DebugPrintln(...) Serial.println(__VA_ARGS__)
#endif

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

// Meshtastic uses software serial on pins 39/40 via J10 Qwiic connector
// NOTE: Apollo3 only has 2 UARTs (Serial=USB, Serial1=Iridium)
// Software serial allows Meshtastic to coexist with Iridium
#define MESHTASTIC_TX_PIN    39  // D39 on J10 to RAK4603 RX
#define MESHTASTIC_RX_PIN    40  // D40 on J10 from RAK4603 TX

// ============================================================================
// SERIAL PORT DEFINITIONS
// ============================================================================
// Serial (USB) - Debug, Config, MAVLink to Navigator
// Serial1 (UART1) - Iridium 9603N on default pins D24/D25
// SoftwareSerial - Meshtastic RAK4603 on pins D39/D40 (J10 connector)

#define IRIDIUM_SERIAL       Serial1  // UART1 on default pins D24/D25
#define DEBUG_SERIAL         Serial
#define MAVLINK_SERIAL       Serial  // USB to Navigator

#define IRIDIUM_BAUD         19200
#define MESHTASTIC_BAUD      9600    // Default GPS baud; AGT TX -> Meshtastic J10 (external GPS)
#define DEBUG_BAUD           57600
#define MAVLINK_BAUD         57600   // Shared with DEBUG on USB Serial

// ============================================================================
// TIMING CONFIGURATION
// ============================================================================
#define GPS_FIX_TIMEOUT_MS         180000  // 3 minutes
#define IRIDIUM_SEND_INTERVAL_MS   600000  // 10 minutes
#define MESHTASTIC_UPDATE_MS       1000    // 1 second (matches typical GPS cadence)
#define MAVLINK_UPDATE_MS          1000    // 1 Hz
#define PSM_UPDATE_MS              5000    // 5 seconds



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
#define COLOR_STANDBY        0x00FFFF  // Cyan (dimmed by global brightness)
#define COLOR_OFF            0x000000  // Off

// ============================================================================
// BATTERY MONITORING & FAILSAFE
// ============================================================================
#define BATTERY_LOW_VOLTAGE      11.5  // Volts
#define BATTERY_CRITICAL_VOLTAGE 11.0  // Volts (failsafe trigger)
#define BATTERY_FULL_VOLTAGE     14.8  // Volts (for 4S LiPo)
#define FAILSAFE_HEARTBEAT_TIMEOUT_MS  120000 // No MAVLink heartbeat -> failsafe (120 s)
#define PI_HEARTBEAT_TIMEOUT_MS        5000   // Pi considered disconnected if no heartbeat in 5 s
#define FAILSAFE_MAX_DEPTH_M           200.0  // Max depth (m) before failsafe
#define MISSION_DEPTH_THRESHOLD_M      5.0    // Depth > this: leave Self Test -> Mission
#define RECOVERY_DEPTH_THRESHOLD_M     1.5    // Depth < this AND GPS fix: Mission -> Recovery
#define MISSION_MIN_DURATION_MS        60000  // Min time in MISSION before RECOVERY transition (60 s)
#define HEARTBEAT_GRACE_PERIOD_MS      90000  // Ignore heartbeat timeout for this long after entering MISSION

// ============================================================================
// RELAY CONFIGURATION
// ============================================================================
// Power management relay uses NC (Normally Closed) wiring:
//   Coil OFF (pin LOW / floating) = NC closed = devices POWERED (safe default)
//   Coil ON  (pin HIGH)           = NC opens  = devices OFF
// This ensures devices stay powered during MCU resets (pin floats low).
//
// Timed event (drop weight) relay uses NO (Normally Open) wiring:
//   Coil OFF (pin LOW / floating) = NO open   = release INACTIVE (safe default)
//   Coil ON  (pin HIGH)           = NO closes = release ACTIVE
#define RELAY_COIL_ACTIVE_HIGH       true   // Both relay modules energize on HIGH
#define RELAY_POWER_MGMT_NC          true   // Power relay wired through NC terminal
#define RELAY_TIMED_EVENT_NC         false  // Timed relay wired through NO terminal
#define RELEASE_RELAY_DURATION_SEC   1500   // Failsafe release: relay on time (e.g. electrolytic release)

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
#define MAVLINK_SYSTEM_ID        1      // Must match autopilot (same vehicle)
#define MAVLINK_COMPONENT_ID     191    // MAV_COMP_ID_ONBOARD_COMPUTER
#define MAVLINK_HEARTBEAT_MS     1000

// ============================================================================
// TIMED EVENT CONFIGURATION (stored in EEPROM/Flash)
// ============================================================================
struct TimedEventConfig {
    bool enabled;
    bool useAbsoluteTime;    // true = GMT time, false = delay from boot
    uint32_t triggerTime;    // Unix timestamp (GMT) or seconds from boot
    uint32_t durationSeconds; // How long to activate relay (seconds)
                              // For drop weight: needs >20 minutes (1200+ seconds)
                              // Uses electrolytic/galvanic action to dissolve link
};

// ============================================================================
// SYSTEM CONFIGURATION STRUCTURE
// ============================================================================
struct SystemConfig {
    // Magic number to validate EEPROM data
    uint32_t magic;

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
#define DEFAULT_MESHTASTIC_INTERVAL  3000
#define DEFAULT_MAVLINK_INTERVAL     1000
#define DEFAULT_POWER_SAVE_VOLTAGE   11.5

#endif // CONFIG_H
