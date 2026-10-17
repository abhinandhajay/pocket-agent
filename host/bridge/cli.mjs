#!/usr/bin/env node
// Local bridge administration. Never registers an Assistant or sends an agent prompt.
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectHome = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    '../../.build/hub-home',
);
const defaultSocket = () =>
    process.env.POCKET_AGENT_SOCKET ||
    path.join(
        process.env.POCKET_AGENT_HOME ||
            (fs.existsSync(projectHome) ? projectHome : path.join(os.homedir(), '.pocket-agent')),
        'hub.sock',
    );
const safe = (value) =>
    String(value ?? '')
        .replace(/[\x00-\x1f\x7f]/g, ' ')
        .slice(0, 240);

export function request(message, want, { socketPath = defaultSocket(), timeout = 3000 } = {}) {
    return new Promise((resolve, reject) => {
        let buffer = '',
            finished = false;
        const peer = net.createConnection(socketPath);
        const timer = setTimeout(() => finish(new Error('Hub request timed out.')), timeout);
        function finish(error, value) {
            if (finished) return;
            finished = true;
            clearTimeout(timer);
            peer.destroy();
            if (error) reject(error);
            else resolve(value);
        }
        peer.setEncoding('utf8');
        peer.on('connect', () => peer.write(JSON.stringify(message) + '\n'));
        peer.on('error', () =>
            finish(new Error('Hub unavailable. Start the badge service or run make hub.')),
        );
        peer.on('close', () => finish(new Error('Hub disconnected before replying.')));
        peer.on('data', (chunk) => {
            buffer += chunk;
            if (Buffer.byteLength(buffer) > 65536)
                return finish(new Error('Hub response exceeded the limit.'));
            let newline;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const line = buffer.slice(0, newline);
                buffer = buffer.slice(newline + 1);
                let value;
                try {
                    value = JSON.parse(line);
                } catch {
                    return finish(new Error('Invalid hub response.'));
                }
                if (value?.t === want) return finish(null, value);
            }
        });
    });
}

export function formatStatus(status) {
    const links = status.links ?? [];
    const connection =
        links
            .map(
                (link) =>
                    `${safe(link.kind)} ${link.active ? 'active' : 'standby'}${link.authed ? '' : ' (unauthenticated)'}`,
            )
            .join(' + ') || 'no board';
    const wifi =
        status.wifi === null
            ? 'Wi-Fi disabled by POCKET_AGENT_WIFI=0'
            : (status.devices ?? [])
                  .map((device) => {
                      if (device.wifi === 'none')
                          return 'Wi-Fi not configured; connect USB and run node host/bridge/cli.mjs wifi <ssid>';
                      return `Wi-Fi ${safe(device.wifi)}${device.ip && device.ip !== '-' ? ' at ' + safe(device.ip) : ''}; ${device.wireless ? (links.some((link) => link.kind === 'wifi' && link.device === device.id && link.active) ? 'wifi active' : 'wifi standby') : 'hub wireless link offline'}${device.error && device.error !== 'none' ? '; error ' + safe(device.error) : ''}`;
                  })
                  .join('; ');
    const assistant = status.assistant;
    return [
        connection,
        wifi,
        assistant ? `${safe(assistant.name)}=${safe(assistant.state)}` : 'Assistant offline',
    ]
        .filter(Boolean)
        .join('; ');
}

export async function main(args = process.argv.slice(2)) {
    const [command = 'status', ...values] = args;
    const usage =
        'Usage: node host/bridge/cli.mjs [status | wifi [ssid] | release | forget | raw <diagnostic>]';
    if (command === '--help' || command === 'help') {
        console.log(usage);
        return 0;
    }
    let message,
        want = 'result',
        timeout = 3000;
    if (command === 'status' && !values.length) {
        message = { t: 'status' };
        want = 'status';
    } else if (command === 'wifi') {
        const ssid = values.join(' ') || undefined;
        if (ssid && (/[\x00-\x1f\x7f]/.test(ssid) || Buffer.byteLength(ssid) > 32)) {
            console.error('Invalid Wi-Fi network name.');
            return 2;
        }
        message = { t: 'wifi', ...(ssid ? { ssid } : {}) };
        timeout = 115000;
        console.error(
            'Reading the saved password from the macOS keychain; approve its dialog if shown.',
        );
    } else if (['release', 'forget'].includes(command) && !values.length) {
        message = { t: command };
        timeout = 5000;
    } else if (command === 'raw' && values.length) {
        const line = values.join(' ');
        if (/[\x00-\x1f\x7f]/.test(line) || Buffer.byteLength(line) > 199) {
            console.error('Invalid diagnostic line.');
            return 2;
        }
        message = { t: 'raw', line, reply: true };
    } else {
        console.error(usage);
        return 2;
    }
    try {
        const response = await request(message, want, { timeout });
        if (want === 'status') {
            console.log(formatStatus(response));
            return 0;
        }
        if (!response.ok) {
            console.error(safe(response.message || 'Bridge command failed.'));
            return 1;
        }
        console.log(safe(response.message || 'Done.'));
        return 0;
    } catch (error) {
        console.error(error.message);
        return 1;
    }
}

if (
    process.argv[1] &&
    fs.existsSync(process.argv[1]) &&
    fs.realpathSync(fileURLToPath(import.meta.url)) === fs.realpathSync(process.argv[1])
) {
    process.exitCode = await main();
}
