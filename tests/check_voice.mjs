import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
const { VoiceRouter } = await import(pathToFileURL(path.join(process.argv[2], 'voice.mjs')));
let checks = 0;
const ok = (v, label) => {
    assert(v, label);
    checks++;
};
const token = 'a'.repeat(64),
    owner = {},
    id = '12345678',
    device = '0123456789ab';
let assistant = { owner, voice: true },
    events = [],
    replies = [],
    time = 1000,
    ordinal = 0;
const router = new VoiceRouter({
    getAssistant: (ident) => (ident === id ? assistant : null),
    token: () => token,
    send: (_, m) => events.push(m),
    feedback: (d, line) => replies.push(line),
    now: () => time,
});
function begin() {
    const turn = (++ordinal).toString(16).padStart(16, '0');
    router.handle(device, `GB_VOICE_BEGIN ${id} ${turn}`);
    return turn;
}
function data(turn, seq, pcm = Buffer.alloc(384, 1), change = {}) {
    const nonce = crypto.randomBytes(12),
        enc = crypto.createCipheriv(
            'aes-256-gcm',
            crypto
                .createHmac('sha256', token)
                .update(
                    'pocket-agent-voice-v1:' + (router.takes.get(device)?.challenge || 'missing'),
                )
                .digest(),
            nonce,
        );
    enc.setAAD(Buffer.from(`${id}:${turn}:${seq}`));
    const bytes = Buffer.concat([enc.update(pcm), enc.final()]);
    return `GB_VOICE_DATA ${turn} ${seq} ${change.nonce || nonce.toString('hex')} ${change.tag || enc.getAuthTag().toString('hex')} ${change.data || bytes.toString('base64')}`;
}
function complete(turn) {
    const pcm = Buffer.alloc(384, 1);
    for (let n = 0; n < 25; n++) router.handle(device, data(turn, n, pcm));
    router.handle(
        device,
        `GB_VOICE_END ${turn} 9600 ${crypto.createHash('sha256').update(Buffer.alloc(9600, 1)).digest('hex')}`,
    );
}
let turn = begin();
complete(turn);
ok(events.filter((m) => m.t === 'voice_data').length === 25, 'validated decrypted chunks routed');
ok(events.at(-1).t === 'voice_end', 'complete audio forwarded');
router.result({}, id, turn, 'submitted');
ok(!replies.some((r) => r.endsWith('submitted')), 'different owner cannot complete');
assistant.voice = false;
router.expire();
router.result(owner, id, turn, 'submitted');
ok(replies.at(-1).endsWith('submitted'), 'success feedback survives new agent turn');
assistant.voice = true;
events = [];
replies = [];
router.handle(device, `GB_VOICE_BEGIN ${id} ${turn}`);
ok(events.length === 0, 'replayed turn rejected');
turn = begin();
router.handle(device, data(turn, 1));
ok(events.at(-1).t === 'voice_cancel', 'missing sequence cancels');
turn = begin();
const first = data(turn, 0);
router.handle(device, first);
router.handle(device, first);
ok(events.at(-1).t === 'voice_cancel', 'duplicate sequence cancels');
turn = begin();
const oldFrame = data(turn, 0);
router.seen.clear();
router.cancel(device);
router.handle(device, `GB_VOICE_BEGIN ${id} ${turn}`);
router.handle(device, oldFrame);
ok(
    events.at(-1).t === 'voice_cancel',
    'fresh host challenge rejects captured audio after replay history expires',
);
turn = begin();
router.handle(device, data(turn, 0, Buffer.alloc(384), { tag: '0'.repeat(32) }));
ok(events.at(-1).t === 'voice_cancel', 'tamper rejected');
turn = begin();
router.handle(device, data(turn, 0, Buffer.alloc(386)));
ok(events.at(-1).t === 'voice_cancel', 'oversize chunk rejected');
turn = begin();
router.handle(device, data(turn, 0));
router.handle(device, `GB_VOICE_END ${turn} 384 ${'0'.repeat(64)}`);
ok(events.at(-1).t === 'voice_cancel', 'short or invalid digest rejects');
turn = begin();
assistant = { owner: {}, voice: true };
router.handle(device, data(turn, 0));
ok(events.at(-1).t === 'voice_cancel', 'owner change rejects');
assistant = { owner, voice: false };
turn = begin();
ok(replies.at(-1).endsWith('busy'), 'busy or nonvoice assistant denied');
assistant = { owner, voice: true };
turn = begin();
time += 60001;
router.expire();
ok(events.at(-1).t === 'voice_cancel', 'bounded take timeout');
turn = begin();
complete(turn);
router.cancel(device);
ok(events.at(-1).t === 'voice_cancel', 'failover invalidates pending transcription');
turn = begin();
router.dropOwner(owner);
ok(events.at(-1).t === 'voice_cancel', 'crashed owner cleanup');
turn = begin();
complete(turn);
events = [];
begin();
ok(
    events.some((m) => m.t === 'voice_cancel'),
    'new take cancels old recognition',
);
turn = begin();
for (let n = 0; n < 1667; n++) router.handle(device, data(turn, n));
ok(events.at(-1).t === 'voice_cancel', '20 second buffer cap');
ok(!router.handle(device, 'GB_FPS fps=30'), 'non-audio diagnostics preserved');
console.log(`Encrypted voice routing: ${checks} checks passed`);
