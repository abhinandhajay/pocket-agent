// Inject a local TCP permission denial for the isolated board port only.
// Unix sockets and every other network target retain the native implementation.
import net from 'node:net';
import fs from 'node:fs';
const connect = net.connect.bind(net);
net.connect = function (options, ...args) {
    if (
        options &&
        typeof options === 'object' &&
        Number(options.port) === Number(process.env.POCKET_AGENT_DEVICE_PORT) &&
        JSON.parse(fs.readFileSync(process.env.POCKET_AGENT_USB_FIXTURE, 'utf8')).connectDenied
    ) {
        const socket = new net.Socket();
        queueMicrotask(() =>
            socket.destroy(
                Object.assign(new Error('fixture permission denial'), { code: 'EPERM' }),
            ),
        );
        return socket;
    }
    return connect(options, ...args);
};
