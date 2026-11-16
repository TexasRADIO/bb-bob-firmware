#!/usr/bin/env python3
"""Embed BB-BOB's fixed banks in an existing verified full-flash FWSC."""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "vendor/fm1/tools"))
import fm1pkg_make as pack
import lz4blk

from identity import load as load_identity
PRODUCT = load_identity()["product_id"]
TIMBRE_OFFSET = 0xA0000
ART_OFFSET = 0xE8000
SLOT_BYTES = 81920


def decode(data, key=0xFFFF):
    out = bytearray()
    for byte in data:
        out.append(byte ^ (key & 0xFF))
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def unpack(package):
    raw = package.read_bytes()
    logical = bytearray()
    identity = ""
    for index in range(20):
        logical += raw[index * 48:index * 48 + 47]
        marker = raw[index * 48 + 47]
        if marker != 0x7D:
            identity += chr((marker - index - 1) & 0xFF)
    logical += raw[960:]
    header = decode(logical[:64])
    total, count = struct.unpack_from("<IH", header, 4)
    if identity != PRODUCT or total != len(logical) or count != 2:
        raise ValueError("unexpected base package identity or layout")
    entries = logical[64:64 + count * 80]
    payload = {}
    for index in range(count):
        entry = decode(entries[index * 80:(index + 1) * 80])
        _, entry_index, checksum, _, offset, size, _ = struct.unpack_from("<HHHHIII", entry)
        name = entry[64:].split(b"\0", 1)[0].decode()
        data = bytes(logical[offset:offset + size])
        if entry_index != index or pack.crc16(data) != checksum:
            raise ValueError("base package checksum failure")
        payload[name] = data
    if set(payload) != {"flash.bin", "ota.bin"}:
        raise ValueError("unexpected base package payload")
    return payload


def loader_payload(loader):
    def header(crc, a, b, attr):
        body = struct.pack("<HIIBBH16s", crc, a, b, attr, 0, 0, b"usb_hid_ota.bin")
        return struct.pack("<H", pack.crc16(body)) + body
    blocks = bytearray()
    for offset in range(0, len(loader), 4096):
        raw = loader[offset:offset + 4096]
        compressed = lz4blk.compress(raw)
        if len(compressed) > 4096 or lz4blk.decompress(compressed) != raw:
            raise ValueError("loader compression verification failed")
        blocks += struct.pack("<II", len(compressed), len(raw)) + compressed
    body = header(pack.crc16(loader), len(loader), 0x01C0A800, 0) + blocks
    return header(pack.crc16(body), 0x20, len(body), 0x41) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--timbres", type=Path, required=True)
    parser.add_argument("--art", type=Path, required=True)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError(f"refusing to overwrite {args.output}")
    payload = unpack(args.base)
    timbres, art = args.timbres.read_bytes(), args.art.read_bytes()
    if len(timbres) != SLOT_BYTES or len(art) != SLOT_BYTES:
        raise ValueError("both banks must be exactly 81920 bytes")
    flash = bytearray(payload["flash.bin"])
    if len(flash) > 0x100000:
        raise ValueError("base flash image exceeds the device flash size")
    flash += b"\xFF" * (0x100000 - len(flash))
    for offset, data in ((TIMBRE_OFFSET, timbres), (ART_OFFSET, art)):
        if any(byte != 0xFF for byte in flash[offset:offset + SLOT_BYTES]):
            raise ValueError(f"base flash region {offset:#x} is not blank")
        flash[offset:offset + SLOT_BYTES] = data
    loader = args.loader.read_bytes()
    if not 0 < len(loader) <= 0x14000:
        raise ValueError("loader size outside RAM-loader bound")
    result = pack.ufw(bytes(flash), loader_payload(loader), PRODUCT)
    args.output.write_bytes(result)
    print(hashlib.sha256(result).hexdigest(), args.output)


if __name__ == "__main__":
    main()
