// oui.c — HomeSentinel vendor (OUI) lookup

#include "oui.h"

#include <string.h>
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "oui";

// We memory-map the partition so we can binary-search it directly without
// pulling the whole blob into RAM. mmap gives us a const pointer into flash.
static const uint8_t *s_blob;        // mapped base
static uint32_t       s_count;       // record count
static const uint8_t *s_records;     // first record (s_blob + 8)
static esp_partition_mmap_handle_t s_map;

// One record as laid out in the blob (see oui.h). Packed so it matches the
// 32-byte on-flash layout exactly regardless of compiler padding.
typedef struct __attribute__((packed)) {
    uint8_t oui24[3];
    uint8_t reserved;
    char    vendor[OUI_VENDOR_LEN];
} oui_record_t;

_Static_assert(sizeof(oui_record_t) == OUI_RECORD_SIZE,
               "OUI record must be exactly 32 bytes");

esp_err_t oui_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x40 /* custom subtype */, "oui");
    if (part == NULL) {
        ESP_LOGW(TAG, "oui partition not found; vendor lookup disabled");
        return ESP_ERR_NOT_FOUND;
    }

    const void *ptr = NULL;
    esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                       ESP_PARTITION_MMAP_DATA, &ptr, &s_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err));
        return err;
    }

    s_blob = (const uint8_t *)ptr;

    // Validate header: 4-byte magic + 4-byte count.
    if (memcmp(s_blob, OUI_MAGIC, 4) != 0) {
        ESP_LOGW(TAG, "oui blob magic mismatch; not flashed?");
        s_blob = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(&s_count, s_blob + 4, sizeof(uint32_t));
    s_records = s_blob + 8;

    ESP_LOGI(TAG, "oui ready: %u vendor records", (unsigned)s_count);
    return ESP_OK;
}

// Compare a 3-byte OUI key against a record's oui24 (both big-endian bytes).
static int oui_cmp(const uint8_t key[3], const uint8_t rec[3])
{
    return memcmp(key, rec, 3);
}

bool oui_lookup(const uint8_t mac[6], char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }

    // Locally-administered (randomized) MACs have no real vendor. Flag them
    // explicitly rather than reporting a misleading or empty vendor.
    if (mac[0] & 0x02) {
        strlcpy(out, "Randomized (private)", out_len);
        return false;
    }

    if (s_blob == NULL || s_count == 0) {
        strlcpy(out, "Unknown", out_len);
        return false;
    }

    const uint8_t key[3] = { mac[0], mac[1], mac[2] };

    // Binary search over fixed-size sorted records.
    int32_t lo = 0;
    int32_t hi = (int32_t)s_count - 1;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        const oui_record_t *rec =
            (const oui_record_t *)(s_records + (size_t)mid * OUI_RECORD_SIZE);
        int c = oui_cmp(key, rec->oui24);
        if (c == 0) {
            // vendor is NUL-padded; strlcpy stops at the first NUL or out_len.
            strlcpy(out, rec->vendor, out_len);
            // Guard against a non-terminated 28-byte field.
            out[out_len - 1] = '\0';
            return true;
        } else if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    strlcpy(out, "Unknown", out_len);
    return false;
}
