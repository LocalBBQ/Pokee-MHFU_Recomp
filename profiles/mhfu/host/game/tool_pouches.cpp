#include "game/tool_pouches.hpp"

#include "game/backpack.hpp"
#include "game/guest_layout.hpp"
#include "game/hooks.hpp"
#include "game/item_sets.hpp"
#include "game/pouch_page.hpp"
#include "game/pouch_slots.hpp"
#include "game/shop_stock.hpp"
#include "install/user_data.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <system_error>

namespace mhfu::game::tool_pouches {

const Upgrade *upgrade_of(std::uint16_t item) {
    for (const Upgrade &upgrade : kUpgrades)
        if (upgrade.item == item) return &upgrade;
    return nullptr;
}

const Upgrade *next_upgrade(Kind kind, std::uint8_t tier) {
    for (const Upgrade &upgrade : kUpgrades)
        if (upgrade.kind == kind && upgrade.tier == tier + 1u) return &upgrade;
    return nullptr;
}

bool owned(const Tiers &tiers, const Upgrade &upgrade) {
    return tiers[static_cast<std::size_t>(upgrade.kind)] >= upgrade.tier;
}

bool locked(const Tiers &tiers, const Upgrade &upgrade) {
    return tiers[static_cast<std::size_t>(upgrade.kind)] + 1u < upgrade.tier;
}

std::string shown_name(const Tiers &tiers, const Upgrade &upgrade) {
    return std::string(locked(tiers, upgrade) ? kLockedName : upgrade.name);
}

std::string shown_description(const Tiers &tiers, const Upgrade &upgrade) {
    if (!locked(tiers, upgrade)) return std::string(upgrade.description);
    const Upgrade *before = next_upgrade(upgrade.kind, static_cast<std::uint8_t>(upgrade.tier - 2u));
    return "Buy the " + std::string(before != nullptr ? before->name : std::string_view{"pouch before"}) +
        "\nfirst to unlock it.";
}

std::uint8_t carry(const Tool &tool, std::uint8_t tier) {
    tier = std::min(tier, kMostTier);
    if (tool.kind == Kind::Knife) return kKnifeCarry[tier];
    return static_cast<std::uint8_t>(tool.carry + tier);
}

std::array<std::uint8_t, kItemRecord> item_record(const Ram &ram, const Upgrade &upgrade) {
    std::array<std::uint8_t, kItemRecord> record{};
    const std::uint32_t from = kItemTable + kTemplateItem * kItemRecord;
    for (std::uint32_t i = 0; i < kItemRecord; ++i) record[i] = ram.load8(from + i);
    for (std::uint32_t i = 0; i < 4u; ++i) {
        record[kBuyPrice + i] = static_cast<std::uint8_t>(upgrade.price >> (8u * i));
        record[kSellPrice + i] = 0u;
    }
    return record;
}

bool game_limits(const Ram &ram) {
    for (const Tool &tool : kTools) {
        const std::uint32_t at = kItemTable + tool.item * kItemRecord + kCarryByte;
        if (!ram.contains(at, 1u) || ram.load8(at) != tool.carry) return false;
    }
    return true;
}

void keep_limits(Ram &ram, const Tiers &tiers, bool on) {
    for (const Tool &tool : kTools) {
        const std::uint32_t at = kItemTable + tool.item * kItemRecord + kCarryByte;
        const std::uint8_t wanted = on ? carry(tool, tiers[static_cast<std::size_t>(tool.kind)]) : tool.carry;
        if (ram.contains(at, 1u) && ram.load8(at) != wanted) ram.store8(at, wanted);
    }
}

bool grey_out(Ram &ram, std::uint32_t list, std::uint16_t item, bool greyed) {
    if (list == 0u || !ram.contains(list, kExchangeRow + kExchangeMostRows * kExchangeRowBytes)) return false;
    const std::uint32_t rows = ram.load32(list + kExchangeRows);
    if (rows > kExchangeMostRows) return false;
    for (std::uint32_t i = 0; i < rows; ++i) {
        const std::uint32_t row = list + kExchangeRow + i * kExchangeRowBytes;
        if (ram.load16(row + kExchangeRowItem) == item) ram.store16(row + kExchangeRowGreyed, greyed ? 1u : 0u);
    }
    return true;
}

std::vector<std::uint16_t> with_upgrades(std::vector<std::uint16_t> ids) {
    for (const Upgrade &upgrade : kUpgrades)
        if (std::find(ids.begin(), ids.end(), upgrade.item) == ids.end()) ids.push_back(upgrade.item);
    return ids;
}

void refund(Ram &ram, std::uint32_t game_sys, std::uint32_t count, const Upgrade &upgrade) {
    const std::uint32_t at = game_sys + kPoints;
    if (count == 0u || !ram.contains(at, 4u)) return;
    const auto now = static_cast<std::int32_t>(ram.load32(at));
    const std::int64_t points = std::int64_t{std::max(now, 0)} + std::int64_t{count} * upgrade.price;
    ram.store32(at, static_cast<std::uint32_t>(std::min<std::int64_t>(points, kMostPoints)));
}

void Store::load_file() {
    loaded_ = true;
    hunters_.clear();
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // The three tiers, then the name, which may hold spaces.
        std::istringstream fields(line);
        Tiers tiers{};
        bool good = true;
        for (std::uint8_t &tier : tiers) {
            unsigned value = 0u;
            if (!(fields >> value) || value > kMostTier) {
                good = false;
                break;
            }
            tier = static_cast<std::uint8_t>(value);
        }
        if (!good || fields.get() != ' ') continue;
        std::string name;
        std::getline(fields, name);
        if (name.empty()) continue;
        hunters_.emplace_back(name, tiers);
    }
}

