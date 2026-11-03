#pragma once

// Empty item slot: scrolling the item bar past either end stops on nothing,
// as the later games do, so □ with the weapon sheathed uses no item. On by
// default (`game.empty_item_slot`, `MHFU_EMPTY_ITEM_SLOT=0` turns it off).
//
// The executable (ULUS-10391) keeps the selected pouch slot in the halfword at
// hunter +1608. The pouch is 24 slots of 4 bytes at GameSys + 0x6AEA6 (GameSys
// is the word at 0x089CC558), an item id halfword and then a count halfword;
// 0x08856884 returns a slot's address. game_task scrolls the bar in
// 0x09A6AA84: with L held (bit 0 of hunter +1556), ○ or □ in hunter +1168
// calls 0x088D81FC(hunter, slot, direction) and stores the result at +1608.
// Direction 0 (○, return 0x09A6AAD0) goes forward, 1 (□, return 0x09A6AB28)
// back. The step acts only for the local hunter (+486 equal to the byte at
// GameSys +40), returns a slot of 24 or more as it is, and otherwise walks the
// 24 slots round from the next one to the first whose id is not 0 and whose
// record in the item table at 0x0899D2B8 (24 bytes an id) has bit 0 of byte 1
// set. It reads and returns; nothing else.
//
// A slot with id 0 is the game's own empty selection, the one a hunter with an
// empty pouch has: the bar draws no item (0x0882693C wants a count above 0 and
// an id), and □ (0x09AA7580, through 0x09AA549C) finds a count of 0, plays
// sound 21 and starts no action. With the switch on, the wrapper answers the
// two scroll calls itself. A step that would wrap round the end lands on the
// empty stop instead. From there ○ goes to the first item and □ to the last.
//
// With the fourth pouch page (game/pouch_page.hpp) the pouch is 32 slots in a
// block of the host's and the stop is slot 32, which the page's hooks give the
// address of that same word; the rest is as below with 32 for 24.
//
// The empty stop is slot 24, the word just past the pouch. 0x08856884 bounds
// a slot by 25 and so still gives its address, while the game's own walk over
// the pouch stops at 24 (0x088568F8), so no item is put there. A full pouch
// keeps the stop, and a gather that fills the last free slot does not turn
// the stop into an item. Only while that word holds nothing: otherwise the
// stop is the last free slot, and with all 24 slots taken as well the bar
// wraps as the game's does.
//
// The bar shows that stop as a slot of its own. The HUD's item bar (EBOOT
// 0x08845EA8) lays out the strip around the selection with the same step:
// up to two slots each side, by the count 0x0884706C returns (5 or more
// fills both sides), with 255 for a place that holds nothing. The HUD draws
// the frame of every place and the icon only of one that is not 255. The
// wrapper answers those calls from the same round as the scroll, with 255
// for the empty stop, so it is an empty frame between the last item and the
// first. The count reaches 0x0884706C by a jump inside its unit, so it is
// not wrapped: with 4 items or fewer and an item selected, the strip can
// leave the empty frame out, as it leaves out items it has no room for.
//
// Under the bar the HUD prints the selected item's name (0x08845AB8): only
// when 0x0882693C says the slot holds an item (return 0x08845AC4), with
// the name 0x08851528(GameSys, id) returns (returns 0x08845BB0 and
// 0x08845C00). For the empty stop the wrapper answers the first 1 and the
// name "Nothing Selected", a string kept at kNothingText. The count is 0,
// so the HUD prints no number.
//
// The empty stop is where the bar starts. game_task 0x09A68128(hunter), from
// the hunter's set-up at 0x09A6581C, selects the first slot that holds an
// item the bar shows, for the local hunter. The wrapper runs it and then
// selects the empty stop. game_task is an overlay, so the wrapper goes back
// in place each time it is installed, after a check of its code.
//
// 0x088292AC(?, pouch) packs a pouch (a1 0 is the items) and first moves a
// selection with no item to the next item (0x088295C0, from 0x08829368, a
// call inside its unit). The wrapper runs it and then selects the empty
// stop again when that was the selection before.
//
// 0x088D72A4 takes items out of the pouch; when the selected slot no longer
// holds an item it steps forward (return 0x088D7504). The empty stop the
// scroll chose stays. Every other call of the step, such as the one after a
// gather, runs the game's code.

#include "psprecomp/guest_memory.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::empty_item_slot {

inline constexpr std::uint32_t kStep = 0x088D81FCu;
inline constexpr std::uint32_t kForwardReturn = 0x09A6AAD0u;
inline constexpr std::uint32_t kBackwardReturn = 0x09A6AB28u;
inline constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
inline constexpr std::uint32_t kLocalHunterByte = 40u;
inline constexpr std::uint32_t kHunterIndex = 486u;
inline constexpr std::uint32_t kSelectedSlot = 1608u;
inline constexpr std::uint32_t kPouch = 0x6AEA6u;
inline constexpr std::uint32_t kItemTable = 0x0899D2B8u;
inline constexpr std::uint32_t kItemRecord = 24u;
inline constexpr std::uint16_t kSlots = 24u;
// The word past the pouch, the empty stop while it holds nothing.
inline constexpr std::uint16_t kSpare = kSlots;
// jal 0x088D81FC, then the direction in its delay slot: addu a2, zr, zr for
// ○ and addiu a2, zr, 1 for □.
inline constexpr std::uint32_t kStepCall = 0x0E23607Fu;
inline constexpr std::uint32_t kForwardDelay = 0x00003021u;
inline constexpr std::uint32_t kBackwardDelay = 0x24060001u;

