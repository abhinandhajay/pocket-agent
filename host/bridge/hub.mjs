#!/usr/bin/env node
// Pocket Agent bridge: routes Assistant state and drives the badge
// over USB serial or the local Wi-Fi network.
//
//   Hermes producers --JSON lines, local Unix socket--> hub --USB serial or TCP--> badge
//
// Client -> hub:  {"t":"hello","id","name","cwd"}  {"t":"state","id","state","detail"}
//                 {"t":"bye","id"}  {"t":"raw","line"}  {"t":"status"}  {"t":"monitor"}
//                 {"t":"release"}  {"t":"wifi","ssid"?}  {"t":"forget"}
// Hub -> client:  {"t":"status",...}  {"t":"device","line"}  {"t":"active","id"}  {"t":"ack","id"}
//                 {"t":"result","ok","message"}
//
// USB is the trusted channel: whenever the board is on USB the hub pairs it (random 32-byte
// token, kept in ~/.pocket-agent/devices.json) and learns its LAN address. Over Wi-Fi the hub dials
// the board (port 47837 at its last address, else pocket-agent-<id4>.local): outgoing connections
// need no firewall approval on the Mac. Both sides prove knowledge of the token
// (HMAC-SHA256 over each side's nonce) before any display traffic.
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { SerialPort } from 'serialport';
import { wifiCredentials } from './wifi.mjs';
import { VoiceRouter } from './voice.mjs';
import { ReplyRouter } from './reply.mjs';

export const HUB_DIR = process.env.POCKET_AGENT_HOME || path.join(os.homedir(), '.pocket-agent');
export const SOCKET_PATH = process.env.POCKET_AGENT_SOCKET || path.join(HUB_DIR, 'hub.sock');
const DEVICES_PATH = path.join(HUB_DIR, 'devices.json');
const LOG_PATH = path.join(HUB_DIR, 'hub.log');
const LOCK_PATH = path.join(HUB_DIR, 'hub.lock');
const LINK_REVISION_PATH = path.join(HUB_DIR, 'link-revision');
const DEVICE_PORT = Number(process.env.POCKET_AGENT_DEVICE_PORT || 47837);
const WIFI_ENABLED = process.env.POCKET_AGENT_WIFI !== '0';
const FIXTURE_LOCK = '/tmp/pocket-agent-device.lock';
function fixtureBusy() {
    try {
        return JSON.parse(fs.readFileSync(FIXTURE_LOCK, 'utf8')).allow_hub !== true;
    } catch {
        return fs.existsSync(FIXTURE_LOCK);
    }
}
const IDLE_EXIT_MS = 5 * 60_000;
const STATES = new Set(['idle', 'thinking', 'tool', 'error', 'done', 'waiting', 'sleeping']);

const log = (...args) => {
    const line = `${new Date().toISOString()} ${args.join(' ')}\n`;
    try {
        fs.appendFileSync(LOG_PATH, line);
    } catch {}
    if (process.stdout.isTTY) process.stdout.write(line);
};

// Device text protocol is space-delimited with a final "name|detail" field.
const clean = (s, max) =>
    (typeof s === 'string' ? s : typeof s === 'number' && Number.isFinite(s) ? String(s) : '')
        .replace(/[^\x20-\x7e]/g, '?')
        .replace(/\|/g, '/')
        .trim()
        .slice(0, max);
const shortId = (id) =>
    clean(id, 64)
        .replace(/[^A-Za-z0-9]/g, '')
        .slice(-8)
        .padStart(8, '0');
const hmac = (key, msg) => crypto.createHmac('sha256', key).update(msg).digest('hex');
const keyPrint = (token) => crypto.createHash('sha256').update(token).digest('hex').slice(0, 8);

let assistant = null;
const getAssistant = (id) => (assistant?.id === id ? assistant : null);
const clients = new Set(); // sockets
const monitors = new Set();
let lastClientAt = Date.now();
let releasedUntil = 0;
let pausedUntil = 0; // tests: leave the board entirely alone (no USB, no Wi-Fi) for a while

