import crypto from 'node:crypto';
export const replyKey = (token, challenge) =>
    crypto
        .createHmac('sha256', token)
        .update('pocket-agent-reply-v1:' + challenge)
        .digest();
const hex16 = /^[0-9a-f]{16}$/;
export class ReplyRouter {
    constructor({ getAssistant, token, send, write, now = Date.now }) {
        Object.assign(this, { getAssistant, token, send, write, now });
        this.tickets = new Map();
        this.outputs = new Map();
    }
    authorize(device, v) {
        this.cancel(device);
        this.tickets.set(device, { id: v.id, turn: v.turn, owner: v.owner, at: this.now() });
    }
    valid(v) {
        return this.getAssistant(v.id)?.owner === v.owner;
    }
    cancel(device) {
        const v = this.outputs.get(device);
        if (v) {
            this.write(device, `reply cancel ${v.reply}`);
            this.send(v.owner, { t: 'reply_cancel', reply: v.reply });
        }
        this.outputs.delete(device);
        this.tickets.delete(device);
    }
    activate(device, id) {
        const v = this.outputs.get(device) || this.tickets.get(device);
        if (v && v.id !== id) this.cancel(device);
    }
    dropOwner(owner) {
        for (const [d, v] of [...this.tickets, ...this.outputs])
            if (v.owner === owner) this.cancel(d);
    }
    expire() {
        for (const [d, v] of this.tickets)
            if (!this.valid(v) || this.now() - v.at > 600000) this.cancel(d);
        for (const [d, v] of this.outputs)
            if (!this.valid(v) || this.now() - v.at > (v.ended ? 70000 : 10000)) this.cancel(d);
    }
    client(owner, m) {
        if (!m || !String(m.t).startsWith('reply_')) return false;
        this.expire();
        if (m.t === 'reply_begin') {
            const entry = [...this.tickets].find(
                ([, v]) => v.owner === owner && v.id === m.id && v.turn === m.turn,
            );
            if (
                !entry ||
                !hex16.test(m.reply ?? '') ||
                !Number.isInteger(m.text_bytes) ||
                m.text_bytes < 1 ||
                m.text_bytes > 4096
            )
                return true;
            const [device, t] = entry;
            if (!this.valid(t) || !this.token(device) || this.outputs.has(device)) return true;
            this.tickets.delete(device);
            const v = {
                ...t,
                reply: m.reply,
                textBytes: m.text_bytes,
                seq: 0,
                acked: 0,
                credit: 0,
                bytes: 0,
                hash: crypto.createHash('sha256'),
                at: this.now(),
                ended: false,
            };
            this.outputs.set(device, v);
            this.write(device, `reply begin ${v.id} ${v.turn} ${v.reply} ${v.textBytes}`);
            return true;
        }
        const entry = [...this.outputs].find(([, v]) => v.owner === owner && v.reply === m.reply);
        if (!entry) return true;
        const [d, v] = entry;
        try {
            if (!this.valid(v)) throw Error('owner');
            if (m.t === 'reply_cancel') {
                this.cancel(d);
                return true;
            }
            if (m.t === 'reply_data') {
                if (
                    !v.key ||
                    v.ended ||
                    v.credit <= 0 ||
                    !Number.isInteger(m.seq) ||
                    m.seq !== v.seq ||
                    typeof m.data !== 'string' ||
                    m.data.length > 512
                )
                    throw Error('sequence');
                const data = Buffer.from(m.data, 'base64');
                if (
                    data.toString('base64') !== m.data ||
                    data.length !== Math.min(384, v.textBytes - v.bytes) ||
                    !data.length
                )
                    throw Error('size');
                const nonce = crypto.randomBytes(12),
                    aes = crypto.createCipheriv('aes-256-gcm', v.key, nonce);
                aes.setAAD(Buffer.from(`${v.id}:${v.turn}:${v.reply}:${v.seq}`));
                const encrypted = Buffer.concat([aes.update(data), aes.final()]);
                this.write(
                    d,
                    `reply data ${v.reply} ${v.seq} ${nonce.toString('hex')} ${aes.getAuthTag().toString('hex')} ${encrypted.toString('base64')}`,
                );
                v.hash.update(data);
                v.bytes += data.length;
                v.seq++;
                v.credit--;
                v.at = this.now();
            } else if (m.t === 'reply_end') {
                if (
                    v.ended ||
                    m.bytes !== v.bytes ||
                    v.bytes !== v.textBytes ||
                    !/^[0-9a-f]{64}$/.test(m.digest ?? '') ||
                    v.hash.digest('hex') !== m.digest
                )
                    throw Error('end');
                v.ended = true;
                v.at = this.now();
                this.write(d, `reply end ${v.reply} ${v.bytes} ${m.digest}`);
            }
        } catch {
            this.cancel(d);
        }
        return true;
    }
    device(device, line) {
        if (!line.startsWith('GB_REPLY_')) return false;
        const a = line.split(' '),
            kind = a.shift(),
            v = this.outputs.get(device);
        if (!v || a[0] !== v.reply) return true;
        try {
            if (!this.valid(v)) throw Error('owner');
            if (kind === 'GB_REPLY_READY') {
                if (a.length !== 2 || v.key || !/^[0-9a-f]{32}$/.test(a[1])) throw Error('ready');
                v.key = replyKey(this.token(device), a[1]);
                v.credit = 8;
                v.at = this.now();
                this.send(v.owner, { t: 'reply_credit', reply: v.reply, count: 8 });
            } else if (kind === 'GB_REPLY_ACK') {
                const n = Number(a[1]);
                if (a.length !== 2 || !/^[0-9]{1,6}$/.test(a[1]) || n < v.acked || n > v.seq)
                    throw Error('ack');
                const count = n - v.acked;
                v.acked = n;
                v.credit += count;
                v.at = this.now();
                if (count) this.send(v.owner, { t: 'reply_credit', reply: v.reply, count });
            } else if (kind === 'GB_REPLY_DONE') {
                if (a.length !== 2 || !['text', 'error', 'cancelled'].includes(a[1]))
                    throw Error('done');
                if (a[1] === 'text' && (!v.ended || v.acked !== v.seq)) throw Error('early done');
                this.send(v.owner, { t: 'reply_result', reply: v.reply, result: a[1] });
                this.outputs.delete(device);
            }
        } catch {
            this.cancel(device);
        }
        return true;
    }
}
