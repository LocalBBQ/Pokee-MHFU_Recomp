#pragma once

// Backpack Lg.: the item that gives the fourth pouch page (game/pouch_page.hpp).
// Once the hunter reaches High Rank, in the Guild (HR 4) or in the village
// (the Village Chief's 7★ quests), the village General Store and the Guild
// Hall's store sell it for 100,000z, and the shopkeeper's line says that it
// just came in. Bought, it is taken back out of the pouch or the box and the
// hunter owns the page, kept when the game next saves; the stores then show
// it greyed out, as the Guides they sell. Off with the page
// (MHFU_POUCH_PAGE=0).
//
// Read off the USA executable (ULUS-10391) and lobby_task, and checked on a
// save at HR 9 with every village level open against a new hunter:
//
// - Hunter Rank is the byte at GameSys + 0x44BC, HR - 1: 0x0887CCF0 sets it,
//   and the Guild's quest counter (lobby_task 0x09A88938) opens a level per
//   value. The game's own text has High Rank from HR 4.
// - A quest is cleared when bit i of the words at GameSys + 0x4428 is set,
//   i being the quest id's place in the u16 table at 0x0899C1C8, which ends
//   at 60000 (0x0884FE80, 0x0884FDC4). lobby_task 0x09A87780 gives the
//   highest village level the counter opens: 7★ (6, counted from 0) once
//   both of the Chief's 6★ Urgent Quests, 10501 and 10509, or a later Urgent
//   (11005, 11107, 11201) are cleared. In the quest packs (DATA.BIN 5342 to
//   5352) a record's id is the halfword at +0x18 and bit 0x02 of +3 marks
//   the Urgent Quest.
// - The item is the unused id 929 ("dummy"), given the record of the
//   Backpacker Guide (830, which the store sells too: carried one at a time,
//   not sold) with a price of 100,000z. Records are 24 bytes from 0x0899D2B8,
//   the price a u32 at +12, the selling price at +16.
// - Item names are entry id of table 1 of the game's text at 0x089CC560,
//   descriptions of table 2 (0x08851528, 0x08851534). A table is the word at
//   (index + 2) * 4 from the block, an offset; an entry is the word at
//   table + id * 4, an offset from the table. Pointing those two words at
//   strings of the host's names the item wherever the game shows it.
// - The stores' stock is a 0xFFFF-ended u16 list at *(store + 0x14), which
//   0x09A7FD50 picks by the store's type: the village store (type 1) by
//   village progress from the eight lists at 0x09B070A8, the Guild Hall's
//   (type 4) by GameSys + 0x44BD from the five at 0x09B070D0. While the item
//   is offered, those pointers are a copy of each list with the item first.
// - The store's line is a text of lobby_task's: table 0 of the text block
//   that 0x09A7B7D8 finds as resource 4 in the list at *(0x09A0B680) +
//   0x83C030 (512 entries of 12 bytes: the id, then the block). 491 is what
//   the store says when buying into the pouch and 495 into the box; while the
//   item is offered they say it just came in. 492, the pouch being full,
//   says 32 kinds of item with the fourth page.
// - The store treats the Organizer and Backpacker Guides (829, 830) apart:
//   0x09A80230 tells them, and the list greys one once it is obtained
//   (0x0885C5CC, the bits at GameSys + 0x4A0 + 0x6A7B4). The list's call of
//   it (from 0x09A7F24C, another unit) is answered yes for the item, and the
//   item counts as obtained once the hunter owns the page, so it is greyed
//   out as they are. The purchase calls 0x09A80230 inside its own unit, so it
//   adds the item as any other: the item is never taken away for being held
//   (a save may hold the unused id already), only when more of it comes while
//   it is offered. Zenny is the u32 at GameSys + 0x696F0; the box is 1000
//   slots at GameSys + 0x3428. Bought more than once, the extra items are
//   paid back.

#include "game/guest_ram.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::backpack {