// ---- devices and links ----
// A link is one transport to a board: {kind: "usb"|"wifi", deviceId, write(line), close()}.
// Each device uses one active link (USB preferred); the other stays connected on standby.
const links = new Set();
const deviceInfo = new Map(); // latest diagnostics, retained when USB becomes standby or closes
let usb = null; // {link, port, path, rx}
const sendOwner = (owner, msg) => {
    if (!owner.destroyed && owner.writableLength < 1_000_000)
        owner.write(JSON.stringify(msg) + '\n');
    else owner.destroy();
};
const replies = new ReplyRouter({
    getAssistant,
    token: (id) => loadDevices()[id]?.token,
    send: sendOwner,
    write: (id, line) => {
        for (const l of activeLinks()) if (l.deviceId === id) l.write(line);
    },
});
const voice = new VoiceRouter({
    getAssistant,
    token: (id) => loadDevices()[id]?.token,
    send: sendOwner,
    feedback: (id, line) => {
        for (const l of activeLinks()) if (l.deviceId === id) l.write(line);
    },
    submitted: (d, v) => replies.authorize(d, v),
    invalidate: (d) => replies.cancel(d),
});
setInterval(() => {
    voice.expire();
    replies.expire();
}, 1000).unref();
function loadDevices() {
    try {
        return JSON.parse(fs.readFileSync(DEVICES_PATH, 'utf8'));
    } catch {
        return {};
    }
}
function saveDevices(d) {
    const temporary = `${DEVICES_PATH}.${process.pid}.tmp`;
    fs.writeFileSync(temporary, JSON.stringify(d, null, 1), { mode: 0o600 });
    fs.chmodSync(temporary, 0o600);
    fs.renameSync(temporary, DEVICES_PATH);
}

function activeLinks() {
    const byDevice = new Map();
    for (const l of links) {
        if (!l.deviceId && l.kind === 'usb') {
            byDevice.set('usb:' + (usb?.path ?? ''), l);
            continue;
        }
        if (!l.deviceId || !l.authed) continue;
        const cur = byDevice.get(l.deviceId);
        if (!cur || (cur.kind === 'wifi' && l.kind === 'usb')) byDevice.set(l.deviceId, l);
    }
    return new Set(byDevice.values());
}
const isActive = (link) => activeLinks().has(link);

function writeDevice(line) {
    for (const l of activeLinks()) l.write(line);
}
function nextLinkRevision() {
    let previous = 0;
    try {
        previous = Number(fs.readFileSync(LINK_REVISION_PATH, 'utf8'));
    } catch {}
    if (!Number.isSafeInteger(previous) || previous < 0 || previous >= Number.MAX_SAFE_INTEGER)
        throw Error('invalid link revision');
    const revision = Math.max(Date.now(), previous + 1);
    const temporary = `${LINK_REVISION_PATH}.${process.pid}.tmp`;
    fs.writeFileSync(temporary, String(revision), { mode: 0o600 });
    fs.renameSync(temporary, LINK_REVISION_PATH);
    return revision;
}
function syncLink(link) {
    link.write('assistant-clear *');
    if (link.deviceId) link.write(`voice link ${link.kind} ${nextLinkRevision()}`);
    if (assistant) {
        const p = assistant;
        link.write(assistantLine(p));
        link.write(`voice target ${p.id} ${p.voice ? 1 : 0}`);
    }
}
function relinked() {
    // after links change, make every active link current
    for (const l of activeLinks())
        if (l.syncedAs !== 'active') {
            voice.cancel(l.deviceId);
            l.syncedAs = 'active';
            syncLink(l);
        }
    for (const l of links) if (!activeLinks().has(l)) l.syncedAs = 'standby';
}

function assistantLine(p) {
    return `assistant ${p.id} ${p.state} ${p.style} ${clean(p.name, 24)}|${clean(p.detail, 40)}`;
}

function broadcast(msg, set = clients) {
    const text = JSON.stringify(msg) + '\n';
    for (const c of set) c.write(text);
}

function statusMsg() {
    const devices = loadDevices();
    const { owner, ...session } = assistant ?? {};
    return {
        t: 'status',
        port: usb?.path ?? null,
        deviceReady: [...links].some((l) => l.ready),
        links: [...links].map((l) => ({
            kind: l.kind,
            device: l.deviceId ?? null,
            active: isActive(l),
            authed: !!l.authed,
        })),
        wifi: WIFI_ENABLED
            ? {
                  dialing: [...dialing],
                  devices: Object.fromEntries(
                      Object.entries(devices).map(([k, v]) => [k, v.ip ?? null]),
                  ),
              }
            : null,
        paired: Object.keys(devices),
        devices: Object.keys(devices).map((id) => ({
            id,
            wifi: deviceInfo.get(id)?.wifi ?? 'unknown',
            ip: deviceInfo.get(id)?.ip ?? devices[id].ip ?? null,
            error: deviceInfo.get(id)?.err ?? 'none',
            wireless: [...links].some((l) => l.deviceId === id && l.kind === 'wifi' && l.authed),
            dialError: wifiDialErrors.get(id) ?? null,
        })),
        assistant: assistant ? session : null,
    };
}

