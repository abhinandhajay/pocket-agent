// Keychain/OS boundaries are substituted; this never reads a real password.
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const { wifiCredentials } = await import(
    pathToFileURL(path.join(path.resolve(process.argv[2]), 'wifi.mjs'))
);
let checks = 0;
const eq = (actual, expected) => {
    assert.deepEqual(actual, expected);
    checks++;
};
function runner(responses) {
    const calls = [];
    return {
        calls,
        run: async (cmd, args) => {
            calls.push([cmd, args]);
            return responses[cmd + ' ' + args[0]] ?? null;
        },
    };
}
{
    const r = runner({ 'security find-generic-password': 'fixture-password\n' });
    const result = await wifiCredentials('café room', r.run);
    eq(result.ok, true);
    eq(r.calls, [
        [
            'security',
            ['find-generic-password', '-D', 'AirPort network password', '-a', 'café room', '-w'],
        ],
    ]);
    const [, ssid, pass] = result.line.split(' ');
    eq(Buffer.from(ssid, 'base64').toString('utf8'), 'café room');
    eq(Buffer.from(pass, 'base64').toString('utf8'), 'fixture-password');
    eq(JSON.stringify({ ...result, line: undefined }).includes('fixture-password'), false);
}
{
    const r = runner({
        'networksetup -listallhardwareports':
            'Hardware Port: Ethernet\nDevice: en0\n\nHardware Port: Wi-Fi\nDevice: en7\n',
        'networksetup -getairportnetwork': 'Current Wi-Fi Network: Current Network\r\n',
        'security find-generic-password': 'fixture\n',
    });
    const result = await wifiCredentials(undefined, r.run);
    eq(result.ssid, 'Current Network');
    eq(r.calls[1], ['networksetup', ['-getairportnetwork', 'en7']]);
    eq(
        r.calls.some(([, args]) => args.includes('-listpreferredwirelessnetworks')),
        false,
    );
}
for (const output of [null, 'You are not associated with an AirPort network.\n']) {
    const r = runner({
        'networksetup -listallhardwareports': 'Hardware Port: Wi-Fi\nDevice: en0\n',
        'networksetup -getairportnetwork': output,
    });
    const result = await wifiCredentials(undefined, r.run);
    eq(result.ok, false);
    eq(result.message.includes('cli.mjs wifi <ssid>'), true);
    eq(
        r.calls.some(([cmd]) => cmd === 'security'),
        false,
    );
}
for (const ssid of ['x'.repeat(33), 'é'.repeat(17), 'bad\nname', 'bad\0name', 42, {}]) {
    const r = runner({ 'security find-generic-password': 'fixture\n' });
    eq((await wifiCredentials(ssid, r.run)).ok, false);
    eq(r.calls.length, 0);
}
{
    const r = runner({ 'security find-generic-password': 'fixture\n' });
    eq((await wifiCredentials('é'.repeat(16), r.run)).ok, true);
}
{
    const r = runner({});
    const result = await wifiCredentials('missing', r.run);
    eq(result.ok, false);
    eq(result.message.includes('access declined'), true);
}
for (const password of ['x'.repeat(65), 'é'.repeat(33), 'bad\0pass']) {
    const r = runner({ 'security find-generic-password': password + '\n' });
    const result = await wifiCredentials('network', r.run);
    eq(result.ok, false);
    eq(JSON.stringify(result).includes(password), false);
}
{
    const r = runner({ 'security find-generic-password': { error: 'timeout' } });
    const result = await wifiCredentials('network', r.run);
    eq(result.ok, false);
    eq(result.message.includes('timed out'), true);
}
console.log(`pocket-agent Wi-Fi credentials: ${checks} checks passed`);
