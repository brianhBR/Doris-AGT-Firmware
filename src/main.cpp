/*
 * Doris AGT Firmware - Oceanographic Drop Camera
 * SparkFun Artemis Global Tracker
 *
 * State-based architecture:
 * - PREDEPLOYMENT: Configuration and system test
 * - MISSION: Active deployment (ArduPilot leads decision making)
 * - RECOVERY: Low power surface mode
 * - EMERGENCY: Drop weight release + shutdown nonessentials
 *
 * ArduPilot/Navigator controls state transitions via MAVLink/serial commands
 * AGT monitors sensors and can trigger emergency mode autonomously
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <IridiumSBD.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <RTC.h>

// Module includes
#include "modules/gps_manager.h"
#include "modules/iridium_manager.h"
#include "modules/meshtastic_interface.h"
#include "modules/mavlink_interface.h"
#include "modules/neopixel_controller.h"
#include "modules/psm_interface.h"
#include "modules/relay_controller.h"
#include "modules/config_manager.h"
#include "modules/state_machine.h"

// Global objects - ALL POINTERS to avoid early construction crashes
SFE_UBLOX_GNSS* myGPSPtr = nullptr;
IridiumSBD* modemPtr = nullptr;
Adafruit_NeoPixel* pixelsPtr = nullptr;

// System configuration
SystemConfig sysConfig;
// Use the platform-provided Apollo3 RTC instance via a reference named `myRTC`
extern Apollo3RTC rtc; // declared in RTC.h
Apollo3RTC &myRTC = rtc;

// Timing variables
unsigned long lastIridiumSend = 0;
unsigned long lastMeshtasticUpdate = 0;
unsigned long lastMAVLinkUpdate = 0;
unsigned long lastPSMUpdate = 0;
unsigned long lastNeoPixelUpdate = 0;
unsigned long lastStateStatus = 0;
unsigned long bootTime = 0;

// NeoPixel state tracking is defined in `neopixel_controller.h`
LEDState currentLEDState = LED_STATE_BOOT;

// Function prototypes
void setupPins();
void loadConfiguration();
void updateLEDState();
void processSerialCommands();
void checkEmergencySensors();

void setup() {
    // Initialize serial ports
    Serial.begin(DEBUG_BAUD);

    // CRITICAL: Wait for serial monitor to connect (Artemis requirement)
    while (!Serial) {
        ; // Wait for USB serial connection
    }
    delay(100);  // Small delay after serial ready

    Serial.println(F("==========================================="));
    Serial.println(F("  Doris AGT Firmware - Drop Camera"));
    Serial.println(F("  State Machine Architecture"));
    Serial.println(F("==========================================="));

    // Setup pins
    Serial.println(F("[1] Setting up pins..."));
    setupPins();
    Serial.println(F("[1] Pins OK"));

    // Initialize RTC
    Serial.println(F("[2] Setting up RTC..."));
    myRTC.setTime(0, 0, 0, 0, 1, 1, 2025);  // Will be updated from GPS
    bootTime = millis();
    Serial.println(F("[2] RTC OK"));

    // Load configuration from EEPROM
    Serial.println(F("[3] Loading configuration..."));
    loadConfiguration();
    Serial.println(F("[3] Config OK"));

    // Initialize I2C (commented out - we use custom agtWire instead)
    Serial.println(F("[4] Initializing I2C..."));
    // Wire.begin();  // Don't initialize default I2C - may conflict with agtWire
    // Wire.setClock(400000);  // 400kHz I2C
    Serial.println(F("[4] I2C (default bus disabled)"));

    // Initialize State Machine FIRST
    Serial.println(F("[5] Initializing State Machine..."));
    StateMachine_init();
    Serial.println(F("[5] State Machine OK"));

    // Initialize Relay Controller
    Serial.println(F("[6] Initializing Relays..."));
    RelayController_init();
    Serial.println(F("[6] Relays OK"));

    // Create GPS object
    Serial.println(F("[7] Creating GPS object..."));
    myGPSPtr = new SFE_UBLOX_GNSS();
    Serial.println(F("[7] GPS object created"));

    // Initialize GPS Manager
    Serial.println(F("[8] Initializing GPS..."));
    if (!GPSManager_init(myGPSPtr)) {
        Serial.println(F("WARNING: GPS initialization failed!"));
        // Continue anyway - GPS may come online later
    }
    Serial.println(F("[8] GPS OK"));

    // Create Iridium object NOW (after Serial1 is ready)
    Serial.println(F("[9] Creating Iridium object..."));
    modemPtr = new IridiumSBD(IRIDIUM_SERIAL, IRIDIUM_SLEEP, IRIDIUM_RI);
    Serial.println(F("[9] Iridium object created"));

    // Initialize Iridium Manager
    if (sysConfig.enableIridium) {
        Serial.println(F("[10] Initializing Iridium..."));
        if (!IridiumManager_init(modemPtr)) {
            Serial.println(F("WARNING: Iridium initialization failed!"));
        }
        Serial.println(F("[10] Iridium OK"));
    }

    // Initialize Meshtastic Interface
    if (sysConfig.enableMeshtastic) {
        Serial.println(F("[11] Initializing Meshtastic..."));
        MeshtasticInterface_init();
        Serial.println(F("[11] Meshtastic OK"));
    }

    // Initialize MAVLink Interface
    if (sysConfig.enableMAVLink) {
        Serial.println(F("[12] Initializing MAVLink..."));
        MAVLinkInterface_init();
        Serial.println(F("[12] MAVLink OK"));
    }

    // Create NeoPixel object
    Serial.println(F("[13] Creating NeoPixel object..."));
    pixelsPtr = new Adafruit_NeoPixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
    Serial.println(F("[13] NeoPixel object created"));

    // Initialize NeoPixel Controller
    if (sysConfig.enableNeoPixels) {
        Serial.println(F("[14] Initializing NeoPixels..."));
        NeoPixelController_init(pixelsPtr);
        NeoPixelController_setColor(COLOR_BOOT);
        Serial.println(F("[14] NeoPixels OK"));
    }

    // Initialize PSM Interface (optional - battery monitoring)
    if (sysConfig.enablePSM) {
        Serial.println(F("[15] Initializing PSM..."));
        if (!PSMInterface_init()) {
            Serial.println(F("WARNING: PSM initialization failed (continuing without battery monitoring)"));
            sysConfig.enablePSM = false;  // Disable if not available
        }
        Serial.println(F("[15] PSM OK"));
    } else {
        Serial.println(F("[15] PSM battery monitoring DISABLED (ArduPilot handles battery)"));
    }

    Serial.println(F("==========================================="));
    Serial.println(F("  Setup Complete"));
    Serial.println(F("  State: PREDEPLOYMENT"));
    Serial.println(F("  Waiting for mission start command..."));
    Serial.println(F("==========================================="));
}

void loop() {
    unsigned long currentMillis = millis();

    // Update state machine (highest priority)
    StateMachine_update();

    // Update GPS
    GPSManager_update();

    // Process commands from serial/MAVLink (ArduPilot commands)
    processSerialCommands();

    // Process incoming MAVLink messages from ArduPilot
    if (sysConfig.enableMAVLink) {
        MAVLinkInterface_update();
    }

    // Check emergency sensors (AGT autonomous emergency detection)
    if (StateMachine_getState() == STATE_MISSION) {
        checkEmergencySensors();
    }

    // Update LED state based on system status
    updateLEDState();

    // Update NeoPixel display
    if (sysConfig.enableNeoPixels && pixelsPtr != nullptr &&
        (currentMillis - lastNeoPixelUpdate >= NEOPIXEL_UPDATE_MS)) {
        NeoPixelController_update(currentLEDState);
        lastNeoPixelUpdate = currentMillis;
    }

    // Send position via Iridium (if allowed in current state)
    if (sysConfig.enableIridium && modemPtr != nullptr &&
        StateMachine_canTransmitIridium() &&
        (currentMillis - lastIridiumSend >= sysConfig.iridiumInterval)) {
        if (GPSManager_hasFix()) {
            currentLEDState = LED_STATE_IRIDIUM_TX;

            GPSData gpsData = GPSManager_getData();
            BatteryData battData = {0};  // Zero if PSM disabled
            if (sysConfig.enablePSM) {
                battData = PSMInterface_getData();
            }

            if (IridiumManager_sendPosition(&gpsData, &battData)) {
                Serial.println(F("Iridium: Transmission successful"));
                lastIridiumSend = currentMillis;
            } else {
                Serial.println(F("Iridium: Transmission failed"));
            }
        }
    }

    // Update Meshtastic (if allowed in current state)
    if (sysConfig.enableMeshtastic &&
        StateMachine_canTransmitMeshtastic() &&
        (currentMillis - lastMeshtasticUpdate >= sysConfig.meshtasticInterval)) {
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MeshtasticInterface_sendPosition(&gpsData);
            lastMeshtasticUpdate = currentMillis;
        }
    }

    // Update MAVLink
    if (sysConfig.enableMAVLink &&
        (currentMillis - lastMAVLinkUpdate >= sysConfig.mavlinkInterval)) {
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MAVLinkInterface_sendGPS(&gpsData);
        }

        // Send battery status if PSM enabled
        if (sysConfig.enablePSM) {
            BatteryData battData = PSMInterface_getData();
            MAVLinkInterface_sendStatus(battData.voltage, battData.current);
        }

        MAVLinkInterface_sendHeartbeat();
        lastMAVLinkUpdate = currentMillis;
    }

    // Update PSM (battery monitoring) if enabled
    if (sysConfig.enablePSM &&
        (currentMillis - lastPSMUpdate >= PSM_UPDATE_MS)) {
        PSMInterface_update();
        lastPSMUpdate = currentMillis;
    }

    // Print state status periodically
    if (currentMillis - lastStateStatus > 60000) {  // Every minute
        StateMachine_printState();
        lastStateStatus = currentMillis;
    }

    // Small delay to prevent tight loop
    delay(10);
}

void setupPins() {
    // Configure power control pins
    pinMode(IRIDIUM_PWR_EN, OUTPUT);
    pinMode(GNSS_EN, OUTPUT);
    pinMode(SUPERCAP_CHG_EN, OUTPUT);
    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    pinMode(LED_WHITE, OUTPUT);
    pinMode(IRIDIUM_SLEEP, OUTPUT);

    // Configure input pins
    pinMode(IRIDIUM_NA, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(GEOFENCE_PIN, INPUT);

    // Configure relay pins (handled by RelayController)
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    // Initial states
    digitalWrite(IRIDIUM_PWR_EN, LOW);      // Iridium off
    digitalWrite(GNSS_EN, LOW);              // GNSS on (active low)
    digitalWrite(SUPERCAP_CHG_EN, LOW);     // Supercap charger off initially
    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH); // Voltage monitoring on
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(IRIDIUM_SLEEP, LOW);       // Iridium sleep
    digitalWrite(RELAY_POWER_MGMT, LOW);
    digitalWrite(RELAY_TIMED_EVENT, LOW);
}

void loadConfiguration() {
    Serial.println(F("Loading configuration..."));

    if (!ConfigManager_load(&sysConfig)) {
        Serial.println(F("No valid config found, using defaults"));
        ConfigManager_setDefaults(&sysConfig);
        ConfigManager_save(&sysConfig);
    }

    ConfigManager_printConfig(&sysConfig);
}

void updateLEDState() {
    // Update LED state based on system status
    SystemState machineState = StateMachine_getState();

    if (machineState == STATE_EMERGENCY) {
        currentLEDState = LED_STATE_EMERGENCY;
    } else if (GPSManager_hasFix()) {
        currentLEDState = LED_STATE_GPS_FIX;
    } else {
        currentLEDState = LED_STATE_GPS_SEARCH;
    }

    // Update RTC from GPS when fix acquired
    static bool rtcSynced = false;
    if (GPSManager_hasFix() && !rtcSynced) {
        GPSData gpsData = GPSManager_getData();
        myRTC.setTime(gpsData.hour, gpsData.minute, gpsData.second,
                     0, gpsData.day, gpsData.month, gpsData.year);
        Serial.println(F("RTC synchronized from GPS"));
        rtcSynced = true;
    }
}

void processSerialCommands() {
    // Process commands from ArduPilot/BlueOS via serial

    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();

        if (command.length() == 0) {
            return;
        }

        Serial.print(F("Command received: "));
        Serial.println(command);

        // State machine commands
        if (command == "start_mission") {
            StateMachine_requestTransition(TRANSITION_START_MISSION);
        }
        else if (command == "enter_recovery") {
            StateMachine_requestTransition(TRANSITION_ENTER_RECOVERY);
        }
        else if (command == "emergency") {
            StateMachine_triggerEmergency(EMERGENCY_ARDUPILOT);
        }
        else if (command == "exit_emergency") {
            StateMachine_requestTransition(TRANSITION_EXIT_EMERGENCY);
        }
        else if (command == "reset") {
            StateMachine_requestTransition(TRANSITION_RESET);
        }
        else if (command == "status") {
            StateMachine_printState();
        }
        else if (command == "gps") {
            // Display GPS status
            Serial.println(F("=== GPS Status ==="));
            if (GPSManager_hasFix()) {
                char gpsStr[200];
                GPSManager_getDataString(gpsStr, sizeof(gpsStr));
                Serial.println(gpsStr);
            } else {
                GPSData gpsData = GPSManager_getData();
                Serial.print(F("Satellites: "));
                Serial.println(gpsData.satellites);
                Serial.print(F("Fix Type: "));
                Serial.println(gpsData.fixType);
                Serial.println(F("Status: Searching for fix..."));
                Serial.println(F("(Needs 4+ satellites for 3D fix)"));
            }
            Serial.println(F("=================="));
        }
        else if (command == "i2c_scan") {
            // Scan both I2C buses for devices
            Serial.println(F("=== I2C Bus Scan ==="));

            // Scan default Wire bus (MS8607 pressure sensor should be here)
            Serial.println(F("Default I2C bus (Wire): DISABLED"));
            Serial.println(F("  (Not initialized to avoid conflict with agtWire)"));
            byte count = 0;
            // Skip Wire scan since we don't initialize it

            // Scan AGT Wire bus (GPS should be here at 0x42)
            Serial.println(F("AGT I2C bus (pins 8/9 - agtWire):"));
            Serial.println(F("  (Scan disabled - GPS module handles I2C internally)"));
            count = 0;

            Serial.println(F("Expected: GPS at 0x42 on agtWire"));
            Serial.println(F("==================="));
        }
        else if (command == "release_now") {
            // Manual drop weight release
            StateMachine_releaseDropWeight();
        }
        else if (command.startsWith("arm_drop ")) {
            // arm_drop <gmt|delay> <time> <duration>
            // Parse command
            int firstSpace = command.indexOf(' ', 9);
            int secondSpace = command.indexOf(' ', firstSpace + 1);

            String mode = command.substring(9, firstSpace);
            uint32_t time = command.substring(firstSpace + 1, secondSpace).toInt();
            uint16_t duration = command.substring(secondSpace + 1).toInt();

            bool useGMT = (mode == "gmt");
            StateMachine_armDropWeight(useGMT, time, duration);
        }
        else if (command == "config_reset") {
            // Reset configuration to defaults
            Serial.println(F("Resetting configuration to defaults..."));
            ConfigManager_setDefaults(&sysConfig);
            ConfigManager_save(&sysConfig);
            Serial.println(F("Configuration reset! Reboot to apply."));
        }
        // Existing configuration commands
        else {
            // Pass to config manager for other commands
            ConfigManager_processCommands();
        }
    }
}

void checkEmergencySensors() {
    // AGT can autonomously trigger emergency based on sensors
    // This allows AGT to act as a failsafe if ArduPilot fails

    // Example: Check depth sensor (via PHT MS8607 pressure)
    // Example: Check temperature sensor
    // Example: Check mission timeout

    // TODO: Implement sensor threshold checks
    // For now, only ArduPilot can trigger emergency

    // Example implementation:
    /*
    if (depth > MAX_DEPTH) {
        StateMachine_triggerEmergency(EMERGENCY_DEPTH_SENSOR);
    }
    if (temperature < MIN_TEMP || temperature > MAX_TEMP) {
        StateMachine_triggerEmergency(EMERGENCY_TEMPERATURE);
    }
    */
}
