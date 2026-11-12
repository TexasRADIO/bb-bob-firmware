#!/usr/bin/env python3
"""Verify the BB-BOB Hub package, identity, loader, application and banks."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "vendor/fm1/tools"))
import lz4blk
from embed_banks import ART_OFFSET, PRODUCT, SLOT_BYTES, TIMBRE_OFFSET, decode, unpack


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    expected = json.loads(args.manifest.read_text())
    payload = unpack(args.package)
    raw = args.package.read_bytes()
    flash, ota = payload["flash.bin"], payload["ota.bin"]
    if sha(raw) != expected["package_sha256"]:
        raise ValueError("package hash mismatch")
    if sha(flash[TIMBRE_OFFSET:TIMBRE_OFFSET + SLOT_BYTES]) != expected["timbres_sha256"]:
        raise ValueError("timbre bank mismatch")
    if sha(flash[ART_OFFSET:ART_OFFSET + SLOT_BYTES]) != expected["art_sha256"]:
        raise ValueError("art bank mismatch")
    at, loader = 64, bytearray()
    while at < len(ota):
        compressed, plain = struct.unpack_from("<II", ota, at)
        at += 8
        block = lz4blk.decompress(ota[at:at + compressed])
        if len(block) != plain:
            raise ValueError("loader block length mismatch")
        loader += block
        at += compressed
    if sha(loader) != expected["loader_sha256"]:
        raise ValueError("loader hash mismatch")
    encrypted = flash[0x4000:]
    area = bytearray()
    for offset in range(0, len(encrypted), 32):
        area += decode(encrypted[offset:offset + 32], 0x980F ^ (offset >> 2))
    entry = area[32:64]
    _, offset, size = struct.unpack_from("<HII", entry, 2)
    app = bytes(area[offset:offset + size]).rstrip(b"\xFF")
    if len(app) != expected["app_bytes"] or sha(app) != expected["app_sha256"]:
        raise ValueError("application mismatch")
    print(json.dumps({"passed": True, "product": PRODUCT, "package_sha256": sha(raw),
                      "app_sha256": sha(app), "loader_sha256": sha(loader),
                      "timbres_sha256": expected["timbres_sha256"],
                      "art_sha256": expected["art_sha256"], "device_contacted": False}, indent=2))


if __name__ == "__main__":
    main()