// The HUD's strip: the step's return addresses in its two layouts, the
// animated one and the still one, and the place that holds nothing.
inline constexpr std::uint32_t kStripFirst = 0x08845FF4u;
inline constexpr std::uint32_t kStripFirstEnd = 0x08846118u;
inline constexpr std::uint32_t kStripSecond = 0x088464F4u;
inline constexpr std::uint32_t kStripSecondEnd = 0x08846590u;
inline constexpr std::uint16_t kNone = 255u;
// The name under the bar.
inline constexpr std::uint32_t kHasItem = 0x0882693Cu;
inline constexpr std::uint32_t kNameGateReturn = 0x08845AC4u;
inline constexpr std::uint32_t kItemName = 0x08851528u;
inline constexpr std::uint32_t kNameWidthReturn = 0x08845BB0u;
inline constexpr std::uint32_t kNamePrintReturn = 0x08845C00u;
// The step after an item leaves the pouch.
inline constexpr std::uint32_t kTakeReturn = 0x088D7504u;
// The pouch's packing.
inline constexpr std::uint32_t kPack = 0x088292ACu;
// game_task's first selection, at the hunter's set-up.
inline constexpr std::uint32_t kSelectFirst = 0x09A68128u;
// In the window the host reserves below the load image (kernel/kernel.hpp).
inline constexpr std::uint32_t kNothingText = 0x08803F00u;
inline constexpr char kNothingName[] = "Nothing Selected";

enum class Direction : std::uint8_t { Forward = 0, Backward = 1 };

struct Slot {
    std::uint16_t item{};
    std::uint16_t count{};
    bool on_bar{}; // an id the step stops at
};
// The slots and then the spare, never on the bar: 24 slots, or 32 with the
// fourth pouch page (game/pouch_slots.hpp), whose spare is the 25th word the
// game keeps after its own 24.
inline constexpr std::uint16_t kMostSlots = 32u;
struct Pouch {
    std::array<Slot, kMostSlots + 1u> slot{};
    std::uint16_t slots{kSlots};
    [[nodiscard]] std::uint16_t spare() const noexcept { return slots; }
    Slot &operator[](std::size_t i) noexcept { return slot[i]; }
    const Slot &operator[](std::size_t i) const noexcept { return slot[i]; }
};

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};

// What the addresses above were read off. A mismatch means another
// executable, and nothing is wrapped.
[[nodiscard]] std::span<const CodeWord> code_signature();
[[nodiscard]] bool code_matches(const psprecomp::GuestMemory &memory);

// The same for game_task's first selection, true only while that overlay is
// the one loaded.
[[nodiscard]] std::span<const CodeWord> select_first_signature();
[[nodiscard]] bool select_first_matches(const psprecomp::GuestMemory &memory);

// The direction of one of the two scroll calls, by its return address and
// the call before it, or none for any other caller.
[[nodiscard]] std::optional<Direction> scroll_direction(
    const psprecomp::GuestMemory &memory, std::uint32_t return_address, std::uint32_t direction);

// The local hunter's pouch, or none for another hunter or memory that does
// not hold one.
[[nodiscard]] std::optional<Pouch> read_pouch(const psprecomp::GuestMemory &memory, std::uint32_t hunter);

// The game's own step: the next slot round from `current` that is on the bar,
// or `current` when it is the only one. None when no slot is.
[[nodiscard]] std::optional<std::uint16_t> game_step(const Pouch &pouch, std::uint16_t current, Direction direction);

// The empty stop: the spare when it holds nothing, else the last empty slot
// (id 0, count 0). None when the spare is in use and the pouch is full.
[[nodiscard]] std::optional<std::uint16_t> empty_slot(const Pouch &pouch);

// Where a scroll from `current` lands with the empty slot in the round.
[[nodiscard]] std::uint16_t scroll(const Pouch &pouch, std::uint16_t current, Direction direction);

// True for a return address of the HUD strip's calls of the step.
[[nodiscard]] bool strip_call(std::uint32_t return_address) noexcept;

// The strip's neighbour of `current` in the same round: kNone for the empty
// stop, and from kNone or an empty slot the first or the last item. kNone
// when nothing is on the bar.
[[nodiscard]] std::uint16_t strip_step(const Pouch &pouch, std::uint16_t current, Direction direction);

// True when the slot, or the spare, holds no id and no count: the empty stop.
[[nodiscard]] bool slot_empty(const psprecomp::GuestMemory &memory, std::uint32_t slot);

// At the flip: puts the wrapper in place once the executable's code is
// installed and the switch has been on. Runs every frame; wrapping again is a
// no-op. Once in place it stays, and follows the switch.
void frame(psprecomp::Runtime &runtime);

} // namespace mhfu::game::empty_item_slot
