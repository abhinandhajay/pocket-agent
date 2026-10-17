// Run the actual hub. Substitute USB and OS commands, and dial a localhost board.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { HubFixture, sleep } from './fixtures/hub.mjs';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
const candidate = path.resolve(process.argv[2]);
const here = path.dirname(new URL(import.meta.url).pathname);
const fixtureHub = new HubFixture(candidate);
const home = fixtureHub.home,
    socketPath = fixtureHub.socketPath,
    fixture = path.join(home, 'usb.json');
const id = 'a1b2c3d4e5f6',
    token = crypto.randomBytes(32).toString('hex');
let config = { id, token, wifi: 'none', wireless: false };
const save = (patch) => {
    config = { ...config, ...patch };
    fs.writeFileSync(fixture, JSON.stringify(config));
};
let checks = 0,
    peer,
    allowDial = false;
const wifiCommands = [],
    revisionPath = path.join(home, 'link-revision');
const seedRevision = Date.now() + 600000;
const usbRevisions = () =>
    fs
        .readFileSync(fixture + '.commands', 'utf8')
        .split('\n')
        .flatMap((line) => {
            const m = line.match(/^voice link usb ([0-9]+)$/);
            return m ? [Number(m[1])] : [];
        });
function startHub() {
    return fixtureHub.start({
        imports: [path.join(here, 'fixtures/register-serial.mjs')],
        env: {
            PATH: path.join(home, 'bin') + path.delimiter + process.env.PATH,
            POCKET_AGENT_PORT: 'fixture-usb',
            POCKET_AGENT_WIFI: '1',
            POCKET_AGENT_DEVICE_PORT: String(board.address().port),
            POCKET_AGENT_USB_FIXTURE: fixture,
        },
    });
}
const ok = (condition, message) => {
    assert.ok(condition, message);
    checks++;
};
const hmac = (message) => crypto.createHmac('sha256', token).update(message).digest('hex');
const board = net.createServer((c) => {
    c.on('error', () => {});
    if (!allowDial) {
        c.destroy();
        return;
    }
    peer = c;
    c.setEncoding('utf8');
    const nonceD = '0123456789abcdef';
    let buffer = '',
        stage = 'challenge';
    c.write(`HELLO ${id} ${nonceD}\n`);
    c.on('data', (chunk) => {
        buffer += chunk;
        let i;
        while ((i = buffer.indexOf('\n')) >= 0) {
            const line = buffer.slice(0, i);
            buffer = buffer.slice(i + 1);
            if (stage === 'challenge') {
                const [, nonceH, mac] = line.split(' ');
                assert.equal(mac, hmac(`hub:${nonceD}:${nonceH}`));
                c.write(`AUTH ${hmac(`dev:${nonceH}:${nonceD}`)}\n`);
                stage = 'ok';
            } else if (stage === 'ok' && line === 'OK') {
                save({ wireless: true });
                stage = 'open';
            } else {
                wifiCommands.push(line);
                if (line === 'hello')
                    c.write(
                        `GB_HELLO id=${id} paired=1 wifi=connected ip=127.0.0.1 link=wifi err=none\n`,
                    );
            }
        }
    });
});
const ask = (...args) => fixtureHub.ask(...args);
try {
    save({ readyBeforeHello: true });
    fs.writeFileSync(path.join(home, 'devices.json'), JSON.stringify({ [id]: { token } }), {
        mode: 0o600,
    });
    fs.writeFileSync(revisionPath, String(seedRevision), { mode: 0o600 });
    fs.mkdirSync(path.join(home, 'bin'));
    fs.writeFileSync(path.join(home, 'bin/security'), '#!/bin/sh\nprintf "fixture-password\\n"\n', {
        mode: 0o700,
    });
    await new Promise((resolve) => board.listen(0, '127.0.0.1', resolve));
    startHub();
    for (let i = 0; i < 50; i++) {
        await sleep(100);
        try {
            if ((await ask({ t: 'status' })).devices[0]?.wifi === 'none') break;
        } catch {}
    }
    let status = await ask({ t: 'status' });
    const firstUsb = usbRevisions().at(-1);
    ok(
        Number.isSafeInteger(firstUsb) && firstUsb > seedRevision,
        'USB selection advances the persisted revision even when wall time is behind it',
    );
    ok(
        usbRevisions().length === 1,
        'an early anonymous GB_READY is followed by a selected-link resync after USB identity arrives',
    );
    const deviceFile = path.join(home, 'devices.json');
    const savedInode = fs.statSync(deviceFile).ino;
    await new Promise((resolve, reject) => {
        const c = net.connect(socketPath);
        c.on('error', reject);
        c.on('connect', () => {
            c.end(JSON.stringify({ t: 'raw', line: 'hello' }) + '\n');
            resolve();
        });
    });
    await sleep(250);
    ok(
        usbRevisions().length === 1,
        'repeated hello for the same USB identity does not cancel or replay the live session',
    );
    ok(
        fs.statSync(deviceFile).ino === savedInode,
        'unchanged USB hello does not rewrite pairing data',
    );
    ok((fs.statSync(revisionPath).mode & 0o777) === 0o600, 'link revision is stored privately');
    ok(
        status.devices[0].wifi === 'none' && !status.devices[0].wireless,
        'status exposes missing Wi-Fi credentials',
    );
    process.env.POCKET_AGENT_SOCKET = socketPath;
    delete process.env.POCKET_AGENT;
    const { formatStatus } = await import(pathToFileURL(path.join(candidate, 'cli.mjs')));
    let notification = formatStatus(await ask({ t: 'status' }));
    ok(
        notification.includes('Wi-Fi not configured') &&
            notification.includes('cli.mjs wifi <ssid>'),
        'the control utility offers setup guidance when credentials are missing',
    );
    ok(!notification.includes('BLE'), 'the control utility reports only supported transports');
    let settled = false;
    const setup = ask({ t: 'wifi', ssid: 'network' }, 'result', 15000).then((result) => {
        settled = true;
        return result;
    });
    await sleep(800);
    ok(!settled, 'credential delivery alone is not success');
    const busy = await ask({ t: 'wifi', ssid: 'network' }, 'result');
    ok(!busy.ok && busy.message.includes('already in progress'), 'concurrent setup is rejected');
    save({ wifi: 'connected' });
    await sleep(800);
    ok(!settled, 'association without an authenticated hub link is not success');
    status = await ask({ t: 'status' });
    ok(
        status.devices[0].wifi === 'connected' && !status.devices[0].wireless,
        'status distinguishes association from the hub connection',
    );
    allowDial = true;
    const result = await setup;
    ok(
        result.ok && result.message.includes('authenticated hub link ready'),
        'setup succeeds after board association and mutual authentication',
    );
    status = await ask({ t: 'status' });
    ok(
        status.links.some((l) => l.kind === 'usb' && l.active) &&
            status.links.some((l) => l.kind === 'wifi' && !l.active),
        'USB remains preferred with Wi-Fi on standby',
    );
    const raw = await ask({ t: 'raw', line: 'bounce', reply: true }, 'result');
    ok(
        raw.ok &&
            fs
                .readFileSync(fixture + '.commands', 'utf8')
                .split('\n')
                .includes('bounce'),
        'diagnostic acknowledges forwarding through the preferred USB link',
    );
    notification = formatStatus(await ask({ t: 'status' }));
    ok(
        notification.includes('wifi standby'),
        'the control utility shows authenticated Wi-Fi standby',
    );
    save({ reject: true });
    const rejected = await ask({ t: 'wifi', ssid: 'network' }, 'result');
    ok(
        !rejected.ok && rejected.message.includes('rejected'),
        'board credential rejection is a failed setup',
    );
    save({ reject: false });
    const disconnected = ask({ t: 'wifi', ssid: 'network' }, 'result');
    await sleep(50);
    save({ disconnect: true });
    const lost = await disconnected;
    ok(
        !lost.ok && lost.message.includes('USB disconnected'),
        'USB loss cancels pending provisioning',
    );
    notification = formatStatus(await ask({ t: 'status' }));
    ok(
        notification.includes('wifi active'),
        'the control utility shows Wi-Fi active after USB loss',
    );
    const wifiRevision = Number(
        wifiCommands.findLast((line) => /^voice link wifi [0-9]+$/.test(line))?.split(' ')[3],
    );
    ok(
        wifiRevision > firstUsb,
        'Wi-Fi failover carries a newer revision than the earlier USB selection',
    );
    const log = fs.readFileSync(path.join(home, 'hub.log'), 'utf8');
    ok(
        !log.includes('fixture-password') &&
            !log.includes(Buffer.from('fixture-password').toString('base64')),
        'Wi-Fi credentials are absent from hub logs',
    );
    await fixtureHub.stop();
    save({ disconnect: false });
    startHub();
    for (let i = 0; i < 50; i++) {
        await sleep(100);
        try {
            if (
                (await ask({ t: 'status' })).links.some(
                    (l) => l.kind === 'usb' && l.active && l.authed,
                )
            )
                break;
        } catch {}
    }
    const restored = usbRevisions().at(-1);
    ok(
        restored > wifiRevision,
        'a restarted hub restores USB with a revision newer than pre-restart Wi-Fi',
    );
    ok(
        Number(fs.readFileSync(revisionPath, 'utf8')) >= restored,
        'the latest selection remains persisted for the next restart',
    );
} finally {
    await fixtureHub.stop();
    peer?.destroy();
    await new Promise((resolve) => board.close(resolve));
    await sleep(200);
    await fixtureHub.close();
}
console.log(`pocket-agent Wi-Fi setup: ${checks} checks passed`);
