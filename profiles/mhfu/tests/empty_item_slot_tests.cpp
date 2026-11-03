// Empty item slot: the scroll with an empty stop, the pouch read, the code and
// caller checks, and the wrapper around a stand-in for the game's step. No
// game data.
#include "game/empty_item_slot.hpp"

#include "game/pouch_slots.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>

namespace {
using namespace mhfu::game::empty_item_slot;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void set_environment(const char *name, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    // _putenv_s removes a variable set to "", so do the same here.
    if (value.empty())
        unsetenv(name);
    else
        setenv(name, value.c_str(), 1);
#endif
}

constexpr std::uint32_t kGameSys = 0x09000000u;
constexpr std::uint32_t kHunter = 0x09900000u;
constexpr std::uint32_t kGatherReturn = 0x09AAAAF8u;
constexpr std::uint16_t kGameAnswer = 7u;
constexpr std::uint32_t kGameName = 0x0899A000u;
int game_calls{};
int has_item_calls{};
int name_calls{};

// The game's step, standing in: it answers one slot whatever it is asked.
void game_step_stand_in(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    ctx.gpr[2] = kGameAnswer;
    ctx.pc = ctx.gpr[31];
}

// The game's item check and name lookup, standing in: no item, and a name.
void has_item_stand_in(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ++has_item_calls;
    ctx.gpr[2] = 0u;
    ctx.pc = ctx.gpr[31];
}

// The pouch's packing, standing in: a selection with no item moves to slot 0.
void pack_stand_in(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    runtime.memory().store16(kHunter + kSelectedSlot, 0u);
    ctx.pc = ctx.gpr[31];
}

// game_task's first selection, standing in: the first slot.
int select_first_calls{};
void select_first_stand_in(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++select_first_calls;
    runtime.memory().store16(ctx.gpr[4] + kSelectedSlot, 0u);
    ctx.pc = ctx.gpr[31];
}

void name_stand_in(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ++name_calls;
    ctx.gpr[2] = kGameName;
    ctx.pc = ctx.gpr[31];
}

// A pouch with the given slots on the bar; the rest empty.
Pouch pouch_with(std::initializer_list<std::uint16_t> slots) {
    Pouch pouch{};
    for (const std::uint16_t slot : slots) pouch[slot] = Slot{static_cast<std::uint16_t>(100u + slot), 1u, true};
    return pouch;
}

void test_scroll() {
    const Pouch three = pouch_with({0, 1, 2});
    check(scroll(three, 0, Direction::Forward) == 1u && scroll(three, 1, Direction::Forward) == 2u,
        "inside the pouch ○ moves to the next item");
    check(scroll(three, 2, Direction::Forward) == kSpare, "past the last item ○ stops on the spare past the pouch");
    check(scroll(three, kSpare, Direction::Forward) == 0u, "from the empty stop ○ goes to the first item");
    check(scroll(three, 0, Direction::Backward) == kSpare, "before the first item □ stops on the empty stop");
    check(scroll(three, kSpare, Direction::Backward) == 2u, "from the empty stop □ goes to the last item");
    check(scroll(three, 2, Direction::Backward) == 1u, "inside the pouch □ moves to the previous item");
    check(empty_slot(three) == std::optional<std::uint16_t>{kSpare}, "the spare comes before any free slot");

    // Regression: Nothing Selected needed a free slot, so a full pouch lost it.
    Pouch full{};
    for (std::uint16_t i = 0; i < kSlots; ++i) full[i] = Slot{static_cast<std::uint16_t>(100u + i), 1u, true};
    check(empty_slot(full) == std::optional<std::uint16_t>{kSpare}, "a full pouch keeps the empty stop");
    check(scroll(full, 23, Direction::Forward) == kSpare && scroll(full, 0, Direction::Backward) == kSpare,
        "and a full pouch's scroll stops on it");
    check(scroll(full, kSpare, Direction::Forward) == 0u && scroll(full, kSpare, Direction::Backward) == 23u,
        "and leaves it for the first or the last item");
    full[kSpare] = Slot{9u, 1u, false};
    check(!empty_slot(full), "with the spare in use a full pouch has no empty stop");
    check(scroll(full, 23, Direction::Forward) == 0u && scroll(full, 0, Direction::Backward) == 23u,
        "and wraps as the game does");

    const Pouch one = pouch_with({4});
    check(scroll(one, 4, Direction::Forward) == kSpare && scroll(one, 4, Direction::Backward) == kSpare,
        "a single item steps to the empty stop either way");
    check(scroll(one, kSpare, Direction::Forward) == 4u && scroll(one, kSpare, Direction::Backward) == 4u,
        "and back to the item either way");

    Pouch holes = pouch_with({0, 5});
    holes[kSpare] = Slot{0u, 1u, false}; // the spare in use
    holes[23] = Slot{9u, 3u, false};     // an item the bar does not show
    holes[22] = Slot{0u, 2u, false};     // no id, but a count: not left empty
    check(empty_slot(holes) == std::optional<std::uint16_t>{21u},
        "with the spare in use the empty stop is the last slot with no id and no count");
    check(scroll(holes, 5, Direction::Forward) == 21u, "a pouch with holes stops on its last empty slot");
    check(scroll(holes, 21, Direction::Forward) == 0u && scroll(holes, 21, Direction::Backward) == 5u,
        "and leaves it for the first or the last item, wherever it is");
    check(scroll(holes, 23, Direction::Forward) == 0u, "a slot the bar does not show counts as nothing selected");

    const Pouch none{};
    check(!game_step(none, 3, Direction::Forward), "an empty pouch has no step");
    check(scroll(none, 3, Direction::Forward) == 3u && scroll(none, 3, Direction::Backward) == 3u,
        "with nothing to select the slot stays");
    check(scroll(none, kSpare, Direction::Forward) == kSpare, "nor from the empty stop");
    check(scroll(three, kSpare + 1u, Direction::Forward) == kSpare + 1u, "a slot past the spare stays, as in the game");
    check(game_step(three, 2, Direction::Forward) == std::optional<std::uint16_t>{0u} &&
            game_step(three, 0, Direction::Backward) == std::optional<std::uint16_t>{2u},
        "the game's own step wraps round");
}

void test_strip() {
    check(strip_call(kStripFirst) && strip_call(kStripFirstEnd) && strip_call(kStripSecond) &&
            strip_call(kStripSecondEnd),
        "both strip layouts' calls are the strip");
    check(!strip_call(kStripFirst - 4u) && !strip_call(kStripSecondEnd + 4u) && !strip_call(kForwardReturn),
        "calls around them and the scroll are not");
    const Pouch three = pouch_with({0, 1, 2});
    check(strip_step(three, 0, Direction::Forward) == 1u && strip_step(three, 1, Direction::Backward) == 0u,
        "inside the pouch the strip shows the next and previous item");
    check(strip_step(three, 2, Direction::Forward) == kNone && strip_step(three, 0, Direction::Backward) == kNone,
        "past either end the strip shows the empty stop as nothing");
    check(strip_step(three, kNone, Direction::Forward) == 0u && strip_step(three, kNone, Direction::Backward) == 2u,
        "beyond the empty stop come the first and the last item");
    check(strip_step(three, kSpare, Direction::Forward) == 0u && strip_step(three, kSpare, Direction::Backward) == 2u,
        "and round the selected empty stop as well");
    Pouch full{};
    for (std::uint16_t i = 0; i < kSlots; ++i) full[i] = Slot{static_cast<std::uint16_t>(100u + i), 1u, true};
    check(strip_step(full, 23, Direction::Forward) == kNone && strip_step(full, 0, Direction::Backward) == kNone,
        "a full pouch's strip shows the empty stop");
    full[kSpare] = Slot{9u, 1u, false};
    check(strip_step(full, 23, Direction::Forward) == 0u, "unless the spare is in use: it wraps as the game's does");
    check(strip_step(Pouch{}, 3, Direction::Forward) == kNone, "an empty pouch's strip shows nothing");
}

void write_pouch(
    psprecomp::GuestMemory &memory, std::uint16_t slot, std::uint16_t item, std::uint16_t count, bool on_bar) {
    memory.store16(kGameSys + kPouch + slot * 4u, item);
    memory.store16(kGameSys + kPouch + slot * 4u + 2u, count);
    memory.store8(kItemTable + std::uint32_t{item} * kItemRecord + 1u, on_bar ? 1u : 0u);
}

void set_up_hunter(psprecomp::GuestMemory &memory) {
    memory.store32(kGameSysPointer, kGameSys);
    memory.store8(kGameSys + kLocalHunterByte, 2u);
    memory.store16(kHunter + kHunterIndex, 2u);
    write_pouch(memory, 0, 1u, 10u, true);
    write_pouch(memory, 1, 2u, 5u, false);
    write_pouch(memory, 2, 3u, 1u, true);
}

void test_read_pouch() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    set_up_hunter(memory);
    const std::optional<Pouch> pouch = read_pouch(memory, kHunter);
    check(pouch.has_value(), "the local hunter's pouch is read");
    if (pouch) {
        check((*pouch)[0].item == 1u && (*pouch)[0].count == 10u && (*pouch)[0].on_bar, "a slot's id and count");
        check(!(*pouch)[1].on_bar, "an item the table leaves off the bar");
        check(!(*pouch)[3].on_bar && (*pouch)[3].item == 0u, "an empty slot");
        check((*pouch)[kSpare].item == 0u && (*pouch)[kSpare].count == 0u, "the spare past the pouch");
    }
    write_pouch(memory, kSpare, 1u, 4u, true);
    const std::optional<Pouch> spare = read_pouch(memory, kHunter);
    check(spare && (*spare)[kSpare].item == 1u && (*spare)[kSpare].count == 4u && !(*spare)[kSpare].on_bar,
        "the spare is read, and is never on the bar");
    write_pouch(memory, kSpare, 0u, 0u, false);
    memory.store16(kHunter + kHunterIndex, 1u);
    check(!read_pouch(memory, kHunter), "another hunter's call is left to the game");
    check(!read_pouch(memory, 0x7FFFFFF0u), "a hunter outside memory is left to the game");
    memory.store16(kHunter + kHunterIndex, 2u);
    memory.store32(kGameSysPointer, 0x7FFFFFF0u);
    check(!read_pouch(memory, kHunter), "no game state is left to the game");
}

