/*
 * Meshtastic RAK4603 - GPS output as NMEA to J10 (external GPS UART)
 *
 * The AGT outputs standard NMEA 0183 sentences (GGA, RMC) on the serial
 * connected to Meshtastic's J10. J10 is UART1 on the RAK board - the same
 * port used for an external GPS module. Configure Meshtastic to use
 * external GPS on that port; it will read NMEA and use the position.
 *
 * Wiring: AGT TX (e.g. D39) -> Meshtastic J10 RX (UART1 RX)
 *        AGT RX (e.g. D40) -> Meshtastic J10 TX if you need replies (optional)
 * Baud: 4800 (standard NMEA)
 */

#include "modules/meshtastic_interface.h"
#include "config.h"
#include "utils/SoftwareSerial.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

SoftwareSerial* MeshtasticSerial = nullptr;
static bool initialized = false;

// Non-blocking TX buffer: NMEA lines are queued here and drained
// a few bytes at a time in update() to avoid blocking the main loop.
#define MESH_TX_BUF_SIZE 320
static uint8_t txBuf[MESH_TX_BUF_SIZE];
static volatile uint16_t txHead = 0;
static volatile uint16_t txTail = 0;

static uint16_t txBufUsed() {
    return (txHead >= txTail) ? (txHead - txTail) : (MESH_TX_BUF_SIZE - txTail + txHead);
}

static void txBufEnqueue(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next = (txHead + 1) % MESH_TX_BUF_SIZE;
        if (next == txTail) return;  // full — drop
        txBuf[txHead] = data[i];
        txHead = next;
    }
}

static void nmea_checksum(const char* sentence, char* outHex) {
    uint8_t cs = 0;
    for (const char* p = sentence; *p; p++)
        cs ^= (uint8_t)*p;
    snprintf(outHex, 4, "%02X", (unsigned)cs);
}

// Format latitude as ddmm.mmmm,N/S (NMEA format, no %f)
static void format_lat(double lat, char* buf, size_t len) {
    char ns = (lat >= 0) ? 'N' : 'S';
    if (lat < 0) lat = -lat;
    int deg = (int)lat;
    double minFloat = (lat - deg) * 60.0;
    int minInt = (int)minFloat;
    long minFrac = (long)((minFloat - minInt) * 10000 + 0.5);
    if (minFrac >= 10000) { minFrac -= 10000; minInt++; }
    snprintf(buf, len, "%02d%02d.%04ld,%c", deg, minInt, minFrac, ns);
}

// Format longitude as dddmm.mmmm,E/W (NMEA format, no %f)
static void format_lon(double lon, char* buf, size_t len) {
    char ew = (lon >= 0) ? 'E' : 'W';
    if (lon < 0) lon = -lon;
    int deg = (int)lon;
    double minFloat = (lon - deg) * 60.0;
    int minInt = (int)minFloat;
    long minFrac = (long)((minFloat - minInt) * 10000 + 0.5);
    if (minFrac >= 10000) { minFrac -= 10000; minInt++; }
    snprintf(buf, len, "%03d%02d.%04ld,%c", deg, minInt, minFrac, ew);
}

// Append a float as "int.frac" into buf at position pos (no %f)
static int appendFixedPoint(char* buf, int pos, int maxLen, double val, int decimals) {
    long intPart = (long)val;
    double fracVal = val - intPart;
    if (fracVal < 0) fracVal = -fracVal;
    long multiplier = 1;
    for (int i = 0; i < decimals; i++) multiplier *= 10;
    long fracPart = (long)(fracVal * multiplier + 0.5);
    if (fracPart >= multiplier) { fracPart = 0; intPart += (val >= 0) ? 1 : -1; }

    int written;
    if (decimals == 1)
        written = snprintf(buf + pos, maxLen - pos, "%ld.%01ld", intPart, fracPart);
    else if (decimals == 2)
        written = snprintf(buf + pos, maxLen - pos, "%ld.%02ld", intPart, fracPart);
    else
        written = snprintf(buf + pos, maxLen - pos, "%ld.%04ld", intPart, fracPart);
    return (written > 0) ? pos + written : pos;
}

// Queue an NMEA sentence into the TX buffer for non-blocking drain.
#define NMEA_LINE_MAX 140
static void send_nmea_line(const char* body) {
    if (!MeshtasticSerial || !initialized) return;
    char csHex[4];
    nmea_checksum(body, csHex);
    char line[NMEA_LINE_MAX];
    size_t bodyLen = strlen(body);
    size_t n = 0;
    line[n++] = '$';
    if (n + bodyLen >= NMEA_LINE_MAX - 6) return;
    memcpy(line + n, body, bodyLen);
    n += bodyLen;
    line[n++] = '*';
    line[n++] = csHex[0];
    line[n++] = csHex[1];
    line[n++] = '\r';
    line[n++] = '\n';
    txBufEnqueue((const uint8_t*)line, n);
}

bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized || MeshtasticSerial == nullptr) return false;
    if (!gpsData->valid) return false;

    char latStr[16], lonStr[16];
    format_lat(gpsData->latitude, latStr, sizeof(latStr));
    format_lon(gpsData->longitude, lonStr, sizeof(lonStr));

    uint8_t h = gpsData->hour, m = gpsData->minute;
    uint8_t s = (uint8_t)gpsData->second;
    uint16_t yr = gpsData->year;
    if (yr >= 100) yr -= 2000;
    uint8_t mo = gpsData->month, d = gpsData->day;

    // $GPGGA — Apollo3 snprintf lacks %f, build with integer math
    char gga[128];
    int pos = snprintf(gga, sizeof(gga), "GPGGA,%02u%02u%02u.00,%s,%s,1,%02u,",
                       (unsigned)h, (unsigned)m, (unsigned)s,
                       latStr, lonStr, (unsigned)gpsData->satellites);
    pos = appendFixedPoint(gga, pos, sizeof(gga), gpsData->hdop, 1);
    pos += snprintf(gga + pos, sizeof(gga) - pos, ",");
    pos = appendFixedPoint(gga, pos, sizeof(gga), gpsData->altitude, 1);
    snprintf(gga + pos, sizeof(gga) - pos, ",M,0.0,M,,,");
    send_nmea_line(gga);

    // $GPRMC
    float speedKnots = gpsData->speed * 0.539957f;
    char rmc[128];
    pos = snprintf(rmc, sizeof(rmc), "GPRMC,%02u%02u%02u.00,A,%s,%s,",
                   (unsigned)h, (unsigned)m, (unsigned)s,
                   latStr, lonStr);
    pos = appendFixedPoint(rmc, pos, sizeof(rmc), speedKnots, 1);
    pos += snprintf(rmc + pos, sizeof(rmc) - pos, ",");
    pos = appendFixedPoint(rmc, pos, sizeof(rmc), gpsData->course, 1);
    snprintf(rmc + pos, sizeof(rmc) - pos, ",%02u%02u%02u,,,A",
             (unsigned)d, (unsigned)mo, (unsigned)(yr % 100));
    send_nmea_line(rmc);
    return true;
}

void MeshtasticInterface_sendNoFixNMEA() {
    if (!MeshtasticSerial || !initialized) return;
    send_nmea_line("GPGGA,000000.00,,,,,0,00,99.9,,,,,,,");
}

void MeshtasticInterface_sendTestNMEA() {
    if (!MeshtasticSerial || !initialized) return;
    send_nmea_line("GPGGA,120000.00,3742.1445,N,12205.0466,W,1,10,0.9,15.2,M,0.0,M,,");
    send_nmea_line("GPRMC,120000.00,A,3742.1445,N,12205.0466,W,0.0,0.0,150326,,,A");
    DebugPrintln(F("Mesh: sent test NMEA (37.7024, -122.0841)"));
}

bool MeshtasticInterface_sendText(const char* message) {
    (void)message;
    if (!initialized || MeshtasticSerial == nullptr) return false;
    return false;
}

bool MeshtasticInterface_sendTelemetry(float voltage, float current) {
    (void)voltage;
    (void)current;
    return true;
}

bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState) {
    (void)stateName;
    (void)timeInState;
    return false;
}

bool MeshtasticInterface_sendAlert(const char* alertMessage) {
    (void)alertMessage;
    return false;
}

bool MeshtasticInterface_checkMessages() {
    return false;
}

void MeshtasticInterface_update() {
    if (!MeshtasticSerial || !initialized) return;

    // Drain up to 4 bytes per call (~4ms at 9600 baud).
    // At 50Hz LED refresh the main loop runs every ~20ms,
    // so this leaves plenty of headroom for smooth animation.
    uint8_t maxBytes = 4;
    while (maxBytes-- && txTail != txHead) {
        MeshtasticSerial->write(txBuf[txTail]);
        txTail = (txTail + 1) % MESH_TX_BUF_SIZE;
    }
}

void MeshtasticInterface_init() {
    MeshtasticSerial = new SoftwareSerial(MESHTASTIC_RX_PIN, MESHTASTIC_TX_PIN);
    MeshtasticSerial->begin(MESHTASTIC_BAUD);
    delay(300);
    initialized = true;
    DebugPrintln(F("Meshtastic: NMEA GPS output -> J10 (external GPS UART)"));
}