// USB: whenever the board is attached, pair it (if needed) and keep its hub address current.
function onUsbHello(link, fields) {
    if (!/^[0-9a-f]{12}$/.test(fields.id ?? '')) return;
    // An anonymous USB link can already be marked active by GB_READY or Wi-Fi.
    // Once its identity is known, send the versioned selector for the real board.
    if (link.deviceId !== fields.id || !link.authed) link.syncedAs = null;
    link.deviceId = fields.id;
    link.authed = true; // physical attachment is the trust anchor
    const devices = loadDevices();
    const previous = JSON.stringify(devices[fields.id]);
    let rec = devices[fields.id];
    const needPair = !rec || fields.paired !== '1' || fields.key !== keyPrint(rec.token);
    if (needPair) {
        rec = {
            token: rec?.token ?? crypto.randomBytes(32).toString('hex'),
            pairedAt: new Date().toISOString(),
        };
        link.write(`pair ${rec.token}`);
        log('paired device', fields.id, 'over USB');
    }
    if (fields.ip && fields.ip !== '-') rec.ip = fields.ip; // where to dial it over Wi-Fi
    devices[fields.id] = rec;
    if (JSON.stringify(rec) !== previous) saveDevices(devices);
    link.info = fields;
    relinked();
}

function parseFields(line) {
    const f = {};
    for (const part of line.split(' ').slice(1)) {
        const i = part.indexOf('=');
        if (i > 0) f[part.slice(0, i)] = part.slice(i + 1);
    }
    return f;
}

function onDeviceLine(link, line) {
    if (line.startsWith('GB_REPLY_') && !line.startsWith('GB_REPLY_STATUS ')) {
        if (isActive(link)) replies.device(link.deviceId, line);
        return;
    }
    if (line.startsWith('GB_VOICE_') && !line.startsWith('GB_VOICE_STATUS ')) {
        if (isActive(link)) voice.handle(link.deviceId, line);
        return; // audio never reaches monitor or generic broadcasts
    }
    if (monitors.size && isActive(link))
        broadcast({ t: 'device', link: link.kind, line }, monitors);
    if (line.startsWith('GB_')) link.ready = true;
    if (line.startsWith('GB_HELLO ')) {
        const f = parseFields(line);
        if (/^[0-9a-f]{12}$/.test(f.id ?? '')) deviceInfo.set(f.id, f);
        link.helloAt = Date.now();
        if (link.kind === 'usb') onUsbHello(link, f);
        else link.info = f;
        return;
    }
    if (line.startsWith('GB_READY')) {
        link.syncedAs = null;
        link.write('hello');
        relinked();
        return;
    }
    if (link.kind === 'usb' && line.startsWith('GB_PAIRED')) {
        return;
    }
    if (link.kind === 'usb' && line.startsWith('GB_WIFI saved ')) link.wifiSavedAt = Date.now();
    if (link.kind === 'usb' && line.startsWith('GB_ERROR ')) link.lastError = line.slice(9);
    if (!isActive(link)) return; // standby link: ignore duplicate device events
    if (line.startsWith('GB_ACTIVE ')) {
        const ident = line.slice('GB_ACTIVE '.length).trim();
        replies.activate(link.deviceId, ident);
        const v = voice.takes.get(link.deviceId) || voice.pending.get(link.deviceId);
        if (v && v.id !== ident) voice.cancel(link.deviceId);
        broadcast({ t: 'active', id: ident });
    } else if (line.startsWith('GB_ACK ')) broadcast({ t: 'ack', id: line.slice(7).trim() });
}

async function findPort() {
    if (process.env.POCKET_AGENT_PORT === 'none') return null; // tests: no board
    if (process.env.POCKET_AGENT_PORT) return process.env.POCKET_AGENT_PORT;
    const ports = await SerialPort.list();
    const board = ports.find((p) => (p.vendorId ?? '').toLowerCase() === '303a');
    return board ? board.path.replace('/dev/tty.', '/dev/cu.') : null;
}