// With the fourth pouch page: 32 slots, and the spare is slot 32.
void test_fourth_page() {
    Pouch three{};
    three.slots = kMostSlots;
    for (const std::uint16_t slot : {0, 1, 30}) three[slot] = Slot{static_cast<std::uint16_t>(100u + slot), 1u, true};
    check(scroll(three, 1, Direction::Forward) == 30u, "○ reaches an item on the fourth page");
    check(scroll(three, 30, Direction::Forward) == kMostSlots, "past the last item ○ stops on slot 32");
    check(scroll(three, kMostSlots, Direction::Backward) == 30u, "from the stop □ goes to the fourth page's item");
    check(scroll(three, 0, Direction::Backward) == kMostSlots, "before the first item □ stops on slot 32");
    check(strip_step(three, 30, Direction::Forward) == kNone, "the strip shows the stop after the last item");
    three[kMostSlots] = Slot{0x80u, 0xFFu, false}; // the bow's coatings
    check(empty_slot(three) == std::optional<std::uint16_t>{31u}, "with slot 32 in use the stop is the last free slot");

    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    set_up_hunter(memory);
    mhfu::game::pouch_slots::fourth_page() = true;
    memory.store16(mhfu::game::pouch_slots::kBlock + 27u * 4u, 1u);
    memory.store16(mhfu::game::pouch_slots::kBlock + 27u * 4u + 2u, 6u);
    const std::optional<Pouch> pouch = read_pouch(memory, kHunter);
    check(pouch && pouch->slots == kMostSlots && (*pouch)[27].item == 1u && (*pouch)[27].count == 6u &&
            (*pouch)[27].on_bar,
        "the fourth page is read from the host's block");
    check(pouch && (*pouch)[0].item == 0u, "the game's own slots are not read with the page on");
    check(slot_empty(memory, kMostSlots) && !slot_empty(memory, 27u) && !slot_empty(memory, kMostSlots + 1u),
        "slot 32 is the stop, slot 27 holds an item, and 33 is no slot");
    mhfu::game::pouch_slots::fourth_page() = false;
    check(read_pouch(memory, kHunter)->slots == kSlots, "off, the pouch is 24 slots again");
}

