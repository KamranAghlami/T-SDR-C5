#!/usr/bin/env python3
"""Convert an ESP-IDF flash_args file into offsets.json: [{"offset": "0x2000", "file": "bootloader.bin"}, ...]."""
import json
import sys
from pathlib import PurePosixPath


def main():
    entries = []
    with open(sys.argv[1]) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2 and parts[0].startswith('0x'):
                entries.append({'offset': parts[0], 'file': PurePosixPath(parts[1]).name})
    json.dump(entries, sys.stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
