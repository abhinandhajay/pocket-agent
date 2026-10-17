#pragma once
#include <cerrno>
#include <cmath>
#include <cstdlib>
namespace badgeprotocol {
inline bool integer(const char *text, long minimum, long maximum, long &value) {
    if (!text || !*text)
        return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno || end == text || *end || parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}
inline bool trailing(const char *text) {
    while (*text == ' ' || *text == '\t')
        ++text;
    return !*text;
}
} // namespace badgeprotocol
