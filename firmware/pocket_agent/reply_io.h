#pragma once
#include "reply_core.h"
namespace badgereply {
static Receiver rx;
static char assistantId[9]{}, input[17]{}, rid[17]{}, usedInput[17]{}, challenge[33]{};
static uint8_t key[32]{}, *nonces = nullptr;
static bool visible = false, hashOpen = false, announced = false;
static uint32_t progressAt = 0;
static mbedtls_sha256_context hash;
static uint16_t page = 0, pages = 1, pageOffsets[350]{};
static uint32_t uiRevision = 0;
static bool hex(const char *s, uint8_t *out, size_t n) {
    if (strlen(s) != n * 2 || strspn(s, "0123456789abcdef") != n * 2)
        return false;
    for (size_t i = 0; i < n; i++) {
        char pair[3] = {s[i * 2], s[i * 2 + 1], 0};
        out[i] = strtoul(pair, nullptr, 16);
    }
    return true;
}
static void cancel() {
    if (rid[0] && (rx.receiving))
        netOut.printf("GB_REPLY_DONE %s cancelled\n", rid);
    if (hashOpen) {
        mbedtls_sha256_free(&hash);
        hashOpen = false;
    }
    rx.clear();
    visible = false;
    rid[0] = 0;
    uiRevision++;
}
static void failure() {
    rx.receiving = false;
    if (hashOpen) {
        mbedtls_sha256_free(&hash);
        hashOpen = false;
    }
    if (rid[0])
        netOut.printf("GB_REPLY_DONE %s error\n", rid);
    visible = rx.textReady();
    uiRevision++;
}
static void begin(const char *id, const char *take, const char *reply, size_t textBytes,
                  const char *focused, bool awake) {
    uint8_t random[16], ignored[8];
    if (!awake || strcmp(id, focused) || strcmp(id, badgevoice::id) ||
        strcmp(take, badgevoice::turn) || !strcmp(take, usedInput) || badgevoice::recording ||
        badgevoice::sending || !hex(reply, ignored, 8) || !textBytes || textBytes > 4096) {
        netOut.printf("GB_REPLY_DONE %s error\n", reply);
        return;
    }
    cancel();
    strcpy(assistantId, id);
    strcpy(input, take);
    strcpy(rid, reply);
    strcpy(usedInput, take);
    if (!rx.begin(textBytes)) {
        failure();
        return;
    }
    free(nonces);
    nonces = static_cast<uint8_t *>(ps_malloc(((textBytes + 383) / 384) * 12));
    if (!nonces) {
        failure();
        return;
    }
    announced = false;
    progressAt = millis();
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    hashOpen = true;
    esp_fill_random(random, sizeof(random));
    netlink::hexOf(random, sizeof(random), challenge);
    char label[64], keyHex[65];
    snprintf(label, sizeof(label), "pocket-agent-reply-v1:%s", challenge);
    netlink::hmacHex(netlink::token, label, keyHex);
    hex(keyHex, key, 32);
    netOut.printf("GB_REPLY_READY %s %s\n", rid, challenge);
}
static void data(const char *reply, size_t seq, const char *nonceText, const char *tagText,
                 const char *encoded) {
    if (strcmp(reply, rid) || !rx.receiving)
        return;
    uint8_t nonce[12], tag[16], cipher[384], plain[384];
    size_t n = 0;
    if (seq != rx.sequence || !hex(nonceText, nonce, 12) || !hex(tagText, tag, 16) ||
        strlen(encoded) > 512 || strlen(encoded) % 4 ||
        strspn(encoded, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") !=
            strlen(encoded) ||
        mbedtls_base64_decode(cipher, sizeof(cipher), &n,
                              reinterpret_cast<const uint8_t *>(encoded), strlen(encoded)) ||
        !n || n > 384) {
        failure();
        return;
    }
    for (size_t i = 0; i < rx.sequence; i++)
        if (!memcmp(nonces + i * 12, nonce, 12)) {
            failure();
            return;
        }
    char aad[96];
    const int length =
        snprintf(aad, sizeof(aad), "%s:%s:%s:%u", assistantId, input, rid, unsigned(seq));
    mbedtls_gcm_context aes;
    mbedtls_gcm_init(&aes);
    int err = mbedtls_gcm_setkey(&aes, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!err)
        err = mbedtls_gcm_auth_decrypt(&aes, n, nonce, 12, reinterpret_cast<uint8_t *>(aad), length,
                                       tag, 16, cipher, plain);
    mbedtls_gcm_free(&aes);
    if (err || !rx.append(seq, plain, n)) {
        failure();
        return;
    }
    memcpy(nonces + seq * 12, nonce, 12);
    mbedtls_sha256_update(&hash, plain, n);
    progressAt = millis();
    if (rx.textReady() && !visible) {
        visible = true;
        page = 0;
        uiRevision++;
    }
    netOut.printf("GB_REPLY_ACK %s %u\n", rid, unsigned(rx.sequence));
}
static void end(const char *reply, size_t bytes, const char *digest) {
    if (strcmp(reply, rid) || !rx.receiving)
        return;
    uint8_t sum[32];
    char result[65];
    mbedtls_sha256_finish(&hash, sum);
    mbedtls_sha256_free(&hash);
    hashOpen = false;
    netlink::hexOf(sum, 32, result);
    if (!rx.end(bytes, !strcmp(digest, result))) {
        failure();
        return;
    }
    uiRevision++;
}
static void poll(const char *focused, bool awake) {
    if ((visible || rx.receiving) && (!awake || strcmp(assistantId, focused))) {
        cancel();
        return;
    }
    if (rx.receiving && millis() - progressAt > 10000) {
        failure();
        return;
    }
    if (rx.complete && !announced) {
        announced = true;
        netOut.printf("GB_REPLY_DONE %s text\n", rid);
        uiRevision++;
    }
}
static bool process(char *line, const char *focused, bool awake) {
    if (!strcmp(line, "reply")) {
        netOut.printf("GB_REPLY_STATUS receiving=%d visible=%d bytes=%u seq=%u "
                      "text_ready=%d page=%u pages=%u\n",
                      rx.receiving, visible, unsigned(rx.bytes), unsigned(rx.sequence),
                      rx.textReady(), unsigned(page), unsigned(pages));
        return true;
    }
    if (strncmp(line, "reply ", 6))
        return false;
    char reply[17], extra[2];
    if (!strncmp(line, "reply begin ", 12)) {
        char id[9], take[17], t[9];
        long nt;
        if (sscanf(line + 12, "%8s %16s %16s %8s %1s", id, take, reply, t, extra) == 4 &&
            badgeprotocol::integer(t, 1, 4096, nt))
            begin(id, take, reply, size_t(nt), focused, awake);
        else
            netOut.println("GB_ERROR bad_reply");
    } else if (!strncmp(line, "reply data ", 11)) {
        char seq[9], nonce[25], tag[33], encoded[513];
        long n;
        if (sscanf(line + 11, "%16s %8s %24s %32s %512s %1s", reply, seq, nonce, tag, encoded,
                   extra) == 5 &&
            badgeprotocol::integer(seq, 0, 10, n))
            data(reply, size_t(n), nonce, tag, encoded);
        else
            failure();
    } else if (!strncmp(line, "reply end ", 10)) {
        char bytes[9], digest[65];
        long n;
        if (sscanf(line + 10, "%16s %8s %64s %1s", reply, bytes, digest, extra) == 3 &&
            badgeprotocol::integer(bytes, 1, 4096, n))
            end(reply, size_t(n), digest);
        else
            failure();
    } else if (sscanf(line, "reply cancel %16s %1s", reply, extra) == 1) {
        if (!strcmp(reply, rid))
            cancel();
    } else
        netOut.println("GB_ERROR bad_reply");
    return true;
}
} // namespace badgereply
