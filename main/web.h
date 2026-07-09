// web.h — HomeSentinel web server
//
// Serves the dashboard UI (gzipped, embedded in flash) and a small JSON REST
// API over the inventory and event log. Protected by HTTP Basic auth using the
// CONFIG_HS_WEB_* credentials. LAN-only, no TLS — documented tradeoff for an
// appliance on a trusted home network.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the HTTP server on port 80. Call after Wi-Fi is up. The discovery
// "scan now" trigger is wired through so the UI button works.
esp_err_t web_start(void);

#ifdef __cplusplus
}
#endif
