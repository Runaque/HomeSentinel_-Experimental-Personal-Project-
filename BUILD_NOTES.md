# Build Notes — verified vs. watch-points

This is an honest account of what's been validated and what to check at first
build, written so you're not surprised. The firmware is **complete and
logic-verified** — every algorithm with edge cases was compiled and unit-tested
on a host machine. It has **not** been compiled against real ESP-IDF or flashed
to hardware (that wasn't possible in the environment it was written in), so the
first `idf.py build` is yours and will likely surface a few fix-ups.

## Host-verified (unit-tested, passing)

- **Inventory ring buffer** — wraparound, newest-first ordering, overflow
  retention. (5/5 tests)
- **Randomized-MAC detection** — locally-administered bit on real Apple OUI vs.
  randomized MAC. (verified)
- **Subnet enumeration + ping batch pacing** — /24 and /22-clamped host ranges,
  batch counts. (verified)
- **IP byte-order round trip** — host-order storage vs. network-order lwIP
  lookups. (verified)
- **mDNS apply-by-IP matching** — name fill, class-preservation, no-clobber of
  user-set fields. (4/4 tests)
- **OUI binary search** — the Python generator and C lookup were tested against
  the *same* generated blob: hit, miss, randomized, boundary cases. (8/8 tests)
- **Anomaly logic** — night-window midnight wrap, integer growth threshold,
  new/missing/returned state machine, randomized suppression. (27/27 tests)
- **JSON escaper** — quotes, backslashes, newlines, control-char stripping,
  truncation safety. (verified)
- **Notifier dedup** — watermark logic that prevents duplicate Discord alerts
  across scans. (4/4 tests)

## Watch-points at first compile

These are the spots most likely to need a small fix. None are deep — they're
the known cost of writing against docs rather than a live toolchain.

1. **`ESP_IPADDR_TYPE_V4` in `discovery.c`** (mDNS result IP-family check).
   Correct for the current separated mDNS component, but if it errors at
   compile, the fix is the lwIP-style spelling `IPADDR_TYPE_V4`. One word.

2. **`crt_bundle_attach` in `notifier.c`** is intentionally `NULL`. Discord's
   webhook is HTTPS and needs cert verification. To enable: add
   `#include "esp_crt_bundle.h"`, set `.crt_bundle_attach = esp_crt_bundle_attach`,
   and enable `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` in menuconfig. Left explicit
   so it's a deliberate choice, not a silent insecure default.

3. **`esp_partition_mmap` signature** (`oui.c`). The handle-based form
   (`esp_partition_mmap_handle_t`) is current for v5.x; confirm the arg order
   against your exact IDF point release if it complains.

4. **`esp_crypto_base64_encode`** (`web.c`, from `esp_tls_crypto.h`) — used for
   the Basic-auth comparison string. Confirm the header is pulled in by the
   `esp-tls` dependency; if not, add `esp-tls` to the `REQUIRES` list in
   `main/CMakeLists.txt`.

5. **mDNS pass duration.** `resolve_hostnames()` browses 10 service types at
   1.5s each — up to ~15s added to a pass *only when a new unnamed device
   appeared* (it early-outs otherwise). If that's too long for your taste, trim
   `MDNS_PROBES[]` or lower `MDNS_QUERY_TIMEOUT_MS`.

6. **First-boot LittleFS.** The `storage` partition needs formatting on first
   boot; the LittleFS component handles this with `format_if_mount_failed`, but
   persistence wiring (snapshot read/write) is scaffolded, not yet implemented —
   the inventory works fully in RAM; snapshot-to-flash is the one feature stub
   remaining. See roadmap.

## Roadmap (intentionally deferred)

- LittleFS snapshot persistence (inventory survives reboot)
- Hostname-change / IP-change event emission (data model supports it; the
  discovery diff doesn't emit these two yet)
- Web UI device detail page + user notes editing (API is read-only today)
- OTA updates
- ESP-IDF v6.0 compatibility pass
