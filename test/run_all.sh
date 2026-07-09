#!/usr/bin/env bash
# Host-side unit tests for HomeSentinel's pure logic. No ESP-IDF required.
set -e
cd "$(dirname "$0")"
echo "== inventory ring buffer ==";  gcc -O2 test_inventory.c    -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== subnet sweep pacing ==";    gcc -O2 test_sweep.c        -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== ip byte order ==";          gcc -O2 test_byteorder.c    -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== mdns ip matching ==";       gcc -O2 test_mdns_match.c   -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== oui binary search ==";      python3 ../tools/build_oui.py --in sample_oui.csv --out /tmp/sample.bin >/dev/null && gcc -O2 test_oui.c -o /tmp/t && OUI_BIN=/tmp/sample.bin /tmp/t >/dev/null && echo "  ok"
echo "== anomaly logic ==";          gcc -O2 test_anomaly.c      -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== json escape ==";            gcc -O2 test_json_escape.c  -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo "== notify dedup ==";           gcc -O2 test_notify_dedup.c -o /tmp/t && /tmp/t >/dev/null && echo "  ok"
echo ""
echo "ALL HOST TEST SUITES PASSED"
