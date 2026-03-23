#include "modules/config_manager.h"
#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_CONFIG_ADDRESS  0
#define CONFIG_MAGIC_NUMBER    0xDEADBEEF

extern SystemConfig sysConfig;  // Reference to global config

// Calculate simple checksum
uint32_t calculateChecksum(SystemConfig* config) {
    uint32_t checksum = 0;
    uint8_t* ptr = (uint8_t*)config;
    size_t size = sizeof(SystemConfig) - sizeof(uint32_t);  // Exclude checksum field

    for (size_t i = 0; i < size; i++) {
        checksum += ptr[i];
    }

    return checksum;
}

bool ConfigManager_load(SystemConfig* config) {
    DebugPrintln(F("Config: Loading from EEPROM..."));

    // Read config from EEPROM
    EEPROM.get(EEPROM_CONFIG_ADDRESS, *config);

    // Verify magic number
    if (config->magic != CONFIG_MAGIC_NUMBER) {
        DebugPrintln(F("Config: Invalid magic number!"));
        return false;
    }

    // Verify checksum
    uint32_t calculatedChecksum = calculateChecksum(config);
    if (config->checksum != calculatedChecksum) {
        DebugPrintln(F("Config: Checksum mismatch!"));
        return false;
    }

    DebugPrintln(F("Config: Loaded successfully"));
    return true;
}

bool ConfigManager_save(SystemConfig* config) {
    DebugPrintln(F("Config: Saving to EEPROM..."));

    // Calculate and set checksum
    config->checksum = calculateChecksum(config);

    // Write config to EEPROM (auto-commits on Apollo3)
    EEPROM.put(EEPROM_CONFIG_ADDRESS, *config);

    DebugPrintln(F("Config: Saved successfully"));
    return true;
}

void ConfigManager_setDefaults(SystemConfig* config) {
    DebugPrintln(F("Config: Setting defaults"));

    // Set magic number
    config->magic = CONFIG_MAGIC_NUMBER;

    // Set default intervals
    config->iridiumInterval = DEFAULT_IRIDIUM_INTERVAL;
    config->meshtasticInterval = DEFAULT_MESHTASTIC_INTERVAL;
    config->mavlinkInterval = DEFAULT_MAVLINK_INTERVAL;

    // Enable all features by default (PSM/NeoPixels disabled - ArduPilot handles battery/indicators)
    config->enableIridium = true;
    config->enableMeshtastic = true;
    config->enableMAVLink = true;
    config->enablePSM = false;  // Disabled - causes MbedOS mutex issues
    config->enableNeoPixels = true;

    // Timed event disabled by default
    config->timedEvent.enabled = false;
    config->timedEvent.useAbsoluteTime = false;
    config->timedEvent.triggerTime = 0;
    config->timedEvent.durationSeconds = 1500;  // Default 25 minutes for electrolytic release

    // Power management
    config->powerSaveVoltage = DEFAULT_POWER_SAVE_VOLTAGE;

    // Calculate checksum
    config->checksum = calculateChecksum(config);
}

void ConfigManager_printConfig(SystemConfig* config) {
    DebugPrintln(F("=== System Configuration ==="));
    DebugPrint(F("Iridium Interval: "));
    DebugPrint(config->iridiumInterval / 1000);
    DebugPrintln(F(" seconds"));

    DebugPrint(F("Meshtastic Interval: "));
    DebugPrint(config->meshtasticInterval / 1000);
    DebugPrintln(F(" seconds"));

    DebugPrint(F("MAVLink Interval: "));
    DebugPrint(config->mavlinkInterval);
    DebugPrintln(F(" ms"));

    DebugPrint(F("Features: "));
    if (config->enableIridium) DebugPrint(F("Iridium "));
    if (config->enableMeshtastic) DebugPrint(F("Meshtastic "));
    if (config->enableMAVLink) DebugPrint(F("MAVLink "));
    if (config->enablePSM) DebugPrint(F("PSM "));
    if (config->enableNeoPixels) DebugPrint(F("NeoPixels"));
    DebugPrintln();

    DebugPrint(F("Timed Event: "));
    if (config->timedEvent.enabled) {
        DebugPrint(F("ENABLED - "));
        if (config->timedEvent.useAbsoluteTime) {
            DebugPrint(F("GMT: "));
            DebugPrint(config->timedEvent.triggerTime);
        } else {
            DebugPrint(F("Delay: "));
            DebugPrint(config->timedEvent.triggerTime);
            DebugPrint(F("s from boot"));
        }
        DebugPrint(F(", Duration: "));
        DebugPrint(config->timedEvent.durationSeconds);
        DebugPrintln(F("s"));
    } else {
        DebugPrintln(F("DISABLED"));
    }

    DebugPrint(F("Power Save Voltage: "));
    DebugPrint(config->powerSaveVoltage, 2);
    DebugPrintln(F("V"));

    DebugPrintln(F("==========================="));
}

void ConfigManager_processCommands() {
    // This function is now just a placeholder
    // All command processing happens in main.cpp processSerialCommands()
    // to avoid duplicate serial reading
}

