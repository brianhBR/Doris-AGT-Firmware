#include "modules/iridium_schedule.h"
#include "config.h"

void IridiumSchedule_reset(IridiumSchedule* s) {
    s->lastAttemptMs = 0;
    s->attemptedSinceRecovery = false;
    s->sentSinceRecovery = false;
    s->locatedUpgradePending = false;
}

void IridiumSchedule_noteAttempt(IridiumSchedule* s, unsigned long now,
                                 bool located) {
    s->lastAttemptMs = now;
    s->attemptedSinceRecovery = true;
    if (located) {
        s->locatedUpgradePending = false;
    }
}

void IridiumSchedule_noteSent(IridiumSchedule* s, unsigned long now,
                              bool located) {
    IridiumSchedule_noteAttempt(s, now, located);
    s->sentSinceRecovery = true;
    s->locatedUpgradePending = !located;
}

bool IridiumSchedule_locatedDue(const IridiumSchedule* s, unsigned long now,
                                unsigned long intervalMs) {
    if (!s->attemptedSinceRecovery) return true;
    // A fix arriving after an unlocated report upgrades it straight away
    // rather than waiting out the interval; the position is the whole point of
    // the report and the operator has so far only been told the vehicle is up.
    if (s->locatedUpgradePending) return true;
    return now - s->lastAttemptMs >= intervalMs;
}

bool IridiumSchedule_unlocatedDue(const IridiumSchedule* s, unsigned long now,
                                  unsigned long intervalMs) {
    if (!s->attemptedSinceRecovery) return true;
    return now - s->lastAttemptMs >= intervalMs;
}
