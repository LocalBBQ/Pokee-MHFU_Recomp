// The stores' lists: the code check, the copies and their reuse, two items in
// one list, the game's lists back, and a window too small. No game data.
#include "game/shop_stock.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using namespace mhfu::game::shop_stock;
using Ids = std::vector<std::uint16_t>;
using mhfu::game::BufferRam;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// Two stand-in features, as the knife and the Lg Backpack: one adds 58 after
// 57 in every list, the other 929 last in the village's and the hall's.
bool g_knife = true;
bool g_backpack = false;

Ids knife(std::uint32_t, Ids ids) {
    if (!g_knife) return ids;
    const auto at = std::find(ids.begin(), ids.end(), std::uint16_t{57u});
    if (at != ids.end()) ids.insert(at + 1, 58u);
    return ids;
}

Ids backpack(std::uint32_t slot, Ids ids) {
    if (g_backpack && village_or_hall(slot)) ids.push_back(929u);
    return ids;
}

constexpr std::array<Stocking, 2> kBoth{&knife, &backpack};

// The village's first list has the Boomerang, its second not; the hall's
// first list is the village's first again; a later table holds a third list.
constexpr std::uint32_t kWithBoomerang = kLists;
constexpr std::uint32_t kWithout = kLists + 0x20u;
constexpr std::uint32_t kOther = kLists + 0x40u;
constexpr std::uint32_t kLaterTable = kHallTable + 0x28u;

void write_list(BufferRam &ram, std::uint32_t list, const Ids &ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) ram.store16(list + static_cast<std::uint32_t>(i) * 2u, ids[i]);
    ram.store16(list + static_cast<std::uint32_t>(ids.size()) * 2u, kListEnd);
}

void write_lobby(BufferRam &ram) {
    for (const CodeWord &word : kSignature) ram.store32(word.address, word.word);
    write_list(ram, kWithBoomerang, {7u, 57u, 42u});
    write_list(ram, kWithout, {43u, 25u});
    write_list(ram, kOther, {57u});
    ram.store32(kVillageTable, kWithBoomerang);
    ram.store32(kVillageTable + 4u, kWithout);
    ram.store32(kVillageTable + kVillageLists * 4u, 0u);
    ram.store32(kHallTable, kWithBoomerang);
    ram.store32(kLaterTable, kOther);
}

Ids list_at(const BufferRam &ram, std::uint32_t list) {
    return read_list(ram, list).value_or(Ids{0xDEADu});
}

void test_lists() {
    BufferRam ram(0x08800000u, 0x01400000u);
    write_list(ram, kWithBoomerang, {7u, 57u});
    check(list_at(ram, kWithBoomerang) == Ids{7u, 57u}, "a list up to its end");
    for (std::uint32_t at = kTables - 8u; at < kTables; at += 2u) ram.store16(at, 7u);
    check(!read_list(ram, kTables - 8u), "a list that runs into the tables is not one");
    check(village_or_hall(kVillageTable) && village_or_hall(kVillageTable + 7u * 4u), "the village's eight lists");
    check(!village_or_hall(kVillageTable + 8u * 4u), "not its end");
    check(village_or_hall(kHallTable + 4u * 4u) && !village_or_hall(kHallTable + 5u * 4u), "the hall's five lists");
    check(!village_or_hall(kLaterTable), "nor the later tables'");
    check(kCopies >= 0x08800200u && kCopiesEnd <= 0x08802000u, "the copies stay below Item Sets' window");
}

