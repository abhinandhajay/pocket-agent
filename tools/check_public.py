#!/usr/bin/env python3
"""Check publishable working files without printing private values."""

import argparse
import ast
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'.git', '.build', 'node_modules', '__pycache__', '.venv'}
DOC_IMAGES = {
    'docs/images/assistant.png',
    'docs/images/reply.png',
    'docs/images/settings.png',
    'docs/images/moods.png',
}
PATTERNS = {
    'personal home path': re.compile(r'/(?:Users|home)/[A-Za-z0-9_.-]+/'),
    'private key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'credential-shaped token': re.compile(
        r'\b(?:sk-[A-Za-z0-9_-]{32,}|gh[pousr]_[A-Za-z0-9]{30,}|AKIA[A-Z0-9]{16})\b'
    ),
}


def files():
    # Include new source as well as tracked files, while respecting ignore rules.
    output = subprocess.run(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        cwd=ROOT,
        capture_output=True,
    )
    if output.returncode == 0:
        names = sorted(set(output.stdout.decode().split('\0')) - {''})
    else:
        # Source archives can be checked without initializing a Git repository.
        names = []
        for folder, directories, children in os.walk(ROOT):
            directories[:] = [name for name in directories if name not in EXCLUDED]
            names.extend(str((Path(folder) / name).relative_to(ROOT)) for name in children)
    for name in names:
        path = ROOT / name
        if path.is_file() and not any(part in EXCLUDED for part in path.relative_to(ROOT).parts):
            yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--history',
        action='store_true',
        help='Also scan historical Git blobs for credential patterns',
    )
    args = parser.parse_args()
    problems = []
    count = 0
    for path in files():
        count += 1
        name = str(path.relative_to(ROOT))
        raw = path.read_bytes()
        if len(raw) > 2 * 1024 * 1024:
            problems.append(f'{name}: oversized source artifact')
        # Only deliberately reviewed README exports may bypass the text scan.
        if name in DOC_IMAGES:
            if not raw.startswith(b'\x89PNG\r\n\x1a\n'):
                problems.append(f'{name}: expected PNG documentation image')
            continue
        try:
            source = raw.decode('utf-8')
        except UnicodeDecodeError:
            problems.append(f'{name}: binary artifact outside ignored build output')
            continue
        for line, content in enumerate(source.splitlines(), 1):
            for label, pattern in PATTERNS.items():
                if pattern.search(content):
                    problems.append(f'{name}:{line}: {label}')
        if path.suffix == '.py':
            try:
                ast.parse(source, filename=name)
            except SyntaxError as exc:
                problems.append(f'{name}:{exc.lineno}: Python syntax error')
        if path.suffix == '.md':
            for target in re.findall(r'\]\(([^\s)]+)\)', source):
                if ':' in target or target.startswith('#') or '{' in target:
                    continue
                dest = target.split('#')[0]
                if not dest:
                    continue
                resolved = (path.parent / dest).resolve()
                # Private review artifacts are intentionally absent from public source.
                if any(
                    part in EXCLUDED or part in {'assets', 'upstream', 'candidate'}
                    for part in resolved.parts
                ):
                    continue
                if not resolved.exists():
                    problems.append(f'{name}: missing relative link {dest}')
    if args.history:
        revisions = subprocess.check_output(
            ['git', 'rev-list', '--objects', '--all'], cwd=ROOT, text=True
        )
        seen = set()
        historical = 0
        for row in revisions.splitlines():
            oid, _, name = row.partition(' ')
            if not name or oid in seen:
                continue
            seen.add(oid)
            kind = subprocess.check_output(
                ['git', 'cat-file', '-t', oid], cwd=ROOT, text=True
            ).strip()
            if kind != 'blob':
                continue
            source = subprocess.check_output(['git', 'cat-file', 'blob', oid], cwd=ROOT).decode(
                'utf-8', errors='replace'
            )
            historical += 1
            for label, pattern in PATTERNS.items():
                # The historical redaction test contains a literal, deliberately invalid key.
                inspected = source
                if label == 'private key' and name.endswith('check_reply_output.py'):
                    inspected = '\n'.join(
                        line for line in source.splitlines() if 'private fixture' not in line
                    )
                if label != 'personal home path' and pattern.search(inspected):
                    problems.append(f'Git blob {oid[:12]} ({name}): {label}')
        print(f'Checked {historical} historical blobs for credential patterns.')
    if problems:
        print('\n'.join(problems))
        raise SystemExit(1)
    print(
        f'Public-source check passed for {count} files. Pattern checks do not replace manual review.'
    )


if __name__ == '__main__':
    main()
