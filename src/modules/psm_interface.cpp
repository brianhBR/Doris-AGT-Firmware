#include "modules/psm_interface.h"
#include "config.h"
#include <Arduino.h>

/*
 * Blue Robotics Power Sense Module (PSM) Interface
 *
 * The PSM outputs analog voltage and current signals:
 * - Voltage Output: 11.0 V/V (11 volts input = 1 volt output at pin)
 * - Current Output: 37.8788 A/V with 0.330V offset
 * - Max voltage: 25.2V (6S)
 * - Max current: 100A
 *
 * Reference: https://bluerobotics.com/store/comm-control-power/control/psm-asm-r2-rp/
 *
 * Connections:
 * - PSM Voltage pin → AGT GPIO11 (AD11)
 * - PSM Current pin → AGT GPIO12 (AD12)
 */

// PSM Calibration Constants (from Blue Robotics documentation)
#define PSM_VOLTAGE_RATIO     11.0        // 11 V/V divider
#define PSM_CURRENT_RATIO     37.8788     // 37.8788 A/V
#define PSM_CURRENT_OFFSET    0.330       // 0.330V offset

// Artemis ADC specifications
#define ADC_RESOLUTION        14          // 14-bit ADC
#define ADC_MAX_VALUE         16383       // 2^14 - 1
#define ADC_REFERENCE_VOLTAGE 2.0         // 2.0V reference on Artemis

static BatteryData currentBatteryData;
static bool initialized = false;
static float totalEnergy = 0.0;     // Wh
static float totalCapacity = 0.0;   // Ah
static unsigned long lastUpdate = 0;

bool PSMInterface_init() {
    Serial.println(F("PSM: Initializing analog interface..."));

    // Configure PSM analog input pins
    pinMode(PSM_VOLTAGE_PIN, INPUT);
    pinMode(PSM_CURRENT_PIN, INPUT);

    // Configure ADC
    analogReadResolution(ADC_RESOLUTION);

    // Initialize battery data
    currentBatteryData.voltage = 0.0;
    currentBatteryData.current = 0.0;
    currentBatteryData.power = 0.0;
    currentBatteryData.energy = 0.0;
    currentBatteryData.capacity = 0.0;
    currentBatteryData.valid = false;

    initialized = true;
    lastUpdate = millis();

    Serial.println(F("PSM: Initialized successfully (analog mode)"));
    Serial.print(F("PSM: Voltage calibration: "));
    Serial.print(PSM_VOLTAGE_RATIO);
    Serial.println(F(" V/V"));
    Serial.print(F("PSM: Current calibration: "));
    Serial.print(PSM_CURRENT_RATIO);
    Serial.print(F(" A/V, offset "));
    Serial.print(PSM_CURRENT_OFFSET);
    Serial.println(F("V"));

    return true;
}

void PSMInterface_update() {
    if (!initialized) {
        return;
    }

    unsigned long currentMillis = millis();
    float deltaTime = (currentMillis - lastUpdate) / 1000.0 / 3600.0;  // hours

    // Read voltage from PSM
    // PSM outputs voltage divided by 11, we need to scale back up
    uint16_t voltageADC = analogRead(PSM_VOLTAGE_PIN);
    float voltageAtPin = (voltageADC / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
    currentBatteryData.voltage = voltageAtPin * PSM_VOLTAGE_RATIO;

    // Read current from PSM
    // PSM outputs: Vout = (Current / 37.8788) + 0.330
    // Therefore: Current = (Vout - 0.330) * 37.8788
    uint16_t currentADC = analogRead(PSM_CURRENT_PIN);
    float currentAtPin = (currentADC / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
    currentBatteryData.current = (currentAtPin - PSM_CURRENT_OFFSET) * PSM_CURRENT_RATIO;

    // Clamp negative current readings to zero (sensor noise at low current)
    if (currentBatteryData.current < 0.0) {
        currentBatteryData.current = 0.0;
    }

    // Calculate power
    currentBatteryData.power = currentBatteryData.voltage * currentBatteryData.current;

    // Accumulate energy and capacity
    totalEnergy += currentBatteryData.power * deltaTime;      // Wh
    totalCapacity += currentBatteryData.current * deltaTime;  // Ah

    currentBatteryData.energy = totalEnergy;
    currentBatteryData.capacity = totalCapacity;
    currentBatteryData.valid = true;

    lastUpdate = currentMillis;

    // Print battery info periodically
    static unsigned long lastPrint = 0;
    if (currentMillis - lastPrint > 30000) {  // Every 30 seconds
        Serial.print(F("PSM: V="));
        Serial.print(currentBatteryData.voltage, 2);
        Serial.print(F("V I="));
        Serial.print(currentBatteryData.current, 2);
        Serial.print(F("A P="));
        Serial.print(currentBatteryData.power, 2);
        Serial.print(F("W (ADC: V="));
        Serial.print(voltageADC);
        Serial.print(F(" I="));
        Serial.print(currentADC);
        Serial.println(F(")"));
        lastPrint = currentMillis;
    }
}

BatteryData PSMInterface_getData() {
    return currentBatteryData;
}

void PSMInterface_resetCounters() {
    totalEnergy = 0.0;
    totalCapacity = 0.0;
    currentBatteryData.energy = 0.0;
    currentBatteryData.capacity = 0.0;

    Serial.println(F("PSM: Energy/capacity counters reset"));
}

float PSMInterface_getSOC() {
    // Calculate state of charge based on voltage
    // This is a simplified calculation for 4S LiPo
    // Actual SOC calculation should use a proper discharge curve

    if (!currentBatteryData.valid) {
        return 0.0;
    }

    float soc = ((currentBatteryData.voltage - BATTERY_CRITICAL_VOLTAGE) /
                (BATTERY_FULL_VOLTAGE - BATTERY_CRITICAL_VOLTAGE)) * 100.0;

    if (soc < 0.0) soc = 0.0;
    if (soc > 100.0) soc = 100.0;

    return soc;
}
