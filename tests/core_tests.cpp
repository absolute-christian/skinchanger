// Portable checks for src/core. Build/run on any OS:
//   g++ -std=c++20 -Wall -Wextra -Isrc tests/core_tests.cpp -o core_tests && ./core_tests
// All numbers in the fixtures below are SYNTHETIC test values, not real CS2 data.
#include <cmath>
#include <cstdio>
#include <string>

#include "core/config.hpp"
#include "core/ini.hpp"
#include "core/pattern.hpp"
#include "core/profile.hpp"
#include "core/skin_db.hpp"

static int g_fail = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

static const char* kFullIni = R"(
[build]
client_time_date_stamp = 0x11111111
client_size_of_image = 0x2000000
[globals]
dwEntityList = 0x100000
dwLocalPlayerPawn = 0x200000
dwLocalPlayerController = 0x300000
[entity_system]
handle_index_mask = 0x7FFF
handle_chunk_shift = 9
handle_chunk_mask = 0x1FF
entlist_chunk_base = 0x10
entity_stride = 0x78
identity_handle_off = 0x10
utl_vector_size_off = 0
utl_vector_data_off = 8
[fields]
m_pWeaponServices = 0x100
m_hMyWeapons = 0x40
m_AttributeManager = 0x200
m_Item = 0x50
m_iItemDefinitionIndex = 0x44
m_iItemIDHigh = 0x48
m_iAccountID = 0x4C
m_nFallbackPaintKit = 0x300
m_nFallbackSeed = 0x304
m_flFallbackWear = 0x308
m_nFallbackStatTrak = 0x30C
m_steamID = 0x400
)";

static void test_numbers() {
    CHECK(core::parse_u64("0x10") == 16u);
    CHECK(core::parse_u64(" 42 ") == 42u);
    CHECK(!core::parse_u64(""));
    CHECK(!core::parse_u64("0x"));
    CHECK(!core::parse_u64("12abc"));
    CHECK(!core::parse_u64("-1"));
    CHECK(core::parse_i64("-1") == -1);
    CHECK(core::parse_f32("0.25") == 0.25f);
    CHECK(!core::parse_f32("nan"));
    CHECK(!core::parse_f32("inf"));
    CHECK(!core::parse_f32("1.0x"));
}

static void test_ini() {
    auto ini = core::Ini::parse("; c\n[a]\nk = 5 ; tail\nempty =\n[b]\nk=7\n");
    CHECK(ini.get("a", "k") == "5");
    CHECK(!ini.get("a", "empty"));  // empty == unset
    CHECK(!ini.get("a", "missing"));
    CHECK(ini.get("b", "k") == "7");
    CHECK(ini.get_any("empty") == std::nullopt);
}

static void test_pattern() {
    CHECK(core::parse_pattern("48 8B ?? 05 11"));
    CHECK(!core::parse_pattern("48 8B"));         // too few concrete bytes
    CHECK(!core::parse_pattern("48 8B ?? 0"));    // bad token
    CHECK(!core::parse_pattern("GG 11 22 33"));
    const uint8_t data[] = {0, 1, 0x48, 0x8B, 0x99, 0x05, 0x11, 0x22, 0x48, 0x8B, 0x00, 0x05, 0x11};
    auto p = *core::parse_pattern("48 8B ?? 05 11");
    CHECK(core::find_pattern(data, sizeof data, p) == 2u);
    CHECK(core::find_pattern(data, sizeof data, p, 3) == 8u);
    CHECK(!core::find_pattern(data, sizeof data, p, 9));
    // pattern ending exactly at the buffer end must still match
    CHECK(core::find_pattern(data + 8, 5, p) == 0u);
    CHECK(!core::find_pattern(data + 8, 4, p));
}

