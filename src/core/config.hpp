#pragma once
// Portable. Every build-dependent value lives in offsets.ini and is read here.
// "Unset" (key missing or empty) is distinct from a real 0.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/ini.hpp"

namespace core {

struct KeyInfo {
    const char* name;
    bool required;
};

inline constexpr KeyInfo kNumericKeys[] = {
    // build identity of client.dll the values below were taken from
    {"client_time_date_stamp", true},
    {"client_size_of_image", true},
    // RVAs from client.dll base (cs2-dumper offsets output)
    {"dwEntityList", true},
    {"dwLocalPlayerPawn", true},
    {"dwLocalPlayerController", true},
    // entity system constants
    {"handle_index_mask", true},
    {"handle_chunk_shift", true},
    {"handle_chunk_mask", true},
    {"entlist_chunk_base", true},
    {"entity_stride", true},
    {"identity_handle_off", false},
    {"utl_vector_size_off", true},
    {"utl_vector_data_off", true},
    // field offsets (cs2-dumper client_dll output)
    {"m_pWeaponServices", true},
    {"m_hMyWeapons", true},
    {"m_AttributeManager", true},
    {"m_Item", true},
    {"m_iItemDefinitionIndex", true},
    {"m_iItemIDHigh", true},
    {"m_iItemIDLow", false},
    {"m_iAccountID", true},
    {"m_nFallbackPaintKit", true},
    {"m_nFallbackSeed", true},
    {"m_flFallbackWear", true},
    {"m_nFallbackStatTrak", true},
    {"m_steamID", true},
    // hook behaviour (only needed when a signature below is set)
    {"frame_stage_apply", false},
    {"fsn_apply_after_original", false},
    {"confirm_fsn_prototype", false},
    {"confirm_regen_prototype", false},
};

inline constexpr const char* kStringKeys[] = {
    "sig_frame_stage_notify",
    "sig_regenerate_weapon_skin",
    "build_note",
};

struct Config {
    std::map<std::string, uint64_t> num;
    std::map<std::string, std::string> str;
    std::vector<std::string> bad_values;  // present but not parsable

    static Config from_ini(const Ini& ini) {
        Config c;
        for (const auto& k : kNumericKeys) {
            auto v = ini.get_any(k.name);
            if (!v) continue;
            if (auto n = parse_u64(*v))
                c.num[k.name] = *n;
            else
                c.bad_values.push_back(k.name);
        }
        for (const char* k : kStringKeys)
            if (auto v = ini.get_any(k)) c.str[k] = *v;
        return c;
    }

    std::optional<uint64_t> get(const std::string& key) const {
        auto it = num.find(key);
        if (it == num.end()) return std::nullopt;
        return it->second;
    }
    std::optional<std::string> get_str(const std::string& key) const {
        auto it = str.find(key);
        if (it == str.end()) return std::nullopt;
        return it->second;
    }

    std::vector<std::string> missing_required() const {
        std::vector<std::string> out;
        for (const auto& k : kNumericKeys)
            if (k.required && !num.count(k.name)) out.push_back(k.name);
        return out;
    }
};

// Plain resolved values for the game code. Only constructible when every
// required key is present and the numbers are mutually consistent.
struct Layout {
    uint64_t entity_list_rva, local_pawn_rva, local_controller_rva;
    uint32_t idx_mask, chunk_shift, chunk_mask, chunk_base, entity_stride;
    std::optional<uint32_t> identity_handle_off;
    uint32_t vec_size_off, vec_data_off;
    uint32_t weapon_services, my_weapons, attribute_manager, item;
    uint32_t item_def_index, item_id_high, account_id;
    std::optional<uint32_t> item_id_low;
    uint32_t fb_paint_kit, fb_seed, fb_wear, fb_stattrak, steam_id;

