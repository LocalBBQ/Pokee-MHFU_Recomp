#include "game/throwing_knives.hpp"

#include "game/hooks.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>

namespace mhfu::game::throwing_knives {
namespace {

constexpr std::array<CodeWord, 24> kSignature = {{
    {kSearch, 0x27BDFFE0u},     // addiu sp, sp, -32: the search
    {0x0885B608u, 0x3C02089Au}, // lui v0, 0x089A
    {0x0885B60Cu, 0x24424FA8u}, // addiu v0, v0, 0x4FA8: the index
    {0x0885B634u, 0x244248F0u}, // addiu v0, v0, 0x48F0: the records
    {kKnown, 0x27BDFFF0u},      // addiu sp, sp, -16: known
    {0x0885B7E0u, 0x0E216D6Eu}, // jal 0x0885B5B8
    {kCombine, 0x27BDFFE0u},    // addiu sp, sp, -32: combine
    {0x0885B87Cu, 0x0E216D6Eu}, // jal 0x0885B5B8, inside its unit
    {kStep, 0x27BDFFD0u},       // addiu sp, sp, -48: the combo list's step
    {0x0885C194u, 0x3C02089Au}, // lui v0, 0x089A
    {0x0885C198u, 0x24425984u}, // addiu v0, v0, 0x5984: the pairs
    {0x0885C1D8u, 0x241300ACu}, // addiu s3, zero, 172
    {0x0885C1E4u, 0x2A4100ACu}, // slti at, s2, 172
    {kListOpen, 0x27BDFFC0u},   // addiu sp, sp, -64: the Combine screen's count
    {0x088262DCu, 0x908205C2u}, // lbu v0, 0x5C2(a0): rows per page
    {0x0882632Cu, 0x0E21704Fu}, // jal 0x0885C13C
    {0x0882644Cu, 0x2A4200ACu}, // slti v0, s2, 172
    {0x088265ACu, 0x0E21704Fu}, // jal 0x0885C13C: the page
    {0x0881E9CCu, 0x0E2098ACu}, // jal 0x088262B0
    {0x0881E3F0u, 0x0E216E0Bu}, // jal 0x0885B82C
    {0x0885C0F4u, 0x90A60006u}, // lbu a2, 6(a1): the known bit
    {0x0885C114u, 0x9083446Cu}, // lbu v1, 0x446C(a0)
    {0x0882997Cu, 0x2442D2BAu}, // addiu v0, v0, -11590: the item table's byte 2
    {0x08851498u, 0x2463C560u}, // addiu v1, v1, -15008: the texts
}};

// A call that returns to one of the port's stops, with what to do there.
struct Pending {
    std::uint32_t return_pc{};
    std::uint32_t screen{};
    std::uint16_t selected{};
};

struct State {
    psprecomp::Runtime::RecompiledFunction search{};
    psprecomp::Runtime::RecompiledFunction known{};
    psprecomp::Runtime::RecompiledFunction combine{};
    psprecomp::Runtime::RecompiledFunction step{};
    psprecomp::Runtime::RecompiledFunction list_open{};
    bool wrapped{};
    bool trace{};
    // The stores' lists get the knife (stock).
    bool shops{};
    // The items' records as the game had them, once read.
    std::optional<std::array<std::array<std::uint8_t, kItemRecord>, kKnives.size()>> items;
    bool items_changed{};
    // Each description cut, by the address of its cut and the byte there.
    std::map<std::uint32_t, std::uint8_t> cuts;
    // Whetstone's record while a new combination stands in for it.
    std::vector<std::pair<std::uint32_t, std::array<std::uint8_t, kRecordSize>>> swapped;
    std::vector<Pending> combining;
    std::vector<Pending> opening;
};

State &state() {
    static State s;
    return s;
}

bool on() {
    return settings::current().throwing_knives;
}

std::uint32_t game_sys(const psprecomp::GuestMemory &memory) {
    return memory.contains(kGameSysPointer, 4u) ? memory.load32(kGameSysPointer) : 0u;
}

std::uint32_t load32_bytes(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    std::uint32_t value = 0u;
    for (std::uint32_t i = 0; i < 4u; ++i) value |= static_cast<std::uint32_t>(memory.load8(address + i)) << (8u * i);
    return value;
}

void answer(psprecomp::AllegrexContext &ctx, std::uint32_t value) {
    ctx.gpr[2] = value;
    ctx.pc = ctx.gpr[31];
}

// The search's answer for a new combination, whose flags are 0: the record
// with bit 0 of mode set, or for t1 of -1, 0 (0x0885B160 finds nothing to
// check for flags of 0) and 1.
bool search_finds(std::uint32_t mode, std::uint32_t t1) {
    return (mode & 1u) != 0u || t1 == 0xFFFFFFFFu || t1 == 0u || t1 == 1u;
}

void search(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const std::optional<std::size_t> combo = new_combo(ctx.gpr[5], ctx.gpr[6]);
    if (on() && combo) {
        answer(ctx, search_finds(ctx.gpr[7], ctx.gpr[9]) ? new_record(*combo) : 0u);
        return;
    }
    ctx.pc = kSearch;
    s.search(runtime, ctx);
}

void known(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const std::optional<std::size_t> combo = new_combo(ctx.gpr[5], ctx.gpr[6]);
    const psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t sys = game_sys(memory);
    if (on() && combo && sys != 0u) {
        const std::uint8_t bit = kNewCombos[*combo].known_bit;
        const std::uint32_t address = sys + kKnownBits + bit / 8u;
        answer(ctx, memory.contains(address) && (memory.load8(address) & (1u << (bit % 8u))) != 0u ? 1u : 0u);
        return;
    }
    ctx.pc = kKnown;
    s.known(runtime, ctx);
}

// Whetstone's first record, which a new combination stands in for.
std::optional<std::uint32_t> stand_in(const psprecomp::GuestMemory &memory) {
    const std::uint32_t entry = kIndex + kWhetstone * 2u;
    if (!memory.contains(entry, 2u) || memory.load8(entry) == 0u) return std::nullopt;
    return kRecords + memory.load8(entry + 1u) * kRecordSize;
}

void put_back(psprecomp::GuestMemory &memory) {
    State &s = state();
    if (s.swapped.empty()) return;
    const auto [address, bytes] = s.swapped.back();
    s.swapped.pop_back();
    for (std::uint32_t i = 0; i < kRecordSize; ++i) memory.store8(address + i, bytes[i]);
}

void combine_returned(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (s.combining.empty()) return;
    const Pending call = s.combining.back();
    s.combining.pop_back();
    put_back(runtime.memory());
    ctx.pc = call.return_pc;
    ctx.gpr[31] = call.return_pc;
}

// a3 and t0 are the two items. A new combination runs the game's code with
// its record in place of Whetstone's, which is put back at the stop.
void combine(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::optional<std::size_t> combo = new_combo(ctx.gpr[7], ctx.gpr[8]);
    const std::optional<std::uint32_t> slot = stand_in(memory);
    if (!on() || !combo || !slot) {
        ctx.pc = kCombine;
        s.combine(runtime, ctx);
        return;
    }
    std::array<std::uint8_t, kRecordSize> game{};
    for (std::uint32_t i = 0; i < kRecordSize; ++i) game[i] = memory.load8(*slot + i);
    s.swapped.emplace_back(*slot, game);
    const std::array<std::uint8_t, kRecordSize> bytes = record_bytes(kNewCombos[*combo]);
    for (std::uint32_t i = 0; i < kRecordSize; ++i) memory.store8(*slot + i, bytes[i]);
    if (s.trace)
        std::cout << "[knives] combining " << ctx.gpr[7] << " + " << ctx.gpr[8] << " in Whetstone's record"
                  << std::endl;
    const std::uint32_t return_pc = ctx.gpr[31];
    s.combining.push_back({return_pc});
    ctx.gpr[31] = kCombineStop;
    ctx.pc = kCombine;
    s.combine(runtime, ctx);
    // It may have gone back to the dispatcher partway; the stop finishes it then.
    if (ctx.pc == kCombineStop) combine_returned(runtime, ctx);
}

// The game's step over 175 entries. a1 points to the index byte, a2 is the
// step (0: the entry itself, else the next one that has a record), a3 and t0
// get the pair, t1 is the mode.
void step(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t index_at = ctx.gpr[5];
    if (!on() || (ctx.gpr[9] & 1u) == 0u || !memory.contains(index_at)) {
        // The game's own walk knows only its 172.
        if (memory.contains(index_at) && memory.load8(index_at) >= kGameCombos)
            memory.store8(index_at, kGameCombos - 1u);
        ctx.pc = kStep;
        s.step(runtime, ctx);
        return;
    }
    const auto record_of = [&](int index) -> std::uint32_t {
        const std::optional<std::array<std::int16_t, 2>> pair = pair_at(memory, index);
        if (!pair) return 0u;
        if (index >= static_cast<int>(kGameCombos)) return new_record(static_cast<std::size_t>(index) - kGameCombos);
        return game_record(memory, static_cast<std::uint16_t>((*pair)[0]), static_cast<std::uint16_t>((*pair)[1]));
    };
    const auto give = [&](int index, std::uint32_t record) {
        const std::array<std::int16_t, 2> pair = *pair_at(memory, index);
        memory.store16(ctx.gpr[7], static_cast<std::uint16_t>(pair[0]));
        memory.store16(ctx.gpr[8], static_cast<std::uint16_t>(pair[1]));
        answer(ctx, record);
    };
    int index = memory.load8(index_at);
    std::int8_t direction = static_cast<std::int8_t>(ctx.gpr[6] & 0xFFu);
    if (direction == 0) {
        if (const std::uint32_t record = record_of(index); record != 0u) {
            give(index, record);
            return;
        }
        direction = 1;
    }
    for (std::uint32_t left = kAllCombos; left > 0u; --left) {
        index += direction;
        if (index >= static_cast<int>(kAllCombos))
            index = 0;
        else if (index < 0)
            index = static_cast<int>(kAllCombos) - 1;
        if (const std::uint32_t record = record_of(index); record != 0u) {
            memory.store8(index_at, static_cast<std::uint8_t>(index));
            give(index, record);
            return;
        }
    }
    answer(ctx, 0u);
}

// The cursor laid out for 175 entries, the selection kept where it was.
void lay_out(psprecomp::GuestMemory &memory, std::uint32_t screen, std::uint16_t selected) {
    const std::uint32_t cursor = screen + kCursor;
    if (!memory.contains(cursor, kLastRows + 1u)) return;
    const std::uint32_t rows = memory.load8(cursor + kRows);
    if (rows == 0u) return;
    const std::uint32_t pages = (kAllCombos + rows - 1u) / rows;
    const std::uint32_t chosen = std::min<std::uint32_t>(selected, kAllCombos - 1u);
    memory.store8(cursor + kPages, static_cast<std::uint8_t>(pages));
    memory.store8(cursor + kLastRows, static_cast<std::uint8_t>(kAllCombos - rows * (pages - 1u)));
    memory.store16(cursor + kSelected, static_cast<std::uint16_t>(chosen));
    memory.store8(cursor + kRow, static_cast<std::uint8_t>(chosen % rows));
    memory.store8(cursor + kPage, static_cast<std::uint8_t>(chosen / rows));
}

void list_opened(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (s.opening.empty()) return;
    const Pending call = s.opening.back();
    s.opening.pop_back();
    // 0 is a list without a first entry, which lays nothing out.
    if (ctx.gpr[2] != 0u && on()) lay_out(runtime.memory(), call.screen, call.selected);
    if (s.trace) std::cout << "[knives] the combo list counts " << kAllCombos << std::endl;
    ctx.pc = call.return_pc;
    ctx.gpr[31] = call.return_pc;
}

// a0 is the Combine screen. The game counts its 172 and lays them out; at the
// stop the count is the 175 the step walks.
void list_open(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t screen = ctx.gpr[4];
    const std::uint32_t selected_at = screen + kCursor + kSelected;
    const std::uint16_t selected = memory.contains(selected_at, 2u) ? memory.load16(selected_at) : 0u;
    const std::uint32_t return_pc = ctx.gpr[31];
    s.opening.push_back({return_pc, screen, selected});
    ctx.gpr[31] = kListOpenStop;
    ctx.pc = kListOpen;
    s.list_open(runtime, ctx);
    if (ctx.pc == kListOpenStop) list_opened(runtime, ctx);
}

void wrap(psprecomp::Runtime &runtime) {
    State &s = state();
    if (s.wrapped) {
        // The executable's functions stay registered; only a reload of its
        // code would put the game's back.
        if (runtime.function_at(kSearch) == &search) return;
        s.wrapped = false;
    }
    for (std::uint32_t address : {kSearch, kKnown, kCombine, kStep, kListOpen})
        if (runtime.function_at(address) == nullptr) return;
    psprecomp::GuestMemory &memory = runtime.memory();
    if (!code_matches(memory)) return;
    // A game that has either combination already keeps its own.
    for (const Combo &combo : kNewCombos)
        if (game_record(memory, combo.first, combo.second) != 0u) return;
    // The stops must be free: a function already there is not ours to replace.
    const psprecomp::Runtime::RecompiledFunction combine_stop = runtime.function_at(kCombineStop);
    const psprecomp::Runtime::RecompiledFunction list_stop = runtime.function_at(kListOpenStop);
    if ((combine_stop != nullptr && combine_stop != &combine_returned) ||
        (list_stop != nullptr && list_stop != &list_opened))
        return;
    s.search = runtime.function_at(kSearch);
    s.known = runtime.function_at(kKnown);
    s.combine = runtime.function_at(kCombine);
    s.step = runtime.function_at(kStep);
    s.list_open = runtime.function_at(kListOpen);
    // Called from generated code, which chains into them (game/hooks.hpp).
    hooks::register_chained(runtime, kSearch, &search, "mhfu_knives_search");
    hooks::register_chained(runtime, kKnown, &known, "mhfu_knives_known");
    hooks::register_chained(runtime, kCombine, &combine, "mhfu_knives_combine");
    hooks::register_chained(runtime, kStep, &step, "mhfu_knives_step");
    hooks::register_chained(runtime, kListOpen, &list_open, "mhfu_knives_list");
    runtime.register_function(kCombineStop, &combine_returned, "mhfu_knives_combine_return");
    runtime.register_function(kListOpenStop, &list_opened, "mhfu_knives_list_return");
    for (std::size_t i = 0; i < kNewCombos.size(); ++i) {
        const std::array<std::uint8_t, kRecordSize> bytes = record_bytes(kNewCombos[i]);
        for (std::uint32_t b = 0; b < kRecordSize; ++b) memory.store8(new_record(i) + b, bytes[b]);
    }
    s.wrapped = true;
    std::cout << "[knives] combinations in place" << std::endl;
}

void keep_items(psprecomp::GuestMemory &memory, bool want) {
    State &s = state();
    if (!s.items) {
        std::array<std::array<std::uint8_t, kItemRecord>, kKnives.size()> records{};
        for (std::size_t k = 0; k < kKnives.size(); ++k) {
            const std::uint32_t record = kItemTable + kKnives[k] * kItemRecord;
            // Supply items all, as the game has them; else not the game's table.
            if (!memory.contains(record, kItemRecord) || (memory.load8(record + kFlags) & kSupplyItem) == 0u) return;
            for (std::uint32_t b = 0; b < kItemRecord; ++b) records[k][b] = memory.load8(record + b);
        }
        s.items = records;
    }
    if (want == s.items_changed) return;
    for (std::size_t k = 0; k < kKnives.size(); ++k) {
        const std::uint32_t record = kItemTable + kKnives[k] * kItemRecord;
        const std::array<std::uint8_t, kItemRecord> &game = (*s.items)[k];
        for (std::uint32_t b = 0; b < kItemRecord; ++b) memory.store8(record + b, game[b]);
        if (!want) continue;
        memory.store8(record + kFlags, static_cast<std::uint8_t>(game[kFlags] & ~kSupplyItem));
        if (kKnives[k] == kThrowingKnife) {
            memory.store32(record + kBuyPrice, kKnifePrice);
            memory.store32(record + kSellPrice, kKnifeSellPrice);
        }
    }
    s.items_changed = want;
    if (s.trace) std::cout << "[knives] the knives are " << (want ? "items" : "supply items again") << std::endl;
}

void keep_texts(psprecomp::GuestMemory &memory, bool want) {
    State &s = state();
    if (!want) {
        for (const auto &[address, byte] : s.cuts)
            if (memory.contains(address) && memory.load8(address) == 0u) memory.store8(address, byte);
        s.cuts.clear();
        return;
    }
    // The texts are read from DATA.BIN at start, and again if the game ever
    // reads them again: a tag is cut wherever one is found.
    for (std::uint16_t item : kKnives) {
        const std::optional<std::uint32_t> cut = tag_start(memory, item);
        if (!cut) continue;
        s.cuts[*cut] = memory.load8(*cut);
        memory.store8(*cut, 0u);
        if (s.trace) std::cout << "[knives] item " << item << "'s description cut" << std::endl;
    }
}

} // namespace

std::span<const CodeWord> code_signature() {
    return kSignature;
}

bool code_matches(const psprecomp::GuestMemory &memory) {
    for (const CodeWord &expected : kSignature)
        if (!memory.contains(expected.address, 4u) || memory.load32(expected.address) != expected.word) return false;
    return true;
}

std::array<std::uint8_t, kRecordSize> record_bytes(const Combo &combo) {
    return {static_cast<std::uint8_t>(combo.second & 0xFFu), static_cast<std::uint8_t>(combo.second >> 8u),
        static_cast<std::uint8_t>(kThrowingKnife & 0xFFu), static_cast<std::uint8_t>(kThrowingKnife >> 8u), kRateClass,
        kYieldClass, combo.known_bit, 0u, combo.known_bit, 0u};
}

std::optional<std::size_t> new_combo(std::uint32_t a, std::uint32_t b) {
    // The game compares the two as signed halfwords.
    std::int16_t first = static_cast<std::int16_t>(a & 0xFFFFu);
    std::int16_t second = static_cast<std::int16_t>(b & 0xFFFFu);
    if (second < first) std::swap(first, second);
    for (std::size_t i = 0; i < kNewCombos.size(); ++i)
        if (first == kNewCombos[i].first && second == kNewCombos[i].second) return i;
    return std::nullopt;
}

std::uint32_t game_record(const psprecomp::GuestMemory &memory, std::uint32_t a, std::uint32_t b) {
    std::int16_t first = static_cast<std::int16_t>(a & 0xFFFFu);
    std::int16_t second = static_cast<std::int16_t>(b & 0xFFFFu);
    if (second < first) std::swap(first, second);
    if (first < 0 || static_cast<std::uint32_t>(first) >= kIndexItems) return 0u;
    const std::uint32_t entry = kIndex + static_cast<std::uint32_t>(first) * 2u;
    if (!memory.contains(entry, 2u)) return 0u;
    const std::uint32_t count = memory.load8(entry);
    std::uint32_t record = kRecords + memory.load8(entry + 1u) * kRecordSize;
    for (std::uint32_t i = 0; i < count; ++i, record += kRecordSize)
        if (memory.contains(record, 2u) && static_cast<std::int16_t>(memory.load16(record)) == second) return record;
    return 0u;
}

std::optional<std::array<std::int16_t, 2>> pair_at(const psprecomp::GuestMemory &memory, int index) {
    if (index < 0 || index >= static_cast<int>(kAllCombos)) return std::nullopt;
    if (index >= static_cast<int>(kGameCombos)) {
        const Combo &combo = kNewCombos[static_cast<std::size_t>(index) - kGameCombos];
        return std::array<std::int16_t, 2>{static_cast<std::int16_t>(combo.first), static_cast<std::int16_t>(combo.second)};
    }
    const std::uint32_t pair = kPairs + static_cast<std::uint32_t>(index) * 4u;
    if (!memory.contains(pair, 4u)) return std::nullopt;
    return std::array<std::int16_t, 2>{
        static_cast<std::int16_t>(memory.load16(pair)), static_cast<std::int16_t>(memory.load16(pair + 2u))};
}

std::vector<std::uint16_t> with_knife(const std::vector<std::uint16_t> &list) {
    if (std::find(list.begin(), list.end(), kThrowingKnife) != list.end()) return list;
    const auto boomerang = std::find(list.begin(), list.end(), kBoomerang);
    if (boomerang == list.end()) return list;
    std::vector<std::uint16_t> stocked(list.begin(), boomerang + 1);
    stocked.push_back(kThrowingKnife);
    stocked.insert(stocked.end(), boomerang + 1, list.end());
    return stocked;
}

std::vector<std::uint16_t> stock(std::uint32_t, std::vector<std::uint16_t> ids) {
    return state().shops ? with_knife(ids) : ids;
}

std::optional<std::uint32_t> tag_start(const psprecomp::GuestMemory &memory, std::uint16_t item) {
    const std::uint32_t offset_at = kText + 4u * (kDescriptions + 2u);
    if (!memory.contains(offset_at, 4u)) return std::nullopt;
    const std::uint32_t section = kText + load32_bytes(memory, offset_at);
    const std::uint32_t entry = section + 4u * item;
    if (!memory.contains(entry, 4u)) return std::nullopt;
    const std::uint32_t text = section + load32_bytes(memory, entry);
    // Up to the end of the text, which is short.
    constexpr std::uint32_t kLongest = 256u;
    std::uint32_t end = text;
    while (end < text + kLongest && memory.contains(end) && memory.load8(end) != 0u) ++end;
    if (end == text + kLongest || !memory.contains(end)) return std::nullopt;
    constexpr std::uint32_t kTagLength = sizeof(kSupplyTag) - 1u;
    if (end - text < kTagLength + 1u) return std::nullopt;
    const std::uint32_t tag = end - kTagLength;
    for (std::uint32_t i = 0; i < kTagLength; ++i)
        if (memory.load8(tag + i) != static_cast<std::uint8_t>(kSupplyTag[i])) return std::nullopt;
    const std::uint8_t before = memory.load8(tag - 1u);
    return before == ' ' || before == '\n' ? tag - 1u : tag;
}

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    static const bool trace = [] {
        const char *text = std::getenv("MHFU_TRACE_THROWING_KNIVES");
        return text != nullptr && *text != '\0' && std::string(text) != "0";
    }();
    s.trace = trace;
    wrap(runtime);
    s.shops = s.wrapped && on();
    if (!s.wrapped) return;
    psprecomp::GuestMemory &memory = runtime.memory();
    const bool want = on();
    keep_items(memory, want);
    keep_texts(memory, want);
}

} // namespace mhfu::game::throwing_knives