void test_keep() {
    BufferRam ram(0x08800000u, 0x01400000u);
    write_lobby(ram);
    check(code_matches(ram), "lobby_task's words are lobby_task");

    g_knife = true;
    g_backpack = false;
    check(keep(ram, kBoth) == 2u, "a copy per list the knife changes");
    const std::uint32_t village = ram.load32(kVillageTable);
    check(village >= kCopies && village < kCopiesEnd && list_at(ram, village) == Ids{7u, 57u, 58u, 42u},
        "the village's list is a copy with the knife");
    check(ram.load32(kHallTable) == village, "the same list and outcome in two tables is one copy");
    check(ram.load32(kVillageTable + 4u) == kWithout, "a list nothing changes is the game's");
    check(list_at(ram, ram.load32(kLaterTable)) == Ids{57u, 58u}, "the later tables get the knife too");
    check(list_at(ram, kWithBoomerang) == Ids{7u, 57u, 42u}, "the game's lists are not written");

    check(keep(ram, kBoth) == 2u && ram.load32(kVillageTable) == village, "a frame later the copies are the same");

    g_backpack = true;
    keep(ram, kBoth);
    check(list_at(ram, ram.load32(kVillageTable)) == Ids{7u, 57u, 58u, 42u, 929u},
        "both items in the village's list, from the game's and not the copy");
    check(list_at(ram, ram.load32(kVillageTable + 4u)) == Ids{43u, 25u, 929u}, "the backpack alone where no knife");
    check(list_at(ram, ram.load32(kLaterTable)) == Ids{57u, 58u}, "the knife alone outside the two stores");

    g_knife = false;
    keep(ram, kBoth);
    check(list_at(ram, ram.load32(kHallTable)) == Ids{7u, 57u, 42u, 929u}, "the knife off, the backpack stays");
    check(ram.load32(kLaterTable) == kOther, "and the later table is the game's again");

    g_backpack = false;
    keep(ram, kBoth);
    check(ram.load32(kVillageTable) == kWithBoomerang && ram.load32(kVillageTable + 4u) == kWithout &&
            ram.load32(kHallTable) == kWithBoomerang,
        "both off, every table is the game's");

    // lobby_task is loaded again: the tables point to its lists.
    g_knife = true;
    keep(ram, kBoth);
    write_lobby(ram);
    keep(ram, kBoth);
    check(list_at(ram, ram.load32(kVillageTable)) == Ids{7u, 57u, 58u, 42u}, "a table loaded again gets the copies");

    // Another overlay in the slot.
    ram.store32(kSignature[1].address, 0u);
    ram.store32(kVillageTable, kWithBoomerang);
    check(keep(ram, kBoth) == 0u && ram.load32(kVillageTable) == kWithBoomerang, "nothing without lobby_task");
    for (const CodeWord &word : kSignature) ram.store32(word.address, word.word);

    // A word that is no list is left.
    ram.store32(kVillageTable + 4u, 0x09B0FFF0u);
    keep(ram, kBoth);
    check(ram.load32(kVillageTable + 4u) == 0x09B0FFF0u, "a word outside the lists is left");
}

// Each table word its own outcome, so each gets a copy of its own.
Ids numbered(std::uint32_t slot, Ids ids) {
    ids.push_back(static_cast<std::uint16_t>(slot & 0xFFFFu));
    return ids;
}

void test_full_window() {
    BufferRam ram(0x08800000u, 0x01400000u);
    write_lobby(ram);
    // The longest list the lists' room has, behind the first table words.
    const Ids longest(1400u, 9u);
    write_list(ram, kLists + 0x60u, longest);
    for (std::uint32_t slot = kVillageTable; slot < kVillageTable + 4u * 4u; slot += 4u)
        ram.store32(slot, kLists + 0x60u);
    constexpr std::array<Stocking, 1> kNumbered{&numbered};
    keep(ram, kNumbered);
    check(list_at(ram, ram.load32(kVillageTable)).size() == 1401u &&
            list_at(ram, ram.load32(kVillageTable + 4u)).size() == 1401u,
        "the copies that fit are made");
    check(ram.load32(kVillageTable + 3u * 4u) == kLists + 0x60u, "one that does not fit stays the game's");
    check(list_at(ram, ram.load32(kLaterTable)) == Ids{57u, static_cast<std::uint16_t>(kLaterTable & 0xFFFFu)},
        "a short one after it still fits");
    keep(ram, kNumbered);
    check(list_at(ram, ram.load32(kVillageTable + 4u)).back() ==
            static_cast<std::uint16_t>((kVillageTable + 4u) & 0xFFFFu),
        "and a frame later they are the same");
}

} // namespace

int main() {
    test_lists();
    test_keep();
    test_full_window();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "shop stock tests passed\n";
    return 0;
}
