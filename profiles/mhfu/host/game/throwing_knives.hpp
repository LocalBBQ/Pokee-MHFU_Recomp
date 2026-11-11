#pragma once

// Throwing knives: the Throwing Knife and its Poison, Sleeping, Paralyze and
// Tranq versions are no longer supply items, so they stay in the pouch after a
// quest and go in the box. The Throwing Knife is sold in the village and
// Guild Hall shops for 500z, next to the Boomerang, and a Whetstone with Iron
// Ore, Disk Stone or Earth Crystal combines into one. The coated knives are
// not sold; they are combined from a Throwing Knife as in the game. On by
// default (Mods page, `game.throwing_knives`; `MHFU_THROWING_KNIVES=0` turns
// it off). Turned off, the records, texts and shop lists are the game's again.
//
// The items. The executable (ULUS-10391) keeps a 24-byte record per item at
// 0x0899D2B8. Bit 0x04 of byte 4 marks a supply item (the five knives, the
// First-Aid Med, the Ration, the ballista and cannon shot...); the buy price
// is the word at +12 and the sell price the word at +16. The knives are items
// 58 to 62. Item texts are DATA.BIN entry 19, which the game keeps at
// 0x089CC560: a word per section from +8, each the offset of a section, and
// in a section a word per item, the offset of its text from the section.
// Section 1 is the names and section 2 the descriptions (0x08851528 and
// 0x08851534 read them), which end in " ~C05(Supply Item)" for the knives.
// The description is cut at the space or line break before the tag.
//
// The shops. Every store list with the Boomerang (57) gets the knife after
// it: game/shop_stock.hpp keeps the lists, which the Lg Backpack changes too.
//
// The combinations. The game has 172, and the count is in its code:
// - 172 records of 10 bytes at 0x089A48F0: the other item (s16), the result
//   (s16), the success rate's class (byte, 0x089A5C78), the yield's class
//   (byte), the bit that records the combination as known (byte, at GameSys
//   + 0x446C), flags (bit 0: alchemy) and a halfword the same as the bit.
//   They are grouped by the lower of the two items, and 0x089A4FA8 holds a
//   count and a first record per item.
// - The combo list, 172 pairs of items at 0x089A5984.
// 0x0885B5B8(?, a, b, mode, t0, t1) finds the record of a pair. With bit 0 of
// mode set, or t1 of -1, 0 or 1, it returns the record of any combination
// whose flags are 0, as the three new ones are. 0x0885C13C(?, &index, step,
// &a, &b, mode) moves the combo list's index (a byte) by step, round the 172,
// to the next pair with a record. The Combine screen counts its list in
// 0x088262B0, which walks the 172 and lays out the pages in the cursor at
// screen + 0x5BC (the selection at +4, rows per page at +6, the row and the
// page at +7 and +8, the pages at +9 and the rows on the last page at +10);
// 0x088264F4 fills the page shown by walking 0x0885C13C again. 0x0885B7D0
// tells whether a pair is known, and 0x0885B82C combines a pair: it rolls the
// rate, takes the two items and gives the result, finding the record by a
// jump inside its own unit. lobby_task's item box finds and combines pairs
// through the same functions (0x09ADF6E0, 0x09ADF2A8).
//
// No record or list entry is free, so the three new combinations live in the
// host's window and the functions are answered around them:
// - 0x0885C13C walks 175 entries, the new pairs as 172 to 174, and reads
//   the records itself; all its callers ask with bit 0 of mode set.
// - 0x088262B0 runs, and the count it laid out is made 175.
// - 0x0885B5B8 and 0x0885B7D0 answer a new pair with its record.
// - 0x0885B82C, for a new pair, runs with the new record in place of
//   Whetstone's one record (Whetstone + Sm Monster Bone = Boomerang), which is
//   put back when it returns, at a stop of the port's own.
// Their bits, 172 to 174, are three of the four unused at the end of the game's 22
// known bytes. The count the Guild Card's combination award checks
// (0x0885B048, 172) stays the game's.
//
// The addresses are checked against the game's code before anything is
// wrapped or written, and lobby_task's before its lists are touched.
// `MHFU_TRACE_THROWING_KNIVES=1` prints each change.

#include "psprecomp/guest_memory.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::throwing_knives {

// The items.
inline constexpr std::uint32_t kItemTable = 0x0899D2B8u;
inline constexpr std::uint32_t kItemRecord = 24u;
inline constexpr std::uint32_t kFlags = 4u;
inline constexpr std::uint8_t kSupplyItem = 0x04u;
inline constexpr std::uint32_t kBuyPrice = 12u;
inline constexpr std::uint32_t kSellPrice = 16u;
inline constexpr std::uint16_t kBoomerang = 57u;
inline constexpr std::uint16_t kThrowingKnife = 58u;
inline constexpr std::array<std::uint16_t, 5> kKnives{58u, 59u, 60u, 61u, 62u};
inline constexpr std::uint32_t kKnifePrice = 500u;
inline constexpr std::uint32_t kKnifeSellPrice = 50u; // a tenth, as the game's shop items
inline constexpr std::uint16_t kWhetstone = 153u;
inline constexpr std::uint16_t kDiskStone = 201u;
inline constexpr std::uint16_t kIronOre = 202u;
inline constexpr std::uint16_t kEarthCrystal = 203u;

