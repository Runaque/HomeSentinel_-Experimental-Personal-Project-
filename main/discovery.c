// discovery.c — HomeSentinel network discovery engine
//
// Sweep strategy (v2, single-socket): instead of creating one esp_ping session
// per host (which exhausts lwIP's socket/raw-PCB pool when sweeping a whole
// /24), we open ONE raw ICMP socket per pass and reuse it for every host:
//
//   1. open socket(AF_INET, SOCK_RAW, IPPROTO_ICMP) once
//   2. send an ICMP echo request (sendto) to each host in the subnet
//   3. drain replies with recvfrom under a short timeout; each reply means
//      that host is alive — harvest its MAC from the ARP cache (which the
//      reply just populated) and upsert into the inventory
//   4. close the one socket
//
// This is how a real network scanner works, and it sidesteps the
// "create socket failed: -1" pool-exhaustion entirely.

#include "discovery.h"
#include "inventory.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/ip.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "mdns.h"

static const char *TAG = "discovery";

// ---- Module state --------------------------------------------------------
static esp_netif_t        *s_netif;
static disc_scan_done_cb_t s_on_done;
static TaskHandle_t        s_task;
static volatile uint32_t   s_last_duration_ms;

static EventGroupHandle_t  s_events;
#define BIT_TRIGGER_NOW   (1 << 0)

// Our ICMP echo identifier (arbitrary, used to match replies to us).
#define ICMP_ECHO_ID      0xAF01
#define ICMP_PKT_SIZE     (sizeof(struct icmp_echo_hdr) + 8)  // hdr + 8B payload

// ---- ARP harvest ---------------------------------------------------------
// Given an IP that just replied, pull its MAC from the ARP cache (fresh,
// because the echo reply just arrived) and upsert into the inventory.
static void harvest_one(uint32_t ip_host_order)
{
    ip4_addr_t lookup;
    lookup.addr = htonl(ip_host_order);

    struct eth_addr  *eth = NULL;
    const ip4_addr_t *ipret = NULL;
    struct netif     *nif = netif_default;

    if (etharp_find_addr(nif, &lookup, &eth, &ipret) < 0) {
        return;   // no stable ARP entry (shouldn't happen right after a reply)
    }

    uint8_t macbytes[6];
    memcpy(macbytes, eth->addr, 6);

    int64_t now = (int64_t)(esp_timer_get_time() / 1000000);

    inv_lock();
    device_t *d = inv_find_or_add(macbytes);
    if (d != NULL) {
        if (d->first_seen == 0) {
            d->first_seen = now;
        }
        d->ip         = ip_host_order;
        d->last_seen  = now;
        d->online     = 1;
        d->miss_count = 0;
    }
    inv_unlock();
}

// ---- Subnet helpers ------------------------------------------------------
static bool get_subnet(uint32_t *net_host_order, uint32_t *mask_host_order,
                       uint32_t *self_host_order)
{
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) != ESP_OK) {
        return false;
    }
    uint32_t ipaddr = ntohl(ip.ip.addr);
    uint32_t mask   = ntohl(ip.netmask.addr);
    *self_host_order = ipaddr;
    *mask_host_order = mask;
    *net_host_order  = ipaddr & mask;
    return true;
}

// ---- Single-socket ICMP sweep -------------------------------------------
// Build one echo-request packet (payload is constant; only the per-host
// destination changes). Returns the prepared packet length.
static size_t build_echo(uint8_t *buf, uint16_t seq)
{
    memset(buf, 0, ICMP_PKT_SIZE);
    struct icmp_echo_hdr *hdr = (struct icmp_echo_hdr *)buf;
    ICMPH_TYPE_SET(hdr, ICMP_ECHO);     // type 8
    ICMPH_CODE_SET(hdr, 0);
    hdr->id    = htons(ICMP_ECHO_ID);
    hdr->seqno = htons(seq);
    // 8-byte payload tag so replies are recognisably ours.
    memcpy(buf + sizeof(struct icmp_echo_hdr), "HSENTNL!", 8);
    hdr->chksum = 0;
    hdr->chksum = inet_chksum(buf, ICMP_PKT_SIZE);
    return ICMP_PKT_SIZE;
}

