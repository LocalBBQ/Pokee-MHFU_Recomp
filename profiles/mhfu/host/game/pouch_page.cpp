#include "game/pouch_page.hpp"

#include "game/guest_layout.hpp"
#include "game/guest_ram.hpp"
#include "install/user_data.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace mhfu::game::pouch_page {
namespace {

// config/site_hooks.txt, as the build embeds it.
constexpr std::string_view kSiteList =
#include "site_hooks.inc"
    ;

constexpr std::array<std::string_view, 32> kRegisters{"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1",
    "t2", "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp",
    "sp", "fp", "ra"};

std::optional<std::uint8_t> register_number(std::string_view name) {
    for (std::size_t i = 0; i < kRegisters.size(); ++i)
        if (kRegisters[i] == name) return static_cast<std::uint8_t>(i);
    return std::nullopt;
}

std::optional<std::int64_t> parse_number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    const std::string copy(text);
    char *end = nullptr;
    const long long value = std::strtoll(copy.c_str(), &end, 0);
    if (end != copy.c_str() + copy.size()) return std::nullopt;
    return value;
}

// "32", "0x80", "C", "C-0x3F28", "W1-0x20".
std::optional<Value> parse_value(std::string_view text) {
    struct Name {
        std::string_view name;
        Base base;
    };
    // Longer names first, so "W1" is not taken for another block.
    constexpr std::array<Name, 7> kNames{{{"W1", Base::List1}, {"W2", Base::List2}, {"W3", Base::List3},
        {"P", Base::Pouch}, {"C", Base::Saved}, {"B", Base::Aside}, {"S", Base::HunterCopy}}};
    for (const Name &name : kNames) {
        if (text.substr(0, name.name.size()) != name.name) continue;
        const std::string_view rest = text.substr(name.name.size());
        if (rest.empty()) return Value{name.base, 0};
        if (rest[0] != '+' && rest[0] != '-') return std::nullopt;
        const auto offset = parse_number(rest.substr(1));
        if (!offset) return std::nullopt;
        return Value{name.base, rest[0] == '-' ? -*offset : *offset};
    }
    const auto number = parse_number(text);
    if (!number) return std::nullopt;
    return Value{Base::Number, *number};
}

std::string slots_text(const PageSlots &slots) {
    std::string text;
    for (const Slot &slot : slots) text += " " + std::to_string(slot.item) + ":" + std::to_string(slot.count);
    return text;
}

PageSlots parse_slots(std::istringstream &fields) {
    PageSlots slots{};
    std::string pair;
    for (std::size_t i = 0; i < kPageSlots && fields >> pair; ++i) {
        const std::size_t colon = pair.find(':');
        if (colon == std::string::npos) continue;
        const unsigned long item = std::strtoul(pair.substr(0, colon).c_str(), nullptr, 10);
        const unsigned long count = std::strtoul(pair.substr(colon + 1u).c_str(), nullptr, 10);
        if (item > 0xFFFFu || count > 0xFFFFu) continue;
        slots[i] = {static_cast<std::uint16_t>(item), static_cast<std::uint16_t>(count)};
    }
    return slots;
}

constexpr std::array<std::string_view, kArrays.size()> kArrayNames{"pouch", "saved", "aside"};

bool env_on(const char *name, bool fallback) {
    const char *text = std::getenv(name);
    if (text == nullptr || *text == '\0') return fallback;
    return std::string_view(text) != "0";
}

} // namespace

std::uint32_t value_of(const Value &value) noexcept {
    std::uint32_t base = 0u;
    switch (value.base) {
    case Base::Number:
        break;
    case Base::Pouch:
        base = kPouch;
        break;
    case Base::Saved:
        base = kSaved;
        break;
    case Base::Aside:
        base = kAside;
        break;
    case Base::HunterCopy:
        base = kHunterCopy;
        break;
    case Base::List1:
        base = kList1;
        break;
    case Base::List2:
        base = kList2;
        break;
    case Base::List3:
        base = kList3;
        break;
    }
    return base + static_cast<std::uint32_t>(value.offset);
}

