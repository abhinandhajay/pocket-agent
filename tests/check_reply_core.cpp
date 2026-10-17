#include "reply_core.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    badgereply::Receiver r;
    uint8_t data[384];
    memset(data, 'a', sizeof(data));
    assert(!r.begin(0));
    assert(!r.begin(4097));
    assert(r.begin(1004));
    memcpy(data, "Test", 4);
    assert(!r.append(1, data, 384));
    assert(!r.textReady());
    assert(r.append(0, data, 384));
    assert(!r.textReady());
    assert(!r.append(0, data, 384));
    assert(r.bytes == 384);
    assert(r.append(1, data, 384));
    assert(!r.end(1004, true));
    assert(!r.append(2, data, 384));
    assert(r.append(2, data, 236));
    assert(r.textReady() && strlen(r.text) == 1004);
    assert(!r.end(1004, false));
    assert(r.end(1004, true));
    assert(r.complete && !r.receiving);
    assert(!r.append(3, data, 1));
    r.clear();
    assert(!r.textReady() && !r.text[0] && !r.complete);
    assert(r.begin(3));
    data[0] = 0;
    assert(!r.append(0, data, 3) && r.bytes == 0);
    memcpy(data, "Yes", 3);
    assert(r.append(0, data, 3) && r.end(3, true));
    assert(!strcmp(r.text, "Yes"));
    assert(r.begin(4096));
    memset(data, 'a', sizeof(data));
    for (size_t seq = 0; seq < 11; ++seq)
        assert(r.append(seq, data, seq == 10 ? 256 : 384));
    assert(r.end(4096, true) && strlen(r.text) == 4096);
    std::cout << "Reply core: text bounds, sequence, digest and cleanup passed\n";
}
