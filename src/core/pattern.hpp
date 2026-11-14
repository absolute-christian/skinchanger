#pragma once
// Portable AOB pattern parsing and matching.
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace core {

struct Pattern {
    std::vector<int16_t> bytes;  // -1 = wildcard
};

// "48 8B ?? 05" style. Needs at least 4 concrete bytes so a typo cannot
// turn into a pattern that matches everywhere.
inline std::optional<Pattern> parse_pattern(std::string_view text) {
    Pattern p;
    size_t concrete = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        std::string_view tok = text.substr(i, j - i);
        i = j;
        if (tok == "?" || tok == "??") {
            p.bytes.push_back(-1);
            continue;
        }
        if (tok.size() != 2) return std::nullopt;
        unsigned v = 0;
        auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + 2, v, 16);
        if (ec != std::errc() || ptr != tok.data() + 2) return std::nullopt;
        p.bytes.push_back(static_cast<int16_t>(v));
        ++concrete;
    }
    if (concrete < 4) return std::nullopt;
    return p;
}

// First match at or after `from`, or nullopt.
inline std::optional<size_t> find_pattern(const uint8_t* data, size_t size, const Pattern& p,
                                          size_t from = 0) {
    const size_t n = p.bytes.size();
    if (n == 0 || size < n) return std::nullopt;
    size_t anchor = 0;
    while (anchor < n && p.bytes[anchor] < 0) ++anchor;
    if (anchor >= n) return std::nullopt;
    for (size_t pos = from; pos + n <= size; ++pos) {
        if (data[pos + anchor] != static_cast<uint8_t>(p.bytes[anchor])) continue;
        bool ok = true;
        for (size_t k = 0; k < n; ++k) {
            if (p.bytes[k] >= 0 && data[pos + k] != static_cast<uint8_t>(p.bytes[k])) {
                ok = false;
                break;
            }
        }
        if (ok) return pos;
    }
    return std::nullopt;
}

}  // namespace core