std::optional<std::vector<Site>> parse_sites(std::string_view text, std::string *error) {
    std::vector<Site> sites;
    std::istringstream in{std::string(text)};
    std::string line;
    const auto fail = [&](std::size_t number) -> std::optional<std::vector<Site>> {
        if (error != nullptr) *error = "line " + std::to_string(number) + ": " + line;
        return std::nullopt;
    };
    for (std::size_t number = 1u; std::getline(in, line); ++number) {
        std::string content = line;
        if (const auto comment = content.find('#'); comment != std::string::npos) content.erase(comment);
        std::istringstream fields(content);
        Site site;
        std::string address, word, site_number, action;
        if (!(fields >> site.image)) continue;
        if (!(fields >> address >> word >> site_number >> action)) return fail(number);
        const auto parsed_address = parse_number(address);
        const auto parsed_word = parse_number(word);
        const auto parsed_number = parse_number(site_number);
        if (!parsed_address || !parsed_word || !parsed_number || *parsed_number <= 0) return fail(number);
        site.address = static_cast<std::uint32_t>(*parsed_address);
        site.word = static_cast<std::uint32_t>(*parsed_word);
        site.number = static_cast<std::uint32_t>(*parsed_number);
        std::vector<std::string> args;
        for (std::string arg; fields >> arg;) args.push_back(arg);
        const auto value_at = [&](std::size_t i) { return i < args.size() ? parse_value(args[i]) : std::nullopt; };
        const auto register_at = [&](std::size_t i) -> std::optional<std::uint8_t> {
            if (i >= args.size()) return std::nullopt;
            if (args[i] == "gamesys") return kGameSysOperand;
            return register_number(args[i]);
        };
        if (action == "bound" || action == "set") {
            const auto value = value_at(0);
            if (!value || args.size() != 1u) return fail(number);
            site.action = action == "bound" ? Action::Bound : Action::Set;
            site.value = *value;
        } else if (action == "setreg" || action == "rebase") {
            const auto reg = register_at(0);
            const auto value = value_at(1);
            if (!reg || !value || args.size() != 2u) return fail(number);
            if (action == "setreg" && *reg == kGameSysOperand) return fail(number);
            site.action = action == "setreg" ? Action::SetReg : Action::Rebase;
            site.reg = *reg;
            site.value = *value;
        } else if (action == "direct" || action == "pouch_slot") {
            if (!args.empty()) return fail(number);
            site.action = action == "direct" ? Action::Direct : Action::PouchSlot;
        } else if (action == "cap_base") {
            const auto reg = register_at(0);
            if (!reg || *reg == kGameSysOperand || args.size() != 1u) return fail(number);
            site.action = Action::CapBase;
            site.reg = *reg;
        } else if (action == "cap_check") {
            const auto reg = register_at(0);
            const auto limit = args.size() == 2u ? parse_number(args[1]) : std::nullopt;
            if (!reg || *reg == kGameSysOperand || !limit || *limit <= 0) return fail(number);
            site.action = Action::CapCheck;
            site.reg = *reg;
            site.limit = static_cast<std::uint32_t>(*limit);
        } else {
            return fail(number);
        }
        sites.push_back(std::move(site));
    }
    return sites;
}

const std::vector<Site> &sites() {
    static const std::vector<Site> list = [] {
        std::string error;
        auto parsed = parse_sites(kSiteList, &error);
        if (!parsed) {
            std::cerr << "[pouch page] site list: " << error << std::endl;
            return std::vector<Site>{};
        }
        return std::move(*parsed);
    }();
    return list;
}

std::uint8_t destination(std::uint32_t word) noexcept {
    return static_cast<std::uint8_t>((word >> 26u) == 0u ? (word >> 11u) & 31u : (word >> 16u) & 31u);
}

std::uint32_t slot_address(std::uint32_t game_sys, std::uint32_t index) noexcept {
    if (index < kSlots) return kPouch + index * kSlotBytes;
    if (index == kBowSlot) return game_sys + kGameBowSlot;
    return 0u;
}