    static std::optional<Layout> from(const Config& c, std::string* why = nullptr) {
        auto fail = [&](const char* m) -> std::optional<Layout> {
            if (why) *why = m;
            return std::nullopt;
        };
        if (!c.missing_required().empty() || !c.bad_values.empty()) return fail("config incomplete");
        auto u = [&](const char* k) { return c.num.at(k); };
        auto opt32 = [&](const char* k) -> std::optional<uint32_t> {
            auto v = c.get(k);
            if (!v) return std::nullopt;
            return static_cast<uint32_t>(*v);
        };
        Layout l{};
        l.entity_list_rva = u("dwEntityList");
        l.local_pawn_rva = u("dwLocalPlayerPawn");
        l.local_controller_rva = u("dwLocalPlayerController");
        if (l.entity_list_rva == 0 || l.local_pawn_rva == 0 || l.local_controller_rva == 0 ||
            l.entity_list_rva > 0x20000000 || l.local_pawn_rva > 0x20000000 ||
            l.local_controller_rva > 0x20000000)
            return fail("global RVA out of range");
        auto u32small = [&](const char* k, uint32_t lim, uint32_t& out) {
            uint64_t v = u(k);
            if (v >= lim) return false;
            out = static_cast<uint32_t>(v);
            return true;
        };
        if (!u32small("handle_index_mask", 0x100000, l.idx_mask) || l.idx_mask == 0)
            return fail("handle_index_mask invalid");
        if (!u32small("handle_chunk_shift", 16, l.chunk_shift) || l.chunk_shift == 0)
            return fail("handle_chunk_shift invalid");
        if (!u32small("handle_chunk_mask", 0x10000, l.chunk_mask) ||
            l.chunk_mask != (1u << l.chunk_shift) - 1)
            return fail("handle_chunk_mask does not match handle_chunk_shift");
        if (!u32small("entlist_chunk_base", 0x1000, l.chunk_base)) return fail("entlist_chunk_base invalid");
        if (!u32small("entity_stride", 0x400, l.entity_stride) || l.entity_stride < 8)
            return fail("entity_stride invalid");
        l.identity_handle_off = opt32("identity_handle_off");
        if (l.identity_handle_off && *l.identity_handle_off >= l.entity_stride)
            return fail("identity_handle_off outside entity_stride");
        if (!u32small("utl_vector_size_off", 0x100, l.vec_size_off) ||
            !u32small("utl_vector_data_off", 0x100, l.vec_data_off))
            return fail("utl_vector offsets invalid");
        const struct {
            const char* k;
            uint32_t* out;
        } fields[] = {
            {"m_pWeaponServices", &l.weapon_services},
            {"m_hMyWeapons", &l.my_weapons},
            {"m_AttributeManager", &l.attribute_manager},
            {"m_Item", &l.item},
            {"m_iItemDefinitionIndex", &l.item_def_index},
            {"m_iItemIDHigh", &l.item_id_high},
            {"m_iAccountID", &l.account_id},
            {"m_nFallbackPaintKit", &l.fb_paint_kit},
            {"m_nFallbackSeed", &l.fb_seed},
            {"m_flFallbackWear", &l.fb_wear},
            {"m_nFallbackStatTrak", &l.fb_stattrak},
            {"m_steamID", &l.steam_id},
        };
        for (const auto& f : fields)
            if (!u32small(f.k, 0x10000, *f.out)) return fail("a field offset is out of range");
        l.item_id_low = opt32("m_iItemIDLow");
        if (l.item_id_low && *l.item_id_low >= 0x10000) return fail("m_iItemIDLow out of range");
        return l;
    }
};

struct EntitySlot {
    uint32_t chunk;
    uint32_t slot;
};

// Splits an entity handle into (chunk, slot). nullopt for the invalid handle
// or a chunk index that cannot exist.
inline std::optional<EntitySlot> split_handle(uint32_t handle, const Layout& l) {
    if (handle == 0xFFFFFFFFu) return std::nullopt;
    uint32_t idx = handle & l.idx_mask;
    EntitySlot s{idx >> l.chunk_shift, idx & l.chunk_mask};
    if (s.chunk >= 128) return std::nullopt;
    return s;
}

// User-mode canonical, 8-aligned, not in the first 64 KiB.
inline bool plausible_pointer(uint64_t p) {
    return p >= 0x10000 && p < 0x00007FFFFFFE0000ull && (p & 7) == 0;
}

}  // namespace core
