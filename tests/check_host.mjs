#!/usr/bin/env node
// Portable host-bridge checks (no board or agent): the
// hub's session merge over a temporary socket. Usage:
//   node check_host.mjs <host-candidate-dir>
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { HubFixture, sleep } from './fixtures/hub.mjs';

const dir = path.resolve(process.argv[2] ?? '');
let checks = 0;
const ok = (cond, msg) => {
    assert.ok(cond, msg);
    checks++;
};

// One Assistant owner, with diagnostics on independent control sockets.
const fixture = new HubFixture(dir);
const mergeHome = fixture.home,
    sock = fixture.socketPath;
fixture.start();
const client = () => fixture.connect();
const send = (c, m) => c.write(JSON.stringify(m) + '\n');
const status = () => fixture.ask();
try {
    const a = await client(),
        b = await client();
    const { request } = await import(pathToFileURL(path.join(dir, 'cli.mjs')));
    const raw = await request({ t: 'raw', line: 'bounce', reply: true }, 'result', {
        socketPath: sock,
    });
    ok(
        !raw.ok && raw.message.includes('no active badge link'),
        'diagnostic fails when the real hub has no board',
    );
    const duplicate = spawn(process.execPath, [path.join(dir, 'hub.mjs')], {
        env: {
            ...process.env,
            POCKET_AGENT_HOME: mergeHome,
            POCKET_AGENT_WIFI: '0',
            POCKET_AGENT_SOCKET: sock,
            POCKET_AGENT_PORT: 'none',
        },
        stdio: 'ignore',
    });
    const duplicateCode = await new Promise((resolve) => duplicate.on('exit', resolve));
    ok(duplicateCode === 0, 'a duplicate hub exits without replacing the live socket');
    a.write(
        'null\n[]\n{"t":"hello"}\n{"t":"hello","id":"bad","cwd":42,"name":{"toString":null}}\n',
    );
    await sleep(100);
    ok(
        (await status()).assistant.name === 'agent',
        'missing usable name falls back to a generic agent label',
    );
    send(a, { t: 'bye', id: 'bad' });
    send(a, { t: 'hello', id: 'session-aaaaaaaa1111', name: 'caf\u00e9 | pipes' });
    send(a, { t: 'state', id: 'session-aaaaaaaa1111', state: 'tool', detail: '$ make' });
    send(b, { t: 'hello', id: 'session-bbbbbbbb2222', name: 'other' });
    send(b, { t: 'state', id: 'session-aaaaaaaa1111', state: 'done' });
    await sleep(200);
    let st = await status();
    ok(st.assistant !== null, 'only one Assistant is registered');
    const pa = st.assistant;
    ok(pa.id === 'aaaa1111', 'stable wire identity');
    ok(pa.state === 'tool' && pa.detail === '$ make', 'unrelated owner cannot overwrite state');
    ok(/^[\x20-\x7e]*$/.test(pa.name) && !pa.name.includes('|'), 'display text stays printable');
    ok(pa.style === 0, 'Assistant uses one stable style');
    send(a, { t: 'state', id: 'session-aaaaaaaa1111', state: 'unknown' });
    await sleep(100);
    ok((await status()).assistant.state === 'idle', 'unknown states become idle');
    b.destroy();
    await sleep(100);
    ok((await status()).assistant !== null, 'control disconnect does not remove Assistant');
    send(a, { t: 'bye', id: 'session-aaaaaaaa1111' });
    await sleep(100);
    ok((await status()).assistant === null, 'owner bye removes Assistant');
    for (let i = 0; i < 10; i++) send(a, { t: 'hello', id: `session-xxxxxxx${i}`, name: `n${i}` });
    await sleep(100);
    ok((await status()).assistant !== null, 'one connection cannot register a second Assistant');
    a.end();
} finally {
    await fixture.close();
}
// 3. Wi-Fi link: the hub dials a paired board, both sides prove the token, traffic flows.
{
    const crypto = await import('node:crypto');
    const hmac = (k, m) => crypto.createHmac('sha256', k).update(m).digest('hex');
    const wireless = new HubFixture(dir);
    const home = wireless.home;
    const token = crypto.randomBytes(32).toString('hex'),
        devId = 'a1b2c3d4e5f6';
    fs.writeFileSync(
        path.join(home, 'devices.json'),
        JSON.stringify({ [devId]: { token, ip: '127.0.0.1' } }),
    );
    let attempts = 0,
        hubProved = false,
        refusedBad = false,
        linked = false;
    const received = [];
    let goodPeer;
    const board = net.createServer((c) => {
        attempts++;
        const bad = attempts === 1; // first attempt answers with a wrong AUTH
        const nonceD = crypto.randomBytes(8).toString('hex');
        c.setEncoding('latin1');
        c.write(`HELLO ${devId} ${nonceD}\n`);
        let buf = '',
            stage = 'challenge';
        c.on('data', (d) => {
            buf += d;
            let i;
            while ((i = buf.indexOf('\n')) >= 0) {
                const line = buf.slice(0, i);
                buf = buf.slice(i + 1);
                if (stage === 'challenge') {
                    const [, nonceH, macH] = line.split(' ');
                    if (macH === hmac(token, `hub:${nonceD}:${nonceH}`)) hubProved = true;
                    c.write(
                        `AUTH ${bad ? '0'.repeat(64) : hmac(token, `dev:${nonceH}:${nonceD}`)}\n`,
                    );
                    stage = 'ok';
                } else if (stage === 'ok') {
                    if (bad) {
                        if (line === 'DENY auth') refusedBad = true;
                    } else if (line === 'OK') {
                        linked = true;
                        stage = 'open';
                        goodPeer = c;
                    }
                } else received.push(line);
            }
        });
        c.on('error', () => {});
    });
    await new Promise((r) => board.listen(0, '127.0.0.1', r));
    const boardPort = board.address().port;
    const sock2 = wireless.socketPath;
    wireless.start({
        env: { POCKET_AGENT_WIFI: '1', POCKET_AGENT_DEVICE_PORT: String(boardPort) },
    });
    try {
        await sleep(7500); // dials every 3 s: first attempt refused, second links
        ok(hubProved, 'the hub proves the pairing token to the board');
        ok(refusedBad, 'a board with the wrong key is refused (DENY auth)');
        ok(linked, 'a board with the right key links (OK)');
        const c = net.createConnection(sock2);
        await new Promise((r) => c.on('connect', r));
        c.write(
            JSON.stringify({ t: 'hello', id: 'session-wifi00000001', name: 'wireless' }) + '\n',
        );
        c.write(
            JSON.stringify({
                t: 'state',
                id: 'session-wifi00000001',
                state: 'tool',
                detail: '$ make',
            }) + '\n',
        );
        await sleep(400);
        ok(received.includes('assistant-clear *'), 'a new Wi-Fi link is resynced');
        ok(
            received.some(
                (l) => l.startsWith('assistant 00000001 tool ') && l.endsWith('wireless|$ make'),
            ),
            'Assistant state updates flow over Wi-Fi',
        );
        ok(
            !received.some((l) => /^(wifi|pair|forget)\b/.test(l)),
            'no provisioning traffic over Wi-Fi',
        );
        const reply = new Promise((resolve) =>
            c.once('data', (chunk) => resolve(JSON.parse(String(chunk).split('\n')[0]))),
        );
        c.write(JSON.stringify({ t: 'status' }) + '\n');
        const wirelessStatus = await reply;
        ok(
            wirelessStatus.links.some((l) => l.kind === 'wifi' && l.authed) &&
                !wirelessStatus.wifi.dialing.includes(devId),
            'an authenticated connection is no longer reported as dialing',
        );
        received.length = 0;
        goodPeer.write('GB_READY\n');
        await sleep(300);
        ok(
            received.includes('assistant-clear *') &&
                received.some((l) => l.startsWith('assistant 00000001 tool ')),
            'GB_READY resends the complete Assistant state on Wi-Fi',
        );
        c.write(JSON.stringify({ t: 'raw', line: 'wifi ZmFrZQ== ZmFrZQ==' }) + '\n');
        await sleep(100);
        ok(
            !received.some((l) => l.startsWith('wifi ')),
            'raw provisioning is never forwarded to Wi-Fi',
        );
        c.end();
    } finally {
        await wireless.close();
        goodPeer?.destroy();
        await new Promise((resolve) => board.close(resolve));
    }
}
console.log(`pocket-agent host bridge: ${checks} checks passed.`);
