// Platform glue for MAVLink convenience functions
#pragma once
#include "config.h"

// Provide a MAVLINK_SEND_UART_BYTES macro so MAVLink will call this
// to send a whole packet. This maps to our `MAVLINK_SERIAL` defined
// in `include/config.h`.
#ifndef MAVLINK_SEND_UART_BYTES
#define MAVLINK_SEND_UART_BYTES(chan, buf, len) (MAVLINK_SERIAL.write((const uint8_t*)(buf), (len)))
#endif

// Provide a default `mavlink_system` structure expected by MAVLink helpers
// This is a minimal object with the fields used by the header functions.
// Initialize to zeros here; the real values will be set in runtime init
// after MAVLink headers are available.
static struct { uint8_t sysid; uint8_t compid; } mavlink_system = { 0, 0 };
