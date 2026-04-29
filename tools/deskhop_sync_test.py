#!/usr/bin/env python3
"""Send the roBa/ZMK DeskHop sync feature report on Windows.

Requires:
    py -m pip install hidapi

Examples:
    py tools\\deskhop_sync_test.py --list
    py tools\\deskhop_sync_test.py --vid 0x1D50 --pid 0x615E --output 1
    py tools\\deskhop_sync_test.py --path "\\\\?\\hid#..." --output 0
"""

from __future__ import annotations

import argparse
import sys


REPORT_ID = 0x7E
MAGIC = 0xA5


def parse_int(value: str) -> int:
    return int(value, 0)


def load_hid():
    try:
        import hid
    except ImportError:
        print("Missing dependency: install with `py -m pip install hidapi`.", file=sys.stderr)
        raise SystemExit(2)

    return hid


def text(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode(errors="replace")
    return str(value)


def list_devices(hid, vid: int | None, pid: int | None) -> list[dict]:
    devices = hid.enumerate(vid or 0, pid or 0)

    for idx, dev in enumerate(devices):
        vendor_id = dev.get("vendor_id", 0)
        product_id = dev.get("product_id", 0)
        usage_page = dev.get("usage_page", 0)
        usage = dev.get("usage", 0)
        print(
            f"[{idx}] vid=0x{vendor_id:04x} pid=0x{product_id:04x} "
            f"usage_page=0x{usage_page:04x} usage=0x{usage:04x} "
            f"manufacturer={text(dev.get('manufacturer_string'))!r} "
            f"product={text(dev.get('product_string'))!r}"
        )
        print(f"    path={text(dev.get('path'))}")

    return devices


def open_device(hid, args):
    if args.path:
        device = hid.device()
        device.open_path(args.path.encode() if isinstance(args.path, str) else args.path)
        return device

    if args.vid is None or args.pid is None:
        raise SystemExit("Provide --vid and --pid, or provide --path from --list output.")

    device = hid.device()
    device.open(args.vid, args.pid)
    return device


def main() -> int:
    parser = argparse.ArgumentParser(description="Send ZMK DeskHop layer sync feature report.")
    parser.add_argument("--list", action="store_true", help="List HID devices and exit.")
    parser.add_argument("--vid", type=parse_int, help="Keyboard USB vendor ID, e.g. 0x1D50.")
    parser.add_argument("--pid", type=parse_int, help="Keyboard USB product ID, e.g. 0x615E.")
    parser.add_argument("--path", help="Exact HID path from --list output.")
    parser.add_argument("--output", type=parse_int, choices=(0, 1), help="DeskHop output: 0=A, 1=B.")
    args = parser.parse_args()

    hid = load_hid()

    if args.list:
        list_devices(hid, args.vid, args.pid)
        return 0

    if args.output is None:
        raise SystemExit("Provide --output 0 or --output 1.")

    report = bytes([REPORT_ID, MAGIC, args.output])
    device = open_device(hid, args)

    try:
        written = device.send_feature_report(report)
    finally:
        device.close()

    print(f"Sent {written} bytes: {report.hex(' ')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
