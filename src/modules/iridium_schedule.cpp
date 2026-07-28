#include "modules/iridium_schedule.h"
#include "config.h"

void IridiumSchedule_reset(IridiumSchedule* s) {
    s->lastReportMs = 0;
    s->sentSinceRecovery = false;
    s->lastReportLocated = true;
}

void IridiumSchedule_noteSent(IridiumSchedule* s, unsigned long now,
                              bool located) {
    s->lastReportMs = now;
    s->sentSinceRecovery = true;
    s->lastReportLocated = located;
}

bool IridiumSchedule_locatedDue(const IridiumSchedule* s, unsigned long now,
                                unsigned long intervalMs) {
    if (!s->sentSinceRecovery) return true;
    // A fix arriving after an unlocated report upgrades it straight away
    // rather than waiting out the interval; the position is the whole point of
    // the report and the operator has so far only been told the vehicle is up.
    if (!s->lastReportLocated) return true;
    return now - s->lastReportMs >= intervalMs;
}

bool IridiumSchedule_unlocatedDue(const IridiumSchedule* s, unsigned long now,
                                  uint32_t secondsInRecovery) {
    if (secondsInRecovery < IRIDIUM_NOFIX_FIRST_MS / 1000UL) return false;
    if (!s->sentSinceRecovery) return true;
    return now - s->lastReportMs >= IRIDIUM_NOFIX_REPEAT_MS;
}