Tiers Store::tiers(const std::string &hunter) {
    if (!loaded_) load_file();
    for (const auto &[name, tiers] : hunters_)
        if (name == hunter) return tiers;
    return {};
}

void Store::set(const std::string &hunter, const Tiers &tiers) {
    if (!loaded_) load_file();
    for (auto &[name, kept] : hunters_) {
        if (name != hunter) continue;
        kept = tiers;
        return;
    }
    hunters_.emplace_back(hunter, tiers);
}

bool Store::save() {
    if (!loaded_) load_file();
    std::error_code ec;
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
    const std::filesystem::path temporary = path_.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "# Tool pouches, written by Pokee when the game saves: the bugnet, pickaxe and knife pouches\n"
               "# each hunter owns (0 to 2), then the hunter's name.\n";
        for (const auto &[hunter, tiers] : hunters_)
            out << unsigned{tiers[0]} << ' ' << unsigned{tiers[1]} << ' ' << unsigned{tiers[2]} << ' ' << hunter
                << '\n';
        if (!out.flush()) return false;
    }
    std::filesystem::rename(temporary, path_, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

namespace {

struct State {
    bool started{};
    bool trace{};
    // The item table holds the game's limits; checked once, before any write.
    std::optional<bool> limits;
    std::string hunter;
    Tiers tiers{};
    bool reload{};
    // The exchange lists the upgrades (stock).
    bool listed{};
    // How many of each upgrade the hunter had, to tell a purchase.
    std::array<std::optional<std::uint32_t>, kUpgrades.size()> had{};
    // The store the list last asked about an upgrade, to grey it out once bought.
    std::uint32_t store{};
    // Traced checks, by caller and check.
    std::set<std::uint64_t> asked;
    std::array<std::optional<std::uint32_t>, kUpgrades.size()> names{}, descriptions{};
    psprecomp::Runtime::RecompiledFunction guide_check{};
    psprecomp::Runtime::RecompiledFunction obtained_check{};
    psprecomp::Runtime::RecompiledFunction pouch_count{};
};

State &state() {
    static State s;
    return s;
}

Store &store() {
    static Store tiers(install::user_data_directory() / "tool_pouches.txt");
    return tiers;
}

bool on() {
    return settings::current().tool_pouches;
}

void answer(psprecomp::AllegrexContext &ctx, std::uint32_t value) {
    ctx.gpr[2] = value;
    ctx.pc = ctx.gpr[31];
}

const Upgrade *active_upgrade(std::uint32_t item) {
    return state().listed ? upgrade_of(static_cast<std::uint16_t>(item & 0xFFFFu)) : nullptr;
}

// Traced: each check the game makes about an upgrade, once per caller.
void asked(const char *check, const psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (!s.trace || upgrade_of(static_cast<std::uint16_t>(ctx.gpr[5] & 0xFFFFu)) == nullptr) return;
    if (!s.asked.insert((std::uint64_t{ctx.gpr[31]} << 8u) | static_cast<std::uint8_t>(check[0])).second) return;
    std::cout << "[tool pouches] " << check << " asked about " << (ctx.gpr[5] & 0xFFFFu) << " from 0x" << std::hex
              << ctx.gpr[31] << " (a0 0x" << ctx.gpr[4] << ")" << std::dec << std::endl;
}

// 0x09A80230(store, item): a Guide, for the list, which greys an obtained one.
void guide_check(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    asked("guide check", ctx);
    if (active_upgrade(ctx.gpr[5]) != nullptr && ctx.gpr[31] == kExchangeListGuideReturn) {
        State &s = state();
        const std::uint32_t list = ctx.gpr[kExchangeListRegister];
        if (s.trace && s.store != list)
            std::cout << "[tool pouches] exchange list at 0x" << std::hex << list << std::dec << std::endl;
        s.store = list;
        answer(ctx, 1u);
        return;
    }
    ctx.pc = backpack::kGuideCheck;
    state().guide_check(runtime, ctx);
}

// 0x0885C5CC(?, item): obtained, once the hunter owns the upgrade; a locked
// one too, so the list greys it out.
void obtained_check(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    asked("obtained check", ctx);
    if (const Upgrade *upgrade = active_upgrade(ctx.gpr[5]); upgrade != nullptr) {
        const Tiers &tiers = state().tiers;
        answer(ctx, owned(tiers, *upgrade) || locked(tiers, *upgrade) ? 1u : 0u);
        return;
    }
    ctx.pc = backpack::kObtainedCheck;
    state().obtained_check(runtime, ctx);
}

// 0x0885AED0(?, item): the pouch's count, in the store's pane one once owned.
void pouch_count(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    asked("pouch count", ctx);
    const Upgrade *upgrade = active_upgrade(ctx.gpr[5]);
    if (upgrade != nullptr && ctx.gpr[31] == kExchangePaneCountReturn && owned(state().tiers, *upgrade)) {
        answer(ctx, 1u);
        return;
    }
    ctx.pc = backpack::kPouchCount;
    state().pouch_count(runtime, ctx);
}

void wrap(psprecomp::Runtime &runtime, std::uint32_t address, psprecomp::Runtime::RecompiledFunction wrapper,
    psprecomp::Runtime::RecompiledFunction &next, const char *name) {
    if (runtime.function_at(address) != nullptr && runtime.function_at(address) != wrapper)
        (void)hooks::wrap(runtime, address, wrapper, next, name);
}

// Writes `text` at `address` unless it is there already.
void keep_text(Ram &ram, std::uint32_t address, const std::string &text) {
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const auto wanted = static_cast<std::uint8_t>(i < text.size() ? text[i] : '\0');
        if (ram.load8(address + static_cast<std::uint32_t>(i)) != wanted) {
            backpack::write_text(ram, address, text);
            return;
        }
    }
}

// The upgrades' records and texts, as the hunter's tiers show them.
void write_texts(Ram &ram) {
    State &s = state();
    for (std::size_t i = 0; i < kUpgrades.size(); ++i) {
        const auto at = static_cast<std::uint32_t>(i);
        keep_text(ram, kNames + at * kNameBytes, shown_name(s.tiers, kUpgrades[i]));
        keep_text(ram, kDescriptions + at * kDescriptionBytes, shown_description(s.tiers, kUpgrades[i]));
    }
    for (std::size_t i = 0; i < kUpgrades.size(); ++i) {
        const auto at = static_cast<std::uint32_t>(i);
        const Upgrade &upgrade = kUpgrades[i];
        const std::uint32_t to = kItemTable + upgrade.item * kItemRecord;
        if (ram.contains(to, kItemRecord)) {
            const auto record = item_record(ram, upgrade);
            for (std::uint32_t b = 0; b < kItemRecord; ++b)
                if (ram.load8(to + b) != record[b]) ram.store8(to + b, record[b]);
        }
        backpack::point_entry(
            ram, backpack::kTextBlock, backpack::kNameTable, upgrade.item, kNames + at * kNameBytes, s.names[i]);
        backpack::point_entry(ram, backpack::kTextBlock, backpack::kDescriptionTable, upgrade.item,
            kDescriptions + at * kDescriptionBytes, s.descriptions[i]);
    }
}

// The hunter's Pokke Points cover the upgrade.
bool affordable(const Ram &ram, std::uint32_t sys, const Upgrade &upgrade) {
    const std::uint32_t at = sys + kPoints;
    return ram.contains(at, 4u) &&
        std::int64_t{static_cast<std::int32_t>(ram.load32(at))} >= std::int64_t{upgrade.price};
}

// The upgrades bought since the last frame: each taken back, the hunter's
// tier raised, its row greyed out, and any beyond the first paid back.
void buy(Ram &ram, std::uint32_t sys, bool lobby) {
    State &s = state();
    const backpack::Places places{sys + backpack::kBox, pouch_slots::first(sys),
        pouch_page::active() ? pouch_page::kSaved : sys + backpack::kSavedPouch, pouch_slots::count()};
    for (std::size_t i = 0; i < kUpgrades.size(); ++i) {
        const Upgrade &upgrade = kUpgrades[i];
        const std::uint32_t now = backpack::held(ram, places, upgrade.item);
        if (lobby && s.had[i] && now > *s.had[i]) {
            const std::uint32_t bought = now - *s.had[i];
            backpack::take(ram, places, bought, upgrade.item);
            std::uint8_t &tier = s.tiers[static_cast<std::size_t>(upgrade.kind)];
            // Only the next upgrade is sold; another is paid back whole.
            const bool next = tier + 1u == upgrade.tier;
            if (next) tier = upgrade.tier;
            refund(ram, sys, next ? bought - 1u : bought, upgrade);
            const bool greyed = grey_out(ram, s.store, upgrade.item);
            // The +1 it unlocks, buyable in the open store when the points cover it.
            if (const Upgrade *after = next_upgrade(upgrade.kind, tier); next && after != nullptr && greyed)
                (void)grey_out(ram, s.store, after->item, !affordable(ram, sys, *after));
            if (s.trace) {
                std::cout << "[tool pouches] " << upgrade.name << (next ? " bought" : " paid back") << " (" << bought
                          << ")";
                if (!greyed)
                    std::cout << "; no store list to grey it out in (store 0x" << std::hex << s.store << std::dec
                              << ")";
                std::cout << std::endl;
            }
        }
        s.had[i] = backpack::held(ram, places, upgrade.item);
    }
}

} // namespace