namespace {

// The hooks and the other features' view of the pouch go on and off together.
void set_hooks(psprecomp::Runtime &runtime, bool on) {
    runtime.set_site_hooks_active(on);
    pouch_slots::fourth_page() = on;
}

} // namespace

void apply(const Site &site, psprecomp::AllegrexContext &ctx, std::uint32_t game_sys, CapState &cap) {
    const std::uint8_t dest = destination(site.word);
    const auto set = [&](std::uint8_t reg, std::uint32_t value) {
        if (reg != 0u) ctx.gpr[reg] = value;
    };
    // An address of slot `index` computed as `base` + 4 * index + `bias`.
    const auto slot_from = [&](std::uint32_t address, std::uint32_t base, std::uint32_t bias) -> std::uint32_t {
        const std::uint32_t bytes = address - base - bias;
        if (bytes % kSlotBytes != 0u) return 0u;
        return slot_address(base, bytes / kSlotBytes);
    };
    switch (site.action) {
    case Action::Bound: {
        const std::uint32_t source = ctx.gpr[(site.word >> 21u) & 31u];
        const std::uint32_t limit = value_of(site.value);
        const bool is_signed = (site.word >> 26u) == 0x0Au; // slti; sltiu otherwise
        const bool below = is_signed ? static_cast<std::int32_t>(source) < static_cast<std::int32_t>(limit) : source < limit;
        set(dest, below ? 1u : 0u);
        break;
    }
    case Action::Set:
        set(dest, value_of(site.value));
        break;
    case Action::SetReg:
        set(site.reg, value_of(site.value));
        break;
    case Action::Rebase: {
        const std::uint32_t from = site.reg == kGameSysOperand ? game_sys : ctx.gpr[site.reg];
        set(dest, ctx.gpr[dest] - from + value_of(site.value));
        break;
    }
    case Action::Direct:
        if (const std::uint32_t slot = slot_from(ctx.gpr[dest], game_sys, kDirectBias); slot != 0u)
            set(dest, slot + kDirectOffset);
        break;
    case Action::PouchSlot:
        // 0x08856884(GameSys, i): the result is a0 + 4i + 0x6AEA6.
        if (const std::uint32_t slot = slot_from(ctx.gpr[dest], ctx.gpr[4], kGamePouch); slot != 0u) set(dest, slot);
        break;
    case Action::CapBase:
        cap.base = ctx.gpr[site.reg];
        break;
    case Action::CapCheck:
        if (ctx.gpr[site.reg] - cap.base >= site.limit) set(dest, 0u);
        break;
    }
}

Mirror fill_blocks(Ram &ram, std::uint32_t game_sys, const HunterPage &page) {
    Mirror mirror{};
    for (std::size_t a = 0; a < kArrays.size(); ++a) {
        for (std::uint32_t i = 0; i < kGameSlots; ++i) {
            const std::uint32_t word = ram.load32(game_sys + kArrays[a].game + i * kSlotBytes);
            ram.store32(kArrays[a].block + i * kSlotBytes, word);
            mirror[a][i] = word;
        }
        for (std::uint32_t i = 0; i < kPageSlots; ++i) {
            const Slot &slot = page.slots[a][i];
            ram.store32(kArrays[a].block + (kGameSlots + i) * kSlotBytes,
                static_cast<std::uint32_t>(slot.item) | (static_cast<std::uint32_t>(slot.count) << 16u));
        }
    }
    for (std::uint32_t at = kHunterCopy; at < kBlocksEnd; at += 4u) ram.store32(at, 0u);
    return mirror;
}

void merge(Ram &ram, std::uint32_t game_sys, Mirror &mirror) {
    for (std::size_t a = 0; a < kArrays.size(); ++a) {
        for (std::uint32_t i = 0; i < kGameSlots; ++i) {
            const std::uint32_t game_at = game_sys + kArrays[a].game + i * kSlotBytes;
            const std::uint32_t block_at = kArrays[a].block + i * kSlotBytes;
            const std::uint32_t game = ram.load32(game_at);
            const std::uint32_t block = ram.load32(block_at);
            if (game != mirror[a][i]) {
                ram.store32(block_at, game);
                mirror[a][i] = game;
            } else if (block != game) {
                ram.store32(game_at, block);
                mirror[a][i] = block;
            }
        }
    }
}