async function connectSerial() {
    if (usb || fixtureBusy() || Date.now() < releasedUntil || Date.now() < pausedUntil) return;
    const found = await findPort().catch(() => null);
    if (!found || usb) return;
    const p = new SerialPort({ path: found, baudRate: 115200, autoOpen: false });
    p.open((err) => {
        if (err) {
            log('serial open failed', found, err.message);
            return;
        }
        const link = {
            kind: 'usb',
            write: (line) => p.isOpen && p.write(line + '\n'),
            close: () => p.close(),
        };
        usb = { link, port: p, path: found, rx: '' };
        links.add(link);
        log('serial open', found);
        // Opening the port may reset the board (it then announces GB_READY); ask who it is either way.
        setTimeout(() => {
            if (usb?.port === p) link.write('hello');
        }, 400);
        setTimeout(() => {
            if (usb?.port === p && !link.deviceId) {
                link.syncedAs = null;
                relinked();
            }
        }, 1500);
    });
    p.on('data', (buf) => {
        if (!usb || usb.port !== p) return;
        usb.rx += buf.toString('latin1');
        let i;
        while ((i = usb.rx.indexOf('\n')) >= 0) {
            const line = usb.rx.slice(0, i).replace(/\r$/, '');
            usb.rx = usb.rx.slice(i + 1);
            if (line) onDeviceLine(usb.link, line);
        }
        if (usb.rx.length > 8192) usb.rx = usb.rx.slice(-1024); // binary screenshot or noise
    });
    p.on('close', () => {
        log('serial closed');
        if (usb?.port === p) {
            links.delete(usb.link);
            usb = null;
            relinked();
        }
    });
    p.on('error', (e) => log('serial error', e.message));
}

// Wi-Fi: the hub dials; the board greets. HELLO id nonceD -> CHALLENGE nonceH macH -> AUTH macD -> OK.
const dialing = new Set();
const wifiDialErrors = new Map();
function dialError(id, stage, code) {
    const value = { stage, code: /^[A-Z0-9_]{1,40}$/.test(code ?? '') ? code : 'CONNECT_ERROR' };
    if (JSON.stringify(wifiDialErrors.get(id)) !== JSON.stringify(value))
        log('wifi dial failed', id, value.stage, value.code);
    wifiDialErrors.set(id, value);
}
function dialDevices() {
    if (!WIFI_ENABLED || fixtureBusy() || Date.now() < pausedUntil) return;
    const devices = loadDevices();
    for (const [id, rec] of Object.entries(devices)) {
        if (dialing.has(id) || [...links].some((l) => l.kind === 'wifi' && l.deviceId === id))
            continue;
        const useName = !rec.ip || (dialTries.get(id) ?? 0) % 2 === 1; // alternate last IP and .local name
        dialTries.set(id, (dialTries.get(id) ?? 0) + 1);
        const host = useName ? `pocket-agent-${id.slice(8)}.local` : rec.ip;
        dialing.add(id);
        const sock = net.connect({ host, port: DEVICE_PORT });
        sock.setTimeout(4000, () => sock.destroy());
        sock.on('connect', () => {
            sock.setTimeout(0);
            dialTries.set(id, 0);
            handleDeviceSocket(sock, id);
        });
        sock.on('close', () => dialing.delete(id));
        sock.on('error', (error) => dialError(id, 'connect', error.code));
    }
}
const dialTries = new Map();