void ConfigManager_processCommand(String command) {
    // Process individual config command (called from main.cpp)
    // Parse command
    if (command.startsWith("config")) {
        // Show configuration
        ConfigManager_printConfig(&sysConfig);
    }
    else if (command.startsWith("save")) {
        // Save configuration
        ConfigManager_save(&sysConfig);
        DebugPrintln(F("Config: Saved"));
    }
    else if (command.startsWith("reset")) {
        // Reset to defaults
        ConfigManager_setDefaults(&sysConfig);
        ConfigManager_save(&sysConfig);
        DebugPrintln(F("Config: Reset to defaults"));
    }
    else if (command.startsWith("set_iridium_interval ")) {
        uint32_t interval = command.substring(21).toInt();
        ConfigManager_setIridiumInterval(interval * 1000);
    }
    else if (command.startsWith("set_meshtastic_interval ")) {
        uint32_t interval = command.substring(24).toInt();
        ConfigManager_setMeshtasticInterval(interval * 1000);
    }
    else if (command.startsWith("set_mavlink_interval ")) {
        uint32_t interval = command.substring(21).toInt();
        ConfigManager_setMAVLinkInterval(interval);
    }
    else if (command.startsWith("enable_")) {
        // Parse feature name
        String feature = command.substring(7);
        ConfigManager_enableFeature(feature.c_str(), true);
    }
    else if (command.startsWith("disable_")) {
        String feature = command.substring(8);
        ConfigManager_enableFeature(feature.c_str(), false);
    }
    else if (command.startsWith("set_timed_event ")) {
        // Format: set_timed_event <gmt|delay> <time> <duration_ms>
        int firstSpace = command.indexOf(' ', 16);
        int secondSpace = command.indexOf(' ', firstSpace + 1);

        String mode = command.substring(16, firstSpace);
        uint32_t time = command.substring(firstSpace + 1, secondSpace).toInt();
        uint16_t duration = command.substring(secondSpace + 1).toInt();

        bool useGMT = (mode == "gmt");
        ConfigManager_setTimedEvent(useGMT, time, duration);
    }
    else if (command.startsWith("set_power_save_voltage ")) {
        float voltage = command.substring(23).toFloat();
        ConfigManager_setPowerSaveVoltage(voltage);
    }
}

void ConfigManager_setIridiumInterval(uint32_t intervalMs) {
    sysConfig.iridiumInterval = intervalMs;
    DebugPrint(F("Config: Iridium interval set to "));
    DebugPrint(intervalMs / 1000);
    DebugPrintln(F(" seconds"));
}

void ConfigManager_setMeshtasticInterval(uint32_t intervalMs) {
    sysConfig.meshtasticInterval = intervalMs;
    DebugPrint(F("Config: Meshtastic interval set to "));
    DebugPrint(intervalMs / 1000);
    DebugPrintln(F(" seconds"));
}

void ConfigManager_setMAVLinkInterval(uint32_t intervalMs) {
    sysConfig.mavlinkInterval = intervalMs;
    DebugPrint(F("Config: MAVLink interval set to "));
    DebugPrint(intervalMs);
    DebugPrintln(F(" ms"));
}

void ConfigManager_enableFeature(const char* feature, bool enable) {
    String feat = String(feature);
    feat.toLowerCase();

    if (feat == "iridium") {
        sysConfig.enableIridium = enable;
    } else if (feat == "meshtastic") {
        sysConfig.enableMeshtastic = enable;
    } else if (feat == "mavlink") {
        sysConfig.enableMAVLink = enable;
    } else if (feat == "psm") {
        sysConfig.enablePSM = enable;
    } else if (feat == "neopixels") {
        sysConfig.enableNeoPixels = enable;
    } else {
        DebugPrintln(F("Config: Unknown feature"));
        return;
    }

    DebugPrint(F("Config: "));
    DebugPrint(feature);
    DebugPrint(F(" "));
    DebugPrintln(enable ? F("enabled") : F("disabled"));
}

void ConfigManager_setTimedEvent(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds) {
    sysConfig.timedEvent.enabled = true;
    sysConfig.timedEvent.useAbsoluteTime = useGMT;
    sysConfig.timedEvent.triggerTime = triggerTime;
    sysConfig.timedEvent.durationSeconds = durationSeconds;

    DebugPrint(F("Config: Timed event set - "));
    if (useGMT) {
        DebugPrint(F("GMT: "));
        DebugPrint(triggerTime);
    } else {
        DebugPrint(F("Delay: "));
        DebugPrint(triggerTime);
        DebugPrint(F("s from boot"));
    }
    DebugPrint(F(", Duration: "));
    DebugPrint(durationSeconds);
    DebugPrintln(F("s"));
}

void ConfigManager_setPowerSaveVoltage(float voltage) {
    sysConfig.powerSaveVoltage = voltage;
    DebugPrint(F("Config: Power save voltage set to "));
    DebugPrint(voltage, 2);
    DebugPrintln(F("V"));
}
