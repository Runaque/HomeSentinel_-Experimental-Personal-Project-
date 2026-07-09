// inventory.c — HomeSentinel device inventory (PSRAM-backed)

#include "inventory.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "inventory";

// ---- Module state (all PSRAM-resident) -----------------------------------
static device_t *s_devices;          // [INV_MAX_DEVICES]
static event_t  *s_events;           // [INV_EVENT_RING_SIZE], ring buffer
static size_t    s_event_head;       // next write position
static size_t    s_event_count;      // valid entries (<= ring size)

static SemaphoreHandle_t s_lock;

// ---- Helpers -------------------------------------------------------------
bool mac_is_randomized(const uint8_t mac[6])
{
    // Locally-administered bit is bit 1 of the first octet. iOS/Android set
    // it for per-network private MACs. Globally-unique vendor MACs clear it.
    return (mac[0] & 0x02) != 0;
}

static inline int64_t now_seconds(void)
{
    return (int64_t)(esp_timer_get_time() / 1000000);
}

static inline bool mac_eq(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

// ---- Lifecycle -----------------------------------------------------------
esp_err_t inventory_init(void)
{
    // Place the big tables in PSRAM explicitly. If this returns NULL, PSRAM
    // almost certainly isn't initialized — check SPIRAM_MODE_OCT for N16R8.
    s_devices = heap_caps_calloc(INV_MAX_DEVICES, sizeof(device_t),
                                 MALLOC_CAP_SPIRAM);
    s_events  = heap_caps_calloc(INV_EVENT_RING_SIZE, sizeof(event_t),
                                 MALLOC_CAP_SPIRAM);

    if (s_devices == NULL || s_events == NULL) {
        ESP_LOGE(TAG, "PSRAM alloc failed (devices=%p events=%p). "
                      "Is octal PSRAM enabled?", s_devices, s_events);
        return ESP_ERR_NO_MEM;
    }

    s_event_head  = 0;
    s_event_count = 0;

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "inventory ready: %d device slots, %d event ring "
                  "(%u bytes PSRAM)",
             INV_MAX_DEVICES, INV_EVENT_RING_SIZE,
             (unsigned)(INV_MAX_DEVICES * sizeof(device_t) +
                        INV_EVENT_RING_SIZE * sizeof(event_t)));
    return ESP_OK;
}

// ---- Locking -------------------------------------------------------------
void inv_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
void inv_unlock(void) { xSemaphoreGive(s_lock); }

// ---- Device access -------------------------------------------------------
device_t *inv_find(const uint8_t mac[6])
{
    for (size_t i = 0; i < INV_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && mac_eq(s_devices[i].mac, mac)) {
            return &s_devices[i];
        }
    }
    return NULL;
}

device_t *inv_find_or_add(const uint8_t mac[6])
{
    device_t *d = inv_find(mac);
    if (d != NULL) {
        return d;
    }
    // Claim the first free slot.
    for (size_t i = 0; i < INV_MAX_DEVICES; i++) {
        if (!s_devices[i].in_use) {
            device_t *slot = &s_devices[i];
            memset(slot, 0, sizeof(*slot));
            memcpy(slot->mac, mac, 6);
            slot->in_use = 1;
            slot->randomized_mac = mac_is_randomized(mac) ? 1 : 0;
            return slot;
        }
    }
    ESP_LOGW(TAG, "device table full (%d), dropping new MAC", INV_MAX_DEVICES);
    return NULL;
}

device_t *inv_next(size_t *idx)
{
    for (size_t i = *idx; i < INV_MAX_DEVICES; i++) {
        if (s_devices[i].in_use) {
            *idx = i + 1;
            return &s_devices[i];
        }
    }
    return NULL;
}

size_t inv_count_total(void)
{
    size_t n = 0;
    for (size_t i = 0; i < INV_MAX_DEVICES; i++) {
        if (s_devices[i].in_use) n++;
    }
    return n;
}

size_t inv_count_online(void)
{
    size_t n = 0;
    for (size_t i = 0; i < INV_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && s_devices[i].online) n++;
    }
    return n;
}

// ---- Event log -----------------------------------------------------------
void inv_log_event(event_type_t type, severity_t sev,
                   const uint8_t mac[6], const char *detail)
{
    event_t *e = &s_events[s_event_head];
    memset(e, 0, sizeof(*e));
    e->ts       = now_seconds();
    e->type     = type;
    e->severity = sev;
    if (mac != NULL) {
        memcpy(e->mac, mac, 6);
    }
    if (detail != NULL) {
        strlcpy(e->detail, detail, sizeof(e->detail));
    }

    s_event_head = (s_event_head + 1) % INV_EVENT_RING_SIZE;
    if (s_event_count < INV_EVENT_RING_SIZE) {
        s_event_count++;
    }
}

size_t inv_get_events(event_t *out, size_t max)
{
    size_t n = (max < s_event_count) ? max : s_event_count;
    // Walk backwards from the most recent write so out[0] is newest.
    for (size_t i = 0; i < n; i++) {
        size_t pos = (s_event_head + INV_EVENT_RING_SIZE - 1 - i)
                     % INV_EVENT_RING_SIZE;
        out[i] = s_events[pos];
    }
    return n;
}
