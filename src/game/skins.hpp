#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/profile.hpp"
#include "game/memory.hpp"

namespace game {

// Filled once by SkinChanger::initialize() before any hook exists; read-only afterwards.
struct Report {
    bool config_file_found = false;
    std::vector<std::string> missing_keys;
    std::vector<std::string> bad_values;
    std::string layout_error;
    bool layout_ok = false;

    bool client_found = false;
    Module client{};
    bool build_ok = false;
    std::string build_detail;

    bool fsn_sig_set = false;
    bool fsn_resolved = false;
    uintptr_t fsn_address = 0;
    std::string fsn_detail;

    bool regen_sig_set = false;
    bool regen_resolved = false;
    uintptr_t regen_address = 0;
    std::string regen_detail;

    std::string notes;  // free-form warnings (e.g. stale-handle check disabled)

    bool apply_ready() const { return layout_ok && build_ok; }
};

enum class Skip : int { None, NoProfile, NoPawn, NoController, NoServices, BadVector, NoEntityList };

struct Counters {
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> weapons_seen{0};
    std::atomic<uint64_t> weapons_modified{0};
    std::atomic<uint64_t> write_failures{0};
    std::atomic<uint64_t> stale_handles{0};
    std::atomic<uint64_t> regen_calls{0};
    std::atomic<int> last_skip{0};
};

class SkinChanger {
public:
    void initialize(const std::filesystem::path& offsets_ini);

    const Report& report() const { return report_; }
    Counters& counters() { return counters_; }

    // FrameStageNotify can be hooked only when every gate is satisfied.
    bool fsn_hookable() const { return report_.fsn_resolved && fsn_stage_.has_value() && fsn_confirmed_; }
    uintptr_t fsn_address() const { return report_.fsn_address; }

    void set_profile(std::shared_ptr<const core::Profile> p);
    void set_enabled(bool on) { enabled_.store(on); }
    bool enabled() const { return enabled_.load(); }
    bool regen_active() const { return regen_fn_ != 0 && !regen_failed_.load(); }

    // Called from the FrameStageNotify detour, on the game thread.
    void on_frame_stage(int stage, bool after_original);

private:
    void apply();
    std::shared_ptr<const core::Profile> profile_snapshot();

    Report report_;
    Counters counters_;
    std::optional<core::Layout> layout_;
    std::optional<int> fsn_stage_;
    bool fsn_confirmed_ = false;
    bool apply_after_original_ = false;
    uintptr_t regen_fn_ = 0;  // nonzero only if signature resolved AND prototype confirmed
    std::atomic<bool> regen_failed_{false};
    std::atomic<bool> enabled_{true};

    std::mutex profile_mutex_;
    std::shared_ptr<const core::Profile> profile_;
};

}  // namespace game
