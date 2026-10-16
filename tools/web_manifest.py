#!/usr/bin/env python3
"""Write the ESP Web Tools manifest for the browser flasher from offsets.json.

The parts are flashed separately (not the merged image) so the NVS partition
between them, which holds the user's settings, survives an update.
"""
import argparse
import json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('offsets', help='offsets.json from tools/offsets.py')
    parser.add_argument('--version', required=True)
    parser.add_argument('--prefix', default='firmware/', help='URL prefix of the image files')
    args = parser.parse_args()

    with open(args.offsets) as f:
        offsets = json.load(f)

    manifest = {
        'name': 't-sdr-c5',
        'version': args.version,
        'new_install_prompt_erase': True,
        'builds': [{
            'chipFamily': 'ESP32-C5',
            'parts': [{'path': args.prefix + p['file'], 'offset': int(p['offset'], 16)} for p in offsets],
        }],
    }
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