static void test_config() {
    auto empty = core::Config::from_ini(core::Ini::parse("[x]\ndwEntityList =\n"));
    CHECK(!empty.get("dwEntityList"));
    CHECK(!empty.missing_required().empty());

    auto full = core::Config::from_ini(core::Ini::parse(kFullIni));
    CHECK(full.missing_required().empty());
    CHECK(full.bad_values.empty());
    std::string why;
    auto lay = core::Layout::from(full, &why);
    CHECK(lay.has_value());
    if (lay) {
        CHECK(lay->fb_wear == 0x308);
        CHECK(!lay->item_id_low.has_value());
        CHECK(lay->identity_handle_off == 0x10u);
    }

    // a real zero must not read as unset
    auto zero = core::Config::from_ini(core::Ini::parse("utl_vector_size_off = 0\n"));
    CHECK(zero.get("utl_vector_size_off") == 0u);

    // inconsistent chunk mask must be rejected
    std::string bad = std::string(kFullIni) + "\n[more]\nhandle_chunk_mask = 0xFF\n";
    auto cfg_bad = core::Config::from_ini(core::Ini::parse(bad));
    // later duplicate does not override the earlier non-empty hit, so patch directly:
    cfg_bad.num["handle_chunk_mask"] = 0xFF;
    CHECK(!core::Layout::from(cfg_bad, &why).has_value());

    // unparsable value is reported
    auto junk = core::Config::from_ini(core::Ini::parse("dwEntityList = zzz\n"));
    CHECK(junk.bad_values.size() == 1);
}

static void test_handles() {
    auto lay = *core::Layout::from(core::Config::from_ini(core::Ini::parse(kFullIni)));
    CHECK(!core::split_handle(0xFFFFFFFFu, lay));
    auto s = core::split_handle(0x00000205u, lay);  // index 0x205 -> chunk 1, slot 5
    CHECK(s && s->chunk == 1 && s->slot == 5);
    auto s2 = core::split_handle(0xABCD8000u | 0x7FFF, lay);  // serial bits are ignored
    CHECK(s2 && s2->chunk == 63 && s2->slot == 0x1FF);
    CHECK(core::plausible_pointer(0x00007FF612340000ull));
    CHECK(!core::plausible_pointer(0));
    CHECK(!core::plausible_pointer(0x1234));
    CHECK(!core::plausible_pointer(0x00007FF612340003ull));
    CHECK(!core::plausible_pointer(0xFFFF800000000000ull));
}

static void test_skin_db() {
    auto db = core::parse_skin_db(
        "# header\nW\t7\tweapon_ak47\tAK-47\nP\t44\tcu_ak47_cobra\tCobra\n"
        "W\t0\tbad\tbad\nW\tx\tbad\tbad\nQ\t1\ta\tb\nW\t9\tshort\n");
    CHECK(db.weapons.size() == 1 && db.weapons[0].def_index == 7);
    CHECK(db.kits.size() == 1 && db.kits[0].label == "Cobra");
    CHECK(db.errors.size() == 4);
}

static void test_profile() {
    core::Profile p;
    p[7] = {true, 44, 123, 0.07f, 500};
    p[9] = {true, 1, 5000, std::nanf(""), -50};
    auto text = core::serialize_profile(p);
    auto q = core::parse_profile(text);
    CHECK(q.size() == 2);
    CHECK(q[7].paint_kit == 44 && q[7].seed == 123 && q[7].stattrak == 500);
    CHECK(std::fabs(q[7].wear - 0.07f) < 1e-6f);
    CHECK(q[9].seed == 1000 && q[9].wear == 0.0f && q[9].stattrak == -1);
    CHECK(core::is_applicable(q[7]));
    core::SkinEntry none;
    none.enabled = true;  // paint kit 0
    CHECK(!core::is_applicable(none));
    auto junk = core::parse_profile("[weapon.0]\nenabled=1\n[weapon.70000]\n[other]\nx=1\n");
    CHECK(junk.empty());
}

int main() {
    test_numbers();
    test_ini();
    test_pattern();
    test_config();
    test_handles();
    test_skin_db();
    test_profile();
    if (g_fail == 0) std::printf("all core tests passed\n");
    return g_fail == 0 ? 0 : 1;
}
