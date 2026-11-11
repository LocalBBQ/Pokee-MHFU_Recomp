#pragma once

// Tool pouches: upgrades bought with Pokke Points that let the hunter carry
// more bug nets, pickaxes and throwing knives. Each kind has two, bought in
// order from the Pokke Point exchange at the farm:
//
//   Bugnet Pouch      3500 pts   one more of each bug net (Old 4, Bugnet 5, Mega 6)
//   Bugnet Pouch+1    5000 pts   two more than the game's (5, 6, 7)
//   Pickaxe Pouch     3500 pts   one more of each pickaxe (Old 4, Iron 5, Mega 6)
//   Pickaxe Pouch+1   5000 pts   two more than the game's (5, 6, 7)
//   Lg Knife Pouch    3500 pts   7 of each throwing knife
//   Lg Knife Pouch+1  5000 pts   10 of each throwing knife
//
// On by default (Mods page, `game.tool_pouches`; `MHFU_TOOL_POUCHES=0` turns
// it off). Off, the exchange sells none of them and every limit is the
// game's; what each hunter owns is kept for when it is on again.
// `MHFU_TRACE_TOOL_POUCHES=1` prints each change.
//
// Read off the USA executable (ULUS-10391) and lobby_task, on a save in the
// village:
//
// - The most of an item the pouch holds is byte 3 of its 24-byte record at
//   0x0899D2B8 (game/item_sets.hpp). The game's: Old Pickaxe (144) 3, Iron
//   Pickaxe (145) 4, Mega Pickaxe (146) 5; Old Bugnet (148) 3, Bugnet (149)
//   4, Mega Bugnet (150) 5; the five throwing knives (58 to 62) 5. Those
//   bytes are written from the hunter's pouches every frame. The Pickaxe Fish
//   and Bugnet Fish (1225, 1226), which the farm's gathering also takes, are
//   not tools the stores sell and keep the game's limit.
// - The exchange is a store of lobby_task's (type 18, 0x09A7FD50) with its
//   own copies of the General Store's list, pane and purchase
//   (game/backpack.hpp), traced with MHFU_TRACE_TOOL_POUCHES. Its list
//   (0x09A65BA8(list)) is rows of 16 bytes from list + 0x160, as many as the
//   word at + 0x10, laid out as the General Store's (the halfword greyed out
//   at + 8, the item at + 0xC), and asks the Guide check from 0x09A65CF0
//   with the list in s5; for a Guide it greys the row once obtained
//   (0x0885C5CC), else when the points fall short. The pane asks the pouch's
//   count from 0x09A67D68, the purchase the Guide check from 0x09A6645C.
//   Its stock is the eight lists of
//   the table at 0x09B07120, by village progress (0x09A80640): the Armor
//   Spheres, the jewels, the Pokke Ticket and the Gourmet BBQ Spit. A row's
//   price is the item's buy price, the word at +12 of its record, paid in
//   Pokke Points, the signed word at GameSys + 0x696E8 (lobby_task checks
//   the farm's prices against it at 0x09A94B50).
// - The upgrades are the unused ids 930 to 935 ("dummy", as the Lg Backpack's
//   929), each given the Backpacker Guide's record (830: carried one at a
//   time, not sold) with its price. Their names and descriptions are texts of
//   the host's, as the Lg Backpack's are.
// - Every exchange list ends with all six, so a list never changes length
//   while a store shows it (the store builds its rows when it opens). A +1
//   whose first pouch the hunter lacks is locked: named "?????", described as
//   what unlocks it, and greyed out. The store's Guide check, its obtained
//   check and its pane's count are answered for the upgrades as for the Lg
//   Backpack, so one owned or locked is greyed out, and one owned shows as
//   held. A row's name is a pointer to the item's text, so rewriting the text
//   renames the row in a store already open.
// - Bought: the item is taken back out of the pouch or the box and the hunter
//   owns the upgrade. In the open store its row is greyed out, and the +1 it
//   unlocks is named and made buyable when the points cover it. More than one
//   bought at once gives the extra points back, as does a locked one.
//
// What each hunter owns is kept by hunter name in tool_pouches.txt, written
// when the game saves, so a game quit without saving keeps what was saved
// (the points spent are not saved either).

