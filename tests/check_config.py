"""Exercise host configuration defaults, overrides and generated service settings."""

import json
import os
from pathlib import Path
import plistlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
NODE = os.environ.get('NODE', 'node')
from support import Checks

check = Checks()


from support import load

bridge_module = load("bridge_module", ROOT / 'host/hermes/bridge.py')

with tempfile.TemporaryDirectory(prefix='pocket-agent-config-', dir='/tmp') as folder:
    home = Path(folder).resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith('POCKET_AGENT')}
    env.update(HOME=str(home), POCKET_AGENT_PORT='none', POCKET_AGENT_WIFI='0')
    with patch.dict(os.environ, env, clear=True), patch.object(Path, 'home', return_value=home):
        bridge = bridge_module.Bridge()
        check(bridge.home == home / '.pocket-agent', 'Hermes default home uses project name')
        check(bridge.path == str(bridge.home / 'hub.sock'), 'default socket follows home')
        check(Path(bridge.hub_script) == ROOT / 'host/bridge/hub.mjs', 'maintained hub default')
        chosen = home / 'chosen'
        bridge = bridge_module.Bridge(home=chosen, socket_path=str(home / 'chosen.sock'))
        check(bridge.home == chosen, 'settings select home')
        bridge.last_spawn = time.monotonic() - 10
        with (
            patch('shutil.which', return_value=shutil.which(NODE)),
            patch('subprocess.Popen') as start,
        ):
            bridge._start_hub()
            child = start.call_args.kwargs['env']
            check(child['POCKET_AGENT_HOME'] == str(chosen), 'child hub receives selected home')
            check(child['POCKET_AGENT_SOCKET'] == bridge.path, 'child hub receives selected socket')
        with patch.dict(
            os.environ,
            {
                'POCKET_AGENT_HOME': str(home / 'override'),
                'POCKET_AGENT_SOCKET': str(home / 'override.sock'),
                'POCKET_AGENT_HUB_SCRIPT': str(home / 'override.mjs'),
            },
        ):
            bridge = bridge_module.Bridge(home=chosen, socket_path='ignored', hub_script='ignored')
            check(bridge.home == home / 'override', 'environment overrides home setting')
            check(
                bridge.path == str(home / 'override.sock'), 'environment overrides socket setting'
            )
            check(
                bridge.hub_script == str(home / 'override.mjs'),
                'environment overrides script setting',
            )

    target = subprocess.run(
        [
            'make',
            '--no-print-directory',
            '-n',
            'status',
            'BUILD_DIR=' + str(home / 'missing-build'),
            'HOME=' + str(home),
            'NODE=' + NODE,
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    check(
        str(home / '.pocket-agent') in target.stdout and 'POCKET_AGENT_HOME=' in target.stdout,
        'Make targets select the same new global configuration home',
    )

    hub = subprocess.Popen(
        [NODE, str(ROOT / 'host/bridge/hub.mjs')],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    try:
        socket_path = home / '.pocket-agent/hub.sock'
        deadline = time.monotonic() + 8
        while not socket_path.exists():
            if hub.poll() is not None:
                raise AssertionError('Default-home hub exited: ' + hub.stderr.read().decode())
            if time.monotonic() >= deadline:
                raise AssertionError('Default-home hub did not start')
            time.sleep(0.05)
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(3)
            peer.connect(str(socket_path))
            peer.sendall(b'{"t":"status"}\n')
            data = b''
            while b'\n' not in data:
                data += peer.recv(65536)
            status = json.loads(data.split(b'\n')[0])
        check(
            status['assistant'] is None and status['links'] == [],
            'default hub has no agent or device I/O',
        )
        # Copy the standalone CLI away from the checkout's existing private hub home.
        cli = home / 'checkout/host/bridge/cli.mjs'
        cli.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / 'host/bridge/cli.mjs', cli)
        result = subprocess.run(
            [NODE, str(cli), 'status'], env=env, capture_output=True, text=True, timeout=5
        )
        check(
            result.returncode == 0 and 'Wi-Fi disabled' in result.stdout,
            'CLI and hub share the new global home and radio flag',
        )
    finally:
        hub.terminate()
        hub.wait(timeout=5)
        hub.stderr.close()

    repo = home / 'hermes'
    (repo / 'tui_gateway').mkdir(parents=True)
    (repo / 'tui_gateway/entry.py').touch()
    model = home / 'model'
    model.mkdir()
    (model / 'model.bin').touch()
    python_entry = home / 'venv/bin/python'
    python_entry.parent.mkdir(parents=True)
    python_entry.symlink_to(sys.executable)
    output = home / 'service.plist'
    subprocess.run(
        [
            sys.executable,
            str(ROOT / 'tools/macos_service.py'),
            '--python',
            str(python_entry),
            '--node',
            shutil.which(NODE),
            '--hermes-repo',
            str(repo),
            '--hermes-home',
            str(repo),
            '--home',
            str(home / 'runtime'),
            '--voice-model',
            str(model),
            '--output',
            str(output),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    service = plistlib.loads(output.read_bytes())
    config = service['EnvironmentVariables']
    check(
        service['ProgramArguments'][0] == str(python_entry),
        'service preserves the supplied virtual-environment interpreter path',
    )
    check(config['POCKET_AGENT_HOME'] == str(home / 'runtime'), 'service home uses canonical name')
    check(
        config['POCKET_AGENT_SOCKET'] == str(home / 'runtime/hub.sock'),
        'service socket follows home',
    )
    check(
        config['POCKET_AGENT_HUB_SCRIPT'] == str(ROOT / 'host/bridge/hub.mjs'),
        'service uses maintained bridge',
    )
    check(
        service['ProgramArguments'][1] == str(ROOT / 'host/hermes/badge_chat.py'),
        'service uses maintained badge manager',
    )
    check(config['HERMES_DISABLE_LAZY_INSTALLS'] == '1', 'service cannot install dependencies')

print(f'Pocket Agent configuration: {check.count} checks')
