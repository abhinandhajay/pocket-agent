export async function resolve(specifier, context, next) {
    if (specifier === 'serialport')
        return { url: new URL('./fake-usb.mjs', import.meta.url).href, shortCircuit: true };
    return next(specifier, context);
}
