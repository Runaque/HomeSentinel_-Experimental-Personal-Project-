// discovery.h — HomeSentinel network discovery engine
//
// One scheduled FreeRTOS task performs a full discovery pass over the local
// /24, then sleeps until the next interval. A pass is:
//
//   1. ICMP ping sweep across the subnet (paced, small concurrent batches).
//      A successful ping forces lwIP to populate the ARP cache for that host.
//   2. ARP read: for each responsive host, pull its MAC from the ARP table.
//   3. mDNS query: resolve hostnames for hosts that advertise them.
//
// Results are written into the inventory under inv_lock(). Discovery does NOT
// run the anomaly diff itself — it raises a "scan complete" signal and the
// anomaly engine reacts. This keeps discovery free of policy.
//
// Design notes:
//   - ARP/ping are ONE pass, not two independent techniques: ping primes the
//     cache, ARP reads it. They cannot be separated.
//   - Only the device's own /24 is visible. Other VLANs / guest networks are
//     invisible by design (documented limitation, not a bug).

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Tunables ------------------------------------------------------------
#define DISC_SCAN_INTERVAL_SEC   120   // seconds between full passes
#define DISC_PING_TIMEOUT_MS     1000  // per-host ICMP timeout
#define DISC_PING_BATCH          4     // hosts pinged concurrently
                                       // (kept small: lwIP's socket/raw-PCB
                                       //  pool is shared with Wi-Fi, DHCP, and
                                       //  the HTTP server; a large batch
                                       //  exhausts it and every ping fails
                                       //  with "create socket failed: -1")
#define DISC_MISS_THRESHOLD      3     // missed scans before "offline"

// Signature for the "scan complete" hook. Called (outside any lock) after a
// full pass finishes, so the anomaly engine can run its diff.
typedef void (*disc_scan_done_cb_t)(void);

// Start the discovery task. `netif` is the STA interface (post-connect).
// `on_done` may be NULL. Returns ESP_OK once the task is launched.
esp_err_t discovery_start(esp_netif_t *netif, disc_scan_done_cb_t on_done);

// Trigger an immediate pass (e.g. from a web "scan now" button). Non-blocking;
// ignored if a pass is already running.
void discovery_trigger_now(void);

// Most recent pass duration in milliseconds (for the dashboard).
uint32_t discovery_last_duration_ms(void);

#ifdef __cplusplus
}
#endif
