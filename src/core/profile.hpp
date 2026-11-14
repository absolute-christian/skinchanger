#pragma once
// Portable. Per-weapon skin settings and their on-disk form (profile.ini).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>

#include "core/ini.hpp"

namespace core {

struct SkinEntry {
    bool enabled = false;
    uint32_t paint_kit = 0;   // 0 = not chosen yet; such an entry is never applied
    uint32_t seed = 0;        // 0..1000
    float wear = 0.0f;        // 0..1, finite
    int32_t stattrak = -1;    // -1 = off, otherwise counter value
};

using Profile = std::map<uint16_t, SkinEntry>;  // key: weapon def index

constexpr uint32_t kMaxSeed = 1000;
constexpr int32_t kMaxStatTrak = 999999;
constexpr uint32_t kMaxPaintKit = 100000;

inline SkinEntry sanitize(SkinEntry e) {
    if (e.paint_kit > kMaxPaintKit) e.paint_kit = 0;
    if (e.seed > kMaxSeed) e.seed = kMaxSeed;
    if (!std::isfinite(e.wear) || e.wear < 0.0f) e.wear = 0.0f;
    if (e.wear > 1.0f) e.wear = 1.0f;
    if (e.stattrak < -1) e.stattrak = -1;
    if (e.stattrak > kMaxStatTrak) e.stattrak = kMaxStatTrak;
    return e;
}

inline bool is_applicable(const SkinEntry& e) { return e.enabled && e.paint_kit != 0; }

inline std::string serialize_profile(const Profile& p) {
    std::string out = "; skin changer profile (written by the tool)\n";
    for (const auto& [def, raw] : p) {
        SkinEntry e = sanitize(raw);
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "[weapon.%u]\nenabled=%d\npaint_kit=%u\nseed=%u\nwear=%.6f\nstattrak=%d\n\n",
                      static_cast<unsigned>(def), e.enabled ? 1 : 0, static_cast<unsigned>(e.paint_kit),
                      static_cast<unsigned>(e.seed), static_cast<double>(e.wear), static_cast<int>(e.stattrak));
        out += buf;
    }
    return out;
}

inline Profile parse_profile(std::string_view text) {
    Profile p;
    Ini ini = Ini::parse(text);
    for (const auto& [name, kv] : ini.sections) {
        constexpr std::string_view prefix = "weapon.";
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        auto def = parse_u64(std::string_view(name).substr(prefix.size()));
        if (!def || *def == 0 || *def > 0xFFFF) continue;
        SkinEntry e;
        auto get = [&](const char* k) { return ini.get(name, k); };
        if (auto v = get("enabled"); v) e.enabled = (*v == "1");
        if (auto v = get("paint_kit"); v) if (auto n = parse_u64(*v)) e.paint_kit = static_cast<uint32_t>(*n > 0xFFFFFFFFull ? 0 : *n);
        if (auto v = get("seed"); v) if (auto n = parse_u64(*v)) e.seed = static_cast<uint32_t>(*n > 0xFFFFFFFFull ? 0xFFFFFFFFull : *n);
        if (auto v = get("wear"); v) if (auto f = parse_f32(*v)) e.wear = *f;
        if (auto v = get("stattrak"); v) if (auto n = parse_i64(*v)) e.stattrak = static_cast<int32_t>(*n < -1 ? -1 : (*n > kMaxStatTrak ? kMaxStatTrak : *n));
        p[static_cast<uint16_t>(*def)] = sanitize(e);
    }
    return p;
}

}  // namespace core
