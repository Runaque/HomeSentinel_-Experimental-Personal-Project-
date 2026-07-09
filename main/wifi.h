// wifi.h — HomeSentinel Wi-Fi station connection
//
// Brings up the STA interface, connects to the configured AP, and blocks until
// an IP is acquired (or fails). Returns the esp_netif so discovery knows which
// interface to scan. Credentials come from menuconfig (CONFIG_HS_WIFI_*), never
// hardcoded — see Kconfig.projbuild.

#pragma once

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Connect to Wi-Fi. Blocks up to ~30s for an IP. On success writes the STA
// netif to *out_netif and returns ESP_OK. On failure returns ESP_FAIL.
esp_err_t wifi_connect_blocking(esp_netif_t **out_netif);

#ifdef __cplusplus
}
#endif