void write_call(psprecomp::GuestMemory &memory, std::uint32_t return_address, std::uint32_t direction) {
    memory.store32(return_address - 8u, kStepCall);
    memory.store32(return_address - 4u, direction == 0u ? kForwardDelay : kBackwardDelay);
}

void test_scroll_direction() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    check(!scroll_direction(memory, kForwardReturn, 0u), "without the quest overlay there is no scroll");
    write_call(memory, kForwardReturn, 0u);
    write_call(memory, kBackwardReturn, 1u);
    check(scroll_direction(memory, kForwardReturn, 0u) == std::optional<Direction>{Direction::Forward},
        "the ○ call goes forward");
    check(scroll_direction(memory, kBackwardReturn, 1u) == std::optional<Direction>{Direction::Backward},
        "the □ call goes back");
    check(!scroll_direction(memory, kForwardReturn, 1u), "a direction the call does not pass is someone else's");
    check(!scroll_direction(memory, kGatherReturn, 0u), "the step after a gather is left to the game");
    // Regression: game_task passes ○'s 0 with addu a2, zr, zr (rd 6, funct
    // 0x21), not addiu a2, zr, 0, and a check for the latter missed every ○.
    check(kForwardDelay == ((6u << 11) | 0x21u), "the ○ call's delay slot is addu a2, zr, zr");
    memory.store32(kForwardReturn - 4u, 0x24060000u);
    check(!scroll_direction(memory, kForwardReturn, 0u), "another overlay at that address is left alone");
}

