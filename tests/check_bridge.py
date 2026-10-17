"""Real isolated hub startup, reconnect replay and producer ownership; no board/model I/O."""

import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch
from support import Checks, load, wait

check = Checks()
source = Path(sys.argv[1]).resolve()
module = load('bridge_fixture', source / 'bridge.py')

with tempfile.TemporaryDirectory(prefix='badge-bridge-', dir='/tmp') as folder:
    home = Path(folder)
    env = {
        **os.environ,
        'POCKET_AGENT_HOME': folder,
        'POCKET_AGENT_SOCKET': str(home / 'hub.sock'),
        'POCKET_AGENT_PORT': 'none',
        'POCKET_AGENT_WIFI': '0',
    }
    child = None
    pids = set()

    def status():
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(1)
            peer.connect(str(home / 'hub.sock'))
            peer.sendall(b'{"t":"status"}\n')
            data = b''
            while b'\n' not in data:
                data += peer.recv(8192)
            return json.loads(data.split(b'\n')[0])

    def observed(predicate):
        try:
            pids.add(int((home / 'hub.lock').read_text()))
            return predicate(status())
        except (OSError, ValueError):
            return False

    def stop_hub():
        pid = int((home / 'hub.lock').read_text())
        pids.add(pid)
        os.kill(pid, signal.SIGTERM)
        wait(lambda: not (home / 'hub.sock').exists(), label='hub shutdown')

    with patch.dict(os.environ, env, clear=True):
        bridge = module.Bridge()
        bridge.last_spawn = time.monotonic() - 10
        check(not (home / 'hub.sock').exists() and bridge.worker is None, 'construction has no I/O')
        try:
            start = time.monotonic()
            bridge.update('badge-session', 'idle', name='Badge chat', voice=True)
            check(time.monotonic() - start < 0.1, 'state updates never wait for hub')
            wait(lambda: observed(lambda s: s['assistant'] is not None), 8, 'on-demand hub startup')
            assistant = status()['assistant']
            stable = assistant['id']
            check(
                assistant['name'] == 'Badge chat' and assistant['voice'],
                'voice ready session report',
            )
            for state in ('thinking', 'tool', 'waiting', 'error', 'done', 'sleeping', 'idle'):
                bridge.update('badge-session', state, 'token=fixture-secret', voice=True)
                wait(lambda: observed(lambda s: s['assistant']['state'] == state))
                check(
                    'fixture-secret' not in status()['assistant']['detail'], 'redacted state detail'
                )
            bridge.update('second', 'tool', name='other')
            time.sleep(0.2)
            check(
                status()['assistant'] is not None and status()['assistant']['id'] == stable,
                'second session cannot replace the Assistant',
            )
            bridge.end('second')
            check(bridge.sid == 'badge-session', 'unrelated end preserves the owner')
            stop_hub()
            bridge.last_spawn = time.monotonic() - 10
            wait(lambda: observed(lambda s: s['assistant'] is not None), 8, 'reconnect replay')
            check(status()['assistant']['id'] == stable, 'identity survives hub restart')
            bridge.close()
            wait(lambda: observed(lambda s: not s['assistant']))
            check(not status()['assistant'], 'close removes the owned Assistant')
            code = (
                "from pathlib import Path; import importlib.util,time; s=importlib.util.spec_from_file_location('b',%r); b=importlib.util.module_from_spec(s);s.loader.exec_module(b);p=b.Bridge();p.update('crash','tool');time.sleep(60)"
                % str(source / 'bridge.py')
            )
            child = subprocess.Popen([sys.executable, '-c', code], env=env)
            wait(lambda: observed(lambda s: s['assistant'] is not None))
            child.kill()
            child.wait(timeout=3)
            wait(lambda: observed(lambda s: not s['assistant']))
            check(not status()['assistant'], 'crashed producer removed')
            check(
                'fixture-secret' not in (home / 'hub.log').read_text(),
                'payload secrets absent from logs',
            )
        finally:
            bridge.close()
            if child and child.poll() is None:
                child.kill()
                child.wait(timeout=3)
            for pid in pids:
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
    with patch.dict(os.environ, {'POCKET_AGENT_DETAIL': '0'}):
        check(module.detail('terminal', {'command': 'private'}) == 'terminal', 'detail suppression')
    check(
        module.detail('read_file', {'path': '/private/project/file.py'}) == 'read_file file.py',
        'basename only',
    )
    check(
        module.detail('terminal', {'command': 'echo token=secret\nprivate'}) == '$ echo token=***',
        'first line redacted',
    )
    for case in json.loads(Path(__file__).with_name('redaction_cases.json').read_text()):
        text = module.redact(case['in'])
        if 'equals' in case:
            check(text == case['equals'], 'shared redaction case')
        if 'must_not_contain' in case:
            check(case['must_not_contain'] not in text, 'secret removal')
print(f'Badge bridge: {check.count} checks passed with a real isolated hub')
