#include "protocol_numbers.h"
#include <cassert>
#include <iostream>
int main() {
    long value = 42;
    assert(badgeprotocol::integer("0", 0, 7, value) && value == 0);
    assert(badgeprotocol::integer("7", 0, 7, value) && value == 7);
    for (const char *input : {"", "abc", "-1", "8", "3suffix", "9999999999999999999999999999"}) {
        value = 42;
        assert(!badgeprotocol::integer(input, 0, 7, value));
        assert(value == 42);
    }
    assert(badgeprotocol::trailing(" \t"));
    assert(!badgeprotocol::trailing(" garbage"));
    std::cout << "Protocol numbers: strict range, overflow, malformed input and trailing-data "
                 "checks passed.\n";
}