function handleDeviceSocket(sock, expectId) {
    sock.setEncoding('latin1');
    sock.setNoDelay(true);
    const link = {
        kind: 'wifi',
        write: (line) => !sock.destroyed && sock.write(line + '\n'),
        close: () => sock.destroy(),
    };
    let stage = 'hello',
        nonceD = '',
        nonceH = '',
        token = '',
        buf = '';
    const timer = setTimeout(() => {
        if (!link.authed) {
            dialError(expectId, 'handshake', 'TIMEOUT');
            sock.destroy();
        }
    }, 8000);
    sock.on('data', (chunk) => {
        buf += chunk;
        if (buf.length > 16384) {
            sock.destroy();
            return;
        }
        let i;
        while ((i = buf.indexOf('\n')) >= 0) {
            const line = buf.slice(0, i).replace(/\r$/, '');
            buf = buf.slice(i + 1);
            if (stage === 'hello') {
                const m = line.match(/^HELLO ([0-9a-f]{12}) ([0-9a-f]{16})$/);
                const rec = m && m[1] === expectId && loadDevices()[m[1]];
                if (!rec) {
                    sock.end('DENY unpaired\n');
                    log('wifi peer refused', m?.[1] ?? line.slice(0, 20));
                    return;
                }
                link.deviceId = m[1];
                nonceD = m[2];
                token = rec.token;
                nonceH = crypto.randomBytes(16).toString('hex');
                sock.write(`CHALLENGE ${nonceH} ${hmac(token, `hub:${nonceD}:${nonceH}`)}\n`);
                stage = 'auth';
            } else if (stage === 'auth') {
                const m = line.match(/^AUTH ([0-9a-f]{64})$/);
                const want = hmac(token, `dev:${nonceH}:${nonceD}`);
                if (!m || !crypto.timingSafeEqual(Buffer.from(m[1]), Buffer.from(want))) {
                    sock.end('DENY auth\n');
                    log('wifi device failed auth', link.deviceId);
                    return;
                }
                sock.write('OK\n');
                link.authed = true;
                wifiDialErrors.delete(link.deviceId);
                dialing.delete(link.deviceId);
                clearTimeout(timer);
                links.add(link);
                stage = 'open';
                log('wifi link up', link.deviceId, sock.remoteAddress);
                link.syncedAs = null;
                relinked();
                link.write('hello');
            } else if (line) {
                onDeviceLine(link, line);
            }
        }
    });
    const drop = () => {
        clearTimeout(timer);
        if (links.delete(link)) {
            log('wifi link down', link.deviceId);
            relinked();
        }
    };
    sock.on('close', drop);
    sock.on('error', () => {});
}

// Bridge Wi-Fi setup: read the network password from the login keychain (macOS asks for approval) and
// push it to the board over USB into device Wi-Fi storage. Never logged.
function run(cmd, args) {
    return new Promise((resolve) =>
        execFile(cmd, args, { timeout: 60_000 }, (err, stdout) =>
            resolve(err ? { error: err.killed ? 'timeout' : 'unavailable' } : stdout),
        ),
    );
}
let provisioning = false;
async function provisionWifi(ssidArg) {
    if (!WIFI_ENABLED) return { ok: false, message: 'Wi-Fi is disabled by POCKET_AGENT_WIFI=0' };
    if (!usb?.link?.deviceId)
        return { ok: false, message: 'connect the Stopwatch over USB first and wait for pairing' };
    if (provisioning) return { ok: false, message: 'Wi-Fi setup is already in progress' };
    provisioning = true;
    const link = usb.link;
    try {
        const credentials = await wifiCredentials(ssidArg, run);
        if (!credentials.ok) return credentials;
        if (usb?.link !== link)
            return {
                ok: false,
                message: 'USB disconnected during Wi-Fi setup; reconnect and retry',
            };
        link.wifiSavedAt = 0;
        link.helloAt = 0;
        link.lastError = '';
        link.write(credentials.line);
        log('pushed Wi-Fi credentials over USB for', JSON.stringify(credentials.ssid));
        const deadline = Date.now() + 45_000;
        while (Date.now() < deadline) {
            if (usb?.link !== link)
                return {
                    ok: false,
                    message: 'USB disconnected during Wi-Fi setup; reconnect and retry',
                };
            if (link.lastError === 'bad_wifi')
                return { ok: false, message: 'the badge rejected the Wi-Fi credentials' };
            if (
                link.wifiSavedAt &&
                link.helloAt >= link.wifiSavedAt &&
                link.info?.wifi === 'connected' &&
                link.info.ip !== '-' &&
                link.info.link === 'wifi' &&
                [...links].some(
                    (l) => l.kind === 'wifi' && l.deviceId === link.deviceId && l.authed,
                )
            ) {
                return {
                    ok: true,
                    message: `Wi-Fi ${JSON.stringify(credentials.ssid)} connected at ${link.info.ip}; authenticated hub link ready. USB remains preferred; node host/bridge/cli.mjs release tests wireless operation`,
                };
            }
            link.write('hello');
            await new Promise((resolve) => setTimeout(resolve, 500));
        }
        if (!link.wifiSavedAt)
            return {
                ok: false,
                message:
                    'the badge did not acknowledge saving Wi-Fi credentials; keep USB connected and retry',
            };
        if (link.info?.wifi !== 'connected')
            return {
                ok: false,
                message:
                    'credentials saved, but the badge could not join Wi-Fi; check the password, 2.4 GHz availability, and signal, then retry',
            };
        return {
            ok: false,
            message: `the badge joined Wi-Fi at ${link.info.ip}, but the hub could not connect/authenticate; check LAN client isolation and that the Mac is on the same LAN`,
        };
    } catch {
        return { ok: false, message: 'Wi-Fi setup failed; keep USB connected and retry' };
    } finally {
        provisioning = false;
    }
}

