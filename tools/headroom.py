#!/usr/bin/env python3
"""Report static SRAM headroom below the ESP32-C5 RF dump bank and enforce a minimum.

Every static section must end below 0x40820000: the modem owns SRAM bank 1 while
it dumps I/Q samples. The upstream linker guard fails the link on overlap; this
check fails earlier, while some margin is left.
"""
import argparse
import shutil
import subprocess
import sys

DUMP_BANK = 0x40820000
SYMBOLS = ('_iram_end', '_data_end', '_bss_end')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--min-kib', type=float, default=8)
    args = parser.parse_args()

    nm = shutil.which('riscv32-esp-elf-nm') or 'nm'
    output = subprocess.run([nm, args.elf], check=True, capture_output=True, text=True).stdout
    addresses = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in SYMBOLS:
            addresses[parts[2]] = int(parts[0], 16)

    missing = [s for s in SYMBOLS if s not in addresses]
    if missing:
        sys.exit(f'symbols not found: {", ".join(missing)}')

    print('| symbol | address | headroom below 0x40820000 |')
    print('| --- | --- | --- |')
    for symbol in SYMBOLS:
        address = addresses[symbol]
        print(f'| `{symbol}` | `0x{address:08x}` | {(DUMP_BANK - address) / 1024:.1f} KiB |')

    headroom = (DUMP_BANK - max(addresses.values())) / 1024
    print()
    if headroom < args.min_kib:
        print(f'**FAIL**: {headroom:.1f} KiB headroom, at least {args.min_kib:g} KiB required')
        sys.exit(1)
    print(f'OK: {headroom:.1f} KiB headroom (minimum {args.min_kib:g} KiB)')


if __name__ == '__main__':
    main()