// Drain any pending replies on the socket (non-blocking-ish via short
// SO_RCVTIMEO). For each valid echo reply, harvest the source IP's MAC.
static void drain_replies(int sock, int budget_ms)
{
    uint8_t rbuf[128];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    int64_t deadline = esp_timer_get_time() + (int64_t)budget_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        fromlen = sizeof(from);
        int n = recvfrom(sock, rbuf, sizeof(rbuf), 0,
                         (struct sockaddr *)&from, &fromlen);
        if (n <= 0) {
            // Timeout for THIS recvfrom (SO_RCVTIMEO). Don't give up the whole
            // drain — replies may still arrive before our deadline. Keep
            // polling until budget_ms elapses.
            continue;
        }

        uint32_t src = ntohl(from.sin_addr.s_addr);

        // The raw socket may hand us either [IP header][ICMP] or just [ICMP].
        // Detect: if byte 0 looks like an IPv4 header (version nibble == 4),
        // skip the IP header; otherwise treat the buffer as starting at ICMP.
        int icmp_off = 0;
        if ((rbuf[0] >> 4) == 4) {
            struct ip_hdr *iph = (struct ip_hdr *)rbuf;
            icmp_off = IPH_HL(iph) * 4;
        }
        if (n < icmp_off + (int)sizeof(struct icmp_echo_hdr)) {
            continue;
        }
        struct icmp_echo_hdr *icmp =
            (struct icmp_echo_hdr *)(rbuf + icmp_off);
        if (ICMPH_TYPE(icmp) != ICMP_ER) {     // ICMP_ER = echo reply (0)
            continue;
        }
        // Accept the reply even if id doesn't match (some stacks rewrite it);
        // the source IP answering an echo is itself proof the host is alive.
        harvest_one(src);
    }
}

// ---- mDNS hostname resolution -------------------------------------------
typedef struct {
    const char  *service;
    const char  *proto;
    dev_class_t  hint;
} mdns_probe_t;

static const mdns_probe_t MDNS_PROBES[] = {
    { "_googlecast", "_tcp", DEV_CLASS_IOT      },
    { "_airplay",    "_tcp", DEV_CLASS_IOT      },
    { "_raop",       "_tcp", DEV_CLASS_IOT      },
    { "_ipp",        "_tcp", DEV_CLASS_IOT      },
    { "_printer",    "_tcp", DEV_CLASS_IOT      },
    { "_homekit",    "_tcp", DEV_CLASS_IOT      },
    { "_hap",        "_tcp", DEV_CLASS_IOT      },
    { "_workstation","_tcp", DEV_CLASS_COMPUTER },
    { "_smb",        "_tcp", DEV_CLASS_COMPUTER },
    { "_ssh",        "_tcp", DEV_CLASS_COMPUTER },
};
#define MDNS_PROBE_COUNT (sizeof(MDNS_PROBES) / sizeof(MDNS_PROBES[0]))
#define MDNS_QUERY_TIMEOUT_MS  1500
#define MDNS_MAX_RESULTS       20

static void apply_mdns_result(const mdns_result_t *r, dev_class_t hint)
{
    const char *name = NULL;
    if (r->instance_name && r->instance_name[0] != '\0') {
        name = r->instance_name;
    } else if (r->hostname && r->hostname[0] != '\0') {
        name = r->hostname;
    }
    if (name == NULL) {
        return;
    }

    for (const mdns_ip_addr_t *a = r->addr; a != NULL; a = a->next) {
        if (a->addr.type != ESP_IPADDR_TYPE_V4) {
            continue;
        }
        uint32_t ip_host = ntohl(a->addr.u_addr.ip4.addr);

        inv_lock();
        size_t idx = 0;
        device_t *d;
        while ((d = inv_next(&idx)) != NULL) {
            if (d->ip == ip_host) {
                if (d->hostname[0] == '\0') {
                    strlcpy(d->hostname, name, sizeof(d->hostname));
                }
                if (d->dev_class == DEV_CLASS_UNKNOWN) {
                    d->dev_class = hint;
                }
                break;
            }
        }
        inv_unlock();
    }
}

static void resolve_hostnames(void)
{
    bool any_unnamed = false;
    size_t idx = 0;
    device_t *d;
    inv_lock();
    while ((d = inv_next(&idx)) != NULL) {
        if (d->online && d->hostname[0] == '\0') { any_unnamed = true; break; }
    }
    inv_unlock();
    if (!any_unnamed) {
        return;
    }

    for (size_t p = 0; p < MDNS_PROBE_COUNT; p++) {
        mdns_result_t *results = NULL;
        esp_err_t err = mdns_query_ptr(MDNS_PROBES[p].service,
                                       MDNS_PROBES[p].proto,
                                       MDNS_QUERY_TIMEOUT_MS,
                                       MDNS_MAX_RESULTS,
                                       &results);
        if (err != ESP_OK || results == NULL) {
            continue;
        }
        for (mdns_result_t *r = results; r != NULL; r = r->next) {
            apply_mdns_result(r, MDNS_PROBES[p].hint);
        }
        mdns_query_results_free(results);
    }
}