function forgetDevices() {
    for (const l of links) if (l.kind === 'usb') l.write('forget');
    saveDevices({});
    for (const l of [...links]) if (l.kind === 'wifi') l.close();
    return { ok: true, message: 'forgot pairing; plug in over USB to pair again' };
}

function upsert(id, patch, owner) {
    const prev = assistant;
    if (prev && (prev.owner !== owner || prev.id !== id)) return;
    const next = prev
        ? { ...prev, ...patch }
        : { id, name: 'agent', state: 'idle', detail: '', style: 0, owner, ...patch };
    if (!STATES.has(next.state)) next.state = 'idle';
    next.name = clean(next.name, 24) || 'agent'; // store exactly what the device will show
    next.detail = clean(next.detail, 40);
    assistant = next;
    if (!prev || assistantLine(prev) !== assistantLine(next)) writeDevice(assistantLine(next));
    if (!prev || prev.voice !== next.voice) writeDevice(`voice target ${id} ${next.voice ? 1 : 0}`);
    voice.expire();
    replies.expire();
}

function remove(id) {
    if (!getAssistant(id)) return;
    assistant = null;
    voice.expire();
    replies.expire();
    writeDevice(`assistant-clear ${id}`);
}

function handleClient(sock) {
    clients.add(sock);
    lastClientAt = Date.now();
    let buf = '';
    sock.setEncoding('utf8');
    const reply = (msg) => sock.write(JSON.stringify(msg) + '\n');
    sock.on('data', (chunk) => {
        buf += chunk;
        let i;
        while ((i = buf.indexOf('\n')) >= 0) {
            const text = buf.slice(0, i);
            buf = buf.slice(i + 1);
            let msg;
            try {
                msg = JSON.parse(text);
            } catch {
                continue;
            }
            if (!msg || typeof msg !== 'object' || Array.isArray(msg)) continue;
            if (replies.client(sock, msg)) continue;
            const id =
                typeof msg.id === 'string' && /[A-Za-z0-9]/.test(msg.id) ? shortId(msg.id) : null;
            if (['hello', 'state', 'bye'].includes(msg.t) && !id) continue;
            switch (msg.t) {
                case 'hello':
                    upsert(
                        id,
                        {
                            voice: msg.voice === true,
                            name:
                                msg.name ||
                                (typeof msg.cwd === 'string' ? path.basename(msg.cwd) : '') ||
                                'agent',
                        },
                        sock,
                    );
                    break;
                case 'state':
                    upsert(id, { state: msg.state, detail: msg.detail ?? '' }, sock);
                    break;
                case 'bye':
                    if (getAssistant(id)?.owner === sock) remove(id);
                    break;
                case 'raw': {
                    const line = clean(msg.line, 199);
                    const provisioning = /^(wifi|pair|forget)( |$)/.test(line);
                    const available = provisioning ? !!usb?.link : activeLinks().size > 0;
                    if (msg.reply === true && (!line || !available)) {
                        reply({
                            t: 'result',
                            ok: false,
                            message: provisioning
                                ? 'provisioning requires USB'
                                : 'no active badge link',
                        });
                        break;
                    }
                    if (provisioning) {
                        if (usb?.link) usb.link.write(line);
                        else
                            reply({ t: 'result', ok: false, message: 'provisioning requires USB' });
                    } else writeDevice(line);
                    if (msg.reply === true)
                        reply({ t: 'result', ok: true, message: 'diagnostic sent to badge link' });
                    break;
                }
                case 'monitor':
                    monitors.add(sock);
                    break;
                case 'status':
                    reply(statusMsg());
                    break;
                case 'voice_result':
                    if (id) voice.result(sock, id, msg.turn, msg.result);
                    break;
                case 'wifi':
                    provisionWifi(msg.ssid).then((r) => reply({ t: 'result', ...r }));
                    break;
                case 'forget':
                    reply({ t: 'result', ...forgetDevices() });
                    break;
                case 'pause': {
                    // tests: hands off the board completely for `seconds` (default 300)
                    pausedUntil = Date.now() + 1000 * Math.min(Number(msg.seconds) || 300, 3600);
                    log('paused for', Math.round((pausedUntil - Date.now()) / 1000), 's');
                    usb?.port.close();
                    for (const l of [...links]) if (l.kind === 'wifi') l.close();
                    reply({ t: 'result', ok: true, message: 'paused' });
                    break;
                }
                case 'resume':
                    pausedUntil = 0;
                    log('resumed');
                    reply({ t: 'result', ok: true, message: 'resumed' });
                    break;
                case 'release': // let esptool have the port for a while (Wi-Fi keeps the Assistant live)
                    releasedUntil = Date.now() + 30_000;
                    log('serial released for 30 s');
                    usb?.port.close();
                    reply({
                        t: 'result',
                        ok: true,
                        message: [...links].some((l) => l.kind === 'wifi' && l.authed)
                            ? 'USB released for 30 s; authenticated Wi-Fi stays up'
                            : 'USB released for 30 s; no authenticated Wi-Fi link, so the badge will be offline',
                    });
                    break;
            }
        }
        if (buf.length > 65536) sock.destroy();
    });
    const drop = () => {
        voice.dropOwner(sock);
        replies.dropOwner(sock);
        clients.delete(sock);
        monitors.delete(sock);
        if (assistant?.owner === sock) remove(assistant.id);
        lastClientAt = Date.now();
    };
    sock.on('close', drop);
    sock.on('error', () => {});
}

