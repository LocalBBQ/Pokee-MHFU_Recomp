#pragma once

// Lg Backpack: the item that gives the fourth pouch page (game/pouch_page.hpp).
// Once the hunter reaches High Rank, in the Guild (HR 4) or in the village
// (the Village Chief's 7★ quests), the village General Store and the Guild
// Hall's store sell it for 100,000z, next to the Backpacker Guide. The
// village shopkeeper has news of it, once, as she has of her other new
// stock. Bought, it is taken
// back out of the pouch or the box and the hunter owns the page, kept when
// the game next saves; the store greys it out at once, as the Guides it
// sells. Off with the page (MHFU_POUCH_PAGE=0).
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
//   is offered, those lists have the item after the Backpacker Guide (830),
//   else the Organizer Guide (829), else last: the village's later lists have
//   both, its earlier ones the Organizer Guide alone, the Guild Hall's
//   neither. game/shop_stock.hpp keeps the lists, which the Throwing Knife
//   changes too.
// - The store's line is a text of lobby_task's: table 0 of the text block
//   that 0x09A7B7D8 finds as resource 4 in the list at *(0x09A0B680) +
//   0x83C030 (512 entries of 12 bytes: the id, then the block). 492, what
//   the store says with the pouch full, says 32 kinds of item with the
//   fourth page. 491 and 495, its prompt to buy into the pouch and into the
//   box, stay the game's: they show the whole time the list is open.
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
// - The list (0x09A7F038) is rows of 16 bytes from store + 0x6C, as many as
//   the word at + 0x48: the price, the name, the halfword greyed out at + 8
//   and the item at + 0xC; the byte at + 0x24 is 0 when buying. It is built
//   when the store opens, so once bought the item's row is greyed out by
//   hand, in the store the list's Guide check last asked about.
// - Beside the list, the store's pane (lobby_task 0x09A83208) shows how many
//   the hunter has: for a Guide whether it is obtained, else the pouch's
//   count (0x0885AED0, called from 0x09A832B0) and the box's, in red at the
//   most the item's record lets the pouch hold, one for the Guides. Its Guide
//   check is inside its own unit, so the pouch's count is answered instead:
//   one once the hunter owns the page, as for an obtained Guide.
// - A talk (lobby_task 0x09A95D08) takes the NPC's type from + 0x480 into
//   + 0x3B and asks the type's lines (0x09A95D90) for its line at + 0x3C. The
//   village store's (type 1, 0x09A98F20(talk, npc, check)) are its news, once
//   per stage of village progress, else 0, its greeting. Checking, when the
//   village is entered, news sets the NPC's byte at + 0x484, which shows it
//   has some. A line's text is 0x09A9F520(talk, line), from the record of
//   the type at + 0x2C; the talk counts the characters shown at + 0x34, from
//   0, -1 once all are. Both are called from other units, so the news is the
//   store's greeting, the first time it is said once the item is offered.

#include "game/guest_ram.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::backpack {

inline constexpr std::uint16_t kItem = 929u;
inline constexpr std::uint16_t kTemplateItem = 830u; // the Backpacker Guide
inline constexpr std::uint16_t kOrganizerGuide = 829u;
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
// The pouch's count of an item, and where the store's pane calls it.
inline constexpr std::uint32_t kPouchCount = 0x0885AED0u;
inline constexpr std::uint32_t kPaneCountReturn = 0x09A832B8u;

// The store's list.
inline constexpr std::uint32_t kStoreMode = 0x24u;
inline constexpr std::uint32_t kStoreRows = 0x48u;
inline constexpr std::uint32_t kStoreRow = 0x6Cu;
inline constexpr std::uint32_t kRowBytes = 16u;
inline constexpr std::uint32_t kRowGreyed = 8u;
inline constexpr std::uint32_t kRowItem = 0xCu;
inline constexpr std::uint32_t kMostRows = 0x4EDu;

// The village store's lines, a talk's text, and the talk and NPC fields.
inline constexpr std::uint32_t kStoreLines = 0x09A98F20u;
inline constexpr std::uint32_t kTalkText = 0x09A9F520u;
inline constexpr std::uint32_t kTalkRecord = 0x2Cu;
inline constexpr std::uint32_t kTalkShown = 0x34u;
inline constexpr std::uint32_t kTalkType = 0x3Bu;
inline constexpr std::uint32_t kNpcNews = 0x484u;
inline constexpr std::uint8_t kVillageStore = 1u;
inline constexpr std::uint32_t kGreeting = 0u;

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
inline constexpr std::uint32_t kBuyFullLine = 492u;

// What the host keeps in its reserved window (kernel/kernel.hpp), after the
// pouch page's blocks.
inline constexpr std::uint32_t kNameText = 0x08803390u;
inline constexpr std::uint32_t kDescriptionText = 0x088033A0u;
inline constexpr std::uint32_t kBuyFullText = 0x08803500u;
inline constexpr std::uint32_t kNewsText = 0x08803580u;
inline constexpr std::uint32_t kTextsEnd = 0x08803640u;

inline constexpr std::string_view kName = "Lg Backpack";
// As the game's own: three lines of at most 26 characters.
inline constexpr std::string_view kDescription =
    "A sturdy pack left behind\nby ancestors. It's fit for\na Wyvernian.";
// As wide as the shopkeeper's own lines: 22 characters, at most 7 lines.
inline constexpr std::string_view kNewsLineText = "Welcome! We have a new\nitem in stock: the Lg\nBackpack! It "
                                                  "will let\nyou carry more items,\nwith a fourth page in\nyour "
                                                  "Item Pouch.";
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

// The list with the item after the Backpacker Guide, else the Organizer
// Guide, else last.
[[nodiscard]] std::vector<std::uint16_t> with_item(std::vector<std::uint16_t> ids);
// A store's list as game/shop_stock.hpp keeps it: from High Rank on, the
// village store's and the Guild Hall's with the item, else as it is.
[[nodiscard]] std::vector<std::uint16_t> stock(std::uint32_t slot, std::vector<std::uint16_t> ids);
// Greys the item's row out in a store's list of things to buy. False when
// there is no such list at `store`.
bool grey_out(Ram &ram, std::uint32_t store);

// The news ---------------------------------------------------------------------

// The talk is with the village store.
[[nodiscard]] bool village_store_talk(const Ram &ram, std::uint32_t talk);

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
