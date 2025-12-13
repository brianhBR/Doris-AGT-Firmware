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
    Serial.println(F("Config: Loading from EEPROM..."));

    // Read config from EEPROM
    EEPROM.get(EEPROM_CONFIG_ADDRESS, *config);

    // Verify checksum
    uint32_t calculatedChecksum = calculateChecksum(config);
    if (config->checksum != calculatedChecksum) {
        Serial.println(F("Config: Checksum mismatch!"));
        return false;
    }

    Serial.println(F("Config: Loaded successfully"));
    return true;
}

bool ConfigManager_save(SystemConfig* config) {
    Serial.println(F("Config: Saving to EEPROM..."));

    // Calculate and set checksum
    config->checksum = calculateChecksum(config);

    // Write config to EEPROM
    EEPROM.put(EEPROM_CONFIG_ADDRESS, *config);

    Serial.println(F("Config: Saved successfully"));
    return true;
}

void ConfigManager_setDefaults(SystemConfig* config) {
    Serial.println(F("Config: Setting defaults"));

    // Set default intervals
    config->iridiumInterval = DEFAULT_IRIDIUM_INTERVAL;
    config->meshtasticInterval = DEFAULT_MESHTASTIC_INTERVAL;
    config->mavlinkInterval = DEFAULT_MAVLINK_INTERVAL;

    // Enable all features by default
    config->enableIridium = true;
    config->enableMeshtastic = true;
    config->enableMAVLink = true;
    config->enablePSM = true;
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
    Serial.println(F("=== System Configuration ==="));
    Serial.print(F("Iridium Interval: "));
    Serial.print(config->iridiumInterval / 1000);
    Serial.println(F(" seconds"));

    Serial.print(F("Meshtastic Interval: "));
    Serial.print(config->meshtasticInterval / 1000);
    Serial.println(F(" seconds"));

    Serial.print(F("MAVLink Interval: "));
    Serial.print(config->mavlinkInterval);
    Serial.println(F(" ms"));

    Serial.print(F("Features: "));
    if (config->enableIridium) Serial.print(F("Iridium "));
    if (config->enableMeshtastic) Serial.print(F("Meshtastic "));
    if (config->enableMAVLink) Serial.print(F("MAVLink "));
    if (config->enablePSM) Serial.print(F("PSM "));
    if (config->enableNeoPixels) Serial.print(F("NeoPixels"));
    Serial.println();

    Serial.print(F("Timed Event: "));
    if (config->timedEvent.enabled) {
        Serial.print(F("ENABLED - "));
        if (config->timedEvent.useAbsoluteTime) {
            Serial.print(F("GMT: "));
            Serial.print(config->timedEvent.triggerTime);
        } else {
            Serial.print(F("Delay: "));
            Serial.print(config->timedEvent.triggerTime);
            Serial.print(F("s from boot"));
        }
        Serial.print(F(", Duration: "));
        Serial.print(config->timedEvent.durationSeconds);
        Serial.println(F("s"));
    } else {
        Serial.println(F("DISABLED"));
    }

    Serial.print(F("Power Save Voltage: "));
    Serial.print(config->powerSaveVoltage, 2);
    Serial.println(F("V"));

    Serial.println(F("==========================="));
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
        Serial.println(F("Config: Saved"));
    }
    else if (command.startsWith("reset")) {
        // Reset to defaults
        ConfigManager_setDefaults(&sysConfig);
        ConfigManager_save(&sysConfig);
        Serial.println(F("Config: Reset to defaults"));
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
    Serial.print(F("Config: Iridium interval set to "));
    Serial.print(intervalMs / 1000);
    Serial.println(F(" seconds"));
}

void ConfigManager_setMeshtasticInterval(uint32_t intervalMs) {
    sysConfig.meshtasticInterval = intervalMs;
    Serial.print(F("Config: Meshtastic interval set to "));
    Serial.print(intervalMs / 1000);
    Serial.println(F(" seconds"));
}

void ConfigManager_setMAVLinkInterval(uint32_t intervalMs) {
    sysConfig.mavlinkInterval = intervalMs;
    Serial.print(F("Config: MAVLink interval set to "));
    Serial.print(intervalMs);
    Serial.println(F(" ms"));
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
        Serial.println(F("Config: Unknown feature"));
        return;
    }

    Serial.print(F("Config: "));
    Serial.print(feature);
    Serial.print(F(" "));
    Serial.println(enable ? F("enabled") : F("disabled"));
}

void ConfigManager_setTimedEvent(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds) {
    sysConfig.timedEvent.enabled = true;
    sysConfig.timedEvent.useAbsoluteTime = useGMT;
    sysConfig.timedEvent.triggerTime = triggerTime;
    sysConfig.timedEvent.durationSeconds = durationSeconds;

    Serial.print(F("Config: Timed event set - "));
    if (useGMT) {
        Serial.print(F("GMT: "));
        Serial.print(triggerTime);
    } else {
        Serial.print(F("Delay: "));
        Serial.print(triggerTime);
        Serial.print(F("s from boot"));
    }
    Serial.print(F(", Duration: "));
    Serial.print(durationSeconds);
    Serial.println(F("s"));
}

void ConfigManager_setPowerSaveVoltage(float voltage) {
    sysConfig.powerSaveVoltage = voltage;
    Serial.print(F("Config: Power save voltage set to "));
    Serial.print(voltage, 2);
    Serial.println(F("V"));
}
