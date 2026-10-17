// Real hub fixtures always use private state and opt in to synthetic transports.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { request } from '../../host/bridge/cli.mjs';

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
export async function waitFor(test, label = 'fixture timed out', timeout = 12000) {
    const end = Date.now() + timeout;
    while (Date.now() < end) {
        const result = await test();
        if (result) return result;
        await sleep(100);
    }
    throw new Error(label);
}
export class HubFixture {
    constructor(candidate, prefix = 'pocket-agent-test-') {
        this.candidate = candidate;
        this.home = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
        this.socketPath = path.join(this.home, 'hub.sock');
        this.peers = new Set();
    }
    start({ env = {}, imports = [] } = {}) {
        this.errors = '';
        this.process = spawn(
            process.execPath,
            [
                ...imports.flatMap((file) => ['--import', file]),
                path.join(this.candidate, 'hub.mjs'),
            ],
            {
                env: {
                    ...process.env,
                    POCKET_AGENT_HOME: this.home,
                    POCKET_AGENT_SOCKET: this.socketPath,
                    POCKET_AGENT_PORT: 'none',
                    POCKET_AGENT_WIFI: '0',
                    ...env,
                },
                stdio: ['ignore', 'ignore', 'pipe'],
            },
        );
        this.process.stderr.on('data', (data) => {
            this.errors += data;
        });
        this.process.on('error', (error) => {
            this.errors += error.message;
        });
        return this.process;
    }
    async connect() {
        return waitFor(async () => {
            if (this.process.exitCode !== null)
                throw new Error(`Hub exited (${this.process.exitCode}): ${this.errors}`);
            const peer = await new Promise((resolve) => {
                const socket = net.connect(this.socketPath);
                socket.once('error', () => {
                    socket.destroy();
                    resolve(null);
                });
                socket.once('connect', () => resolve(socket));
            });
            if (!peer) return null;
            peer.setEncoding('utf8');
            this.peers.add(peer);
            peer.once('close', () => this.peers.delete(peer));
            return peer;
        }, 'hub did not start');
    }
    ask(message = { t: 'status' }, type = 'status', timeout = 5000) {
        return request(message, type, { socketPath: this.socketPath, timeout });
    }
    async wait(test, label) {
        return waitFor(async () => {
            let status;
            try {
                status = await this.ask();
            } catch {
                return null;
            }
            return test(status) ? status : null;
        }, label);
    }
    async stop() {
        for (const peer of this.peers) peer.destroy();
        if (this.process && this.process.exitCode === null) {
            const exited = new Promise((resolve) => this.process.once('exit', resolve));
            this.process.kill('SIGTERM');
            await exited;
        }
    }
    async close() {
        await this.stop();
        fs.rmSync(this.home, { recursive: true, force: true });
    }
}
