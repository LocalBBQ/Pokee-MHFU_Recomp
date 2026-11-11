#pragma once

// The stores' stock, as the port's features want it: the Throwing Knife
// after the Boomerang (game/throwing_knives.hpp), the Lg Backpack by the
// Guides (game/backpack.hpp) and the tool pouches in the Pokke Point exchange
// (game/tool_pouches.hpp). They change the same tables, so one place keeps
// them: each feature only says what a list should hold.
//
// lobby_task (ULUS-10391) keeps each store's stock as lists of item ids
// ending in 0xFFFF, from 0x09B06510, one list per stage of the village or the
// hall, and tables of pointers to them from 0x09B070A8, each table ending in
// 0: the village store's at 0x09B070A8 (eight lists, read at 0x09A803A0), the
// Guild Hall's at 0x09B070D0 (five) and 0x09B070F8 (read at 0x09A8051C), and
// the Pokke Point exchange's at 0x09B07120 (eight, read at 0x09A80640), up to
// 0x09B07148. A pointer whose list a feature changes
// points to a copy in the window the host reserves below the executable
// (kernel/kernel.hpp); the word before a copy is the game's list it was made
// from, so the game's list is known again whatever the pointer is now. The
// game's lists are never written. lobby_task is loaded again each time the
// village or the hall is entered, so the tables are kept every frame while
// its code is there.

#include "game/guest_ram.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mhfu::game::shop_stock {

inline constexpr std::uint32_t kLists = 0x09B06510u;
inline constexpr std::uint32_t kTables = 0x09B070A8u;
inline constexpr std::uint32_t kTablesEnd = 0x09B07148u;
inline constexpr std::uint32_t kVillageTable = 0x09B070A8u;
inline constexpr std::uint32_t kVillageLists = 8u;
inline constexpr std::uint32_t kHallTable = 0x09B070D0u;
inline constexpr std::uint32_t kHallLists = 5u;
inline constexpr std::uint32_t kExchangeTable = 0x09B07120u;
inline constexpr std::uint32_t kExchangeLists = 8u;
inline constexpr std::uint16_t kListEnd = 0xFFFFu;

// The copies: the game's lists take about 3 KiB in all.
inline constexpr std::uint32_t kCopies = 0x08800200u;
inline constexpr std::uint32_t kCopiesEnd = 0x08802000u;

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};

inline constexpr std::array<CodeWord, 4> kSignature{{
    {0x09A803A0u, 0x3C0209B0u}, // lui v0, 0x09B0
    {0x09A803A8u, 0x244270A8u}, // addiu v0, v0, 0x70A8: the village's lists
    {0x09A8051Cu, 0x3C0209B0u}, // lui v0, 0x09B0
    {0x09A80524u, 0x244270F8u}, // addiu v0, v0, 0x70F8: the hall's lists
}};

// lobby_task is in the task slot: its code reads the tables, and they lie
// in memory.
[[nodiscard]] bool code_matches(const Ram &ram);

// A list from `list`, without its end, or nothing when it does not end
// before the tables.
[[nodiscard]] std::optional<std::vector<std::uint16_t>> read_list(const Ram &ram, std::uint32_t list);

// The table word at `slot` is one of the village store's lists, or one of the
// Guild Hall store's.
[[nodiscard]] bool village_or_hall(std::uint32_t slot);
// The table word at `slot` is one of the Pokke Point exchange's lists.
[[nodiscard]] bool exchange(std::uint32_t slot);

// A feature's change to the list the table word at `slot` points to: the
// ids it gets, the game's own, and returns them as the feature wants them.
using Stocking = std::vector<std::uint16_t> (*)(std::uint32_t slot, std::vector<std::uint16_t> ids);

// Points each table word at the game's list as `stockings` leave it, one
// after the other: at the game's own list when they change nothing, else at a
// copy, one per list and outcome. Nothing while lobby_task is not loaded. A
// copy that does not fit leaves the game's list. Returns the copies made.
std::size_t keep(Ram &ram, std::span<const Stocking> stockings);

} // namespace mhfu::game::shop_stock
