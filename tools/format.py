#!/usr/bin/env python3
"""Format maintained source with installed tools; never install packages."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'.git', '.build', 'node_modules', '__pycache__', '.venv'}
SUFFIXES = {
    'prettier': {'.js', '.mjs', '.json', '.yaml', '.yml', '.md'},
    'ruff': {'.py'},
    'clang-format': {'.c', '.cpp', '.h', '.ino'},
}


def git_files(changed):
    args = ['git', 'ls-files', '-z', '--others', '--exclude-standard']
    if not changed:
        args.append('--cached')
    output = subprocess.check_output(args, cwd=ROOT)
    if changed:
        output += subprocess.check_output(
            ['git', 'diff', '--name-only', '--diff-filter=ACMR', '-z', 'HEAD'], cwd=ROOT
        )
    return {os.fsdecode(name) for name in output.split(b'\0') if name}


def source_files(names):
    groups = {name: [] for name in SUFFIXES}
    allowed = git_files(False)
    for name in sorted(names):
        path = Path(name)
        path = path if path.is_absolute() else ROOT / path
        relative = path.resolve().relative_to(ROOT)
        if str(relative) not in allowed:
            continue
        if any(part in EXCLUDED for part in relative.parts) or path.name == 'package-lock.json':
            continue
        if path.is_symlink() or not path.is_file():
            continue
        for tool, suffixes in SUFFIXES.items():
            if path.suffix in suffixes:
                groups[tool].append(str(path))
    return groups


def executable(tool):
    configured = os.environ.get(tool.upper().replace('-', '_'))
    if configured:
        result = shutil.which(configured)
    elif tool == 'ruff':
        result = shutil.which(str(ROOT / '.build/python/bin/ruff')) or shutil.which('ruff')
    else:
        result = shutil.which(tool)
        if not result and tool == 'clang-format' and shutil.which('xcrun'):
            probe = subprocess.run(
                ['xcrun', '--find', 'clang-format'], capture_output=True, text=True
            )
            if probe.returncode == 0:
                result = probe.stdout.strip()
    if not result:
        raise ValueError(f'Missing {tool}; see docs/SETUP.md. No packages were installed.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check without writing files')
    parser.add_argument('--changed', action='store_true', help='Only changed and new Git files')
    parser.add_argument('files', nargs='*', help='Optional source files relative to the checkout')
    args = parser.parse_args()
    if args.files and args.changed:
        parser.error('Use file arguments or --changed, not both')
    try:
        groups = source_files(set(args.files) if args.files else git_files(args.changed))
        commands = []
        # Resolve every required tool before allowing any formatter to write.
        for tool, paths in groups.items():
            if not paths:
                continue
            if tool == 'prettier':
                binary = ROOT / 'node_modules/prettier/bin/prettier.cjs'
                if not binary.is_file():
                    raise ValueError(
                        'Missing Prettier; run npm ci when approved. No packages were installed.'
                    )
                node = shutil.which(os.environ.get('NODE', 'node'))
                if not node:
                    raise ValueError('Missing Node.js; see docs/SETUP.md.')
                command = [
                    node,
                    str(binary),
                    '--check' if args.check else '--write',
                    '--log-level',
                    'warn',
                ]
            elif tool == 'ruff':
                command = [executable(tool), 'format', '--quiet']
                if args.check:
                    command.append('--check')
            else:
                command = [executable(tool), '--style=file']
                command += ['--dry-run', '--Werror'] if args.check else ['-i']
            commands.append(command + paths)
        failed = False
        for command in commands:
            failed |= subprocess.run(command, cwd=ROOT).returncode != 0
        count = sum(map(len, groups.values()))
        action = 'Checked' if args.check else 'Formatted'
        print(f'{action} {count} maintained files.', flush=True)
        return int(failed)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Formatting failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
