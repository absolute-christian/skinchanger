#include "game/skins.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "util/log.hpp"

namespace game {
namespace {

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

// Prototype void(void* weapon) is an ASSUMPTION; it is only ever used when the
// user set confirm_regen_prototype=1 in offsets.ini. No C++ objects here (SEH).
bool call_regen(uintptr_t fn, uintptr_t weapon) {
    using Fn = void(__fastcall*)(void*);
    __try {
        reinterpret_cast<Fn>(fn)(reinterpret_cast<void*>(weapon));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
bool set_if_different(uintptr_t addr, T value, bool& changed, Counters& c) {
    auto cur = rd<T>(addr);
    if (!cur) {
        c.write_failures++;
        return false;
    }
    if (std::memcmp(&*cur, &value, sizeof(T)) == 0) return true;
    if (!safe_write(addr, &value, sizeof(T))) {
        c.write_failures++;
        return false;
    }
    changed = true;
    return true;
}

}  // namespace

void SkinChanger::initialize(const std::filesystem::path& offsets_ini) {
    Report& r = report_;
    std::ifstream in(offsets_ini, std::ios::binary);
    if (!in) {
        util::log("offsets.ini not found at %s", offsets_ini.string().c_str());
        return;
    }
    r.config_file_found = true;
    std::stringstream ss;
    ss << in.rdbuf();
    const core::Config cfg = core::Config::from_ini(core::Ini::parse(ss.str()));
    r.missing_keys = cfg.missing_required();
    r.bad_values = cfg.bad_values;

    if (auto m = find_module(L"client.dll")) {
        r.client_found = true;
        r.client = *m;
        auto ts = cfg.get("client_time_date_stamp");
        auto sz = cfg.get("client_size_of_image");
        if (ts && sz) {
            r.build_ok = (*ts == m->time_date_stamp) && (*sz == m->size);
            r.build_detail = "expected stamp " + hex(*ts) + " size " + hex(*sz) + ", actual stamp " +
                             hex(m->time_date_stamp) + " size " + hex(m->size);
        } else {
            r.build_detail = "client_time_date_stamp / client_size_of_image not set";
        }
    } else {
        r.build_detail = "client.dll is not loaded (or its PE headers are not valid)";
    }

    if (r.missing_keys.empty() && r.bad_values.empty()) {
        layout_ = core::Layout::from(cfg, &r.layout_error);
        r.layout_ok = layout_.has_value();
        if (r.layout_ok && !layout_->identity_handle_off)
            r.notes += "identity_handle_off unset: stale entity handles are NOT checked. ";
        if (r.layout_ok && !layout_->item_id_low) r.notes += "m_iItemIDLow unset: only ItemIDHigh is written. ";
    }

    // Signatures are scanned only on a build the offsets were taken from.
    auto resolve = [&](const char* key, bool& set, bool& resolved, uintptr_t& addr, std::string& detail) {
        auto sig = cfg.get_str(key);
        if (!sig) {
            detail = "signature not set";
            return;
        }
        set = true;
        if (!r.client_found || !r.build_ok) {
            detail = "skipped: build identity not confirmed";
            return;
        }
        auto pat = core::parse_pattern(*sig);
        if (!pat) {
            detail = "pattern is invalid (need hex bytes / ??, at least 4 concrete bytes)";
            return;
        }
        auto res = scan_unique(r.client, *pat);
        switch (res.status) {
            case ScanStatus::Found:
                resolved = true;
                addr = res.address;
                detail = "unique match at client.dll+" + hex(res.address - r.client.base);
                break;
            case ScanStatus::NotFound: detail = "no match in client.dll"; break;
            case ScanStatus::Ambiguous: detail = "more than one match; pattern must be unique"; break;
        }
    };
    resolve("sig_frame_stage_notify", r.fsn_sig_set, r.fsn_resolved, r.fsn_address, r.fsn_detail);
    resolve("sig_regenerate_weapon_skin", r.regen_sig_set, r.regen_resolved, r.regen_address, r.regen_detail);

    if (auto v = cfg.get("frame_stage_apply")) fsn_stage_ = static_cast<int>(*v);
    fsn_confirmed_ = cfg.get("confirm_fsn_prototype") == 1u;
    apply_after_original_ = cfg.get("fsn_apply_after_original") == 1u;
    if (r.fsn_resolved && !fsn_stage_) r.fsn_detail += "; frame_stage_apply not set";
    if (r.fsn_resolved && !fsn_confirmed_) r.fsn_detail += "; confirm_fsn_prototype != 1";

    if (r.regen_resolved) {
        if (cfg.get("confirm_regen_prototype") == 1u)
            regen_fn_ = r.regen_address;
        else
            r.regen_detail += "; confirm_regen_prototype != 1, function will not be called";
    }

    util::log("init: config=%d layout=%d client=%d build_ok=%d fsn_hookable=%d regen=%d", r.config_file_found,
              r.layout_ok, r.client_found, r.build_ok, fsn_hookable(), regen_fn_ != 0);
}

void SkinChanger::set_profile(std::shared_ptr<const core::Profile> p) {
    std::lock_guard<std::mutex> g(profile_mutex_);
    profile_ = std::move(p);
}

std::shared_ptr<const core::Profile> SkinChanger::profile_snapshot() {
    std::lock_guard<std::mutex> g(profile_mutex_);
    return profile_;
}

void SkinChanger::on_frame_stage(int stage, bool after_original) {
    if (!enabled_.load() || !report_.apply_ready() || !fsn_stage_) return;
    if (stage != *fsn_stage_ || after_original != apply_after_original_) return;
    apply();
}

void SkinChanger::apply() {
    counters_.frames++;
    auto prof = profile_snapshot();
    auto skip = [&](Skip s) { counters_.last_skip.store(static_cast<int>(s)); };
    if (!prof || prof->empty()) return skip(Skip::NoProfile);
    const core::Layout& L = *layout_;
    const uintptr_t client = report_.client.base;

    auto pawn = rd<uintptr_t>(client + L.local_pawn_rva);
    if (!pawn || !core::plausible_pointer(*pawn)) return skip(Skip::NoPawn);
    auto ctrl = rd<uintptr_t>(client + L.local_controller_rva);
    if (!ctrl || !core::plausible_pointer(*ctrl)) return skip(Skip::NoController);
    auto steam = rd<uint64_t>(*ctrl + L.steam_id);
    if (!steam || static_cast<uint32_t>(*steam) == 0) return skip(Skip::NoController);
    const uint32_t account_id = static_cast<uint32_t>(*steam);  // low 32 bits of SteamID64

    auto list = rd<uintptr_t>(client + L.entity_list_rva);
    if (!list || !core::plausible_pointer(*list)) return skip(Skip::NoEntityList);

    auto services = rd<uintptr_t>(*pawn + L.weapon_services);
    if (!services || !core::plausible_pointer(*services)) return skip(Skip::NoServices);
    auto count = rd<int32_t>(*services + L.my_weapons + L.vec_size_off);
    auto data = rd<uintptr_t>(*services + L.my_weapons + L.vec_data_off);
    if (!count || !data || *count < 0 || *count > 32 || (*count > 0 && !core::plausible_pointer(*data)))
        return skip(Skip::BadVector);

    skip(Skip::None);
    for (int i = 0; i < *count; ++i) {
        auto handle = rd<uint32_t>(*data + static_cast<uintptr_t>(i) * 4);
        if (!handle) continue;
        auto slot = core::split_handle(*handle, L);
        if (!slot) continue;

        auto chunk = rd<uintptr_t>(*list + 8ull * slot->chunk + L.chunk_base);
        if (!chunk || !core::plausible_pointer(*chunk)) continue;
        const uintptr_t identity = *chunk + static_cast<uintptr_t>(L.entity_stride) * slot->slot;
        auto weapon = rd<uintptr_t>(identity);  // identity -> entity pointer at +0
        if (!weapon || !core::plausible_pointer(*weapon)) continue;
        if (L.identity_handle_off) {
            auto stored = rd<uint32_t>(identity + *L.identity_handle_off);
            if (!stored || *stored != *handle) {
                counters_.stale_handles++;
                continue;
            }
        }
        counters_.weapons_seen++;

        const uintptr_t item = *weapon + L.attribute_manager + L.item;
        auto def = rd<uint16_t>(item + L.item_def_index);
        if (!def) continue;
        auto it = prof->find(*def);
        if (it == prof->end()) continue;
        const core::SkinEntry e = core::sanitize(it->second);
        if (!core::is_applicable(e)) continue;

        bool changed = false;
        bool ok = true;
        ok &= set_if_different<uint32_t>(item + L.item_id_high, 0xFFFFFFFFu, changed, counters_);
        if (L.item_id_low) ok &= set_if_different<uint32_t>(item + *L.item_id_low, 0xFFFFFFFFu, changed, counters_);
        ok &= set_if_different<uint32_t>(item + L.account_id, account_id, changed, counters_);
        ok &= set_if_different<uint32_t>(*weapon + L.fb_paint_kit, e.paint_kit, changed, counters_);
        ok &= set_if_different<uint32_t>(*weapon + L.fb_seed, e.seed, changed, counters_);
        ok &= set_if_different<float>(*weapon + L.fb_wear, e.wear, changed, counters_);
        ok &= set_if_different<int32_t>(*weapon + L.fb_stattrak, e.stattrak, changed, counters_);

        if (changed) {
            counters_.weapons_modified++;
            if (ok && regen_fn_ != 0 && !regen_failed_.load()) {
                if (call_regen(regen_fn_, *weapon))
                    counters_.regen_calls++;
                else {
                    regen_failed_.store(true);  // never call an unknown function again after it faulted
                    util::log("regenerate function faulted; disabled for this session");
                }
            }
        }
    }
}

}  // namespace game
