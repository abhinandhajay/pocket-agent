#pragma once
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <sys/socket.h>
#include "voice_button.h"
#include "voice_link.h"
namespace badgevoice {
static constexpr size_t kMaxSamples = 16000 * 20, kBlock = 512, kChunk = 384;
static int16_t *pcm = nullptr;
static int16_t blocks[2][kBlock];
static size_t samples = 0, queued = 0, head = 0, sentBytes = 0, sequence = 0;
static bool recording = false, sending = false, cancelWanted = false, beginSent = false;
static uint32_t started = 0, progressAt = 0, labelUntil = 0;
static char id[9]{}, turn[17]{}, label[44]{}, digest[65]{}, channel[5] = "none";
static char challenge[33]{};
static char tx[768]{};
static size_t txSize = 0, txPos = 0;
static mbedtls_sha256_context hash;
static HoldButton hold;
static LinkSelection selection;
static void (*beforeStart)() = nullptr;
static void (*linkChanged)() = nullptr;
static bool ready() {
    return strlen(netlink::token) == 64 &&
           ((!strcmp(channel, "usb") && Serial) || (!strcmp(channel, "wifi") && netlink::linkUp));
}
static void text(const char *value, uint32_t duration = 60000) {
    strncpy(label, value, sizeof(label) - 1);
    labelUntil = millis() + duration;
}
static const char *activity(const char *assistantId) {
    return !strcmp(id, assistantId) && int32_t(labelUntil - millis()) > 0 ? label : nullptr;
}
static void microphoneEnd() {
    M5.Mic.end();
    recording = false;
    queued = 0;
    M5.Speaker.begin();
}
static void cancel() {
    if (recording)
        microphoneEnd();
    cancelWanted = turn[0] != 0;
    sending = false;
    text("Voice cancelled", 1800);
}
static bool start(const char *assistantId) {
    if (!ready() || txSize)
        return false;
    if (beforeStart)
        beforeStart();
    if (recording)
        microphoneEnd();
    M5.Speaker.stop();
    M5.Speaker.end();
    if (!pcm)
        pcm = static_cast<int16_t *>(ps_malloc(kMaxSamples * sizeof(int16_t)));
    if (!pcm || !M5.Mic.begin()) {
        M5.Mic.end();
        M5.Speaker.begin();
        text("Microphone unavailable", 2500);
        return false;
    }
    uint8_t random[8];
    esp_fill_random(random, sizeof(random));
    netlink::hexOf(random, sizeof(random), turn);
    snprintf(id, sizeof(id), "%.8s", assistantId);
    samples = queued = head = sentBytes = sequence = 0;
    sending = cancelWanted = beginSent = false;
    challenge[0] = 0;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    for (int k = 0; k < 2; ++k) {
        if (!M5.Mic.record(blocks[k], kBlock, 16000, false)) {
            microphoneEnd();
            text("Microphone unavailable", 2500);
            return false;
        }
        queued++;
    }
    recording = true;
    started = progressAt = millis();
    text("Listening...");
    return true;
}
static void collect() {
    while (queued > M5.Mic.isRecording()) {
        const size_t count = min(kBlock, kMaxSamples - samples);
        memcpy(pcm + samples, blocks[head], count * 2);
        mbedtls_sha256_update(&hash, reinterpret_cast<uint8_t *>(blocks[head]), count * 2);
        samples += count;
        head = (head + 1) % 2;
        queued--;
        if (samples == kMaxSamples)
            break;
    }
}
static void stop() {
    if (!recording)
        return;
    collect();
    microphoneEnd();
    if (samples < 4800) {
        cancel();
        text("Hold and speak", 2000);
        return;
    }
    uint8_t sum[32];
    mbedtls_sha256_finish(&hash, sum);
    mbedtls_sha256_free(&hash);
    netlink::hexOf(sum, 32, digest);
    sending = true;
    progressAt = millis();
    text("Transcribing...");
}
static void clearTx() {
    txSize = txPos = 0;
    netlink::voiceUsbBusy = netlink::voiceWifiBusy = false;
}
static bool flush() {
    if (!txSize)
        return true;
    if (!ready()) {
        clearTx();
        sending = cancelWanted = false;
        if (recording)
            microphoneEnd();
        text("Voice disconnected", 2500);
        return false;
    }
    int n = 0;
    if (!strcmp(channel, "usb")) {
        // One writer, one whole line: no partial audio frame can interleave a diagnostic.
        if (Serial.availableForWrite() >= int(txSize - txPos))
            n = Serial.write(reinterpret_cast<uint8_t *>(tx) + txPos, txSize - txPos);
    } else if (xSemaphoreTake(netlink::clientMutex, 0) == pdTRUE) {
        n = ::send(netlink::client.fd(), tx + txPos, txSize - txPos, MSG_DONTWAIT);
        xSemaphoreGive(netlink::clientMutex);
    }
    if (n > 0) {
        txPos += n;
        progressAt = millis();
    }
    if (txPos == txSize) {
        clearTx();
        return true;
    }
    netlink::voiceUsbBusy = !strcmp(channel, "usb");
    netlink::voiceWifiBusy = !strcmp(channel, "wifi");
    if (millis() - progressAt > 5000) {
        clearTx();
        sending = cancelWanted = false;
        text("Voice transfer failed", 2500);
    }
    return false;
}
static void packet() {
    if (cancelWanted) {
        txSize = snprintf(tx, sizeof(tx), "GB_VOICE_CANCEL %s\n", turn);
        cancelWanted = false;
    } else if (!beginSent) {
        txSize = snprintf(tx, sizeof(tx), "GB_VOICE_BEGIN %s %s\n", id, turn);
        beginSent = true;
    } else if (sentBytes < samples * 2) {
        const size_t n = min(kChunk, samples * 2 - sentBytes);
        uint8_t key[32], nonce[12], tag[16], ciphertext[kChunk];
        char keyHex[65], nonceHex[25], tagHex[33], b64[516], aad[64];
        char keyLabel[64];
        snprintf(keyLabel, sizeof(keyLabel), "pocket-agent-voice-v1:%s", challenge);
        netlink::hmacHex(netlink::token, keyLabel, keyHex);
        for (int k = 0; k < 32; ++k) {
            char pair[3] = {keyHex[2 * k], keyHex[2 * k + 1], 0};
            key[k] = strtoul(pair, nullptr, 16);
        }
        esp_fill_random(nonce, sizeof(nonce));
        const int aadSize = snprintf(aad, sizeof(aad), "%s:%s:%u", id, turn, unsigned(sequence));
        mbedtls_gcm_context aes;
        mbedtls_gcm_init(&aes);
        int err = mbedtls_gcm_setkey(&aes, MBEDTLS_CIPHER_ID_AES, key, 256);
        if (!err)
            err = mbedtls_gcm_crypt_and_tag(
                &aes, MBEDTLS_GCM_ENCRYPT, n, nonce, 12, reinterpret_cast<uint8_t *>(aad), aadSize,
                reinterpret_cast<uint8_t *>(pcm) + sentBytes, ciphertext, 16, tag);
        mbedtls_gcm_free(&aes);
        memset(key, 0, sizeof(key));
        if (err) {
            cancel();
            return;
        }
        netlink::hexOf(nonce, 12, nonceHex);
        netlink::hexOf(tag, 16, tagHex);
        size_t encoded = 0;
        mbedtls_base64_encode(reinterpret_cast<uint8_t *>(b64), sizeof(b64) - 1, &encoded,
                              ciphertext, n);
        b64[encoded] = 0;
        txSize = snprintf(tx, sizeof(tx), "GB_VOICE_DATA %s %u %s %s %s\n", turn,
                          unsigned(sequence++), nonceHex, tagHex, b64);
        sentBytes += n;
    } else {
        txSize = snprintf(tx, sizeof(tx), "GB_VOICE_END %s %u %s\n", turn, unsigned(samples * 2),
                          digest);
        sending = false;
    }
}
static void poll() {
    if (recording) {
        if (!ready())
            cancel();
        else {
            collect();
            if (samples == kMaxSamples || millis() - started >= 20000)
                stop();
            else
                while (queued < 2) {
                    const size_t slot = (head + queued) % 2;
                    if (!M5.Mic.record(blocks[slot], kBlock, 16000, false)) {
                        cancel();
                        text("Microphone failed", 2500);
                        break;
                    }
                    queued++;
                }
        }
    }
    for (int k = 0; k < 8; ++k) {
        if (!flush())
            break;
        if (!sending && !cancelWanted && !(recording && !beginSent))
            break;
        if (sending && beginSent && !challenge[0] && !cancelWanted) {
            if (millis() - progressAt > 5000) {
                cancel();
                text("Voice host unavailable", 2500);
            }
            break;
        }
        packet();
    }
    flush();
}
static void link(const char *next, const char *revision) {
    if (!selection.select(next, revision))
        return;
    if (strcmp(channel, next)) {
        if (linkChanged)
            linkChanged();
        cancel();
        clearTx();
        cancelWanted = false;
        strcpy(channel, selection.name());
    }
}
static void feedback(const char *take, const char *result) {
    if (strcmp(turn, take))
        return;
    if (!strcmp(result, "submitted"))
        text("Command sent", 2000);
    else if (!strcmp(result, "silent"))
        text("No speech heard", 2500);
    else if (!strcmp(result, "busy")) {
        cancel();
        text("Hermes is busy", 2500);
    } else {
        cancel();
        text("Voice failed", 2500);
    }
}
static void acceptChallenge(const char *take, const char *value) {
    if (!strcmp(turn, take) && strlen(value) == 32 && strspn(value, "0123456789abcdef") == 32 &&
        !challenge[0])
        strcpy(challenge, value);
}
} // namespace badgevoice
