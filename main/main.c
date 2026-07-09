// main.c — HomeSentinel application entry point
//
// Boot sequence (order matters — each step depends on the previous):
//   1. PSRAM check + inventory (live state lives in PSRAM)
//   2. OUI vendor database (mmap the data partition)
//   3. Wi-Fi STA connect (blocks until we have an IP)
//   4. mDNS init (needs the netif up)
//   5. anomaly + notifier engines
//   6. discovery task, with anomaly_run wired as its scan-done hook
//   7. web server
//
// The scan-done hook is the spine of the runtime: discovery finishes a pass,
// calls back into anomaly which diffs the inventory and logs events, and any
// emitted events get pushed to the notifier.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "mdns.h"

#include "inventory.h"
#include "oui.h"
#include "wifi.h"
#include "discovery.h"
#include "anomaly.h"
#include "notifier.h"
#include "web.h"

static const char *TAG = "main";

static esp_netif_t *s_netif;

// How many events we'd already logged before the current scan, so the hook
// only forwards NEW events to the notifier (not the whole backlog each pass).
static int64_t s_last_notified_ts;

// Wired as discovery's scan-done callback. Runs the anomaly diff, then pushes
// any freshly-logged events to the notifier.
static void on_scan_complete(void)
{
    anomaly_run(0);   // 0 -> use wall-clock time internally

    // Forward newly-created events (ts strictly after the last we forwarded)
    // to the notifier. We copy under lock, notify outside.
    static event_t buf[64];
    size_t n;
    inv_lock();
    n = inv_get_events(buf, 64);
    inv_unlock();

    // Events come back newest-first; walk oldest-first so Discord order is
    // chronological, forwarding only those newer than the last we sent.
    int64_t newest_seen = s_last_notified_ts;
    for (size_t i = n; i > 0; i--) {
        event_t *e = &buf[i - 1];
        if (e->ts > s_last_notified_ts) {
            notifier_notify(e);
            if (e->ts > newest_seen) {
                newest_seen = e->ts;
            }
        }
    }
    s_last_notified_ts = newest_seen;
}

void app_main(void)
{
    ESP_LOGI(TAG, "HomeSentinel starting");

    // --- 1. PSRAM sanity + inventory --------------------------------------
    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM total: %u bytes", (unsigned)psram);
    if (psram == 0) {
        ESP_LOGE(TAG, "No PSRAM detected! Check SPIRAM_MODE_OCT for N16R8. "
                      "Halting.");
        return;   // inventory_init would fail anyway; fail loudly here.
    }
    ESP_ERROR_CHECK(inventory_init());

    // --- 2. OUI vendor database -------------------------------------------
    // Non-fatal if missing: lookups just return "Unknown".
    esp_err_t oui_err = oui_init();
    if (oui_err != ESP_OK) {
        ESP_LOGW(TAG, "OUI db unavailable; vendors will show as Unknown");
    }

    // --- 3. Wi-Fi (blocks for IP) -----------------------------------------
    if (wifi_connect_blocking(&s_netif) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed; restarting in 10s");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    // --- 4. mDNS ----------------------------------------------------------
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("homesentinel");
    mdns_instance_name_set("HomeSentinel");

    // --- 5. anomaly + notifier --------------------------------------------
    anomaly_init(NULL);    // defaults: night 00-06, growth +50%, no rand alerts
    notifier_init();

    // --- 6. discovery (hooks anomaly via on_scan_complete) ----------------
    ESP_ERROR_CHECK(discovery_start(s_netif, on_scan_complete));

    // --- 7. web server ----------------------------------------------------
    ESP_ERROR_CHECK(web_start());

    ESP_LOGI(TAG, "HomeSentinel up. Dashboard at http://homesentinel.local/");

    // app_main can return; FreeRTOS tasks (discovery, web, notifier) keep
    // running. Nothing left to do on this stack.
}
