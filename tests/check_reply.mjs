import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
const { ReplyRouter } = await import(pathToFileURL(path.join(process.argv[2], 'reply.mjs')));
const owner = {},
    other = {},
    id = '12345678',
    device = '0123456789ab',
    turn = 'a'.repeat(16),
    token = 'b'.repeat(64),
    challenge = 'c'.repeat(32);
let p = { owner },
    events = [],
    lines = [],
    time = 1000,
    ordinal = 0,
    checks = 0;
const ok = (condition, name) => {
    assert(condition, name);
    checks++;
};
const router = new ReplyRouter({
    getAssistant: (x) => (x === id ? p : null),
    token: () => token,
    send: (o, m) => events.push({ o, ...m }),
    write: (_, l) => lines.push(l),
    now: () => time,
});
function start(textBytes = 4) {
    router.authorize(device, { owner, id, turn });
    const reply = (++ordinal).toString(16).padStart(16, '0');
    router.client(owner, {
        t: 'reply_begin',
        id,
        turn,
        reply,
        text_bytes: textBytes,
    });
    return reply;
}
let reply = start(1004);
ok(lines.at(-1).startsWith('reply begin '), 'authorized owner/device');
router.client(other, { t: 'reply_data', reply, seq: 0, data: 'AAAA' });
ok(router.outputs.has(device), 'other owner cannot mutate');
router.device(device, `GB_REPLY_READY ${reply} ${challenge}`);
ok(events.at(-1).count === 8, 'bounded initial credit');
const payload = Buffer.alloc(1004, 'a');
payload.write('Text');
for (let seq = 0, offset = 0; offset < payload.length; seq++, offset += 384) {
    const data = payload.subarray(offset, offset + 384);
    router.client(owner, { t: 'reply_data', reply, seq, data: data.toString('base64') });
    const fields = lines.at(-1).split(' '),
        dec = crypto.createDecipheriv(
            'aes-256-gcm',
            crypto
                .createHmac('sha256', token)
                .update('pocket-agent-reply-v1:' + challenge)
                .digest(),
            Buffer.from(fields[4], 'hex'),
        );
    dec.setAAD(Buffer.from(`${id}:${turn}:${reply}:${seq}`));
    dec.setAuthTag(Buffer.from(fields[5], 'hex'));
    ok(
        Buffer.concat([dec.update(Buffer.from(fields[6], 'base64')), dec.final()]).equals(data),
        'authenticated text matches',
    );
    router.device(device, `GB_REPLY_ACK ${reply} ${seq + 1}`);
}
router.client(owner, {
    t: 'reply_end',
    reply,
    bytes: payload.length,
    digest: crypto.createHash('sha256').update(payload).digest('hex'),
});
ok(lines.at(-1).startsWith('reply end'), 'full digest verified');
router.device(device, `GB_REPLY_DONE ${reply} text`);
ok(events.at(-1).result === 'text', 'completed text feedback');
router.client(owner, { t: 'reply_begin', id, turn, reply, text_bytes: 4 });
ok(!router.outputs.size, 'ticket consumed once');
reply = start();
router.device(device, `GB_REPLY_READY ${reply} ${challenge}`);
router.client(owner, {
    t: 'reply_data',
    reply,
    seq: 1,
    data: Buffer.from('Text').toString('base64'),
});
ok(!router.outputs.size, 'bad sequence cancels');
reply = start();
router.device(device, `GB_REPLY_READY ${reply} ${challenge}`);
router.client(owner, { t: 'reply_data', reply, seq: 0, data: 'QQ==' });
ok(!router.outputs.size, 'short non-final chunk rejected');
reply = start();
router.device(device, `GB_REPLY_READY ${reply} ${challenge}`);
router.device(device, `GB_REPLY_ACK ${reply} 100`);
ok(!router.outputs.size, 'invented ACK rejected');
reply = start();
router.device(device, `GB_REPLY_READY ${reply} ${challenge}`);
router.device(device, `GB_REPLY_DONE ${reply} text`);
ok(!router.outputs.size && events.at(-1).t === 'reply_cancel', 'early completion rejected');
reply = start();
router.activate(device, '87654321');
ok(!router.outputs.size, 'Assistant identity change cancels');
reply = start();
router.dropOwner(owner);
ok(!router.outputs.size && !router.tickets.size, 'owner crash cancels');
reply = start();
p = { owner: other };
router.expire();
ok(!router.outputs.size, 'replacement owner cancels');
p = { owner };
reply = start();
time += 10001;
router.expire();
ok(!router.outputs.size, 'handshake timeout');
router.authorize(device, { owner, id, turn });
router.client(owner, {
    t: 'reply_begin',
    id,
    turn,
    reply: 'd'.repeat(16),
    text_bytes: 4097,
});
ok(!router.outputs.size, 'text cap');
ok(!router.device(device, 'GB_FPS fps=30'), 'normal diagnostics preserved');
console.log(`Reply routing: ${checks} checks passed`);
