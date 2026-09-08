#include "modules/iridium_schedule.h"
#include "config.h"

void IridiumSchedule_reset(IridiumSchedule* s) {
    s->lastReportMs = 0;
    s->lastAttemptMs = 0;
    s->sentSinceRecovery = false;
    s->lastReportLocated = true;
}

void IridiumSchedule_noteSent(IridiumSchedule* s, unsigned long now,
                              bool located) {
    s->lastReportMs = now;
    s->lastAttemptMs = now;
    s->sentSinceRecovery = true;
    s->lastReportLocated = located;
}

void IridiumSchedule_noteFailed(IridiumSchedule* s, unsigned long now) {
    s->lastAttemptMs = now;
}

static bool retryBackoffElapsed(const IridiumSchedule* s, unsigned long now) {
    if (s->lastAttemptMs == 0) return true;
    return now - s->lastAttemptMs >= IRIDIUM_RETRY_BACKOFF_MS;
}

bool IridiumSchedule_locatedDue(const IridiumSchedule* s, unsigned long now,
                                unsigned long intervalMs) {
    if (!s->sentSinceRecovery) return retryBackoffElapsed(s, now);
    // A fix arriving after an unlocated report upgrades it straight away
    // rather than waiting out the interval; the position is the whole point of
    // the report and the operator has so far only been told the vehicle is up.
    if (!s->lastReportLocated) return true;
    return now - s->lastReportMs >= intervalMs;
}

bool IridiumSchedule_unlocatedDue(const IridiumSchedule* s, unsigned long now,
                                  unsigned long intervalMs) {
    if (!s->sentSinceRecovery) return retryBackoffElapsed(s, now);
    return now - s->lastReportMs >= intervalMs;
}