inline constexpr std::uint16_t kItem = 929u;
inline constexpr std::uint16_t kTemplateItem = 830u; // the Backpacker Guide
inline constexpr std::uint32_t kPrice = 100000u;
inline constexpr std::uint32_t kMostZenny = 99999999u;

inline constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
inline constexpr std::uint32_t kHunterRank = 0x44BCu; // HR - 1
inline constexpr std::uint8_t kHighRank = 3u;         // HR 4
inline constexpr std::uint32_t kCleared = 0x4428u;
inline constexpr std::uint32_t kQuestTable = 0x0899C1C8u;
inline constexpr std::uint16_t kQuestTableEnd = 60000u;
inline constexpr std::uint32_t kMostQuests = 1024u;
inline constexpr std::uint32_t kZenny = 0x696F0u;
inline constexpr std::uint32_t kBox = 0x3428u;
inline constexpr std::uint32_t kBoxSlots = 1000u;
inline constexpr std::uint32_t kSavedPouch = 0x43C8u;
// The store's Guide check, where the list calls it, and the obtained check.
inline constexpr std::uint32_t kGuideCheck = 0x09A80230u;
inline constexpr std::uint32_t kListGuideReturn = 0x09A7F254u;
inline constexpr std::uint32_t kObtainedCheck = 0x0885C5CCu;

// The Chief's 6★ Urgent Quests, and the Urgent Quests after them.
inline constexpr std::array<std::uint16_t, 2> kSixStarUrgents{10501u, 10509u};
inline constexpr std::array<std::uint16_t, 3> kLaterUrgents{11005u, 11107u, 11201u};

inline constexpr std::uint32_t kItemTable = 0x0899D2B8u;
inline constexpr std::uint32_t kItemRecord = 24u;
inline constexpr std::uint32_t kBuyPrice = 12u;
inline constexpr std::uint32_t kSellPrice = 16u;

inline constexpr std::uint32_t kTextBlock = 0x089CC560u;
inline constexpr int kNameTable = 1;
inline constexpr int kDescriptionTable = 2;

inline constexpr std::uint32_t kResourcesPointer = 0x09A0B680u;
inline constexpr std::uint32_t kResources = 0x83C030u;
inline constexpr std::uint32_t kResourceBytes = 12u;
inline constexpr std::uint32_t kMostResources = 0x200u;
inline constexpr std::uint32_t kLobbyText = 4u;
inline constexpr int kStoreTable = 0;
inline constexpr std::uint32_t kBuyLine = 491u;
inline constexpr std::uint32_t kBuyFullLine = 492u;
inline constexpr std::uint32_t kBuyToBoxLine = 495u;

// The stock lists, as lobby_task (ULUS-10391) has them.
inline constexpr std::uint32_t kVillageStock = 0x09B070A8u;
inline constexpr std::uint32_t kHallStock = 0x09B070D0u;
inline constexpr std::array<std::uint32_t, 8> kVillageLists{
    0x09B06510u, 0x09B06580u, 0x09B065F8u, 0x09B06678u, 0x09B06710u, 0x09B067B0u, 0x09B06858u, 0x09B06900u};
inline constexpr std::array<std::uint32_t, 5> kHallLists{0x09B069B0u, 0x09B06A20u, 0x09B06A60u, 0x09B06AA0u, 0x09B06AD8u};
inline constexpr std::uint32_t kMostStock = 200u;

// What the host keeps in its reserved window (kernel/kernel.hpp), after the
// pouch page's blocks.
inline constexpr std::uint32_t kNameText = 0x08803380u;
inline constexpr std::uint32_t kDescriptionText = 0x08803390u;
inline constexpr std::uint32_t kBuyText = 0x08803400u;
inline constexpr std::uint32_t kBuyToBoxText = 0x08803480u;
inline constexpr std::uint32_t kBuyFullText = 0x08803500u;
inline constexpr std::uint32_t kStockCopies = 0x08803580u;
inline constexpr std::uint32_t kWindowEnd = 0x08803F00u;