#include "game/guest_ram.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psprecomp {
class Runtime;
class GuestMemory;
} // namespace psprecomp

namespace mhfu::game::tool_pouches {

enum class Kind : std::uint8_t { Bugnet, Pickaxe, Knife };
inline constexpr std::size_t kKinds = 3u;
inline constexpr std::uint8_t kMostTier = 2u;

// What the hunter owns of each kind, 0 to kMostTier, in Kind's order.
using Tiers = std::array<std::uint8_t, kKinds>;

struct Upgrade {
    std::uint16_t item{};
    Kind kind{};
    std::uint8_t tier{}; // 1 or 2
    std::uint32_t price{};
    std::string_view name;
    std::string_view description;
};
// Names of at most 16 characters, descriptions of three lines of at most 26,
// as the game's own.
inline constexpr std::array<Upgrade, 6> kUpgrades{{
    {930u, Kind::Bugnet, 1u, 3500u, "Bugnet Pouch", "A pouch with room for one\nmore of each kind of\nbugnet."},
    {931u, Kind::Bugnet, 2u, 5000u, "Bugnet Pouch+1", "A roomier pouch: carry two\nmore of each kind of\nbugnet."},
    {932u, Kind::Pickaxe, 1u, 3500u, "Pickaxe Pouch", "A pouch with room for one\nmore of each kind of\npickaxe."},
    {933u, Kind::Pickaxe, 2u, 5000u, "Pickaxe Pouch+1", "A roomier pouch: carry two\nmore of each kind of\npickaxe."},
    {934u, Kind::Knife, 1u, 3500u, "Lg Knife Pouch", "A large pouch with room\nfor 7 of each kind of\nthrowing knife."},
    {935u, Kind::Knife, 2u, 5000u, "Lg Knife Pouch+1",
        "The largest pouch, with\nroom for 10 of each kind\nof throwing knife."},
}};

struct Tool {
    std::uint16_t item{};
    Kind kind{};
    std::uint8_t carry{}; // the game's
};
inline constexpr std::array<Tool, 11> kTools{{
    {148u, Kind::Bugnet, 3u},
    {149u, Kind::Bugnet, 4u},
    {150u, Kind::Bugnet, 5u},
    {144u, Kind::Pickaxe, 3u},
    {145u, Kind::Pickaxe, 4u},
    {146u, Kind::Pickaxe, 5u},
    {58u, Kind::Knife, 5u},
    {59u, Kind::Knife, 5u},
    {60u, Kind::Knife, 5u},
    {61u, Kind::Knife, 5u},
    {62u, Kind::Knife, 5u},
}};
// The knives' limit by tier.
inline constexpr std::array<std::uint8_t, kMostTier + 1u> kKnifeCarry{5u, 7u, 10u};

inline constexpr std::uint16_t kTemplateItem = 830u; // the Backpacker Guide
inline constexpr std::uint32_t kItemTable = 0x0899D2B8u;
inline constexpr std::uint32_t kItemRecord = 24u;
inline constexpr std::uint32_t kCarryByte = 3u;
inline constexpr std::uint32_t kBuyPrice = 12u;
inline constexpr std::uint32_t kSellPrice = 16u;
inline constexpr std::uint32_t kPoints = 0x696E8u; // GameSys +, signed

// The exchange's list, where its calls return, and its rows.
inline constexpr std::uint32_t kExchangeListGuideReturn = 0x09A65CF8u;
inline constexpr std::uint32_t kExchangeListRegister = 21u; // s5
inline constexpr std::uint32_t kExchangePaneCountReturn = 0x09A67D70u;
inline constexpr std::uint32_t kExchangeRows = 0x10u;
inline constexpr std::uint32_t kExchangeRow = 0x160u;
inline constexpr std::uint32_t kExchangeRowBytes = 16u;
inline constexpr std::uint32_t kExchangeRowGreyed = 8u;
inline constexpr std::uint32_t kExchangeRowItem = 0xCu;
inline constexpr std::uint32_t kExchangeMostRows = 1261u;
inline constexpr std::uint32_t kMostPoints = 99999999u;

// The texts, in the window the host reserves below the load image
// (kernel/kernel.hpp), after the Lg Backpack's.
inline constexpr std::uint32_t kNames = 0x08803640u;
inline constexpr std::uint32_t kNameBytes = 0x20u;
inline constexpr std::uint32_t kDescriptions = kNames + static_cast<std::uint32_t>(kUpgrades.size()) * kNameBytes;
inline constexpr std::uint32_t kDescriptionBytes = 0x60u;
inline constexpr std::uint32_t kTextsEnd =
    kDescriptions + static_cast<std::uint32_t>(kUpgrades.size()) * kDescriptionBytes;

// The upgrade an item is, or none.
[[nodiscard]] const Upgrade *upgrade_of(std::uint16_t item);
// The upgrade of `kind` after `tier`, or none once the kind is complete.
[[nodiscard]] const Upgrade *next_upgrade(Kind kind, std::uint8_t tier);
// The hunter owns the upgrade.
[[nodiscard]] bool owned(const Tiers &tiers, const Upgrade &upgrade);
// The hunter lacks the upgrade before it: shown as "?????" and greyed out.
[[nodiscard]] bool locked(const Tiers &tiers, const Upgrade &upgrade);
// The name and description the hunter sees.
inline constexpr std::string_view kLockedName = "?????";
[[nodiscard]] std::string shown_name(const Tiers &tiers, const Upgrade &upgrade);
[[nodiscard]] std::string shown_description(const Tiers &tiers, const Upgrade &upgrade);
// The most of a tool the pouch holds with `tier` of its kind.
[[nodiscard]] std::uint8_t carry(const Tool &tool, std::uint8_t tier);

// An upgrade's record: the Backpacker Guide's at its price, sold for nothing.
[[nodiscard]] std::array<std::uint8_t, kItemRecord> item_record(const Ram &ram, const Upgrade &upgrade);
// The game's limits are in the item table: else it is not the game's table,
// and nothing is changed.
[[nodiscard]] bool game_limits(const Ram &ram);
// Writes each tool's limit for `tiers`; the game's with `on` false.
void keep_limits(Ram &ram, const Tiers &tiers, bool on);

// An exchange list with every upgrade at its end.
[[nodiscard]] std::vector<std::uint16_t> with_upgrades(std::vector<std::uint16_t> ids);
// A store's list as game/shop_stock.hpp keeps it: the exchange's with the
// upgrades while the pouches are on, else as it is.
[[nodiscard]] std::vector<std::uint16_t> stock(std::uint32_t slot, std::vector<std::uint16_t> ids);

// Greys the item's row out in the exchange's list at `list`, or with `greyed`
// false makes it buyable again. False when there is no such list.
bool grey_out(Ram &ram, std::uint32_t list, std::uint16_t item, bool greyed = true);

// Pokke Points back for `count` of an upgrade bought beyond the first.
void refund(Ram &ram, std::uint32_t game_sys, std::uint32_t count, const Upgrade &upgrade);

// Every hunter's tiers, by name, in a text file: a line per hunter, the
// three tiers and the name.
class Store {
public:
    explicit Store(std::filesystem::path path) : path_(std::move(path)) {}
    [[nodiscard]] Tiers tiers(const std::string &hunter);
    void set(const std::string &hunter, const Tiers &tiers);
    // False when the file could not be written.
    [[nodiscard]] bool save();

private:
    void load_file();
    std::filesystem::path path_;
    bool loaded_{};
    std::vector<std::pair<std::string, Tiers>> hunters_;
};

// At the flip, after the Lg Backpack's.
void frame(psprecomp::Runtime &runtime);
// The game wrote its save: the hunter's tiers go into tool_pouches.txt.
void game_saved();
// The game read a save: the tiers are read again once the hunter is in memory.
void game_loaded();
// The hunter in memory's tiers.
[[nodiscard]] Tiers current();

} // namespace mhfu::game::tool_pouches