std::vector<std::uint16_t> stock(std::uint32_t slot, std::vector<std::uint16_t> ids) {
    const State &s = state();
    return s.listed && shop_stock::exchange(slot) ? with_upgrades(std::move(ids)) : ids;
}

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    if (!s.started) {
        s.started = true;
        const char *text = std::getenv("MHFU_TRACE_TOOL_POUCHES");
        s.trace = text != nullptr && *text != '\0' && std::string(text) != "0";
    }
    s.listed = false;
    GuestRam ram(runtime.memory());
    if (!s.limits) {
        if (!ram.contains(kItemTable + kTools[0].item * kItemRecord, kItemRecord)) return;
        s.limits = game_limits(ram);
        if (!*s.limits) std::cout << "[tool pouches] the item table is not the game's; off" << std::endl;
    }
    if (!*s.limits) return;

    const std::uint32_t sys = item_sets::game_sys(ram);
    const std::string hunter = sys != 0u ? item_sets::hunter_name(ram, sys) : std::string{};
    if (hunter.empty()) {
        s.hunter.clear();
        keep_limits(ram, {}, false);
        return;
    }
    if (hunter != s.hunter || s.reload) {
        s.hunter = hunter;
        s.reload = false;
        s.tiers = store().tiers(hunter);
        s.had = {};
        if (s.trace)
            std::cout << "[tool pouches] hunter " << hunter << ": bugnet " << unsigned{s.tiers[0]} << ", pickaxe "
                      << unsigned{s.tiers[1]} << ", knife " << unsigned{s.tiers[2]} << std::endl;
    }
    if (!on()) {
        keep_limits(ram, s.tiers, false);
        s.had = {};
        return;
    }
    s.listed = true;
    wrap(runtime, backpack::kObtainedCheck, &obtained_check, s.obtained_check, "mhfu_tool_pouches_obtained");
    wrap(runtime, backpack::kPouchCount, &pouch_count, s.pouch_count, "mhfu_tool_pouches_pouch_count");
    const bool lobby = layout::overlay_loaded(runtime.memory(), layout::kTaskSlot, "lobby_task.ovl");
    if (lobby) wrap(runtime, backpack::kGuideCheck, &guide_check, s.guide_check, "mhfu_tool_pouches_guide");
    buy(ram, sys, lobby);
    write_texts(ram);
    keep_limits(ram, s.tiers, true);
}

void game_saved() {
    State &s = state();
    if (s.hunter.empty()) return;
    store().set(s.hunter, s.tiers);
    if (!store().save()) std::cerr << "[tool pouches] could not write tool_pouches.txt" << std::endl;
}

void game_loaded() {
    state().reload = true;
}

Tiers current() {
    return state().tiers;
}

} // namespace mhfu::game::tool_pouches
