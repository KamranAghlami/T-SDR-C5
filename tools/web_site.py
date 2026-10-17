#!/usr/bin/env python3
"""Lay out every release for the browser flasher.

Inputs: the JSON of `gh release list --json tagName,isPrerelease,isLatest,publishedAt`
and a directory holding each release's extracted archive (T-SDR-C5-<tag>/, or
t-sdr-c5-<tag>/ for releases before the rename, with offsets.json and the images). Output, under the site directory:

  firmware/<tag>/*.bin     the images of each release
  manifests/<tag>.json     parts and offsets per release (ESP Web Tools format)
  versions.json            [{version, date, prerelease, latest}], newest first

Manifests list the bootloader, partition table and app separately (not the
merged image), so the NVS partition between them, which holds the user's
settings, survives an update.
"""
import argparse
import json
import shutil
from pathlib import Path


def manifest(version, offsets):
    return {
        'name': 'T-SDR-C5',
        'version': version,
        'new_install_prompt_erase': True,
        'builds': [{
            'chipFamily': 'ESP32-C5',
            'parts': [{'path': f'../firmware/{version}/{p["file"]}', 'offset': int(p['offset'], 16)} for p in offsets],
        }],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('releases', type=Path)
    parser.add_argument('archives', type=Path)
    parser.add_argument('site', type=Path)
    args = parser.parse_args()

    releases = json.loads(args.releases.read_text())
    versions = []
    for release in sorted(releases, key=lambda r: r['publishedAt'], reverse=True):
        tag = release['tagName']
        candidates = [args.archives / f'{name}-{tag}' for name in ('T-SDR-C5', 't-sdr-c5')]
        source = next((c for c in candidates if (c / 'offsets.json').exists()), candidates[0])
        offsets_file = source / 'offsets.json'
        if not offsets_file.exists():
            print(f'skipping {tag}: no extracted archive')
            continue
        offsets = json.loads(offsets_file.read_text())

        firmware = args.site / 'firmware' / tag
        firmware.mkdir(parents=True, exist_ok=True)
        for part in offsets:
            shutil.copy2(source / part['file'], firmware / part['file'])

        manifests = args.site / 'manifests'
        manifests.mkdir(parents=True, exist_ok=True)
        (manifests / f'{tag}.json').write_text(json.dumps(manifest(tag, offsets), indent=2) + '\n')

        versions.append({
            'version': tag,
            'date': release['publishedAt'][:10],
            'prerelease': release['isPrerelease'],
            'latest': release['isLatest'],
        })
        print(f'added {tag}')

    if not versions:
        raise SystemExit('no releases found')
    (args.site / 'versions.json').write_text(json.dumps(versions, indent=2) + '\n')


if __name__ == '__main__':
    main()