function claimHub() {
    for (let attempt = 0; attempt < 2; attempt++) {
        try {
            const fd = fs.openSync(LOCK_PATH, 'wx', 0o600);
            fs.writeFileSync(fd, String(process.pid));
            fs.closeSync(fd);
            process.on('exit', () => {
                try {
                    if (fs.readFileSync(LOCK_PATH, 'utf8') === String(process.pid))
                        fs.unlinkSync(LOCK_PATH);
                } catch {}
            });
            return true;
        } catch (error) {
            if (error.code !== 'EEXIST') throw error;
            try {
                const pid = Number(fs.readFileSync(LOCK_PATH, 'utf8'));
                if (Number.isSafeInteger(pid) && pid > 0) {
                    try {
                        process.kill(pid, 0);
                        return false;
                    } catch (error) {
                        if (error.code !== 'ESRCH') return false;
                    }
                }
                if (Date.now() - fs.statSync(LOCK_PATH).mtimeMs < 2000) return false;
                fs.unlinkSync(LOCK_PATH);
            } catch {
                return false;
            }
        }
    }
    return false;
}

function main() {
    fs.mkdirSync(HUB_DIR, { recursive: true, mode: 0o700 });
    fs.mkdirSync(path.dirname(SOCKET_PATH), { recursive: true, mode: 0o700 });
    if (!claimHub()) return;
    // A stale socket from a dead hub blocks listen(); a live hub answers.
    const probe = net.createConnection(SOCKET_PATH);
    probe.on('connect', () => {
        console.error('Pocket Agent hub already running');
        process.exit(0);
    });
    probe.on('error', () => {
        try {
            fs.unlinkSync(SOCKET_PATH);
        } catch {}
        const server = net.createServer(handleClient);
        server.on('error', (error) => {
            log('socket error', error.code);
            process.exit(error.code === 'EADDRINUSE' ? 0 : 1);
        });
        server.listen(SOCKET_PATH, () => {
            fs.chmodSync(SOCKET_PATH, 0o600);
            log('hub listening', SOCKET_PATH, 'pid', process.pid);
        });
        connectSerial();
        setInterval(connectSerial, 2000);
        setInterval(dialDevices, 3000);
        // Heartbeat: the board clears a stale Assistant state (screensaver) after 20 s without hub traffic.
        setInterval(() => writeDevice('ping'), 5000);
        setInterval(() => {
            for (const link of links) if (link.authed) link.write('hello');
        }, 5000);
        setInterval(() => {
            if (!clients.size && Date.now() - lastClientAt > IDLE_EXIT_MS) {
                log('idle; exiting');
                bye();
            }
        }, 10_000);
        const bye = () => {
            try {
                writeDevice('assistant-clear *');
            } catch {}
            try {
                fs.unlinkSync(SOCKET_PATH);
            } catch {}
            process.exit(0);
        };
        process.on('SIGTERM', bye);
        process.on('SIGINT', bye);
    });
}

main();