// ---- Mark missing devices ------------------------------------------------
static void age_unseen(int64_t pass_start)
{
    size_t idx = 0;
    device_t *d;
    inv_lock();
    while ((d = inv_next(&idx)) != NULL) {
        if (d->last_seen < pass_start) {
            d->miss_count++;
            if (d->miss_count >= DISC_MISS_THRESHOLD) {
                d->online = 0;
            }
        }
    }
    inv_unlock();
}

// ---- One full pass -------------------------------------------------------
static void run_pass(void)
{
    int64_t t0 = esp_timer_get_time();

    uint32_t net, mask, self;
    if (!get_subnet(&net, &mask, &self)) {
        ESP_LOGW(TAG, "no IP yet; skipping pass");
        return;
    }

    uint32_t host_count = (~mask) & 0xFFFFFFFFu;
    if (host_count > 1022) {
        host_count = 1022;
    }

    ESP_LOGI(TAG, "pass start: net=%u.%u.%u.%u/%u, %u hosts",
             (unsigned)((net>>24)&0xFF),(unsigned)((net>>16)&0xFF),
             (unsigned)((net>>8)&0xFF),(unsigned)(net&0xFF),
             (unsigned)__builtin_popcount(mask), (unsigned)(host_count - 1));

    // Open ONE raw ICMP socket for the whole sweep.
    int sock = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (sock < 0) {
        ESP_LOGE(TAG, "raw ICMP socket failed: errno %d", errno);
        return;
    }

    // Short receive timeout so the drain loop never blocks long.
    struct timeval tv = { .tv_sec = 0, .tv_usec = 50 * 1000 };  // 50 ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t pkt[ICMP_PKT_SIZE];

    // Send echo requests to every host, draining replies periodically so the
    // socket's receive buffer never overflows on a busy subnet.
    uint16_t seq = 0;
    for (uint32_t h = 1; h < host_count; h++) {
        uint32_t ip = net + h;
        if (ip == self) {
            continue;
        }
        build_echo(pkt, seq++);

        struct sockaddr_in dst = {0};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = htonl(ip);
        sendto(sock, pkt, ICMP_PKT_SIZE, 0,
               (struct sockaddr *)&dst, sizeof(dst));

        // Every 16 sends, drain whatever has replied so far.
        if ((h & 0x0F) == 0) {
            drain_replies(sock, 30);
        }
    }

    // Final drain: give stragglers time to answer (covers the slowest host).
    drain_replies(sock, DISC_PING_TIMEOUT_MS);

    close(sock);

    // Hostname enrichment + aging.
    resolve_hostnames();
    age_unseen((int64_t)(t0 / 1000000));

    s_last_duration_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "pass done in %u ms; %u online / %u total",
             (unsigned)s_last_duration_ms,
             (unsigned)inv_count_online(), (unsigned)inv_count_total());

    if (s_on_done != NULL) {
        s_on_done();
    }
}

// ---- Task ----------------------------------------------------------------
static void discovery_task(void *arg)
{
    ESP_LOGI(TAG, "discovery task up; interval=%ds", DISC_SCAN_INTERVAL_SEC);
    for (;;) {
        run_pass();
        EventBits_t bits = xEventGroupWaitBits(
            s_events, BIT_TRIGGER_NOW, pdTRUE, pdFALSE,
            pdMS_TO_TICKS(DISC_SCAN_INTERVAL_SEC * 1000));
        if (bits & BIT_TRIGGER_NOW) {
            ESP_LOGI(TAG, "manual scan trigger");
        }
    }
}

// ---- Public API ----------------------------------------------------------
esp_err_t discovery_start(esp_netif_t *netif, disc_scan_done_cb_t on_done)
{
    s_netif   = netif;
    s_on_done = on_done;

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        discovery_task, "discovery", 6144, NULL, 4, &s_task, tskNO_AFFINITY);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void discovery_trigger_now(void)
{
    if (s_events != NULL) {
        xEventGroupSetBits(s_events, BIT_TRIGGER_NOW);
    }
}

uint32_t discovery_last_duration_ms(void)
{
    return s_last_duration_ms;
}