#pragma once
// Portable (no Windows headers). Tiny INI reader plus strict number parsing.
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace core {

inline std::string_view trim(std::string_view s) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

// Decimal or 0x-prefixed hex. Rejects empty input, signs and trailing junk.
inline std::optional<uint64_t> parse_u64(std::string_view s) {
    s = trim(s);
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s.remove_prefix(2);
    }
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

inline std::optional<int64_t> parse_i64(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    int64_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

// Finite floats only.
inline std::optional<float> parse_f32(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    float v = 0.0f;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

struct Ini {
    std::map<std::string, std::map<std::string, std::string>> sections;

    static Ini parse(std::string_view text) {
        Ini ini;
        std::string section;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos) eol = text.size();
            std::string_view line = trim(text.substr(pos, eol - pos));
            pos = eol + 1;
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[') {
                auto close = line.find(']');
                if (close == std::string_view::npos) continue;
                section = std::string(trim(line.substr(1, close - 1)));
                ini.sections[section];
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string_view::npos) continue;
            std::string key(trim(line.substr(0, eq)));
            std::string_view val = line.substr(eq + 1);
            if (auto sc = val.find(';'); sc != std::string_view::npos) val = val.substr(0, sc);
            if (!key.empty()) ini.sections[section][key] = std::string(trim(val));
        }
        return ini;
    }

    // nullopt when the key is absent OR its value is empty ("unset").
    std::optional<std::string> get(std::string_view section, std::string_view key) const {
        auto s = sections.find(std::string(section));
        if (s == sections.end()) return std::nullopt;
        auto k = s->second.find(std::string(key));
        if (k == s->second.end() || k->second.empty()) return std::nullopt;
        return k->second;
    }

    // Looks the key up in every section; first non-empty hit wins.
    std::optional<std::string> get_any(std::string_view key) const {
        for (const auto& [name, kv] : sections) {
            auto k = kv.find(std::string(key));
            if (k != kv.end() && !k->second.empty()) return k->second;
        }
        return std::nullopt;
    }
};

}  // namespace core
