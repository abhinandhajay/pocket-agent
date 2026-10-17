#!/usr/bin/env python3
"""Format changed source at Codex turn completion; leave the Git index untouched."""

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    event = {}
    try:
        event = json.load(sys.stdin)
        if not isinstance(event, dict):
            raise ValueError('Expected a Codex hook event')
        if event.get('hook_event_name') != 'Stop':
            print('{}')
            return
        python = ROOT / '.build/python/bin/python'
        result = subprocess.run(
            [
                str(python) if python.is_file() else sys.executable,
                str(ROOT / 'tools/format.py'),
                '--changed',
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=50,
        )
        if result.returncode:
            raise ValueError((result.stderr or result.stdout).strip()[-2000:])
        print('{}')
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        reason = (
            'Automatic formatting failed. Run make format and resolve the failure before finishing. '
            + str(error)
        )
        # Ask for one repair pass, then report persistent failures without an endless Stop loop.
        response = (
            {'systemMessage': reason}
            if isinstance(event, dict) and event.get('stop_hook_active')
            else {'decision': 'block', 'reason': reason}
        )
        print(json.dumps(response))


if __name__ == '__main__':
    main()
