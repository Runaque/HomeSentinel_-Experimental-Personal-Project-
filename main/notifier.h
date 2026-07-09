// notifier.h — HomeSentinel notifications (Discord webhook)
//
// Optional. If CONFIG_HS_DISCORD_WEBHOOK is empty, all calls are no-ops. Posts
// a simple JSON {"content": "..."} to the configured Discord webhook over HTTP.
// Runs the POST on a short-lived detached task so callers (the anomaly engine,
// holding no locks at call time) never block on the network.

#pragma once

#include "inventory.h"   // event_t, severity_t

#ifdef __cplusplus
extern "C" {
#endif

void notifier_init(void);

// Queue a notification for an event. Non-blocking; silently dropped if the
// webhook is unconfigured or the queue is full.
void notifier_notify(const event_t *ev);

#ifdef __cplusplus
}
#endif
