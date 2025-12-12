#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "config.h"

// Load configuration from EEPROM/Flash
bool ConfigManager_load(SystemConfig* config);

// Save configuration to EEPROM/Flash
bool ConfigManager_save(SystemConfig* config);

// Set default configuration
void ConfigManager_setDefaults(SystemConfig* config);

// Print configuration to serial
void ConfigManager_printConfig(SystemConfig* config);

// Process serial commands for configuration
void ConfigManager_processCommands();

// Configuration commands (can be called via serial)
void ConfigManager_setIridiumInterval(uint32_t intervalMs);
void ConfigManager_setMeshtasticInterval(uint32_t intervalMs);
void ConfigManager_setMAVLinkInterval(uint32_t intervalMs);
void ConfigManager_enableFeature(const char* feature, bool enable);
void ConfigManager_setTimedEvent(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds);
void ConfigManager_setPowerSaveVoltage(float voltage);

#endif // CONFIG_MANAGER_H
