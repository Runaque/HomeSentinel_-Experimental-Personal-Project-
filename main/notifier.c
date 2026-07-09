// notifier.c — HomeSentinel Discord webhook notifier

#include "notifier.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "notifier";

#define NOTIFY_QUEUE_LEN   8
#define WEBHOOK_URL        CONFIG_HS_DISCORD_WEBHOOK

static QueueHandle_t s_queue;

// A queued message is just the pre-formatted content string.
typedef struct {
    char content[160];
} notify_msg_t;

static bool webhook_configured(void)
{
    return WEBHOOK_URL[0] != '\0';
}

// Map an event type to a short human label for the alert text.
static const char *event_label(event_type_t t)
{
    switch (t) {
        case EV_NEW_DEVICE:       return "New device";
        case EV_DEVICE_MISSING:   return "Device missing";
        case EV_DEVICE_RETURNED:  return "Device returned";
        case EV_HOSTNAME_CHANGED: return "Hostname changed";
        case EV_IP_CHANGED:       return "IP changed";
        case EV_UNKNOWN_VENDOR:   return "Unknown vendor";
        case EV_DEVICE_GROWTH:    return "Device growth";
        case EV_NIGHT_ACTIVITY:   return "Night activity";
        default:                  return "Alert";
    }
}

// Minimal JSON string escaper for the content field (quotes/backslashes and
// control chars). Discord content is plain text; we keep it simple.
static void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 2 < out_len; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if (c == '\n') {
            if (o + 2 < out_len) { out[o++] = '\\'; out[o++] = 'n'; }
        } else if ((unsigned char)c >= 0x20) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static void post_one(const char *content)
{
    char escaped[200];
    json_escape(content, escaped, sizeof(escaped));

    char body[256];
    int n = snprintf(body, sizeof(body), "{\"content\":\"%s\"}", escaped);
    if (n <= 0 || n >= (int)sizeof(body)) {
        return;
    }

    esp_http_client_config_t cfg = {
        .url = WEBHOOK_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        // Discord uses HTTPS; bundle the cert via the global CA store. For a
        // LAN appliance posting to one fixed host, crt_bundle is simplest.
        .crt_bundle_attach = NULL,   // set in init note below
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        return;
    }
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(cli);
        if (code / 100 != 2) {
            ESP_LOGW(TAG, "webhook returned HTTP %d", code);
        }
    } else {
        ESP_LOGW(TAG, "webhook POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(cli);
}

static void notifier_task(void *arg)
{
    notify_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            post_one(msg.content);
        }
    }
}

void notifier_init(void)
{
    if (!webhook_configured()) {
        ESP_LOGI(TAG, "no webhook configured; notifications disabled");
        return;
    }
    s_queue = xQueueCreate(NOTIFY_QUEUE_LEN, sizeof(notify_msg_t));
    xTaskCreate(notifier_task, "notifier", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "notifier ready");
}

void notifier_notify(const event_t *ev)
{
    if (!webhook_configured() || s_queue == NULL || ev == NULL) {
        return;
    }
    notify_msg_t msg;
    snprintf(msg.content, sizeof(msg.content), "**%s**: %s",
             event_label(ev->type), ev->detail);
    // Non-blocking: if the queue is full we drop the alert rather than stall.
    xQueueSend(s_queue, &msg, 0);
}
