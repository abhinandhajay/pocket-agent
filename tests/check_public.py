"""Keep documentation image exceptions scoped without weakening source checks."""

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

from support import Checks, load

ROOT = Path(__file__).resolve().parents[1]
public = load('public_check', ROOT / 'tools/check_public.py')


def main():
    check = Checks()
    image = (ROOT / 'docs/images/assistant.png').read_bytes()
    cases = [
        ('docs/images/assistant.png', image, None),
        ('docs/images/reply.png', image, None),
        ('docs/images/settings.png', image, None),
        ('docs/images/moods.png', image, None),
        ('docs/images/assistant.png', b'not a PNG', 'expected PNG documentation image'),
        ('docs/images/unreviewed.png', image, 'binary artifact outside ignored build output'),
        ('firmware/assistant.png', image, 'binary artifact outside ignored build output'),
        (
            'docs/images/assistant.png',
            image + bytes(2 * 1024 * 1024),
            'oversized source artifact',
        ),
        ('README.md', b'[missing](missing.md)', 'missing relative link'),
        ('config.txt', b'/Users/' + b'private-example/file', 'personal home path'),
    ]
    for name, contents, expected in cases:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
            output = StringIO()
            with (
                patch.object(public, 'ROOT', root),
                patch.object(public, 'files', return_value=iter([path])),
                patch.object(sys, 'argv', ['check_public.py']),
                redirect_stdout(output),
            ):
                try:
                    public.main()
                    passed = True
                except SystemExit as error:
                    check(error.code == 1, name + ': expected validation failure')
                    passed = False
            check(passed == (expected is None), name + ': acceptance')
            if expected:
                check(expected in output.getvalue(), name + ': diagnostic')
    print(f'Public-source guard: {check.count} checks passed')


if __name__ == '__main__':
    main()
