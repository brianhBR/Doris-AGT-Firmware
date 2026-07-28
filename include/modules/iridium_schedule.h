#ifndef IRIDIUM_SCHEDULE_H
#define IRIDIUM_SCHEDULE_H

#include <stdint.h>

// When to send an Iridium report during RECOVERY.
//
// A position report goes out as soon as there is a fix. Without one the
// vehicle used to stay silent for as long as acquisition took, measured at up
// to 38 minutes after surfacing, so an unlocated report carries state and
// health instead. Repeats of that report are deliberately far apart: Iridium
// and GPS share one antenna, so every session interrupts acquisition and a
// chatty schedule would make the fix take longer still.
struct IridiumSchedule {
    unsigned long lastReportMs;
    bool sentSinceRecovery;
    bool lastReportLocated;
};

// Call on every entry to RECOVERY, whichever path got there.
void IridiumSchedule_reset(IridiumSchedule* s);

// Record a report of either kind.
void IridiumSchedule_noteSent(IridiumSchedule* s, unsigned long now,
                              bool located);

// True when a position report is due, assuming a fix is available.
bool IridiumSchedule_locatedDue(const IridiumSchedule* s, unsigned long now,
                                unsigned long intervalMs);

// True when an unlocated report is due, assuming no fix is available. The
// first one is timed from entry to RECOVERY so a failsafe surfacing reports on
// the same schedule as a normal one.
bool IridiumSchedule_unlocatedDue(const IridiumSchedule* s, unsigned long now,
                                  uint32_t secondsInRecovery);

#endif // IRIDIUM_SCHEDULE_H
