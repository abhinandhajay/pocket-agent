#!/usr/bin/env python3
"""Prepare a private Arduino sketch and project-local toolchain configuration."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def config(build):
    toolchain = build / 'toolchain'
    toolchain.mkdir(parents=True, exist_ok=True)
    # JSON is valid YAML; encode paths safely on all platforms.
    path = toolchain / 'arduino-cli.yaml'
    path.write_text(
        json.dumps(
            {
                'board_manager': {
                    'additional_urls': [
                        'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
                    ]
                },
                'directories': {key: str(toolchain / key) for key in ('data', 'downloads', 'user')},
            },
            indent=2,
        )
        + '\n'
    )
    return path


def stage(build):
    source = ROOT / 'firmware/pocket_agent'
    target = build / 'sketch/pocket_agent'
    target.mkdir(parents=True, exist_ok=True)
    for path in target.iterdir():
        if (
            path.suffix in ('.h', '.cpp', '.ino')
            and path.name != 'avatar_assets.h'
            and not (source / path.name).exists()
        ):
            path.unlink()
    for path in source.iterdir():
        if path.suffix in ('.h', '.cpp', '.ino'):
            output = target / path.name
            if not output.is_file() or output.read_bytes() != path.read_bytes():
                shutil.copy2(path, output)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['config', 'stage', 'check-toolchain'])
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.build')
    parser.add_argument('--cli', default='arduino-cli')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if args.action == 'config':
        print(config(build))
    elif args.action == 'stage':
        print(stage(build))
    else:
        cfg = config(build)
        output = subprocess.check_output(
            [args.cli, '--config-file', str(cfg), 'core', 'list', '--format', 'json'], text=True
        )
        cores = json.loads(output)
        if isinstance(cores, dict):
            cores = cores.get('platforms', [])
        if not any(
            p.get('id') == 'esp32:esp32'
            and str(p.get('installed', p.get('installed_version'))) == '3.3.10'
            for p in cores
        ):
            raise SystemExit('Install the pinned ESP32 core 3.3.10; see docs/SETUP.md.')
        for name, version in [('M5Unified', '0.2.19'), ('M5GFX', '0.2.26')]:
            properties = build / 'toolchain/user/libraries' / name / 'library.properties'
            if (
                not properties.is_file()
                or f'version={version}' not in properties.read_text().splitlines()
            ):
                raise SystemExit(f'Install the pinned {name} {version}; see docs/SETUP.md.')
        print('Pinned ESP32 core and M5 libraries verified.')


if __name__ == '__main__':
    main()
