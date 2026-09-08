#ifndef IRIDIUM_SCHEDULE_H
#define IRIDIUM_SCHEDULE_H

#include <stdint.h>

// When to send an Iridium report during RECOVERY.
//
// The first report goes out as soon as RECOVERY transmission is allowed,
// whether or not a position is available. Successful reports then use the
// configured interval. A failed session retries after IRIDIUM_RETRY_BACKOFF_MS.
// A fix arriving after an unlocated report upgrades it immediately.
struct IridiumSchedule {
    unsigned long lastReportMs;
    unsigned long lastAttemptMs;
    bool sentSinceRecovery;
    bool lastReportLocated;
};

// Call on every entry to RECOVERY, whichever path got there.
void IridiumSchedule_reset(IridiumSchedule* s);

// Record a successful report of either kind.
void IridiumSchedule_noteSent(IridiumSchedule* s, unsigned long now,
                              bool located);

// Record a failed session so the next retry waits IRIDIUM_RETRY_BACKOFF_MS.
void IridiumSchedule_noteFailed(IridiumSchedule* s, unsigned long now);

// True when a position report is due, assuming a fix is available.
bool IridiumSchedule_locatedDue(const IridiumSchedule* s, unsigned long now,
                                unsigned long intervalMs);

// True when an unlocated report is due, assuming no fix is available.
bool IridiumSchedule_unlocatedDue(const IridiumSchedule* s, unsigned long now,
                                  unsigned long intervalMs);

#endif // IRIDIUM_SCHEDULE_H