inline constexpr std::string_view kName = "Backpack Lg.";
inline constexpr std::string_view kDescription =
    "A large backpack. Adds a\nfourth page to your pouch:\n8 more kinds of item.";
inline constexpr std::string_view kBuyLineText =
    "~C00We have a new item in stock!\n~C05Backpack Lg.~C00 will let you\ncarry more items.";
inline constexpr std::string_view kBuyToBoxLineText =
    "~C00New in stock: ~C05Backpack Lg.~C00,\nto carry more items.\n~C05The item will be automatically\nsent to "
    "your Item Box.";
inline constexpr std::string_view kBuyFullLineText = "~C00Please select an item to\npurchase. \n~C02Item Bag is full. "
                                                     "(It can\nonly carry 32 types of items.)";

// High Rank ------------------------------------------------------------------

// The quest's bit in the cleared words, or none for a quest the table lacks.
[[nodiscard]] std::optional<std::uint32_t> quest_bit(const Ram &ram, std::uint16_t quest);
[[nodiscard]] bool cleared(const Ram &ram, std::uint32_t game_sys, std::uint16_t quest);
// HR 4 or more.
[[nodiscard]] bool guild_high_rank(const Ram &ram, std::uint32_t game_sys);
// The Village Chief's 7★ quests are open.
[[nodiscard]] bool village_high_rank(const Ram &ram, std::uint32_t game_sys);
[[nodiscard]] bool high_rank(const Ram &ram, std::uint32_t game_sys);

// The item -------------------------------------------------------------------

// The item's record: the Backpacker Guide's at 100,000z, sold for nothing.
[[nodiscard]] std::array<std::uint8_t, kItemRecord> item_record(const Ram &ram);
void write_item_record(Ram &ram);

// Text tables: an entry's word, the address it points at, and pointing it
// elsewhere. Zero or false when the block does not hold that table.
[[nodiscard]] std::uint32_t table_address(const Ram &ram, std::uint32_t block, int index);
[[nodiscard]] std::uint32_t entry_word(const Ram &ram, std::uint32_t block, int index, std::uint32_t entry);
// Points the entry at `text`, or back at the game's when `text` is 0. Keeps
// the game's word in `original` the first time.
bool point_entry(Ram &ram, std::uint32_t block, int index, std::uint32_t entry, std::uint32_t text,
    std::optional<std::uint32_t> &original);
// Writes a string, zero-ended, at `address`.
void write_text(Ram &ram, std::uint32_t address, std::string_view text);
// lobby_task's text block, resource 4, or 0.
[[nodiscard]] std::uint32_t lobby_text_block(const Ram &ram);

// The stock ------------------------------------------------------------------

// Offered: each table's pointers go to copies of their lists with the item
// first, built from `free`; not offered, back to the game's lists. Only while
// a table still holds the game's lists or the copies. Returns where the
// copies end.
std::uint32_t offer_stock(Ram &ram, bool offered, std::uint32_t free);

// The purchase -----------------------------------------------------------------

// Where the item can be: the box, the pouch and the pouch as saved, a copy
// of the pouch (`slots` slots each).
struct Places {
    std::uint32_t box{};
    std::uint32_t pouch{};
    std::uint32_t saved{};
    std::uint32_t slots{};
};
// How many of the item the hunter has.
[[nodiscard]] std::uint32_t held(const Ram &ram, const Places &places);
// Takes `count` of the item out: from the pouch and its copy first, then the
// box.
void take(Ram &ram, const Places &places, std::uint32_t count);
// The zenny back for `extra` items.
void refund(Ram &ram, std::uint32_t game_sys, std::uint32_t extra);

// At the flip, after the pouch page's.
void frame(psprecomp::Runtime &runtime);

} // namespace mhfu::game::backpack