void test_code_check() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    check(!code_matches(memory), "empty memory is not the executable");
    for (const CodeWord &word : code_signature()) memory.store32(word.address, word.word);
    check(code_matches(memory), "the traced words are the executable");
    memory.store32(code_signature()[3].address, 0u);
    check(!code_matches(memory), "one different word is another executable");
}

psprecomp::AllegrexContext call(psprecomp::Runtime &runtime, std::uint32_t return_address, std::uint16_t slot,
    std::uint32_t direction, std::uint32_t function = kStep) {
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = kHunter;
    ctx.gpr[5] = slot;
    ctx.gpr[6] = direction;
    ctx.gpr[31] = return_address;
    runtime.function_at(function)(runtime, ctx);
    check(ctx.pc == return_address, "the call returns to its caller");
    return ctx;
}

std::string guest_string(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    std::string text;
    for (std::uint32_t at = address; memory.contains(at) && memory.load8(at) != 0u && text.size() < 64u; ++at)
        text += static_cast<char>(memory.load8(at));
    return text;
}

void test_wrapper() {
    auto &settings = mhfu::settings::current();
    psprecomp::Runtime runtime(32u * 1024u * 1024u);
    psprecomp::GuestMemory &memory = runtime.memory();
    for (const CodeWord &word : code_signature()) memory.store32(word.address, word.word);
    set_up_hunter(memory);
    write_call(memory, kForwardReturn, 0u);
    write_call(memory, kBackwardReturn, 1u);
    runtime.register_function(kStep, &game_step_stand_in, "game_step");
    runtime.register_function(kHasItem, &has_item_stand_in, "game_has_item");
    runtime.register_function(kItemName, &name_stand_in, "game_item_name");
    runtime.register_function(kPack, &pack_stand_in, "game_pack");

    settings.empty_item_slot = false;
    frame(runtime);
    check(runtime.function_at(kStep) == &game_step_stand_in, "off from the start, nothing is wrapped");

    settings.empty_item_slot = true;
    frame(runtime);
    check(runtime.function_at(kStep) != &game_step_stand_in, "on, the step is wrapped");
    frame(runtime);

    game_calls = 0;
    check(call(runtime, kForwardReturn, 2, 0u).gpr[2] == kSpare && game_calls == 0,
        "○ past the last item stops on the empty stop without the game's step");
    memory.store16(kHunter + kSelectedSlot, kSpare);
    frame(runtime);
    check(call(runtime, kForwardReturn, kSpare, 0u).gpr[2] == 0u, "and ○ goes on to the first item");
    check(call(runtime, kBackwardReturn, 0, 1u).gpr[2] == kSpare, "□ before the first item stops on the empty stop");
    check(call(runtime, kGatherReturn, 2, 0u).gpr[2] == kGameAnswer && game_calls == 1,
        "the gather's step runs the game's code");
    check(call(runtime, kForwardReturn, kSpare + 1u, 0u).gpr[2] == kGameAnswer && game_calls == 2,
        "a slot past the spare runs the game's code");

    // The strip around the selection, in both layouts.
    check(call(runtime, kStripFirst, 2, 0u).gpr[2] == kNone && game_calls == 2,
        "the strip shows the empty stop after the last item");
    check(call(runtime, kStripSecondEnd, kNone, 1u).gpr[2] == 2u, "and the last item before it");
    check(call(runtime, kStripSecond, 0, 0x100u).gpr[2] == 2u, "a direction byte of 0 is forward");

    // An item leaving the pouch keeps the empty stop the scroll chose.
    check(call(runtime, kTakeReturn, kSpare, 0u).gpr[2] == kSpare && game_calls == 2,
        "taking an item keeps the empty stop selected");
    check(call(runtime, kForwardReturn, 2, 0u).gpr[2] == kSpare, "the scroll lands on the empty stop again");
    check(call(runtime, kForwardReturn, kSpare, 0u).gpr[2] == 0u, "and leaves it");
    check(call(runtime, kTakeReturn, kSpare, 0u).gpr[2] == kGameAnswer && game_calls == 3,
        "a slot the scroll did not choose is the game's to step");

    // Regression: a full pouch, or one a gather fills, keeps Nothing Selected.
    for (std::uint16_t i = 3; i < kSlots; ++i) write_pouch(memory, i, static_cast<std::uint16_t>(10u + i), 1u, true);
    check(call(runtime, kForwardReturn, 23, 0u).gpr[2] == kSpare, "a full pouch's scroll stops on the empty stop");
    check(call(runtime, kTakeReturn, kSpare, 0u).gpr[2] == kSpare && game_calls == 3,
        "which an item leaving the full pouch keeps");
    check(call(runtime, kStripFirst, 23, 0u).gpr[2] == kNone, "and the strip shows it");
    check(call(runtime, kForwardReturn, kSpare, 0u).gpr[2] == 0u &&
            call(runtime, kBackwardReturn, kSpare, 1u).gpr[2] == 23u,
        "and the scroll leaves it for the first or the last item");
    memory.store32(kGameSys + kPouch + kSpare * 4u, 0x00010009u);
    check(call(runtime, kForwardReturn, 23, 0u).gpr[2] == 0u, "with the spare in use a full pouch wraps");
    memory.store32(kGameSys + kPouch + kSpare * 4u, 0u);
    for (std::uint16_t i = 3; i < kSlots; ++i) write_pouch(memory, i, 0u, 0u, false);

    // The name under the bar.
    has_item_calls = 0;
    name_calls = 0;
    check(call(runtime, kNameGateReturn, kSpare, 0u, kHasItem).gpr[2] == 1u && has_item_calls == 0,
        "the empty stop has a name under the bar");
    check(call(runtime, kNameGateReturn, 0, 0u, kHasItem).gpr[2] == 0u && has_item_calls == 1,
        "an item is the game's to check");
    check(call(runtime, kNameGateReturn - 8u, kSpare, 0u, kHasItem).gpr[2] == 0u && has_item_calls == 2,
        "and so is every other caller");
    check(call(runtime, kNameGateReturn, kSpare + 1u, 0u, kHasItem).gpr[2] == 0u && has_item_calls == 3,
        "and a slot past the spare");
    const std::uint32_t width = call(runtime, kNameWidthReturn, 0, 0u, kItemName).gpr[2];
    const std::uint32_t print = call(runtime, kNamePrintReturn, 0, 0u, kItemName).gpr[2];
    check(width == kNothingText && print == kNothingText && name_calls == 0, "item 0 is named by the port");
    check(guest_string(memory, kNothingText) == "Nothing Selected", "Nothing Selected");
    check(call(runtime, kNamePrintReturn, 1, 0u, kItemName).gpr[2] == kGameName && name_calls == 1,
        "an item's name is the game's");
    check(call(runtime, 0x08800000u, 0, 0u, kItemName).gpr[2] == kGameName && name_calls == 2,
        "and item 0's name anywhere else");

    // The bar starts on the empty stop, once game_task is installed.
    check(runtime.function_at(kSelectFirst) == nullptr, "without game_task there is no first selection");
    runtime.register_function(kSelectFirst, &select_first_stand_in, "game_select_first");
    frame(runtime);
    check(runtime.function_at(kSelectFirst) == &select_first_stand_in, "code that is not game_task's is left alone");
    for (const CodeWord &word : select_first_signature()) memory.store32(word.address, word.word);
    check(select_first_matches(memory), "game_task's first selection is recognised");
    frame(runtime);
    check(runtime.function_at(kSelectFirst) != &select_first_stand_in, "and wrapped");
    select_first_calls = 0;
    call(runtime, 0x09A65824u, 0, 0u, kSelectFirst);
    check(select_first_calls == 1 && memory.load16(kHunter + kSelectedSlot) == kSpare,
        "the hunter starts on the empty stop");
    check(call(runtime, kTakeReturn, kSpare, 0u).gpr[2] == kSpare, "which an item leaving the pouch keeps");
    call(runtime, 0x0881CC5Cu, 1, 0u, kPack);
    check(memory.load16(kHunter + kSelectedSlot) == 0u, "packing the ammo is the game's");
    memory.store16(kHunter + kSelectedSlot, kSpare);
    call(runtime, 0x0881CC5Cu, 0, 0u, kPack);
    check(memory.load16(kHunter + kSelectedSlot) == kSpare, "packing the items keeps the empty stop");
    memory.store32(kGameSys + kPouch + kSpare * 4u, 0x00010009u);
    memory.store16(kHunter + kSelectedSlot, kSpare);
    call(runtime, 0x0881CC5Cu, 0, 0u, kPack);
    check(memory.load16(kHunter + kSelectedSlot) == 0u, "a stop that now holds something is the game's to move");
    memory.store32(kGameSys + kPouch + kSpare * 4u, 0u);
    check(call(runtime, kForwardReturn, kSpare, 0u).gpr[2] == 0u, "a scroll onto an item");
    memory.store16(kHunter + kSelectedSlot, kSpare);
    call(runtime, 0x0881CC5Cu, 0, 0u, kPack);
    check(memory.load16(kHunter + kSelectedSlot) == 0u, "leaves an empty slot the bar did not choose to the game");
    call(runtime, 0x09A65824u, 0, 0u, kSelectFirst);
    runtime.register_function(kSelectFirst, &select_first_stand_in, "game_select_first");
    frame(runtime);
    check(runtime.function_at(kSelectFirst) != &select_first_stand_in, "game_task installed again is wrapped again");
    memory.store16(kHunter + kHunterIndex, 1u);
    call(runtime, 0x09A65824u, 0, 0u, kSelectFirst);
    check(memory.load16(kHunter + kSelectedSlot) == 0u, "another hunter keeps the game's first item");
    memory.store16(kHunter + kHunterIndex, 2u);

    settings.empty_item_slot = false;
    frame(runtime);
    call(runtime, 0x09A65824u, 0, 0u, kSelectFirst);
    check(memory.load16(kHunter + kSelectedSlot) == 0u, "turned off, the hunter starts on the first item");
    check(call(runtime, kForwardReturn, 2, 0u).gpr[2] == kGameAnswer && game_calls == 4,
        "turned off, the scroll runs the game's code");
    check(call(runtime, kStripFirst, 2, 0u).gpr[2] == kGameAnswer && game_calls == 5, "and the strip");
    check(call(runtime, kNameGateReturn, kSpare, 0u, kHasItem).gpr[2] == 0u &&
            call(runtime, kNamePrintReturn, 0, 0u, kItemName).gpr[2] == kGameName,
        "and the name");
    check(runtime.function_at(kStep) != &game_step_stand_in, "and the wrapper stays in place");
}

void test_missing_functions() {
    // Before the name lookup is installed, nothing is wrapped.
    auto &settings = mhfu::settings::current();
    settings.empty_item_slot = true;
    psprecomp::Runtime runtime(32u * 1024u * 1024u);
    for (const CodeWord &word : code_signature()) runtime.memory().store32(word.address, word.word);
    runtime.register_function(kStep, &game_step_stand_in, "game_step");
    runtime.register_function(kHasItem, &has_item_stand_in, "game_has_item");
    frame(runtime);
    check(runtime.function_at(kStep) == &game_step_stand_in, "without every function nothing is wrapped");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_empty_item_slot_tests";
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_EMPTY_ITEM_SLOT", "");
    set_environment("MHFU_TRACE_ITEM_BAR", "1");
    test_scroll();
    test_strip();
    test_read_pouch();
    test_fourth_page();
    test_scroll_direction();
    test_code_check();
    test_missing_functions();
    test_wrapper();
    std::error_code ec;
    std::filesystem::remove_all(data, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "empty item slot tests passed\n";
    return 0;
}
