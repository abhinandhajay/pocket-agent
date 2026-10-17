#!/usr/bin/env python3
"""Run the configured Codex Stop hook with real formatters and an isolated Git index."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
from support import Checks

ROOT = Path(__file__).resolve().parents[1]
check = Checks()


def main():
    config = json.loads((ROOT / '.codex/hooks.json').read_text())
    check(set(config['hooks']) == {'Stop'}, 'format once at turn completion')
    handler = config['hooks']['Stop'][0]['hooks'][0]
    check(handler['type'] == 'command' and handler['timeout'] == 60, 'bounded command hook')
    check('git rev-parse --show-toplevel' in handler['command'], 'subdirectory-safe command')
    index = Path(
        subprocess.check_output(
            ['git', 'rev-parse', '--git-path', 'index'], cwd=ROOT, text=True
        ).strip()
    )
    index = index if index.is_absolute() else ROOT / index
    before = index.read_bytes() if index.exists() else None
    with tempfile.TemporaryDirectory(prefix='.agent-hook-fixture-', dir=ROOT) as folder:
        fixture = Path(folder)
        env = dict(os.environ, GIT_INDEX_FILE=str(fixture / 'index'))

        def git(*args):
            return subprocess.check_output(['git', *args], cwd=ROOT, env=env)

        git('read-tree', 'HEAD')

        def run(event=None, extra=None, raw=None):
            result = subprocess.run(
                handler['command'],
                shell=True,
                cwd=fixture,
                input=raw
                if raw is not None
                else json.dumps(event or {'hook_event_name': 'Stop', 'stop_hook_active': False}),
                env=dict(env, **(extra or {})),
                capture_output=True,
                text=True,
                timeout=60,
            )
            check(result.returncode == 0, 'hook returns structured completion feedback')
            return json.loads(result.stdout)

        sources = {
            fixture / 'assistant input.py': 'def f():\n  return {"ok":True}\n',
            fixture
            / '-options.json': '{"ok":true,"description":"Long JSON content for checking four-space multiline indentation without using a snapshot of the formatter implementation."}\n',
            fixture / 'assistant.cpp': 'int main(){if(true){return 0;}return 1;}\n',
        }
        for path, text in sources.items():
            path.write_text(text)
        untouched = fixture / 'notes.txt'
        untouched.write_text('unformatted unsupported text\n')
        staged_before = git('ls-files', '--stage', '-z')
        check(run() == {}, 'success produces valid quiet Stop JSON')
        for path, expected in zip(sources, (b'    return', b'    "ok"', b'    if')):
            check(expected in path.read_bytes(), 'real four-space formatting')
        check(git('ls-files', '--stage', '-z') == staged_before, 'formatting never stages files')
        check(
            untouched.read_text() == 'unformatted unsupported text\n', 'unsupported files preserved'
        )
        formatted = {p: p.read_bytes() for p in sources}
        check(run() == {} and formatted == {p: p.read_bytes() for p in sources}, 'idempotence')
        py = next(iter(sources))
        py.write_text(sources[py])
        relative = str(py.relative_to(ROOT))
        git('add', '--', relative)
        staged = git('show', ':' + relative)
        py.write_text(py.read_text() + '# unstaged change\n')
        check(run() == {}, 'partially staged working file can be formatted')
        check(git('show', ':' + relative) == staged, 'staged bytes preserved')
        check(
            py.read_text().endswith('# unstaged change\n') and '    return' in py.read_text(),
            'working changes retained',
        )
        for path, text in sources.items():
            path.write_text(text)
        working = {p: p.read_bytes() for p in sources}
        result = run(extra={'RUFF': '/missing/formatter'})
        check(
            result.get('decision') == 'block' and 'Missing ruff' in result['reason'],
            'formatter failure requests a repair pass',
        )
        check(
            working == {p: p.read_bytes() for p in sources}, 'missing-tool preflight writes nothing'
        )
        result = run(
            {'hook_event_name': 'Stop', 'stop_hook_active': True},
            extra={'RUFF': '/missing/formatter'},
        )
        check(
            'systemMessage' in result and 'decision' not in result,
            'persistent failure cannot create an endless continuation loop',
        )
        check(run({'hook_event_name': 'SessionStart'}) == {}, 'other events ignored')
        check(working == {p: p.read_bytes() for p in sources}, 'unrelated event writes nothing')
        check(
            run(raw='not json').get('decision') == 'block',
            'malformed event rejected without formatting',
        )
        excluded = ROOT / '.build/agent-hook-excluded.py'
        check(not excluded.exists(), 'isolated ignored fixture')
        try:
            excluded.write_text('invalid ignored Python fixture\n')
            git('add', '-f', '--', str(excluded.relative_to(ROOT)))
            check(run() == {}, 'ignored source never formatted')
            check(
                excluded.read_text() == 'invalid ignored Python fixture\n',
                'ignored bytes preserved',
            )
        finally:
            excluded.unlink(missing_ok=True)
    check((index.read_bytes() if index.exists() else None) == before, 'real index unchanged')
    print(
        f'Codex Stop hook: {check.count} checks passed; real formatters, isolated index, no commits.'
    )


if __name__ == '__main__':
    main()
