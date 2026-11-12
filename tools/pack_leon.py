#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pack the exact already-reviewed PCM header for an offline target build.

The raw host header stays unchanged. No new extraction, normalization,
quantization, resampling or sample-boundary change is permitted here.
"""
from pathlib import Path
import argparse, hashlib, json, re
from pcm_blocks import encode


def pack_header(source, output, report_path=None):
    if source.resolve() == output.resolve() or (report_path and report_path.resolve() in (source.resolve(), output.resolve())):
        raise ValueError('Source, packed output and report must be distinct files')
    raw = source.read_bytes()
    text = raw.decode()
    frames = int(re.search(r'^#define SW_LEON_CLIP_FRAMES (\d+)u$', text, re.M)[1])
    report = {'schema': 'bb-bob-leon-lossless-packing/v1',
              'source_header_sha256': hashlib.sha256(raw).hexdigest(),
              'frames': frames, 'raw_pcm_bytes': frames,
              'source_unchanged': True, 'quantization_changed': False,
              'installed': False, 'install_permitted': False}
    if not frames:
        result = raw
        report.update(format='raw/no supplied clip', stored_pcm_bytes=0)
    else:
        match = re.search(r'static const int8_t sw_leon_clip_pcm\[SW_LEON_CLIP_FRAMES\]=\{(.*?)\};', text, re.S)
        if not match:
            raise ValueError('Expected the original raw signed-byte Leon header')
        samples = [int(n) for n in match[1].replace('\n', '').strip(',').split(',')]
        if len(samples) != frames:
            raise ValueError('PCM count differs from declared frame count')
        report['pcm_sha256'] = hashlib.sha256(bytes(n & 255 for n in samples)).hexdigest()
        try:
            data, blocks = encode(samples)
        except OverflowError:
            data, blocks = b'', []
        if not blocks or len(data)+4*len(blocks) >= frames:
            result = raw
            report.update(format='raw/fixed block format does not save data or fit offsets',
                          stored_pcm_bytes=frames)
        else:
            prefix = text[:match.start()]
            result = (prefix+'#define SW_LEON_CLIP_BLOCK_SHIFT 7u\n'
                      '#include "pcm_blocks.h"\n'
                      'static const sw_pcm_block sw_leon_clip_blocks[]={'+
                      ','.join('{'+','.join(map(str, b))+'}' for b in blocks)+'};\n'
                      'static const uint8_t sw_leon_clip_bits[]={\n'+
                      '\n'.join(','.join(map(str, data[i:i+32]))+',' for i in range(0, len(data), 32))+
                      '\n};\n').encode()
            report.update(format='lossless128-frame block minima and0..8bit differences',
                          block_shift=7, blocks=len(blocks), descriptor_bytes=4*len(blocks),
                          payload_and_guard_bytes=len(data), stored_pcm_bytes=len(data)+4*len(blocks),
                          minimum_bits=min(b[2] for b in blocks), maximum_bits=max(b[2] for b in blocks),
                          runtime='One descriptor and at most two payload bytes per addressed sample; no expansion buffer/heap/variable decoding loop')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    report.update(data_bytes_saved=frames-report['stored_pcm_bytes'],
                  packed_header_sha256=hashlib.sha256(result).hexdigest())
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    print(json.dumps(pack_header(args.source, args.output, args.report), indent=2))
