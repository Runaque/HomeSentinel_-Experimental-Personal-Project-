#!/usr/bin/env python3
"""build_oui.py — generate HomeSentinel's sorted OUI blob from IEEE data.

The blob is a fixed-size, sorted-by-OUI binary table that oui.c binary-searches
directly from a flash partition. We ship the *generator*, not just the blob, so
anyone can regenerate it from the official source and verify what's on-device.

Source: IEEE "oui.csv" (the MA-L registry).
  https://standards-oui.ieee.org/oui/oui.csv

Blob format (little-endian), matching oui.h:
  magic  : 4 bytes  b"OUI1"
  count  : uint32   number of records
  records: count * 32 bytes, each:
             oui24    : 3 bytes (MAC bytes 0..2, big-endian, as on the wire)
             reserved : 1 byte (0)
             vendor   : 28 bytes ASCII, NUL-padded/truncated
  Records are sorted ascending by oui24 so the device can binary-search.

Usage:
  python build_oui.py --in oui.csv --out ../oui.bin
  # then flash to the 'oui' partition (offset from partitions.csv):
  #   esptool.py write_flash 0x310000 oui.bin
"""

import argparse
import csv
import struct
import sys

VENDOR_LEN = 28
RECORD_SIZE = 32
MAGIC = b"OUI1"


def parse_ieee_csv(path):
    """Yield (oui24_bytes, vendor_str) from the IEEE oui.csv."""
    records = []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            assignment = (row.get("Assignment") or "").strip()
            org = (row.get("Organization Name") or "").strip()
            if len(assignment) != 6 or not org:
                continue
            try:
                oui = bytes.fromhex(assignment)
            except ValueError:
                continue
            if len(oui) != 3:
                continue
            records.append((oui, org))
    return records


def pack_blob(records, max_size):
    """Return the packed blob bytes, truncating to fit max_size if needed."""
    # Sort by the 3-byte OUI so the device can binary-search.
    records.sort(key=lambda r: r[0])

    # De-duplicate; keep first occurrence.
    seen = set()
    deduped = []
    for oui, vendor in records:
        if oui in seen:
            continue
        seen.add(oui)
        deduped.append((oui, vendor))

    # How many records fit in the partition?
    # Header = 8 bytes, each record = 32 bytes.
    max_records = (max_size - 8) // RECORD_SIZE
    if len(deduped) > max_records:
        print(f"note: IEEE registry has {len(deduped)} vendors; "
              f"truncating to {max_records} to fit {max_size}-byte partition.")
        deduped = deduped[:max_records]

    out = bytearray()
    out += MAGIC
    out += struct.pack("<I", len(deduped))
    for oui, vendor in deduped:
        vbytes = vendor.encode("ascii", errors="replace")[:VENDOR_LEN]
        vbytes = vbytes.ljust(VENDOR_LEN, b"\x00")
        out += oui
        out += b"\x00"
        out += vbytes
    return bytes(out), len(deduped)


def main():
    ap = argparse.ArgumentParser(description="Build HomeSentinel OUI blob")
    ap.add_argument("--in", dest="infile", required=True,
                    help="path to IEEE oui.csv")
    ap.add_argument("--out", dest="outfile", required=True,
                    help="output blob path (e.g. ../oui.bin)")
    ap.add_argument("--max-size", type=int, default=0x100000,
                    help="partition size cap in bytes (default 1 MiB)")
    args = ap.parse_args()

    records = parse_ieee_csv(args.infile)
    if not records:
        print("error: no records parsed from", args.infile, file=sys.stderr)
        return 1

    blob, count = pack_blob(records, args.max_size)

    with open(args.outfile, "wb") as f:
        f.write(blob)

    print(f"wrote {args.outfile}: {count} vendors, {len(blob)} bytes "
          f"({100*len(blob)/args.max_size:.1f}% of partition)")
    return 0


if __name__ == "__main__":
    sys.exit(main())