HunterPage page_of(const Ram &ram) {
    HunterPage page;
    page.owned = true;
    for (std::size_t a = 0; a < kArrays.size(); ++a) {
        for (std::uint32_t i = 0; i < kPageSlots; ++i) {
            const std::uint32_t at = kArrays[a].block + (kGameSlots + i) * kSlotBytes;
            page.slots[a][i] = {ram.load16(at), ram.load16(at + 2u)};
        }
    }
    return page;
}

void Store::load_file() {
    loaded_ = true;
    hunters_.clear();
    std::ifstream in(path_);
    std::string line;
    HunterPage *page = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("hunter ", 0) == 0) {
            hunters_.emplace_back(line.substr(7), HunterPage{});
            page = &hunters_.back().second;
            continue;
        }
        if (page == nullptr) continue;
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key)) continue;
        if (key == "owned") {
            page->owned = true;
            continue;
        }
        for (std::size_t a = 0; a < kArrayNames.size(); ++a)
            if (key == kArrayNames[a]) page->slots[a] = parse_slots(fields);
    }
}

HunterPage Store::page(const std::string &hunter) {
    if (!loaded_) load_file();
    for (const auto &[name, page] : hunters_)
        if (name == hunter) return page;
    return {};
}

void Store::set(const std::string &hunter, const HunterPage &page) {
    if (!loaded_) load_file();
    for (auto &[name, kept] : hunters_) {
        if (name != hunter) continue;
        kept = page;
        return;
    }
    hunters_.emplace_back(hunter, page);
}

bool Store::save() {
    if (!loaded_) load_file();
    std::error_code ec;
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
    const std::filesystem::path temporary = path_.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "# The fourth pouch page, written by Pokee when the game saves: one block per hunter, by name.\n";
        for (const auto &[hunter, page] : hunters_) {
            out << "hunter " << hunter << "\n";
            if (page.owned) out << "owned\n";
            for (std::size_t a = 0; a < kArrayNames.size(); ++a)
                out << kArrayNames[a] << slots_text(page.slots[a]) << "\n";
        }
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
    bool enabled{true};
    bool grant_all{};
    bool trace{};
    // Sites by number.
    std::vector<const Site *> by_number;
    std::vector<bool> traced;
    bool reported_mismatch{};
    std::string hunter;
    HunterPage page;
    bool active{};
    bool reload{};
    Mirror mirror{};
    CapState cap;
    // The overlays the list names, and the one whose sites the interpreter
    // has, per slot.
    std::vector<std::string> images;
    std::string task_image;
    std::string em_image;
};

State &state() {
    static State s;
    return s;
}

Store &store() {
    static Store pages(install::user_data_directory() / "pouch_page.txt");
    return pages;
}

void on_site(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx, std::uint32_t number, std::uint32_t address) {
    State &s = state();
    const Site *site = number < s.by_number.size() ? s.by_number[number] : nullptr;
    if (site == nullptr || site->address != address) {
        // A corpus generated from another list: leave the instruction as it ran.
        if (!s.reported_mismatch) {
            s.reported_mismatch = true;
            std::cerr << "[pouch page] site " << number << " at 0x" << std::hex << address << std::dec
                      << " is not in this build's list; regenerate the corpus" << std::endl;
        }
        return;
    }
    if (s.trace && !s.traced[number]) {
        s.traced[number] = true;
        std::cout << "[pouch page] site " << number << " " << site->image << " 0x" << std::hex << address << std::dec
                  << std::endl;
    }
    const auto &memory = runtime.memory();
    const std::uint32_t game_sys = memory.contains(kGameSysPointer, 4u) ? memory.load32(kGameSysPointer) : 0u;
    apply(*site, ctx, game_sys, s.cap);
}

