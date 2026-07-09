// anomaly.c — HomeSentinel anomaly detection engine

#include "anomaly.h"
#include "inventory.h"
#include "oui.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "anomaly";

static anomaly_cfg_t s_cfg;

// Rolling baseline of online device count, used for growth detection. We use a
// simple exponential-ish baseline: it tracks the count but reacts slowly, so a
// genuine spike stands out while normal churn doesn't trip it.
static uint32_t s_baseline_online;
static bool     s_baseline_primed;

// ---- Pure helpers (unit-tested) ------------------------------------------
bool anomaly_is_night(uint8_t hour, uint8_t start, uint8_t end)
{
    if (start == end) {
        return false;            // empty window
    }
    if (start < end) {
        return (hour >= start && hour < end);          // same-day window
    }
    // Wrap-around window (e.g. 22 -> 06): night if >= start OR < end.
    return (hour >= start || hour < end);
}

bool anomaly_is_growth(uint32_t baseline, uint32_t current, uint8_t pct)
{
    if (baseline == 0) {
        return false;            // can't compute growth from nothing
    }
    if (current <= baseline) {
        return false;
    }
    // current >= baseline * (1 + pct/100), done in integer math:
    //   current * 100 >= baseline * (100 + pct)
    return ((uint64_t)current * 100) >= ((uint64_t)baseline * (100 + pct));
}

// ---- Init ----------------------------------------------------------------
void anomaly_init(const anomaly_cfg_t *cfg)
{
    if (cfg != NULL) {
        s_cfg = *cfg;
    } else {
        s_cfg.night_start_hour = 0;
        s_cfg.night_end_hour   = 6;
        s_cfg.growth_pct       = 50;
        s_cfg.alert_randomized = false;
    }
    s_baseline_online = 0;
    s_baseline_primed = false;
    ESP_LOGI(TAG, "anomaly engine: night %02u-%02u, growth +%u%%, "
                  "randomized alerts %s",
             s_cfg.night_start_hour, s_cfg.night_end_hour, s_cfg.growth_pct,
             s_cfg.alert_randomized ? "on" : "off");
}

// ---- Helpers -------------------------------------------------------------
static void fmt_mac(const uint8_t mac[6], char *out, size_t n)
{
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---- Main diff -----------------------------------------------------------
void anomaly_run(time_t now)
{
    if (now == 0) {
        now = time(NULL);
    }
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    bool is_night = anomaly_is_night((uint8_t)tm_now.tm_hour,
                                     s_cfg.night_start_hour,
                                     s_cfg.night_end_hour);

    char detail[96];
    char macstr[18];

    inv_lock();

    size_t idx = 0;
    device_t *d;
    while ((d = inv_next(&idx)) != NULL) {

        // --- New device detection -------------------------------------
        // 'announced' is set once we've emitted the new-device event, so we
        // never re-announce. Randomized MACs are recorded but (by default)
        // don't trigger the alert, since phones rotate them constantly.
        if (!d->announced && d->online) {
            bool randomized = d->randomized_mac;
            bool announce = !(randomized && !s_cfg.alert_randomized);

            if (announce) {
                if (d->vendor[0] == '\0') {
                    oui_lookup(d->mac, d->vendor, sizeof(d->vendor));
                }
                snprintf(detail, sizeof(detail), "%.40s (%.30s) %u.%u.%u.%u",
                         d->hostname[0] ? d->hostname : "unnamed",
                         d->vendor[0] ? d->vendor : "unknown vendor",
                         (unsigned)((d->ip >> 24) & 0xFF),
                         (unsigned)((d->ip >> 16) & 0xFF),
                         (unsigned)((d->ip >> 8) & 0xFF),
                         (unsigned)(d->ip & 0xFF));

                inv_log_event(EV_NEW_DEVICE,
                              is_night ? SEV_WARNING : SEV_INFO,
                              d->mac, detail);

                if (is_night) {
                    inv_log_event(EV_NIGHT_ACTIVITY, SEV_WARNING,
                                  d->mac, detail);
                }

                if (!randomized &&
                    (d->vendor[0] == '\0' ||
                     strcmp(d->vendor, "Unknown") == 0)) {
                    fmt_mac(d->mac, macstr, sizeof(macstr));
                    inv_log_event(EV_UNKNOWN_VENDOR, SEV_INFO,
                                  d->mac, macstr);
                }
            }
            // Mark announced regardless: a suppressed randomized MAC shouldn't
            // re-evaluate every pass either.
            d->announced = 1;
            d->was_online = d->online;
            continue;   // a brand-new device can't also be a missing/returned
        }

        // --- Missing / returned ---------------------------------------
        // Compare current online state to the state at the last pass. The
        // transition is the event; steady states are silent.
        if (d->was_online && !d->online) {
            fmt_mac(d->mac, macstr, sizeof(macstr));
            snprintf(detail, sizeof(detail), "%.40s (%.30s)",
                     d->hostname[0] ? d->hostname : macstr,
                     d->vendor[0] ? d->vendor : "unknown");
            inv_log_event(EV_DEVICE_MISSING, SEV_WARNING, d->mac, detail);
        } else if (!d->was_online && d->online) {
            fmt_mac(d->mac, macstr, sizeof(macstr));
            snprintf(detail, sizeof(detail), "%.40s (%.30s)",
                     d->hostname[0] ? d->hostname : macstr,
                     d->vendor[0] ? d->vendor : "unknown");
            inv_log_event(EV_DEVICE_RETURNED, SEV_INFO, d->mac, detail);
        }
        d->was_online = d->online;
    }

    // --- Growth detection (whole-network signal) ----------------------
    uint32_t online_now = inv_count_online();
    if (!s_baseline_primed) {
        s_baseline_online = online_now;
        s_baseline_primed = true;
    } else {
        if (anomaly_is_growth(s_baseline_online, online_now, s_cfg.growth_pct)) {
            snprintf(detail, sizeof(detail),
                     "online devices %u -> %u (+%u%% baseline)",
                     (unsigned)s_baseline_online, (unsigned)online_now,
                     s_cfg.growth_pct);
            inv_log_event(EV_DEVICE_GROWTH, SEV_WARNING, NULL, detail);
        }
        // Slowly track the baseline toward the current count (1/4 step) so it
        // adapts to a genuinely larger network without constant re-alerting.
        s_baseline_online += ((int32_t)online_now - (int32_t)s_baseline_online) / 4;
    }

    inv_unlock();
}
