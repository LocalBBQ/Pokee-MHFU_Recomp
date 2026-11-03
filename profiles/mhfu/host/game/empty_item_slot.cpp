#include "game/empty_item_slot.hpp"

#include "game/hooks.hpp"
#include "game/pouch_slots.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

namespace mhfu::game::empty_item_slot {
namespace {

constexpr std::array<CodeWord, 29> kSignature = {{
    // The pouch's packing and the move off a slot with no item before it.
    {kPack, 0x27BDFFD0u},       // addiu sp, sp, -48
    {0x088292FCu, 0x321000FFu}, // andi s0, s0, 0xFF: the pouch
    {0x08829368u, 0x0E20A570u}, // jal 0x088295C0
    {0x088295C0u, 0x27BDFFE0u}, // addiu sp, sp, -32
    {0x08829624u, 0xA6500648u}, // sh s0, 1608(s2)
    // The HUD's strip: calls of the step, and 255 as nothing.
    {0x08845FECu, kStepCall},   // the animated layout's first
    {0x08846110u, kStepCall},   // and last
    {0x088464ECu, kStepCall},   // the still layout's first
    {0x08846588u, kStepCall},   // and last
    {0x08846170u, 0x240200FFu}, // addiu v0, zr, 255
    {0x08846174u, 0x50A20012u}, // beql a1, v0: no icon
    // The name: the item check, then the name for its width and its print.
    {0x08845ABCu, 0x0E209A4Fu}, // jal 0x0882693C
    {kHasItem, 0x27BDFFF0u},    // addiu sp, sp, -16
    {0x08845BA8u, 0x0E21454Au}, // jal 0x08851528
    {0x08845BF8u, 0x0E21454Au}, // jal 0x08851528
    {kItemName, 0x00A03021u},   // addu a2, a1, zr
    {0x0885152Cu, 0x0A21452Bu}, // j 0x088514AC
    // The step after an item leaves the pouch.
    {0x088D74FCu, kStepCall}, // jal 0x088D81FC
    // The step.
    {kStep, 0x27BDFFE0u},       // addiu sp, sp, -32
    {0x088D8214u, 0x3C02089Du}, // lui v0, 0x089D
    {0x088D8218u, 0x8C42C558u}, // lw v0, -15016(v0): GameSys
    {0x088D821Cu, 0x948301E6u}, // lhu v1, 486(a0)
    {0x088D8220u, 0x90420028u}, // lbu v0, 40(v0)
    {0x088D8240u, 0x28420018u}, // slti v0, v0, 24
    {0x088D82A4u, 0x3C02089Au}, // lui v0, 0x089A
    {0x088D82A8u, 0x2442D2B8u}, // addiu v0, v0, -11592: the item table
    {0x088D82B0u, 0x90420001u}, // lbu v0, 1(v0)
    {0x088D82B4u, 0x30420001u}, // andi v0, v0, 1
    {0x088568A0u, 0x3421AEA6u}, // ori at, at, 0xAEA6: the pouch
}};

// game_task.ovl's first selection, as loaded at 0x09A5F300.
constexpr std::array<CodeWord, 5> kSelectFirstSignature = {{
    {kSelectFirst, 0x27BDFFE0u}, // addiu sp, sp, -32
    {0x09A6814Cu, 0x948401E6u},  // lhu a0, 486(a0)
    {0x09A68150u, 0x90630028u},  // lbu v1, 40(v1)
    {0x09A68164u, 0xA6200648u},  // sh zr, 1608(s1)
    {0x09A681E0u, 0xA6230648u},  // sh v1, 1608(s1)
}};

struct State {
    psprecomp::Runtime::RecompiledFunction original{};
    psprecomp::Runtime::RecompiledFunction has_item{};
    psprecomp::Runtime::RecompiledFunction item_name{};
    psprecomp::Runtime::RecompiledFunction select_first{};
    psprecomp::Runtime::RecompiledFunction pack{};
    bool select_first_refused{};         // reported once
    std::optional<std::uint16_t> chosen; // the empty stop the last scroll landed on
    bool installed{};
    bool refused{};
    bool checked{};
    bool on{};
    bool trace{};
    std::uint32_t hunter{};
    std::uint16_t last_selected{0xFFFFu};
};

State &state() {
    static State s;
    return s;
}

const char *name(Direction direction) {
    return direction == Direction::Forward ? "forward" : "back";
}

void trace_scroll(const psprecomp::GuestMemory &memory, const Pouch &pouch, std::uint16_t from, std::uint16_t to,
    Direction direction) {
    const std::optional<std::uint16_t> game = game_step(pouch, from, direction);
    const std::optional<std::uint16_t> empty = empty_slot(pouch);
    std::cout << "[item bar] " << name(direction) << ' ' << from << " -> " << to;
    if (to <= pouch.spare() && !pouch[to].on_bar) std::cout << " (empty)";
    std::cout << ", game " << (game ? std::to_string(*game) : std::string("none")) << ", empty slot "
              << (empty ? std::to_string(*empty) : std::string("none"));
    // The four bytes past the pouch, the 25th slot 0x08856884 still accepts.
    const std::uint32_t game_sys = memory.load32(kGameSysPointer);
    const std::uint32_t past = pouch_slots::address(game_sys, pouch.spare());
    if (past != 0u && memory.contains(past, 4u))
        std::cout << ", past the pouch " << std::hex << memory.load32(past) << std::dec;
    std::cout << std::endl;
}

void answer(psprecomp::AllegrexContext &ctx, std::uint32_t value) {
    ctx.gpr[2] = value;
    ctx.pc = ctx.gpr[31];
}

void step(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint16_t current = static_cast<std::uint16_t>(ctx.gpr[5]);
    const std::uint32_t return_address = ctx.gpr[31];
    if (s.on) {
        if (strip_call(return_address)) {
            // The step reads a2 as a byte: 0 forward, anything else back.
            const Direction direction = (ctx.gpr[6] & 0xFFu) == 0u ? Direction::Forward : Direction::Backward;
            if (const std::optional<Pouch> pouch = read_pouch(memory, ctx.gpr[4])) {
                answer(ctx, strip_step(*pouch, current, direction));
                return;
            }
        } else if (return_address == kTakeReturn) {
            if (s.chosen == current && current <= pouch_slots::count() && read_pouch(memory, ctx.gpr[4]) &&
                slot_empty(memory, current)) {
                answer(ctx, current);
                return;
            }
        } else if (current <= pouch_slots::count()) {
            if (const std::optional<Direction> direction = scroll_direction(memory, return_address, ctx.gpr[6])) {
                if (const std::optional<Pouch> pouch = read_pouch(memory, ctx.gpr[4])) {
                    const std::uint16_t next = scroll(*pouch, current, *direction);
                    s.hunter = ctx.gpr[4];
                    s.chosen =
                        next <= pouch->spare() && !(*pouch)[next].on_bar ? std::optional<std::uint16_t>{next}
                                                                         : std::nullopt;
                    if (s.trace) trace_scroll(memory, *pouch, current, next, *direction);
                    answer(ctx, next);
                    return;
                }
            }
        }
    }
    ctx.pc = kStep;
    s.original(runtime, ctx);
}

// a0 is the hunter. The game selects its first item; the bar starts on the
// empty stop instead.
void select_first(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const std::uint32_t hunter = ctx.gpr[4];
    ctx.pc = kSelectFirst;
    s.select_first(runtime, ctx);
    if (!s.on) return;
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::optional<Pouch> pouch = read_pouch(memory, hunter);
    if (!pouch || !memory.contains(hunter + kSelectedSlot, 2u)) return;
    const std::optional<std::uint16_t> empty = empty_slot(*pouch);
    if (!empty) return;
    memory.store16(hunter + kSelectedSlot, *empty);
    s.chosen = empty;
    s.hunter = hunter;
    if (s.trace) std::cout << "[item bar] starts on the empty slot " << *empty << std::endl;
}

// a1 is the pouch, 0 for the items. Packing moves a selection with no item to
// the next item; the empty stop the hunter was on is selected again after it.
void pack(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    psprecomp::GuestMemory &memory = runtime.memory();
    const bool items = (ctx.gpr[5] & 0xFFu) == 0u;
    const std::uint32_t hunter = s.hunter;
    const bool on_empty = s.on && items && hunter != 0u && memory.contains(hunter + kSelectedSlot, 2u) &&
        s.chosen == memory.load16(hunter + kSelectedSlot) && slot_empty(memory, *s.chosen);
    ctx.pc = kPack;
    s.pack(runtime, ctx);
    if (!on_empty) return;
    const std::optional<Pouch> pouch = read_pouch(memory, hunter);
    if (!pouch) return;
    const std::optional<std::uint16_t> empty = empty_slot(*pouch);
    const std::uint16_t moved = memory.load16(hunter + kSelectedSlot);
    if (s.trace)
        std::cout << "[item bar] the pouch was packed: " << *s.chosen << " -> " << moved << ", empty slot "
                  << (empty ? std::to_string(*empty) : std::string("none")) << std::endl;
    s.chosen = empty;
    if (empty) memory.store16(hunter + kSelectedSlot, *empty);
}

// a1 is the slot. Only the name under the bar is answered.
void has_item(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (s.on && ctx.gpr[31] == kNameGateReturn && slot_empty(runtime.memory(), ctx.gpr[5] & 0xFFFFu)) {
        answer(ctx, 1u);
        return;
    }
    ctx.pc = kHasItem;
    s.has_item(runtime, ctx);
}

// a1 is the item id. Only the name under the bar is answered.
void item_name(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const std::uint32_t return_address = ctx.gpr[31];
    if (s.on && (ctx.gpr[5] & 0xFFFFu) == 0u &&
        (return_address == kNameWidthReturn || return_address == kNamePrintReturn)) {
        psprecomp::GuestMemory &memory = runtime.memory();
        if (memory.contains(kNothingText, sizeof kNothingName)) {
            for (std::size_t i = 0; i < sizeof kNothingName; ++i)
                memory.store8(kNothingText + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(kNothingName[i]));
            answer(ctx, kNothingText);
            return;
        }
    }
    ctx.pc = kItemName;
    s.item_name(runtime, ctx);
}

bool check(const psprecomp::Runtime &runtime) {
    if (!code_matches(runtime.memory())) {
        std::cerr << "[item bar] the item bar step is not as traced; the empty item slot is off\n";
        return false;
    }
    return true;
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

std::span<const CodeWord> select_first_signature() {
    return kSelectFirstSignature;
}

bool select_first_matches(const psprecomp::GuestMemory &memory) {
    for (const CodeWord &expected : kSelectFirstSignature)
        if (!memory.contains(expected.address, 4u) || memory.load32(expected.address) != expected.word) return false;
    return true;
}

std::optional<Direction> scroll_direction(
    const psprecomp::GuestMemory &memory, std::uint32_t return_address, std::uint32_t direction) {
    Direction wanted{};
    if (return_address == kForwardReturn)
        wanted = Direction::Forward;
    else if (return_address == kBackwardReturn)
        wanted = Direction::Backward;
    else
        return std::nullopt;
    if (direction != static_cast<std::uint32_t>(wanted)) return std::nullopt;
    // game_task is the overlay at that address only while a quest is loaded.
    if (!memory.contains(return_address - 8u, 8u)) return std::nullopt;
    if (memory.load32(return_address - 8u) != kStepCall) return std::nullopt;
    const std::uint32_t delay = wanted == Direction::Forward ? kForwardDelay : kBackwardDelay;
    if (memory.load32(return_address - 4u) != delay) return std::nullopt;
    return wanted;
}

std::optional<Pouch> read_pouch(const psprecomp::GuestMemory &memory, std::uint32_t hunter) {
    if (!memory.contains(kGameSysPointer, 4u) || !memory.contains(hunter + kHunterIndex, 2u)) return std::nullopt;
    const std::uint32_t game_sys = memory.load32(kGameSysPointer);
    if (!memory.contains(game_sys + kLocalHunterByte, 1u)) return std::nullopt;
    if (memory.load16(hunter + kHunterIndex) != memory.load8(game_sys + kLocalHunterByte)) return std::nullopt;
    Pouch pouch{};
    pouch.slots = static_cast<std::uint16_t>(pouch_slots::count());
    for (std::uint16_t i = 0; i <= pouch.spare(); ++i) {
        const std::uint32_t at = pouch_slots::address(game_sys, i);
        if (at == 0u || !memory.contains(at, 4u)) return std::nullopt;
        Slot &slot = pouch[i];
        slot.item = memory.load16(at);
        slot.count = memory.load16(at + 2u);
        const std::uint32_t record = kItemTable + std::uint32_t{slot.item} * kItemRecord;
        slot.on_bar = i < pouch.slots && slot.item != 0u && memory.contains(record + 1u, 1u) &&
            (memory.load8(record + 1u) & 1u) != 0u;
    }
    return pouch;
}

std::optional<std::uint16_t> game_step(const Pouch &pouch, std::uint16_t current, Direction direction) {
    const std::uint16_t slots = pouch.slots;
    for (std::uint16_t n = 1; n <= slots; ++n) {
        const std::uint16_t slot = direction == Direction::Forward
            ? static_cast<std::uint16_t>((current + n) % slots)
            : static_cast<std::uint16_t>((current + slots * 2u - n) % slots);
        if (pouch[slot].on_bar) return slot;
    }
    return std::nullopt;
}

std::optional<std::uint16_t> empty_slot(const Pouch &pouch) {
    for (std::uint16_t i = pouch.spare() + 1u; i-- > 0;)
        if (pouch[i].item == 0u && pouch[i].count == 0u) return i;
    return std::nullopt;
}

std::uint16_t scroll(const Pouch &pouch, std::uint16_t current, Direction direction) {
    if (current > pouch.spare()) return current;
    if (!pouch[current].on_bar) {
        // Nothing is selected: ○ starts at the first item, □ at the last. A
        // step from the far edge looks at every slot, the edge last.
        const std::uint16_t edge = direction == Direction::Forward ? pouch.slots - 1u : 0u;
        return game_step(pouch, edge, direction).value_or(current);
    }
    const std::uint16_t next = game_step(pouch, current, direction).value_or(current);
    const bool wrapped = direction == Direction::Forward ? next <= current : next >= current;
    if (!wrapped) return next;
    return empty_slot(pouch).value_or(next);
}

bool strip_call(std::uint32_t return_address) noexcept {
    return (return_address >= kStripFirst && return_address <= kStripFirstEnd) ||
        (return_address >= kStripSecond && return_address <= kStripSecondEnd);
}

std::uint16_t strip_step(const Pouch &pouch, std::uint16_t current, Direction direction) {
    if (current >= pouch.slots || !pouch[current].on_bar) {
        const std::uint16_t edge = direction == Direction::Forward ? pouch.slots - 1u : 0u;
        return game_step(pouch, edge, direction).value_or(kNone);
    }
    const std::uint16_t next = game_step(pouch, current, direction).value_or(current);
    const bool wrapped = direction == Direction::Forward ? next <= current : next >= current;
    if (wrapped && empty_slot(pouch)) return kNone;
    return next;
}

bool slot_empty(const psprecomp::GuestMemory &memory, std::uint32_t slot) {
    if (slot > pouch_slots::count() || !memory.contains(kGameSysPointer, 4u)) return false;
    const std::uint32_t address = pouch_slots::address(memory.load32(kGameSysPointer), slot);
    return address != 0u && memory.contains(address, 4u) && memory.load32(address) == 0u;
}

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    s.on = settings::current().empty_item_slot;
    // Off from the start: nothing is registered, and the game runs as built.
    if (!s.on && !s.installed) return;
    if (!s.checked) {
        s.checked = true;
        const char *text = std::getenv("MHFU_TRACE_ITEM_BAR");
        s.trace = text != nullptr && *text != '\0' && std::string(text) != "0";
    }
    if (s.trace && s.hunter != 0u && runtime.memory().contains(s.hunter + kSelectedSlot, 2u)) {
        const std::uint16_t selected = runtime.memory().load16(s.hunter + kSelectedSlot);
        if (selected != s.last_selected) {
            std::cout << "[item bar] selected " << s.last_selected << " -> " << selected << std::endl;
            s.last_selected = selected;
        }
    }
    if (s.refused) return;
    // Only at the flip, from an import: no generated code is running, so the
    // dispatch table can change here.
    if (!s.installed) {
        if (runtime.function_at(kStep) == nullptr || runtime.function_at(kHasItem) == nullptr ||
            runtime.function_at(kItemName) == nullptr || runtime.function_at(kPack) == nullptr)
            return;
        if (!check(runtime)) {
            s.refused = true; // reported once; the game's step stays
            return;
        }
        s.original = runtime.function_at(kStep);
        s.has_item = runtime.function_at(kHasItem);
        s.item_name = runtime.function_at(kItemName);
        s.pack = runtime.function_at(kPack);
        runtime.register_function(kPack, &pack, "mhfu_empty_item_slot_pack");
        // The HUD's item bar calls these three, and Gather with ○ wraps the
        // bar (game/gather_icon.hpp): chained, the bar finishes inside that
        // wrapper. They switch no thread.
        hooks::register_chained(runtime, kStep, &step, "mhfu_empty_item_slot");
        hooks::register_chained(runtime, kHasItem, &has_item, "mhfu_empty_item_slot_has_item");
        hooks::register_chained(runtime, kItemName, &item_name, "mhfu_empty_item_slot_name");
        s.installed = true;
        std::cout << "[item bar] empty item slot in place" << std::endl;
    }
    // game_task registers its functions again each time it is installed.
    const psprecomp::Runtime::RecompiledFunction current = runtime.function_at(kSelectFirst);
    if (current == nullptr || current == &select_first) return;
    if (!select_first_matches(runtime.memory())) {
        if (s.trace && !s.select_first_refused)
            std::cout << "[item bar] the code at 0x09A68128 is not game_task's first selection" << std::endl;
        s.select_first_refused = true;
        return;
    }
    s.select_first = current;
    runtime.register_function(kSelectFirst, &select_first, "mhfu_empty_item_slot_first");
    if (s.trace) std::cout << "[item bar] the bar starts on the empty slot" << std::endl;
}

} // namespace mhfu::game::empty_item_slot
