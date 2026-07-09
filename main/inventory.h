// inventory.h — HomeSentinel device inventory (PSRAM-backed)
//
// The inventory is the single source of truth at runtime. It holds:
//   - a fixed-capacity table of known devices (live state)
//   - a ring buffer of recent events (the timeline)
//
// Everything lives in PSRAM and is allocated once at init. No per-device
// malloc/free, so the heap never fragments across long uptimes. Persistence
// (LittleFS snapshots) reads from this; the web layer serializes from this;
// the anomaly engine diffs against this.
//
// Concurrency: discovery runs in its own task; the HTTP server runs in
// another. All access goes through inv_lock()/inv_unlock() (a mutex). Keep
// critical sections short — copy out what you need, then release.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Capacities ----------------------------------------------------------
// A home /24 rarely exceeds ~120 active hosts. 256 gives generous headroom
// while keeping the table small enough to snapshot cheaply.
#define INV_MAX_DEVICES        256
#define INV_EVENT_RING_SIZE    512   // recent events kept in RAM

#define INV_HOSTNAME_LEN       64
#define INV_VENDOR_LEN         48
#define INV_NOTES_LEN          64

// ---- Device classification ----------------------------------------------
typedef enum {
    DEV_CLASS_UNKNOWN = 0,
    DEV_CLASS_COMPUTER,
    DEV_CLASS_MOBILE,
    DEV_CLASS_IOT,
    DEV_CLASS_INFRA,        // router, AP, switch
} dev_class_t;

// ---- Device record -------------------------------------------------------
// 6-byte MAC is the primary key. We keep IP as a 32-bit value (host order)
// for compact storage; format to dotted-quad only at the edges.
typedef struct {
    uint8_t  mac[6];
    uint8_t  in_use;                    // slot occupied?
    uint8_t  randomized_mac;            // locally-administered bit set?
    uint32_t ip;                        // IPv4, host byte order
    char     hostname[INV_HOSTNAME_LEN];
    char     vendor[INV_VENDOR_LEN];
    char     notes[INV_NOTES_LEN];      // user-editable
    dev_class_t dev_class;
    uint8_t  known;                     // user has acknowledged this device
    uint8_t  announced;                 // "new device" event already emitted
    uint8_t  online;                    // seen in the most recent scan?
    uint8_t  was_online;                // online state at last anomaly pass
    int64_t  first_seen;                // epoch seconds
    int64_t  last_seen;                 // epoch seconds
    uint32_t miss_count;                // consecutive scans missed
} device_t;

// ---- Event log -----------------------------------------------------------
typedef enum {
    EV_NONE = 0,
    EV_NEW_DEVICE,
    EV_DEVICE_MISSING,
    EV_DEVICE_RETURNED,
    EV_HOSTNAME_CHANGED,
    EV_IP_CHANGED,
    EV_UNKNOWN_VENDOR,
    EV_DEVICE_GROWTH,        // device count spiked
    EV_NIGHT_ACTIVITY,       // new device during quiet hours
} event_type_t;

typedef enum {
    SEV_INFO = 0,
    SEV_WARNING,
    SEV_CRITICAL,
} severity_t;

typedef struct {
    int64_t      ts;             // epoch seconds
    event_type_t type;
    severity_t   severity;
    uint8_t      mac[6];         // device this event concerns (if any)
    char         detail[96];     // human-readable, e.g. "DESKTOP-A -> DESKTOP-B"
} event_t;

// ---- Lifecycle -----------------------------------------------------------
// Allocates all tables in PSRAM. Call once after PSRAM is up. Returns
// ESP_ERR_NO_MEM if PSRAM allocation fails (e.g. octal mode misconfigured).
esp_err_t inventory_init(void);

// ---- Locking -------------------------------------------------------------
void inv_lock(void);
void inv_unlock(void);

// ---- Device access (call under lock) -------------------------------------
// Find a device by MAC. Returns NULL if not present.
device_t *inv_find(const uint8_t mac[6]);

// Find an existing device by MAC, or allocate a fresh slot for it.
// Returns NULL only if the table is full. New slots come back with
// in_use=1 and first_seen unset (caller fills the rest).
device_t *inv_find_or_add(const uint8_t mac[6]);

// Iterate occupied slots. Pass *idx = 0 to start; returns NULL at the end.
device_t *inv_next(size_t *idx);

size_t inv_count_total(void);    // occupied slots
size_t inv_count_online(void);   // online == 1

// ---- Event log (call under lock) -----------------------------------------
// Append an event to the ring buffer (oldest is overwritten when full).
void inv_log_event(event_type_t type, severity_t sev,
                   const uint8_t mac[6], const char *detail);

// Copy up to max events, newest first, into out[]. Returns count copied.
size_t inv_get_events(event_t *out, size_t max);

// ---- Helpers -------------------------------------------------------------
// True if the MAC's locally-administered bit is set (randomized MAC).
bool mac_is_randomized(const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
