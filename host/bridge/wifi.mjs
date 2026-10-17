// Keychain access stays in the hub; never include command output in errors or logs.
export async function wifiCredentials(ssidArg, run) {
    let ssid = ssidArg;
    if (ssid === undefined || ssid === '') {
        const ports = await run('networksetup', ['-listallhardwareports']);
        const iface =
            typeof ports === 'string' &&
            ports.match(/Hardware Port: (?:Wi-Fi|AirPort)\r?\nDevice: ([A-Za-z0-9]+)\b/)?.[1];
        const current = iface && (await run('networksetup', ['-getairportnetwork', iface]));
        ssid =
            typeof current === 'string' &&
            current.match(/^Current (?:Wi-Fi|AirPort) Network: ([^\r\n]+)\r?$/m)?.[1];
        if (!ssid)
            return {
                ok: false,
                message:
                    'macOS did not disclose the current Wi-Fi network; use node host/bridge/cli.mjs wifi <ssid> with a saved 2.4 GHz network',
            };
    }
    if (
        typeof ssid !== 'string' ||
        /[\x00-\x1f\x7f]/.test(ssid) ||
        Buffer.byteLength(ssid, 'utf8') > 32 ||
        !ssid.length
    ) {
        return {
            ok: false,
            message: 'Wi-Fi network name must contain 1–32 UTF-8 bytes and no control characters',
        };
    }
    const output = await run('security', [
        'find-generic-password',
        '-D',
        'AirPort network password',
        '-a',
        ssid,
        '-w',
    ]);
    if (output?.error === 'timeout')
        return {
            ok: false,
            message:
                'keychain access timed out; approve the macOS keychain dialog and retry node host/bridge/cli.mjs wifi <ssid>',
        };
    if (typeof output !== 'string')
        return {
            ok: false,
            message: `no keychain password for ${JSON.stringify(ssid)} (or access declined)`,
        };
    const pass = output.replace(/\r?\n$/, '');
    if (Buffer.byteLength(pass, 'utf8') > 64 || pass.includes('\0')) {
        return {
            ok: false,
            message: "the saved Wi-Fi password exceeds the badge's supported format",
        };
    }
    const b64 = (value) => Buffer.from(value, 'utf8').toString('base64');
    return { ok: true, ssid, line: `wifi ${b64(ssid)} ${b64(pass)}` };
}
