#!/usr/bin/env python3
"""Package the exact full-install target without weakening package.py's guard.

This is an offline packager.  It binds a previously compiled base-machine
application to its source report and produces one FWSC; it never opens USB.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "vendor/fm1/tools"))
import fm1pkg_make as pack  # type: ignore
import identity  # type: ignore
import lz4blk  # type: ignore


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    target = args.target.resolve()
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError(f"refusing to overwrite {output}")
    report = json.loads((target / "REPORT.json").read_text())
    assert report["compiled"] and report["linked"] and report["base_machine"]
    assert report["external_timbre_bank"] and report["art_enabled"]
    app = (target / "bbswing.bin").read_bytes()
    assert sha(app) == report["app_sha256"]
    for name, digest in report["sources"].items():
        source = ROOT / name
        assert source.is_file(), f"missing frozen input {name}"
        assert sha(source.read_bytes()) == digest, f"source drift: {name}"

    if not os.environ.get("AC79_SDK"):
        raise RuntimeError("Set AC79_SDK to the AC79 SDK checkout")
    sdk = Path(os.environ["AC79_SDK"])
    expected = {
        "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
        "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
        "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
    }
    for name, digest in expected.items():
        assert sha((sdk / "cpu/wl82/tools" / name).read_bytes()) == digest, name

    loader = (target / "loader.bin").read_bytes()
    assert 0 < len(loader) <= 0x14000
    blocks = bytearray()
    for offset in range(0, len(loader), 4096):
        raw = loader[offset:offset + 4096]
        compressed = lz4blk.compress(raw)
        assert len(compressed) <= 4096 and lz4blk.decompress(compressed) == raw
        blocks += struct.pack("<II", len(compressed), len(raw)) + compressed

    def header(crc: int, a: int, b: int, attr: int) -> bytes:
        body = struct.pack("<HIIBBH16s", crc, a, b, attr, 0, 0, b"usb_hid_ota.bin")
        return struct.pack("<H", pack.crc16(body)) + body

    body = header(pack.crc16(loader), len(loader), 0x01C0A800, 0) + blocks
    ota = header(pack.crc16(body), 0x20, len(body), 0x41) + body
    identity_row = identity.check()
    pack.SDK = sdk
    package = pack.ufw(pack.flash_image(app, pack.KEY), ota, identity_row["product_id"])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    result = {
        "schema": "bb-bob-full-install-app-package/v1",
        "file": output.name,
        "bytes": len(package),
        "sha256": sha(package),
        "app_bytes": len(app),
        "app_sha256": sha(app),
        "product": identity_row["product_id"],
        "display": identity_row["display_version"],
        "loader_sha256": sha(loader),
        "device_contacted": False,
        "banks_in_fwsc": False,
    }
    (output.parent / (output.name + ".json")).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