// The texts.
inline constexpr std::uint32_t kText = 0x089CC560u;
inline constexpr std::uint32_t kDescriptions = 2u;
inline constexpr char kSupplyTag[] = "~C05(Supply Item)";

// The combinations.
inline constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
inline constexpr std::uint32_t kKnownBits = 0x446Cu;
inline constexpr std::uint32_t kRecords = 0x089A48F0u;
inline constexpr std::uint32_t kRecordSize = 10u;
inline constexpr std::uint32_t kIndex = 0x089A4FA8u;
inline constexpr std::uint32_t kIndexItems = 1262u; // up to the combo list
inline constexpr std::uint32_t kPairs = 0x089A5984u;
inline constexpr std::uint32_t kGameCombos = 172u;
inline constexpr std::uint32_t kAllCombos = kGameCombos + 3u;
inline constexpr std::uint32_t kSearch = 0x0885B5B8u;
inline constexpr std::uint32_t kKnown = 0x0885B7D0u;
inline constexpr std::uint32_t kCombine = 0x0885B82Cu;
inline constexpr std::uint32_t kStep = 0x0885C13Cu;
inline constexpr std::uint32_t kListOpen = 0x088262B0u;
// The Combine screen's cursor.
inline constexpr std::uint32_t kCursor = 0x5BCu;
inline constexpr std::uint32_t kSelected = 4u;
inline constexpr std::uint32_t kRows = 6u;
inline constexpr std::uint32_t kRow = 7u;
inline constexpr std::uint32_t kPage = 8u;
inline constexpr std::uint32_t kPages = 9u;
inline constexpr std::uint32_t kLastRows = 10u;

// In the window the host reserves below the load image (kernel/kernel.hpp).
inline constexpr std::uint32_t kNewRecords = 0x08802800u;
inline constexpr std::uint32_t kCombineStop = 0x08800120u;
inline constexpr std::uint32_t kListOpenStop = 0x08800124u;

struct Combo {
    std::uint16_t first{};  // the lower item
    std::uint16_t second{}; // the higher
    std::uint8_t known_bit{};
};
// Whetstone + Iron Ore, Whetstone + Disk Stone and Whetstone + Earth Crystal,
// each a Throwing Knife at the Boomerang's rate (class 2, 90%) and yield
// (class 0, one).
inline constexpr std::array<Combo, 3> kNewCombos{{
    {kWhetstone, kIronOre, 172u},
    {kWhetstone, kDiskStone, 173u},
    {kWhetstone, kEarthCrystal, 174u},
}};
inline constexpr std::uint8_t kRateClass = 2u;
inline constexpr std::uint8_t kYieldClass = 0u;

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};

// What the executable's addresses were read off. A mismatch means another
// executable.
[[nodiscard]] std::span<const CodeWord> code_signature();
[[nodiscard]] bool code_matches(const psprecomp::GuestMemory &memory);

// The 10 bytes of a new combination's record.
[[nodiscard]] std::array<std::uint8_t, kRecordSize> record_bytes(const Combo &combo);
// Where a new combination's record is kept.
[[nodiscard]] constexpr std::uint32_t new_record(std::size_t i) {
    return kNewRecords + static_cast<std::uint32_t>(i) * kRecordSize;
}
// The new combination of two items, in either order.
[[nodiscard]] std::optional<std::size_t> new_combo(std::uint32_t a, std::uint32_t b);
// The game's record of two items, as 0x0885B5B8 finds it with bit 0 of mode
// set, or 0.
[[nodiscard]] std::uint32_t game_record(const psprecomp::GuestMemory &memory, std::uint32_t a, std::uint32_t b);
// The two items of combo list entry `index`, the new ones after the game's.
[[nodiscard]] std::optional<std::array<std::int16_t, 2>> pair_at(const psprecomp::GuestMemory &memory, int index);

// The list with the knife after the Boomerang; the same list when it has no
// Boomerang or already has the knife.
[[nodiscard]] std::vector<std::uint16_t> with_knife(const std::vector<std::uint16_t> &list);
// A store's list as game/shop_stock.hpp keeps it: with the knife while the
// knives are in place and on, else as it is.
[[nodiscard]] std::vector<std::uint16_t> stock(std::uint32_t slot, std::vector<std::uint16_t> ids);

// Where a description's tag starts, the space or line break before it, or
// nothing when the text has no tag at its end.
[[nodiscard]] std::optional<std::uint32_t> tag_start(const psprecomp::GuestMemory &memory, std::uint16_t item);

// At the flip: wraps the functions once the executable is checked, and keeps
// the items and the texts as the setting says, and what stock() answers.
// Runs every frame, before shop_stock::keep.
void frame(psprecomp::Runtime &runtime);

} // namespace mhfu::game::throwing_knives