void start(psprecomp::Runtime &runtime) {
    State &s = state();
    s.started = true;
    s.enabled = env_on("MHFU_POUCH_PAGE", true);
    s.grant_all = env_on("MHFU_POUCH_PAGE_GRANT", false);
    s.trace = env_on("MHFU_TRACE_POUCH_PAGE", false);
    std::uint32_t highest = 0u;
    for (const Site &site : sites()) highest = std::max(highest, site.number);
    s.by_number.assign(highest + 1u, nullptr);
    s.traced.assign(highest + 1u, false);
    for (const Site &site : sites()) {
        s.by_number[site.number] = &site;
        if (site.image == "eboot")
            runtime.set_interpreted_site(site.address, site.number);
        else if (std::find(s.images.begin(), s.images.end(), site.image) == s.images.end())
            s.images.push_back(site.image);
    }
    runtime.set_site_hook(&on_site);
    std::cout << "[pouch page] " << sites().size() << " sites" << (s.enabled ? "" : ", off") << std::endl;
}

// The sites of the overlay in a slot, for code the interpreter runs.
void follow_overlay(psprecomp::Runtime &runtime, std::uint32_t slot, std::uint32_t end, std::string &image) {
    std::string loaded;
    for (const std::string &name : state().images)
        if (layout::overlay_loaded(runtime.memory(), slot, name + ".ovl")) loaded = name;
    if (loaded == image) return;
    runtime.clear_interpreted_sites(slot, end);
    image = loaded;
    for (const Site &site : sites())
        if (site.image == image) runtime.set_interpreted_site(site.address, site.number);
}

void deactivate(psprecomp::Runtime &runtime) {
    State &s = state();
    s.active = false;
    set_hooks(runtime, false);
}

} // namespace

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    if (!s.started) start(runtime);
    if (!s.enabled) return;
    GuestRam ram(runtime.memory());
    const std::uint32_t sys = item_sets::game_sys(ram);
    const std::string hunter = sys != 0u ? item_sets::hunter_name(ram, sys) : std::string{};
    if (hunter.empty()) {
        s.hunter.clear();
        deactivate(runtime);
        return;
    }
    if (hunter != s.hunter || s.reload) {
        s.hunter = hunter;
        s.reload = false;
        s.page = store().page(hunter);
        if (s.grant_all) s.page.owned = true;
        s.active = s.page.owned;
        if (s.active) s.mirror = fill_blocks(ram, sys, s.page);
        if (s.trace)
            std::cout << "[pouch page] hunter " << hunter << (s.active ? ": fourth page" : ": three pages") << std::endl;
    }
    if (s.active) {
        merge(ram, sys, s.mirror);
        follow_overlay(runtime, layout::kTaskSlot, layout::kSubSlot, s.task_image);
        follow_overlay(runtime, layout::kEmOverlay, layout::kEmOverlayEnd, s.em_image);
    }
    set_hooks(runtime, s.active);
}

void game_saved(psprecomp::GuestMemory &memory) {
    State &s = state();
    if (!s.active || s.hunter.empty()) return;
    GuestRam ram(memory);
    store().set(s.hunter, page_of(ram));
    if (!store().save())
        std::cerr << "[pouch page] could not write " << store().path().string() << std::endl;
    else if (s.trace)
        std::cout << "[pouch page] kept " << s.hunter << "'s page" << std::endl;
}

void game_loaded() {
    state().reload = true;
}

bool active() {
    return state().active;
}

bool owned() {
    return !state().hunter.empty() && state().page.owned;
}

bool enabled() {
    return state().started && state().enabled;
}

void grant(psprecomp::Runtime &runtime) {
    State &s = state();
    if (!s.enabled || s.hunter.empty() || s.active) return;
    GuestRam ram(runtime.memory());
    const std::uint32_t sys = item_sets::game_sys(ram);
    if (sys == 0u) return;
    s.page.owned = true;
    s.mirror = fill_blocks(ram, sys, s.page);
    s.active = true;
    set_hooks(runtime, true);
}

} // namespace mhfu::game::pouch_page
