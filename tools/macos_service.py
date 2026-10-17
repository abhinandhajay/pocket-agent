#!/usr/bin/env python3
"""Generate a reviewable macOS LaunchAgent without installing or starting it."""

import argparse
from pathlib import Path
import plistlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('python', 'hermes-repo', 'hermes-home', 'home', 'voice-model', 'node', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--label', default='io.pocket-agent.badge-chat')
    args = parser.parse_args()
    for path in (
        args.python,
        args.hermes_repo / 'tui_gateway/entry.py',
        args.node,
        args.voice_model / 'model.bin',
    ):
        if not path.is_file():
            parser.error(f'Missing prerequisite: {path}')
    home = args.home.expanduser().resolve()
    command = [
        # Keep the venv entry path; resolving its symlink loses installed packages.
        str(args.python.expanduser().absolute()),
        str(ROOT / 'host/hermes/badge_chat.py'),
        '--hermes-home',
        str(args.hermes_home.resolve()),
        '--hermes-repo',
        str(args.hermes_repo.resolve()),
        '--home',
        str(home),
        '--voice-model',
        str(args.voice_model.resolve()),
    ]
    # Launchd does not expand shell variables; capture the supplied locations.
    env = {
        'PATH': str(args.node.resolve().parent) + ':/usr/bin:/bin:/usr/sbin:/sbin',
        'POCKET_AGENT_HOME': str(home),
        'POCKET_AGENT_SOCKET': str(home / 'hub.sock'),
        'POCKET_AGENT_HUB_SCRIPT': str(ROOT / 'host/bridge/hub.mjs'),
        'HERMES_DISABLE_LAZY_INSTALLS': '1',
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(
        plistlib.dumps(
            {
                'Label': args.label,
                'ProgramArguments': command,
                'WorkingDirectory': str(ROOT),
                'EnvironmentVariables': env,
                'RunAtLoad': True,
                'KeepAlive': True,
                'ThrottleInterval': 10,
            }
        )
    )
    print(f'Generated {args.output}; review before installing into ~/Library/LaunchAgents/.')


if __name__ == '__main__':
    main()
