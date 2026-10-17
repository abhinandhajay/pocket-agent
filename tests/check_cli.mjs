// Execute the public CLI against private socket peers; no keychain, agent or badge.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { pathToFileURL } from 'node:url';
const dir = path.resolve(process.argv[2]);
const { request } = await import(pathToFileURL(path.join(dir, 'cli.mjs')));
const home = fs.mkdtempSync(path.join('/tmp', 'pocket-agent-cli-'));
const socketPath = path.join(home, 'hub.sock');
let checks = 0,
    response = null;
const received = [],
    peers = new Set();
const server = net.createServer((peer) => {
    peers.add(peer);
    peer.on('close', () => peers.delete(peer));
    peer.on('error', () => {});
    let input = '';
    peer.on('data', (chunk) => {
        input += chunk;
        const newline = input.indexOf('\n');
        if (newline < 0) return;
        received.push(JSON.parse(input.slice(0, newline)));
        input = input.slice(newline + 1);
        if (response !== null) {
            const text = typeof response === 'string' ? response : JSON.stringify(response) + '\n';
            peer.write(text.slice(0, 3));
            peer.write(text.slice(3));
        }
    });
});
async function run(args) {
    const child = spawn(process.execPath, [path.join(dir, 'cli.mjs'), ...args], {
        env: { ...process.env, POCKET_AGENT_SOCKET: socketPath },
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    let stdout = '',
        stderr = '';
    child.stdout.on('data', (chunk) => (stdout += chunk));
    child.stderr.on('data', (chunk) => (stderr += chunk));
    return new Promise((resolve, reject) => {
        child.on('error', reject);
        child.on('exit', (code) => resolve({ code, stdout, stderr }));
    });
}
const ok = (condition, message) => {
    assert.ok(condition, message);
    checks++;
};
try {
    await new Promise((resolve) => server.listen(socketPath, resolve));
    response = {
        t: 'status',
        links: [{ kind: 'usb', active: true, authed: true }],
        devices: [{ wifi: 'none' }],
        assistant: { name: 'Assistant', state: 'idle' },
    };
    let result = await run([]);
    ok(
        result.code === 0 &&
            result.stdout.includes('usb active') &&
            result.stdout.includes('Assistant=idle'),
        'default status handles fragmented replies',
    );
    ok(result.stdout.includes('cli.mjs wifi <ssid>'), 'status retains Wi-Fi setup guidance');
    ok(
        received.at(-1).t === 'status' &&
            !received.some((frame) => ['hello', 'state'].includes(frame.t)),
        'CLI never registers an Assistant',
    );
    for (const command of ['release', 'forget']) {
        response = { t: 'result', ok: true, message: command + ' complete' };
        result = await run([command]);
        ok(result.code === 0 && received.at(-1).t === command, command + ' reaches hub');
    }
    response = { t: 'result', ok: true, message: 'authenticated hub link ready' };
    result = await run(['wifi', 'café room']);
    ok(
        result.code === 0 && received.at(-1).ssid === 'café room',
        'Wi-Fi SSID is a field, not a shell command',
    );
    ok(!result.stdout.includes('password'), 'Wi-Fi command never prints a password');
    await run(['wifi']);
    ok(!('ssid' in received.at(-1)), 'omitted SSID delegates current-network lookup to hub');
    response = { t: 'result', ok: false, message: 'no active badge link' };
    result = await run(['raw', 'bounce']);
    ok(
        result.code === 1 && received.at(-1).line === 'bounce' && received.at(-1).reply === true,
        'raw waits for an explicit bridge result',
    );
    const before = received.length;
    for (const args of [
        ['unknown'],
        ['release', 'extra'],
        ['raw', 'bounce\nreset'],
        ['raw', 'x'.repeat(200)],
        ['wifi', 'é'.repeat(17)],
    ]) {
        result = await run(args);
        ok(result.code === 2, 'invalid CLI command rejected');
    }
    ok(received.length === before, 'invalid commands send nothing');
    const copyRoot = path.join(home, 'checkout');
    fs.mkdirSync(path.join(copyRoot, 'host/bridge'), { recursive: true });
    fs.mkdirSync(path.join(copyRoot, '.build/hub-home'), { recursive: true });
    fs.copyFileSync(path.join(dir, 'cli.mjs'), path.join(copyRoot, 'host/bridge/cli.mjs'));
    fs.symlinkSync(socketPath, path.join(copyRoot, '.build/hub-home/hub.sock'));
    response = { t: 'status', links: [], assistant: { name: 'Local chat', state: 'idle' } };
    const localEnv = { ...process.env };
    delete localEnv.POCKET_AGENT_HOME;
    delete localEnv.POCKET_AGENT_SOCKET;
    const local = spawn(process.execPath, [path.join(copyRoot, 'host/bridge/cli.mjs'), 'status'], {
        env: localEnv,
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    let localOutput = '';
    local.stdout.on('data', (chunk) => (localOutput += chunk));
    const localCode = await new Promise((resolve) => local.on('exit', resolve));
    ok(
        localCode === 0 && localOutput.includes('Local chat=idle'),
        'no-override CLI discovers the checkout hub without starting another',
    );
    response = null;
    await assert.rejects(
        request({ t: 'status' }, 'status', { socketPath, timeout: 30 }),
        /timed out/,
    );
    checks++;
    response = 'not-json\n';
    await assert.rejects(
        request({ t: 'status' }, 'status', { socketPath }),
        /Invalid hub response/,
    );
    checks++;
    response = 'x'.repeat(65537);
    await assert.rejects(request({ t: 'status' }, 'status', { socketPath }), /exceeded/);
    checks++;
    for (const peer of peers) peer.destroy();
    await new Promise((resolve) => server.close(resolve));
    result = await run(['status']);
    ok(
        result.code === 1 && result.stderr.includes('Hub unavailable'),
        'missing hub fails with actionable error',
    );
} finally {
    for (const peer of peers) peer.destroy();
    if (server.listening) await new Promise((resolve) => server.close(resolve));
    fs.rmSync(home, { recursive: true, force: true });
}
console.log(`Bridge CLI: ${checks} checks passed; isolated sockets only.`);
