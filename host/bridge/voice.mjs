import crypto from 'node:crypto';
export const audioKey = (token, challenge) =>
    crypto
        .createHmac('sha256', token)
        .update('pocket-agent-voice-v1:' + challenge)
        .digest();
const idPattern = /^[A-Za-z0-9]{8}$/;
const turnPattern = /^[0-9a-f]{16}$/;
export class VoiceRouter {
    constructor({
        getAssistant,
        token,
        send,
        feedback,
        now = Date.now,
        submitted = () => {},
        invalidate = () => {},
    }) {
        Object.assign(this, { getAssistant, token, send, feedback, now, submitted, invalidate });
        this.takes = new Map();
        this.pending = new Map();
        this.seen = new Map();
    }
    cancel(device, reason = 'error') {
        this.invalidate(device);
        const v = this.takes.get(device) || this.pending.get(device);
        if (v) {
            this.send(v.owner, { t: 'voice_cancel', id: v.id, turn: v.turn });
            this.feedback(device, `voice feedback ${v.turn} ${reason}`);
        }
        this.takes.delete(device);
        this.pending.delete(device);
    }
    valid(v) {
        const p = this.getAssistant(v.id);
        return p && p.owner === v.owner && p.voice === true;
    }
    expire() {
        for (const [d, v] of this.takes)
            if (!this.valid(v) || this.now() - v.at > 60_000) this.cancel(d);
        for (const [d, v] of this.pending)
            if (this.getAssistant(v.id)?.owner !== v.owner || this.now() - v.at > 60_000)
                this.cancel(d);
    }
    dropOwner(owner) {
        for (const [d, v] of [...this.takes, ...this.pending])
            if (v.owner === owner) this.cancel(d);
    }
    result(owner, id, turn, result) {
        for (const [d, v] of this.pending)
            if (v.owner === owner && v.id === id && v.turn === turn) {
                if (!['submitted', 'silent', 'error', 'busy'].includes(result)) return;
                if (result === 'submitted') this.submitted(d, v);
                this.feedback(d, `voice feedback ${turn} ${result}`);
                this.pending.delete(d);
            }
    }
    handle(device, line) {
        const a = line.split(' '),
            kind = a.shift();
        if (!['GB_VOICE_BEGIN', 'GB_VOICE_DATA', 'GB_VOICE_END', 'GB_VOICE_CANCEL'].includes(kind))
            return false;
        if (!device) return true;
        this.expire();
        if (kind === 'GB_VOICE_BEGIN') {
            const [id, turn] = a,
                p = this.getAssistant(id),
                token = this.token(device);
            if (a.length !== 2 || !idPattern.test(id) || !turnPattern.test(turn)) return true;
            const history = this.seen.get(device) || [];
            if (history.includes(turn)) return true;
            this.cancel(device);
            history.push(turn);
            this.seen.set(device, history.slice(-32));
            if (!p?.voice || !token) {
                this.feedback(device, `voice feedback ${turn} busy`);
                return true;
            }
            const challenge = crypto.randomBytes(16).toString('hex');
            const v = {
                id,
                turn,
                owner: p.owner,
                challenge,
                key: audioKey(token, challenge),
                seq: 0,
                bytes: 0,
                hash: crypto.createHash('sha256'),
                at: this.now(),
                nonces: new Set(),
            };
            this.takes.set(device, v);
            this.send(v.owner, { t: 'voice_begin', id, turn, rate: 16000 });
            this.feedback(device, `voice challenge ${turn} ${challenge}`);
            return true;
        }
        const v = this.takes.get(device);
        if (kind === 'GB_VOICE_CANCEL') {
            const pending = v || this.pending.get(device);
            if (a.length === 1 && pending?.turn === a[0]) this.cancel(device);
            return true;
        }
        if (!v || a[0] !== v.turn) return true;
        try {
            if (!this.valid(v)) throw Error('owner');
            if (kind === 'GB_VOICE_DATA') {
                const [turn, seq, nonce, tag, data] = a;
                if (
                    a.length !== 5 ||
                    !/^(0|[1-9][0-9]{0,4})$/.test(seq) ||
                    Number(seq) !== v.seq ||
                    !/^[0-9a-f]{24}$/.test(nonce) ||
                    !/^[0-9a-f]{32}$/.test(tag) ||
                    v.nonces.has(nonce) ||
                    !/^[A-Za-z0-9+/]{2,512}={0,2}$/.test(data) ||
                    data.length % 4
                )
                    throw Error('frame');
                const ciphertext = Buffer.from(data, 'base64');
                if (
                    ciphertext.toString('base64') !== data ||
                    !ciphertext.length ||
                    ciphertext.length > 384 ||
                    ciphertext.length % 2 ||
                    v.bytes + ciphertext.length > 640000
                )
                    throw Error('size');
                const dec = crypto.createDecipheriv(
                    'aes-256-gcm',
                    v.key,
                    Buffer.from(nonce, 'hex'),
                );
                dec.setAAD(Buffer.from(`${v.id}:${turn}:${seq}`));
                dec.setAuthTag(Buffer.from(tag, 'hex'));
                const pcm = Buffer.concat([dec.update(ciphertext), dec.final()]);
                v.hash.update(pcm);
                v.bytes += pcm.length;
                v.seq++;
                v.nonces.add(nonce);
                this.send(v.owner, {
                    t: 'voice_data',
                    id: v.id,
                    turn,
                    seq: Number(seq),
                    data: pcm.toString('base64'),
                });
            } else {
                const [turn, bytes, digest] = a;
                if (
                    a.length !== 3 ||
                    !/^[0-9]{1,6}$/.test(bytes) ||
                    Number(bytes) !== v.bytes ||
                    v.bytes < 9600 ||
                    !/^[0-9a-f]{64}$/.test(digest) ||
                    digest !== v.hash.digest('hex')
                )
                    throw Error('end');
                this.send(v.owner, { t: 'voice_end', id: v.id, turn, bytes: v.bytes, digest });
                this.takes.delete(device);
                v.at = this.now();
                this.pending.set(device, v);
            }
        } catch {
            this.cancel(device);
        }
        return true;
    }
}
