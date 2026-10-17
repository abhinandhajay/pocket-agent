#pragma once
#include <cstdint>
#include <cstring>
namespace badgevoice {
class LinkSelection {
  public:
    bool select(const char *kind, const char *value) {
        if (!kind ||
            (std::strcmp(kind, "usb") && std::strcmp(kind, "wifi") && std::strcmp(kind, "none")))
            return false;
        uint64_t next = 0;
        if (!value || !*value)
            return false;
        for (const char *p = value; *p; ++p) {
            if (*p < '0' || *p > '9')
                return false;
            const unsigned digit = unsigned(*p - '0');
            if (next > (kMaxRevision - digit) / 10)
                return false;
            next = next * 10 + digit;
        }
        if (!next || next <= revision_)
            return false;
        revision_ = next;
        std::strcpy(kind_, kind);
        return true;
    }
    const char *name() const { return kind_; }
    uint64_t revision() const { return revision_; }

  private:
    static constexpr uint64_t kMaxRevision = 9007199254740991ULL;
    uint64_t revision_ = 0;
    char kind_[5] = "none";
};
} // namespace badgevoice
