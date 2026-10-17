"""Incremental staging and asset cache invalidation without downloading dependencies."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from support import Checks, load

check = Checks()
root = Path(__file__).resolve().parents[1]
build = load('build_tools', root / 'tools/build.py')
derive = load('derive_assets', root / 'tools/assets/derive_assets.py')
with tempfile.TemporaryDirectory() as folder:
    work = Path(folder)
    source = work / 'firmware/pocket_agent'
    source.mkdir(parents=True)
    sketch = source / 'pocket_agent.ino'
    sketch.write_text('void setup() {}\n')
    with patch.object(build, 'ROOT', work):
        target = build.stage(work / 'out')
        staged = target / sketch.name
        before = staged.stat().st_mtime_ns
        build.stage(work / 'out')
        check(staged.stat().st_mtime_ns == before, 'unchanged source is not copied')
        sketch.write_text('void setup() { begin(); }\n')
        build.stage(work / 'out')
        check(staged.read_bytes() == sketch.read_bytes(), 'changed source is staged')
        (target / 'removed.h').touch()
        (target / 'avatar_assets.h').write_text('cached asset')
        build.stage(work / 'out')
        check(not (target / 'removed.h').exists(), 'removed source is removed from staged sketch')
        check(
            (target / 'avatar_assets.h').read_text() == 'cached asset', 'generated header retained'
        )

    for name in ('derive_assets.py', 'gen_assets.py', 'robot_art.py'):
        (work / name).write_text('generator input')
    output = work / 'avatar.h'
    calls = []

    def generate(destination):
        calls.append(destination)
        Path(destination).write_text('generated geometry')

    with (
        patch.object(derive, '__file__', str(work / 'derive_assets.py')),
        patch.dict(
            sys.modules,
            {
                'gen_assets': SimpleNamespace(main=generate),
            },
        ),
        patch.object(sys, 'argv', ['derive_assets.py', str(output)]),
        contextlib.redirect_stdout(io.StringIO()),
    ):
        derive.main()
        before = output.stat().st_mtime_ns
        derive.main()
        check(
            len(calls) == 1 and output.stat().st_mtime_ns == before, 'unchanged assets are reused'
        )
        (work / 'robot_art.py').write_text('changed artwork')
        derive.main()
        check(len(calls) == 2, 'generator changes invalidate the cache')
        output.write_text('damaged header')
        derive.main()
        check(
            len(calls) == 3 and output.read_text() == 'generated geometry',
            'damaged output is repaired',
        )
        output.unlink()
        derive.main()
        check(len(calls) == 4, 'missing output is regenerated')
        stamp = output.with_suffix('.stamp.json')
        stamp.write_text('invalid JSON')
        derive.main()
        check(len(calls) == 5, 'damaged stamp is repaired')
print(f'Incremental build: {check.count} checks passed')
