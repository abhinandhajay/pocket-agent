// Real isolated hub: permission errors, authenticated recovery and USB/Wi-Fi failover.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { HubFixture, sleep } from './fixtures/hub.mjs';
const candidate = path.resolve(process.argv[2]),
    here = path.dirname(new URL(import.meta.url).pathname);
const fixtureHub = new HubFixture(candidate);
const home = fixtureHub.home,
    socketPath = fixtureHub.socketPath,
    fixture = path.join(home, 'usb.json');
const id = 'a1b2c3d4e5f6',
    token = crypto.randomBytes(32).toString('hex');
let config = { id, token, wifi: 'connected', connectDenied: true },
    owner,
    peer,
    checks = 0;
const save = (change) => {
    config = { ...config, ...change };
    fs.writeFileSync(fixture, JSON.stringify(config));
};
save({});
fs.writeFileSync(
    path.join(home, 'devices.json'),
    JSON.stringify({ [id]: { token, ip: '127.0.0.1' } }),
    { mode: 0o600 },
);
const commands = [],
    mac = (text) => crypto.createHmac('sha256', token).update(text).digest('hex');
const ok = (value, label) => {
    assert(value, label);
    checks++;
};
const board = net.createServer((c) => {
    peer = c;
    c.setEncoding('utf8');
    c.on('error', () => {});
    let buf = '',
        stage = 0;
    const nonce = crypto.randomBytes(8).toString('hex');
    c.write(`HELLO ${id} ${nonce}\n`);
    c.on('data', (data) => {
        buf += data;
        while (buf.includes('\n')) {
            const i = buf.indexOf('\n'),
                line = buf.slice(0, i);
            buf = buf.slice(i + 1);
            const a = line.split(' ');
            if (stage === 0) {
                assert(a[0] === 'CHALLENGE' && a[2] === mac(`hub:${nonce}:${a[1]}`));
                c.write(`AUTH ${mac(`dev:${a[1]}:${nonce}`)}\n`);
                stage = 1;
            } else if (stage === 1) {
                assert(line === 'OK');
                stage = 2;
            } else {
                commands.push(line);
                if (line === 'hello')
                    c.write(`GB_HELLO id=${id} paired=1 wifi=connected ip=127.0.0.1 err=none\n`);
            }
        }
    });
});
await new Promise((resolve) => board.listen(0, '127.0.0.1', resolve));
const wait = (...args) => fixtureHub.wait(...args);
try {
    fixtureHub.start({
        imports: [
            path.join(here, 'fixtures/register-serial.mjs'),
            path.join(here, 'fixtures/register-net-denial.mjs'),
        ],
        env: {
            POCKET_AGENT_PORT: 'fixture-usb',
            POCKET_AGENT_WIFI: '1',
            POCKET_AGENT_DEVICE_PORT: String(board.address().port),
            POCKET_AGENT_USB_FIXTURE: fixture,
        },
    });
    let s = await wait(
        (v) => v.devices[0]?.dialError?.code === 'EPERM',
        'permission error missing',
    );
    ok(
        s.devices[0].wifi === 'connected' && !s.devices[0].wireless,
        'Wi-Fi association remains distinct from usable hub link',
    );
    ok(s.devices[0].dialError.stage === 'connect', 'safe host-side connect error exposed');
    await sleep(3200);
    ok(
        fs.readFileSync(path.join(home, 'hub.log'), 'utf8').split('wifi dial failed').length - 1 ===
            1,
        'repeated identical dial failures do not flood logs',
    );
    owner = net.connect(socketPath);
    await new Promise((resolve) => owner.once('connect', resolve));
    owner.write(
        '{"t":"hello","id":"fixtureassistant","name":"Reconnect fixture","voice":true}\n{"t":"state","id":"fixtureassistant","state":"idle"}\n',
    );
    save({ connectDenied: false });
    s = await wait((v) => v.devices[0]?.wireless, 'authenticated recovery');
    ok(s.devices[0].dialError === null, 'authenticated recovery clears host dial error');
    ok(
        s.links.some((l) => l.kind === 'usb' && l.active) &&
            s.links.some((l) => l.kind === 'wifi' && !l.active),
        'USB preferred with authenticated standby',
    );
    save({ disconnect: true });
    s = await wait(
        (v) =>
            v.links.some((l) => l.kind === 'wifi' && l.active) &&
            !v.links.some((l) => l.kind === 'usb'),
        'USB removal failover',
    );
    await sleep(100);
    ok(
        commands.some((l) => l.startsWith('assistant ')) &&
            commands.some((l) => l.startsWith('voice target ')),
        'Wi-Fi failover replays Assistant state and voice availability',
    );
    const first = Number(commands.findLast((l) => l.startsWith('voice link wifi ')).split(' ')[3]);
    save({ connectDenied: true });
    peer.destroy();
    s = await wait(
        (v) => v.devices[0]?.dialError?.code === 'EPERM',
        'failed reconnection diagnosis',
    );
    ok(!s.devices[0].wireless, 'lost wireless connection is not reported as connected');
    save({ connectDenied: false });
    await wait(
        (v) => v.links.some((l) => l.kind === 'wifi' && l.active),
        'second authenticated recovery',
    );
    await sleep(100);
    const second = Number(commands.findLast((l) => l.startsWith('voice link wifi ')).split(' ')[3]);
    ok(second > first, 'reconnection resync advances voice selector revision');
    save({ disconnect: false });
    await wait(
        (v) => v.links.some((l) => l.kind === 'usb' && l.active && l.authed && l.device === id),
        'identified USB return',
    );
    const serial = fs.readFileSync(fixture + '.commands', 'utf8').split('\n');
    ok(
        Number(serial.findLast((l) => l.startsWith('voice link usb ')).split(' ')[3]) > second,
        'USB return uses a newer revision',
    );
    ok(
        !fs.readFileSync(path.join(home, 'hub.log'), 'utf8').includes(token),
        'diagnostics do not log pairing secrets',
    );
} finally {
    owner?.destroy();
    peer?.destroy();
    await fixtureHub.stop();
    await new Promise((resolve) => board.close(resolve));
    await sleep(100);
    await fixtureHub.close();
}
console.log(
    `Wi-Fi reconnect: ${checks} checks passed; isolated permission/authentication/failover fixtures`,
);
