#!/usr/bin/env python3
"""Build the R36 browser player from the same C sources as the firmware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'swing', 'rodeo', 'display', 'panel', 'showbill', 'art_bank', 'bank',
    'sample_player', 'timbre_bank', 'bob_identity', 'host',
]
EXPORTS = [
    'sw_host_reset', 'sw_host_art_bank', 'sw_host_calls_bank',
    'sw_host_panel_enable', 'sw_host_panel_control', 'sw_host_command',
    'sw_host_render', 'sw_host_state', 'sw_host_screen',
    'sw_host_visual_advance', 'sw_host_bob_screen',
    'sw_timbre_bank_boot', 'sw_timbre_bank_status', 'malloc', 'free',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emcc', default=os.environ.get('EMCC', 'emcc'))
    args = parser.parse_args()
    native = ROOT / 'build/browser'
    native.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        'python3', 'tools/showbill_calls.py', 'web/call-index.json',
        'tools/call-figure-assignments.json', str(native / 'showbill_calls.h'),
    ], cwd=ROOT, check=True)
    command = [args.emcc, '-std=c99', '-O2', '-ffp-contract=off',
               '-Ibuild/browser', '-Isrc', '-Ibuild',
               '-DSW_WITH_CUE=1', '-DSW_WITH_ART=1', '-DSW_WITH_BANK=1',
               '-DSW_WITH_TIMBRE_BANK=1']
    command += [f'src/{name}.c' for name in SOURCES]
    command += [
        '-lm', '-sMODULARIZE=1', '-sEXPORT_ES6=1',
        '-sENVIRONMENT=web,worker,node', '-sFILESYSTEM=0', '-sMALLOC=emmalloc',
        '-sINITIAL_MEMORY=16777216', '-sSTACK_SIZE=65536',
        '-sALLOW_MEMORY_GROWTH=0', '-sINVOKE_RUN=0',
        '-sEXPORTED_FUNCTIONS=' + json.dumps(['_' + name for name in EXPORTS]),
        '-sEXPORTED_RUNTIME_METHODS=["UTF8ToString","HEAPU8","HEAPU16","HEAPF32"]',
        '-o', str(native / 'bbswing.js'),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    output = ROOT / 'build/browser-player'
    shutil.copytree(ROOT / 'web', output, dirs_exist_ok=True)
    (output / 'native').mkdir(exist_ok=True)
    for name in ('bbswing.js', 'bbswing.wasm'):
        shutil.copy2(native / name, output / 'native' / name)
    shutil.copy2(ROOT / 'identity.json', output / 'identity.json')
    report = {
        'identity': json.loads((ROOT / 'identity.json').read_text()),
        'source_sha256': {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((ROOT / 'src').glob('*')) if p.is_file()
        },
        'wasm_sha256': hashlib.sha256((native / 'bbswing.wasm').read_bytes()).hexdigest(),
        'device_contacted': False,
    }
    (output / 'build-info.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'player': str(output), 'wasm_sha256': report['wasm_sha256']}))


if __name__ == '__main__':
    main()
