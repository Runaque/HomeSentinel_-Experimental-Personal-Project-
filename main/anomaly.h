// anomaly.h — HomeSentinel anomaly detection engine
//
// Runs once after each discovery pass (wired as discovery's scan-done hook).
// It diffs the current inventory against remembered baselines and emits events
// into the inventory's event log. Discovery stays policy-free; all "is this
// worth alerting on?" logic lives here.
//
// Detections:
//   - New device              (first time we've ever seen this MAC)
//   - Device missing/returned  (miss_count crossing the offline threshold)
//   - Unknown vendor           (globally-unique MAC with no OUI match)
//   - Excessive device growth  (online count jumped vs rolling baseline)
//   - Night activity           (new device during user-configured quiet hours)
//
// Randomized MACs are handled specially: a brand-new randomized MAC is NOT a
// "new device" alert by default (phones rotate them constantly). This is the
// single biggest false-positive source and we suppress it at the source.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tunables (could be surfaced in the web UI later).
typedef struct {
    uint8_t  night_start_hour;   // local hour [0..23], inclusive
    uint8_t  night_end_hour;     // local hour [0..23], exclusive
    uint8_t  growth_pct;         // alert if online count grows by >= this %
    bool     alert_randomized;   // treat new randomized MACs as new devices?
} anomaly_cfg_t;

// Sensible defaults: quiet hours 00:00–06:00, growth alert at +50%, randomized
// MACs suppressed.
void anomaly_init(const anomaly_cfg_t *cfg /* NULL = defaults */);

// Run a full diff against the current inventory. Call after each scan. `now`
// is the current wall-clock time (for night-window checks); pass 0 to use
// time(NULL) internally.
void anomaly_run(time_t now);

// ---- Exposed for unit testing (pure helpers, no inventory access) --------
// True if `hour` falls within [start, end) treating wrap-around midnight.
bool anomaly_is_night(uint8_t hour, uint8_t start, uint8_t end);

// Given previous baseline and current count, returns true if growth >= pct.
bool anomaly_is_growth(uint32_t baseline, uint32_t current, uint8_t pct);

#ifdef __cplusplus
}
#endif
