/*
 * AGT Self-Test Firmware
 * SparkFun Artemis Global Tracker - GPS, Iridium, NeoPixel verification
 *
 * NeoPixel layout:
 *   LEDs 1-6  (index 0-5):  Upward facing  - GPS/Iridium status
 *   LEDs 7-30 (index 6-29): Horizontal     - Solid dim white to confirm working
 *
 * Upward LED status:
 *   Yellow       - Searching for GPS (3D fix, >8 satellites)
 *   Green        - GPS fix acquired
 *   Magenta      - Iridium transmission in progress
 *   Blue         - Iridium message sent successfully
 *   Red          - Error (init failure or send failure)
 *
 * Every 30 seconds: verify GPS fix -> send Iridium position report -> repeat
 *
 * NOTE: Uses a custom WS2812B driver instead of Adafruit NeoPixel to avoid
 * MbedOS RTOS conflicts on the Apollo3 platform.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <IridiumSBD.h>

// ============================================================================
// AGT HARDWARE PINS (from SparkFun Example16)
// ============================================================================
#define gnssEN              26
#define gnssBckpBatChgEN    44
#define iridiumSleep        17
#define iridiumNA           18
#define iridiumPwrEN        22
#define iridiumRI           41
#define superCapChgEN       27
#define superCapPGOOD       28
#define busVoltagePin       13
#define busVoltageMonEN     34
#define LED                 19
#define NEOPIXEL_PIN        32

// AGT GPS I2C bus
const byte PIN_AGTWIRE_SCL = 8;
const byte PIN_AGTWIRE_SDA = 9;

// ============================================================================
// CONFIGURATION
// ============================================================================
#define NUM_LEDS            30
#define LED_BRIGHTNESS      40    // 0-255, applied in software
#define REQUIRED_FIX_TYPE   3
#define REQUIRED_SATS       9     // >8 means >= 9
#define CYCLE_INTERVAL_MS   90000
#define GNSS_TIMEOUT_MIN    5
#define CHG_TIMEOUT_MIN     2
#define TOPUP_SECONDS       10

// LED groups
#define UPWARD_FIRST  0
#define UPWARD_LAST   5
#define HORIZ_FIRST   6
#define HORIZ_LAST    29

// ============================================================================
// MINIMAL SK6805 RGBW DRIVER (MbedOS-safe: saves/restores PRIMASK)
// SK6805SIDE-FRGBW uses 4 bytes per pixel in GRBW order
// ============================================================================
#define BYTES_PER_LED 4  // GRBW

#define NOP1  __asm volatile("nop")
#define NOP2  NOP1; NOP1
#define NOP4  NOP2; NOP2
#define NOP5  NOP4; NOP1
#define NOP8  NOP4; NOP4
#define NOP10 NOP8; NOP2

static uint8_t ledBuffer[NUM_LEDS * BYTES_PER_LED]; // GRBW order

static void ws_init() {
    pinMode(NEOPIXEL_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_PIN, LOW);
    memset(ledBuffer, 0, sizeof(ledBuffer));
}

static void ws_setPixel(uint16_t n, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
    if (n >= NUM_LEDS) return;
    uint16_t offset = n * BYTES_PER_LED;
    ledBuffer[offset]     = ((uint16_t)g * LED_BRIGHTNESS) >> 8;
    ledBuffer[offset + 1] = ((uint16_t)r * LED_BRIGHTNESS) >> 8;
    ledBuffer[offset + 2] = ((uint16_t)b * LED_BRIGHTNESS) >> 8;
    ledBuffer[offset + 3] = ((uint16_t)w * LED_BRIGHTNESS) >> 8;
}

static void ws_clear() {
    memset(ledBuffer, 0, sizeof(ledBuffer));
}

// Bitbang SK6805 RGBW data at 800kHz using Apollo3 GPIO HAL.
// Timing calibrated for 48MHz Cortex-M4.
// Uses PRIMASK save/restore to avoid corrupting MbedOS RTOS state.
static void ws_show() {
    uint8_t* ptr = ledBuffer;
    uint16_t numBytes = NUM_LEDS * BYTES_PER_LED;

    uint32_t savedPrimask = __get_PRIMASK();
    __disable_irq();

    while (numBytes--) {
        uint8_t b = *ptr++;
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            if (b & mask) {
                // 1-bit: ~750ns HIGH, ~500ns LOW
                am_hal_gpio_output_set(NEOPIXEL_PIN);
                NOP10; NOP10; NOP8;
                am_hal_gpio_output_clear(NEOPIXEL_PIN);
                NOP5;
            } else {
                // 0-bit: ~400ns HIGH, ~850ns LOW
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
// LED HELPERS
// ============================================================================
static void setUpwardLEDs(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = UPWARD_FIRST; i <= UPWARD_LAST; i++)
        ws_setPixel(i, r, g, b);
}

static void setHorizontalLEDs(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
    for (int i = HORIZ_FIRST; i <= HORIZ_LAST; i++)
        ws_setPixel(i, r, g, b, w);
}

static void showStatus(uint8_t r, uint8_t g, uint8_t b) {
    setUpwardLEDs(r, g, b);
    setHorizontalLEDs(0, 0, 0, 30); // dim white via W channel on horizontal
    ws_show();
}

// ============================================================================
// GLOBALS (pointers allocated in setup)
// ============================================================================
static SFE_UBLOX_GNSS* pGNSS  = nullptr;
static IridiumSBD*     pModem = nullptr;

static bool gpsInitOK     = false;
static bool iridiumInitOK = false;

static uint8_t  gpsSats    = 0;
static uint8_t  gpsFixType = 0;
static double   gpsLat     = 0;
static double   gpsLon     = 0;
static float    gpsAlt     = 0;
static bool     hasGoodFix = false;

static unsigned long lastCycleTime = 0;
static uint32_t sendCount = 0;
static bool lastSendOK = false;
static bool hasSentMessage = false;

// ============================================================================
// LAZY I2C INIT
// ============================================================================
static TwoWire& getAGTWire() {
    static TwoWire wire(PIN_AGTWIRE_SDA, PIN_AGTWIRE_SCL);
    return wire;
}

// ============================================================================
// AS179 RF ANTENNA SWITCH CONTROL
// gnssEN and iridiumPwrEN must NEVER both be active simultaneously.
//   GPS mode:     gnssEN=LOW (on), iridiumPwrEN=LOW (off)
//   Iridium mode: gnssEN=HIGH (off), iridiumPwrEN=HIGH (on)
//   Both off:     gnssEN=HIGH, iridiumPwrEN=LOW
// ============================================================================
static void configureGnssEnPin() {
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(gnssEN), pinCfg);
    delay(1);
}

static void switchToGPS() {
    Serial.println(F("[RF] Switch -> GPS (Iridium OFF first, then GPS ON)"));
    digitalWrite(iridiumPwrEN, LOW);   // Iridium power OFF first
    digitalWrite(iridiumSleep, LOW);   // Modem sleep
    delay(250);                        // Wait for Iridium front-end to fully power down
    configureGnssEnPin();
    digitalWrite(gnssEN, LOW);         // GPS power ON
    delay(750);                        // ZOE-M8Q needs time to start up
}

static void switchToIridium() {
    Serial.println(F("[RF] Switch -> Iridium (GPS OFF first, then Iridium ON)"));
    configureGnssEnPin();
    digitalWrite(gnssEN, HIGH);        // GPS power OFF first
    delay(250);                        // Wait for GPS front-end to fully power down
    digitalWrite(iridiumPwrEN, HIGH);  // Iridium power ON
    delay(250);
}

static void switchToNone() {
    Serial.println(F("[RF] Switch -> OFF (both OFF)"));
    digitalWrite(iridiumPwrEN, LOW);
    digitalWrite(iridiumSleep, LOW);
    delay(50);
    configureGnssEnPin();
    digitalWrite(gnssEN, HIGH);
    delay(250);
}

// ============================================================================
// IRIDIUM CALLBACK
// ============================================================================
bool ISBDCallback() {
    if ((millis() / 250) % 2 == 1)
        digitalWrite(LED, HIGH);
    else
        digitalWrite(LED, LOW);
    return true;
}

// ============================================================================
// I2C BUS RECOVERY
// Toggle SCL manually to free a stuck I2C bus (SDA held low by slave).
// ============================================================================
static void i2cBusRecovery() {
    Serial.println(F("[I2C] Bus recovery..."));
    pinMode(PIN_AGTWIRE_SDA, INPUT);
    pinMode(PIN_AGTWIRE_SCL, OUTPUT);
    for (int i = 0; i < 16; i++) {
        digitalWrite(PIN_AGTWIRE_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(PIN_AGTWIRE_SCL, LOW);
        delayMicroseconds(5);
    }
    digitalWrite(PIN_AGTWIRE_SCL, HIGH);
    delayMicroseconds(5);

    // Generate STOP condition
    pinMode(PIN_AGTWIRE_SDA, OUTPUT);
    digitalWrite(PIN_AGTWIRE_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_AGTWIRE_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_AGTWIRE_SDA, HIGH);
    delayMicroseconds(5);
}

// ============================================================================
// GPS INIT
// ============================================================================
static bool initGPS() {
    Serial.println(F("[GPS] Initializing ZOE-M8Q..."));

    // Ensure Iridium is off, GPS is on
    switchToGPS();
    delay(2000);

    // Recover I2C bus if stuck from previous session
    i2cBusRecovery();

    TwoWire& agtWire = getAGTWire();
    agtWire.begin();
    delay(100);
    agtWire.setClock(100000);

    am_hal_gpio_pincfg_t sclCfg = g_AM_BSP_GPIO_IOM1_SCL;
    sclCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SCL), sclCfg);
    am_hal_gpio_pincfg_t sdaCfg = g_AM_BSP_GPIO_IOM1_SDA;
    sdaCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SDA), sdaCfg);
    delay(100);

    if (!pGNSS->begin(agtWire)) {
        Serial.println(F("[GPS] FAIL - not detected"));
        return false;
    }

    pGNSS->setI2COutput(COM_TYPE_UBX);
    pGNSS->setNavigationFrequency(1);
    pGNSS->setDynamicModel(DYN_MODEL_PORTABLE);
    pGNSS->setAutoPVT(true);
    pGNSS->setI2CpollingWait(25);

    pinMode(gnssBckpBatChgEN, OUTPUT);
    digitalWrite(gnssBckpBatChgEN, LOW);

    Serial.println(F("[GPS] OK"));
    return true;
}

// ============================================================================
// IRIDIUM SEND - handles full antenna switch cycle:
//   GPS off -> supercap charge -> Iridium on -> send -> Iridium off -> GPS on
// ============================================================================
static int iridiumSend(const char* msg) {
    Serial.println(F("[RF] Preparing Iridium send..."));

    // 1. Switch antenna from GPS to Iridium
    switchToIridium();

    // 2. Charge supercaps
    Serial.println(F("[Iridium] Charging supercaps..."));
    digitalWrite(superCapChgEN, HIGH);
    delay(2000);

    unsigned long t0 = millis();
    bool pgood = false;
    while (!pgood && (millis() - t0 < (unsigned long)CHG_TIMEOUT_MIN * 60000UL)) {
        pgood = (digitalRead(superCapPGOOD) == HIGH);
        if ((millis() / 500) % 2 == 1)
            digitalWrite(LED, HIGH);
        else
            digitalWrite(LED, LOW);
        delay(100);
    }
    digitalWrite(LED, LOW);

    if (!pgood) {
        Serial.println(F("[Iridium] FAIL - supercap timeout"));
        digitalWrite(superCapChgEN, LOW);
        switchToGPS();
        initGPS();
        return -1;
    }

    Serial.println(F("[Iridium] PGOOD, top-up..."));
    for (int i = 0; i < (int)(TOPUP_SECONDS * 10); i++) {
        delay(100);
    }

    // 3. Wake modem and send
    Serial.println(F("[Iridium] Waking modem..."));
    int err = pModem->begin();
    if (err != ISBD_SUCCESS) {
        Serial.print(F("[Iridium] Wake failed, error="));
        Serial.println(err);
        pModem->sleep();
        digitalWrite(superCapChgEN, LOW);
        switchToGPS();
        initGPS();
        return err;
    }
    iridiumInitOK = true;

    // 3b. Check signal quality (0-5 bars)
    int csq = -1;
    err = pModem->getSignalQuality(csq);
    if (err == ISBD_SUCCESS) {
        Serial.print(F("[Iridium] Signal quality (CSQ): "));
        Serial.print(csq);
        Serial.println(F("/5"));
    } else {
        Serial.print(F("[Iridium] CSQ check failed, error="));
        Serial.println(err);
    }

    Serial.print(F("[Iridium] Sending: "));
    Serial.println(msg);
    err = pModem->sendSBDText(msg);

    if (err == ISBD_SUCCESS) {
        pModem->clearBuffers(ISBD_CLEAR_MO);
    }

    // 4. Sleep modem, disable charger, switch back to GPS
    pModem->sleep();
    digitalWrite(superCapChgEN, LOW);
    switchToGPS();

    // 5. GPS was power-cycled, must re-initialize
    Serial.println(F("[GPS] Re-initializing after Iridium send..."));
    delay(2000);
    gpsInitOK = initGPS();
    if (!gpsInitOK) {
        Serial.println(F("[GPS] Re-init FAILED"));
    }

    return err;
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    // CRITICAL: Immediately set both RF paths OFF before anything else.
    // At boot, GPIO pins are high-Z (input) — gnssEN could float LOW (GPS on)
    // and iridiumPwrEN could float HIGH (Iridium on), powering both simultaneously.
    pinMode(iridiumPwrEN, OUTPUT);
    digitalWrite(iridiumPwrEN, LOW);    // Iridium power OFF
    pinMode(iridiumSleep, OUTPUT);
    digitalWrite(iridiumSleep, LOW);    // Iridium modem sleep
    pinMode(superCapChgEN, OUTPUT);
    digitalWrite(superCapChgEN, LOW);   // Supercap charger OFF
    configureGnssEnPin();
    digitalWrite(gnssEN, HIGH);         // GPS power OFF (active low)

    Serial.begin(115200);
    while (!Serial && millis() < 10000) { ; }
    delay(100);

    Serial.println();
    Serial.println(F("============================================="));
    Serial.println(F("  AGT Self-Test: GPS + Iridium + NeoPixel   "));
    Serial.println(F("============================================="));
    Serial.println(F("  Upward LEDs 1-6:   Status indicators"));
    Serial.println(F("  Horizontal 7-30:   Dim white"));
    Serial.println(F("  GPS requirement:   3D fix, >8 sats"));
    Serial.println(F("  Send interval:     90 seconds"));
    Serial.println(F("============================================="));
    Serial.println();

    // Default pin states
    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);
    pinMode(busVoltageMonEN, OUTPUT);
    digitalWrite(busVoltageMonEN, LOW);

    // NeoPixels - custom driver, no library
    ws_init();
    showStatus(255, 255, 0); // yellow = starting up
    Serial.println(F("[NeoPixel] Custom WS2812B driver initialized"));

    // Allocate hardware objects in setup (not global scope) for RTOS safety
    pGNSS  = new SFE_UBLOX_GNSS();
    pModem = new IridiumSBD(Serial1, iridiumSleep, iridiumRI);

    // GPS only at startup — Iridium deferred until first send
    showStatus(255, 255, 0); // yellow
    gpsInitOK = initGPS();
    if (!gpsInitOK) {
        showStatus(255, 0, 0); // red
        delay(3000);
    }

    // Configure Iridium pins but do NOT power it on yet
    pinMode(superCapPGOOD, INPUT);
    pinMode(iridiumRI, INPUT);
    pinMode(iridiumNA, INPUT);
    Serial1.begin(19200);
    pModem->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    pModem->adjustSendReceiveTimeout(180);
    Serial.println(F("[Iridium] Pins configured, init deferred until first send"));

    if (gpsInitOK) {
        showStatus(0, 255, 0); // green = GPS good
    } else {
        showStatus(255, 0, 0); // red
    }

    Serial.println();
    Serial.println(F("--- Hardware Status ---"));
    Serial.print(F("  GPS:      "));
    Serial.println(gpsInitOK ? F("OK") : F("FAILED"));
    Serial.println(F("  Iridium:  deferred (will init on first send)"));
    Serial.println(F("  NeoPixel: OK (custom driver)"));
    Serial.println(F("-----------------------"));
    Serial.println();
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    unsigned long now = millis();

    // ---- Update GPS (with I2C hang detection) ----
    static uint32_t pvtOkCount = 0;
    static uint32_t pvtFailCount = 0;
    static unsigned long lastLoopComplete = 0;
    if (lastLoopComplete == 0) lastLoopComplete = millis();

    if (gpsInitOK) {
        unsigned long beforePVT = millis();
        bool pvtResult = pGNSS->getPVT();
        unsigned long pvtDuration = millis() - beforePVT;

        // If getPVT took more than 5 seconds, I2C bus may be degraded
        if (pvtDuration > 5000) {
            Serial.print(F("[GPS] WARNING: getPVT took "));
            Serial.print(pvtDuration);
            Serial.println(F("ms - I2C may be stuck, recovering..."));
            i2cBusRecovery();
            TwoWire& agtWire = getAGTWire();
            agtWire.begin();
            delay(100);
            agtWire.setClock(100000);
        }

        if (pvtResult) {
            pvtOkCount++;
            gpsFixType = pGNSS->getFixType();
            gpsSats    = pGNSS->getSIV();
            gpsLat     = pGNSS->getLatitude()    / 10000000.0;
            gpsLon     = pGNSS->getLongitude()   / 10000000.0;
            gpsAlt     = pGNSS->getAltitudeMSL() / 1000.0f;
            hasGoodFix = (gpsFixType >= REQUIRED_FIX_TYPE && gpsSats >= REQUIRED_SATS);
        } else {
            pvtFailCount++;
        }
    }
    lastLoopComplete = millis();

    // ---- Periodic GPS status ----
    static unsigned long lastPrint = 0;
    if (now - lastPrint >= 5000) {
        if (gpsInitOK) {
            Serial.print(F("[GPS] Fix="));
            Serial.print(gpsFixType);
            Serial.print(F("  Sats="));
            Serial.print(gpsSats);
            Serial.print(F("  PVT(ok/fail)="));
            Serial.print(pvtOkCount);
            Serial.print(F("/"));
            Serial.print(pvtFailCount);
            if (hasGoodFix) {
                Serial.print(F("  Lat="));
                Serial.print(gpsLat, 6);
                Serial.print(F("  Lon="));
                Serial.print(gpsLon, 6);
                Serial.println(F("  [GOOD]"));
            } else {
                Serial.println(F("  (searching...)"));
            }
        }
        lastPrint = now;
    }

    // ---- Status LEDs + Iridium send logic ----
    if (!hasGoodFix) {
        showStatus(255, 255, 0); // yellow = searching

    } else if (lastCycleTime == 0 || (now - lastCycleTime >= CYCLE_INTERVAL_MS)) {
        // ========== SEND CYCLE ==========
        Serial.println();
        Serial.println(F("========== SEND CYCLE =========="));
        Serial.print(F("[GPS] Fix confirmed  Sats="));
        Serial.println(gpsSats);

        showStatus(0, 255, 0); // green = GPS good
        delay(2000);

        showStatus(255, 0, 255); // magenta = transmitting

        sendCount++;

        // Apollo3 snprintf lacks %f support; format floats via integer parts
        long latInt = (long)gpsLat;
        long latFrac = abs((long)((gpsLat - latInt) * 1000000));
        long lonInt = (long)gpsLon;
        long lonFrac = abs((long)((gpsLon - lonInt) * 1000000));
        long altInt = (long)gpsAlt;

        char msg[200];
        snprintf(msg, sizeof(msg),
                 "SELFTEST #%lu LAT:%ld.%06ld LON:%ld.%06ld ALT:%ld SATS:%d",
                 (unsigned long)sendCount, latInt, latFrac, lonInt, lonFrac, altInt, (int)gpsSats);

        int err = iridiumSend(msg);

        if (err == ISBD_SUCCESS) {
            showStatus(0, 0, 255); // blue = success!
            lastSendOK = true;
            hasSentMessage = true;
            Serial.println(F("[Iridium] >>> SUCCESS <<<"));
        } else {
            showStatus(255, 0, 0); // red = failed
            lastSendOK = false;
            hasSentMessage = true;
            Serial.print(F("[Iridium] FAILED error="));
            Serial.println(err);
        }

        lastCycleTime = millis();
        Serial.print(F("Next cycle in "));
        Serial.print(CYCLE_INTERVAL_MS / 1000);
        Serial.println(F("s"));
        Serial.println(F("================================"));

    } else {
        // Between cycles: show last result
        if (hasSentMessage) {
            if (lastSendOK)
                showStatus(0, 0, 255); // blue
            else
                showStatus(255, 0, 0); // red
        } else {
            showStatus(0, 255, 0); // green
        }
    }

    delay(500);
}
