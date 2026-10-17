#include "voice_link.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    badgevoice::LinkSelection link;
    assert(!link.select("wifi", nullptr));
    assert(link.select("wifi", "100"));
    assert(link.select("usb", "102"));
    // An earlier Wi-Fi command arrives after USB on the other transport.
    assert(!link.select("wifi", "101"));
    assert(!std::strcmp(link.name(), "usb") && link.revision() == 102);
    assert(!link.select("wifi", "102"));
    assert(!link.select("wifi", nullptr));
    // A real later release must still switch to Wi-Fi, and return to USB.
    assert(link.select("wifi", "103"));
    assert(!std::strcmp(link.name(), "wifi"));
    assert(link.select("usb", "104"));
    for (const char *bad :
         {"", "0", "-1", "+105", "105x", "105 ", "9007199254740992", "18446744073709551616"}) {
        assert(!link.select("wifi", bad));
        assert(!std::strcmp(link.name(), "usb") && link.revision() == 104);
    }
    assert(!link.select("bad", "105"));
    assert(link.select("none", "105"));
    assert(link.select("usb", "9007199254740991"));
    assert(!link.select("wifi", "9007199254740991"));
    std::cout << "Voice link: delayed standby, duplicate, missing revision, failover and revision "
                 "bounds passed\n";
}
