#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Lossless independently addressed blocks of signed-byte PCM; no resampling."""

def encode(samples, shift=7):
    if not 4 <= shift <= 8:
        raise ValueError('Block shift must be4..8')
    if any(not isinstance(x, int) or not -128 <= x <= 127 for x in samples):
        raise ValueError('Expected signed-byte samples')
    data = bytearray()
    blocks = []
    for start in range(0, len(samples), 1 << shift):
        group = samples[start:start+(1 << shift)]
        minimum, maximum = min(group), max(group)
        width = (maximum-minimum).bit_length()
        if len(data) > 65535:
            raise OverflowError('PCM block byte offset exceeds16bits')
        blocks.append((len(data), minimum, width))
        value = buffered = 0
        for sample in group:
            value |= (sample-minimum) << buffered
            buffered += width
            while buffered >= 8:
                data.append(value & 255)
                value >>= 8
                buffered -= 8
        if buffered:
            data.append(value)
    data.append(0)
    return bytes(data), blocks
