# HomeSentinel

**An ESP32-S3 network inventory and anomaly-detection appliance for home networks.**

HomeSentinel answers one question continuously: *what's on my network right now, and has anything changed?* It discovers devices on your LAN, builds a persistent inventory, detects when devices appear, disappear, or behave unusually, and presents everything through a clean web dashboard — running standalone on a single ESP32-S3 N16R8.

<img width="1919" height="1079" alt="image" src="https://github.com/user-attachments/assets/23ae2c8e-4885-4cc9-94e2-5150a1a170c9" />


It is an **asset-discovery and change-detection** tool, not a packet sniffer, IDS, or pentest tool. It watches *what is present*, not *what is being said*.

---

## Hardware

- ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM)
- USB-C power

No display, no buttons. All interaction is through the browser.

---

## What it does

- **Discovery** — a scheduled pass pings the local /24, harvests each responding host's MAC from the ARP cache the moment it replies, and enriches names/classes via mDNS service discovery (`_googlecast`, `_airplay`, `_ipp`, `_homekit`, `_smb`, `_ssh`, …).
- **Inventory** — a PSRAM-resident device table keyed on MAC (not IP), with a ring-buffer event log. Periodically snapshotted to LittleFS.
- **Anomaly detection** — new device, missing/returned, unknown vendor, excessive growth, and night-activity events, all emitted into the timeline.
- **Randomized-MAC awareness** — iOS/Android private MACs (locally-administered bit set) are detected and, by default, do *not* trigger false "new device" alerts. This is the single biggest source of false positives in naive scanners, handled here from day one.
- **Web dashboard** — headless device, so the dashboard is the interface: live stats, sortable inventory, event timeline, manual scan. Served gzipped straight from flash.
- **Notifications** — optional Discord webhook for alerts.

---

## Architecture

```
Wi-Fi STA  ->  Discovery task  ->  Inventory (PSRAM)  ->  Anomaly engine
                (ping->ARP->mDNS)    |  live table          |  emits events
                                     |  event ring          v
                                     |                    Notifier (Discord)
                                     v
                              LittleFS snapshot
                                     |
                              Web server (:80) -- gzipped UI + JSON API
```

Key design decisions:

- **Live state in PSRAM, not flash.** The inventory and event log live in RAM; LittleFS gets periodic snapshots only. This protects flash from wear and keeps the scan loop fast.
- **MAC is the primary key.** An IP change is a logged event, not a phantom new device (DHCP-safe).
- **Ping primes ARP; harvest is inline.** Each MAC is read from the ARP cache the instant its host's echo returns, while the entry is fresh — so lwIP's small ARP table size never causes missed devices.
- **No TLS on the web UI.** LAN appliance on a trusted network; HTTP Basic auth + documented LAN-only scope rather than fighting mbedTLS for a self-signed cert.

---

## Build

Requires **ESP-IDF v5.5.x** (pinned; v6.0 has breaking changes not yet validated here).

```bash
idf.py set-target esp32s3
idf.py menuconfig      # set HomeSentinel Configuration -> Wi-Fi SSID/password,
                       # web credentials, optional Discord webhook
idf.py build
idf.py -p <PORT> flash monitor
```

### OUI vendor database

The vendor lookup reads a sorted binary blob from the `oui` flash partition. Generate and flash it separately:

```bash
# Download the IEEE registry
curl -o oui.csv https://standards-oui.ieee.org/oui/oui.csv

# Build the trimmed, sorted blob
python tools/build_oui.py --in oui.csv --out oui.bin

# Flash it to the oui partition (offset from partitions.csv)
esptool.py -p <PORT> write_flash 0x310000 oui.bin
```

The blob is regenerated from the official source so you can verify exactly what's on-device — the generator is shipped, not just the data.

---

## Limitations (by design)

- **Single subnet only.** HomeSentinel sees its own /24. Devices on separate VLANs or guest networks are invisible — that's an ARP/ping constraint, not a bug.
- **mDNS naming is best-effort.** Devices that advertise no services won't get a friendly name; they show as MAC/IP/vendor.
- **Not real-time.** A full /24 pass takes ~30–90s. This is an appliance you leave running, not a live monitor.

---

## Support

If you found this project interesting and helpful, consider [buying me a coffee](https://buymeacoffee.com/runaque)!


<a href="https://buymeacoffee.com/runaque" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" >
</a>
