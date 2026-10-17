import { EventEmitter } from 'node:events';
import fs from 'node:fs';
import crypto from 'node:crypto';
const fixture = process.env.POCKET_AGENT_USB_FIXTURE;
const config = () => JSON.parse(fs.readFileSync(fixture, 'utf8'));
export class SerialPort extends EventEmitter {
    isOpen = false;
    static async list() {
        return [];
    }
    open(callback) {
        this.isOpen = true;
        callback(null);
        if (config().readyBeforeHello)
            queueMicrotask(() => this.isOpen && this.emit('data', Buffer.from('GB_READY\n')));
        this.timer = setInterval(() => {
            if (config().disconnect) this.close();
        }, 50);
    }
    close() {
        clearInterval(this.timer);
        this.isOpen = false;
        this.emit('close');
    }
    write(text) {
        const c = config(),
            line = text.trim();
        fs.appendFileSync(
            fixture + '.commands',
            (/^wifi /.test(line) ? 'wifi [credentials omitted]' : line) + '\n',
        );
        const emit = (line) =>
            queueMicrotask(() => this.isOpen && this.emit('data', Buffer.from(line + '\n')));
        if (line === 'hello')
            emit(
                `GB_HELLO id=${c.id} paired=1 key=${crypto.createHash('sha256').update(c.token).digest('hex').slice(0, 8)} wifi=${c.wifi} ip=${c.wifi === 'connected' ? '127.0.0.1' : '-'} link=${c.wireless ? 'wifi' : '-'} err=none`,
            );
        if (line.startsWith('wifi '))
            emit(c.reject ? 'GB_ERROR bad_wifi' : 'GB_WIFI saved ssid_len=7');
    }
}
