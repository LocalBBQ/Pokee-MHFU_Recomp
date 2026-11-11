// Tool pouches: the limits by tier, the exchange's stock, the items and their
// texts, the tiers file, and the frame on a runtime with stand-ins for the
// store's functions: a purchase, a second one paid back, the greying out, a
// save and a load, and the setting turned off. No game data.
#include "game/tool_pouches.hpp"

#include "game/backpack.hpp"
#include "game/guest_layout.hpp"
#include "game/shop_stock.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {
using namespace mhfu::game::tool_pouches;
using mhfu::game::BufferRam;
namespace backpack = mhfu::game::backpack;
namespace shop_stock = mhfu::game::shop_stock;
using Ids = std::vector<std::uint16_t>;

int failures = 0;

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
    if (value.empty())
        unsetenv(name);
    else
        setenv(name, value.c_str(), 1);
#endif
}

constexpr std::uint32_t kSys = 0x09000000u;
constexpr std::uint32_t kPouch = kSys + 0x6AEA6u;

std::uint32_t record(std::uint16_t item) {
    return kItemTable + item * kItemRecord;
}

template <typename Memory> void write_items(Memory &ram) {
    for (const Tool &tool : kTools) ram.store8(record(tool.item) + kCarryByte, tool.carry);
    // The Backpacker Guide: carried one at a time, 5000z.
    ram.store8(record(kTemplateItem) + 3u, 1u);
    ram.store8(record(kTemplateItem) + 4u, 0x20u);
    ram.store8(record(kTemplateItem) + 5u, 0x24u);
    ram.store32(record(kTemplateItem) + kBuyPrice, 5000u);
}

template <typename Memory> std::uint8_t limit(const Memory &ram, std::uint16_t item) {
    return ram.load8(record(item) + kCarryByte);
}

std::string read_string(const mhfu::game::Ram &ram, std::uint32_t address) {
    std::string text;
    for (std::uint8_t c = ram.load8(address); c != 0u; c = ram.load8(++address)) text.push_back(static_cast<char>(c));
    return text;
}

void test_upgrades() {
    check(upgrade_of(930u) == &kUpgrades[0] && upgrade_of(935u) == &kUpgrades[5], "the six upgrades are 930 to 935");
    check(upgrade_of(929u) == nullptr && upgrade_of(936u) == nullptr, "the Lg Backpack and the next dummy are not");
    check(next_upgrade(Kind::Bugnet, 0u) == &kUpgrades[0] && next_upgrade(Kind::Bugnet, 1u) == &kUpgrades[1],
        "the Bugnet Pouch comes before its +1");
    check(next_upgrade(Kind::Pickaxe, 0u)->item == 932u && next_upgrade(Kind::Knife, 1u)->item == 935u,
        "each kind has its own");
    check(next_upgrade(Kind::Knife, kMostTier) == nullptr, "nothing follows the last");
    check(kUpgrades[0].price == 3500u && kUpgrades[1].price == 5000u && kUpgrades[4].price == 3500u &&
            kUpgrades[5].price == 5000u,
        "3500 points for the first of a kind, 5000 for the second");
    const Tiers tiers{1u, 0u, 2u};
    check(owned(tiers, kUpgrades[0]) && !owned(tiers, kUpgrades[1]), "one bugnet pouch owned, not its +1");
    check(!owned(tiers, kUpgrades[2]), "no pickaxe pouch");
    check(owned(tiers, kUpgrades[4]) && owned(tiers, kUpgrades[5]), "both knife pouches");
    check(!locked(tiers, kUpgrades[1]) && locked(tiers, kUpgrades[3]) && !locked(tiers, kUpgrades[2]) &&
            !locked(tiers, kUpgrades[5]),
        "a +1 is locked until the pouch before it is owned; a first pouch never is");
    check(shown_name(tiers, kUpgrades[3]) == "?????" && shown_name(tiers, kUpgrades[1]) == "Bugnet Pouch+1",
        "a locked +1 is named ?????");
    check(shown_description(tiers, kUpgrades[3]) == "Buy the Pickaxe Pouch\nfirst to unlock it." &&
            shown_description(tiers, kUpgrades[1]) == kUpgrades[1].description,
        "and says what unlocks it");
    const auto fits = [](const std::string &name, const std::string &description) {
        check(name.size() <= 16u, "a name fits the game's 16 characters");
        check(name.size() < kNameBytes && description.size() < kDescriptionBytes, "and its texts their window");
        std::size_t lines = 1u, width = 0u, widest = 0u;
        for (char c : description) {
            if (c == '\n') {
                ++lines;
                width = 0u;
            } else {
                widest = std::max(widest, ++width);
            }
        }
        check(lines <= 3u && widest <= 26u, "a description is three lines of 26 at most");
    };
    for (const Upgrade &upgrade : kUpgrades) {
        fits(shown_name(Tiers{}, upgrade), shown_description(Tiers{}, upgrade));
        fits(shown_name(Tiers{2u, 2u, 2u}, upgrade), shown_description(Tiers{2u, 2u, 2u}, upgrade));
    }
    check(kTextsEnd <= 0x08803F00u, "the texts end before the item bar's");
}

void test_limits() {
    const Tool &old_bugnet = kTools[0], &mega_bugnet = kTools[2], &iron_pickaxe = kTools[4], &knife = kTools[6];
    check(carry(old_bugnet, 0u) == 3u && carry(old_bugnet, 1u) == 4u && carry(old_bugnet, 2u) == 5u,
        "an Old Bugnet: 3, then 4, then 5");
    check(carry(mega_bugnet, 1u) == 6u && carry(mega_bugnet, 2u) == 7u, "a Mega Bugnet: 6, then 7");
    check(carry(iron_pickaxe, 0u) == 4u && carry(iron_pickaxe, 2u) == 6u, "an Iron Pickaxe: 4, then 6");
    check(carry(knife, 0u) == 5u && carry(knife, 1u) == 7u && carry(knife, 2u) == 10u, "a knife: 5, 7, then 10");
    check(carry(knife, 9u) == 10u, "a tier past the last is the last");

    BufferRam ram(0x08800000u, 0x01400000u);
    check(!game_limits(ram), "an empty item table is not the game's");
    write_items(ram);
    check(game_limits(ram), "the game's limits");
    keep_limits(ram, Tiers{1u, 2u, 1u}, true);
    check(limit(ram, 148u) == 4u && limit(ram, 149u) == 5u && limit(ram, 150u) == 6u, "bugnets one more");
    check(limit(ram, 144u) == 5u && limit(ram, 145u) == 6u && limit(ram, 146u) == 7u, "pickaxes two more");
    for (std::uint16_t item = 58u; item <= 62u; ++item) check(limit(ram, item) == 7u, "every knife 7");
    check(limit(ram, 1225u) == 0u && limit(ram, 147u) == 0u, "other items are left alone");
    check(!game_limits(ram), "raised, they are not the game's");
    keep_limits(ram, Tiers{1u, 2u, 1u}, false);
    check(game_limits(ram), "off, every limit is the game's again");
    keep_limits(ram, Tiers{}, true);
    check(game_limits(ram), "and with no pouch");
}

void test_stock() {
    check(with_upgrades({223u, 152u}) == Ids{223u, 152u, 930u, 931u, 932u, 933u, 934u, 935u},
        "every upgrade is listed, last, whatever the hunter owns");
    check(with_upgrades({931u}) == Ids{931u, 930u, 932u, 933u, 934u, 935u}, "an upgrade is never listed twice");
    check(shop_stock::exchange(shop_stock::kExchangeTable) && shop_stock::exchange(shop_stock::kExchangeTable + 28u),
        "the exchange's eight lists");
    check(!shop_stock::exchange(shop_stock::kExchangeTable + 32u) && !shop_stock::exchange(shop_stock::kVillageTable),
        "and no other store's");
    check(stock(shop_stock::kExchangeTable, {223u}) == Ids{223u}, "before a frame, nothing is listed");
}

void test_items() {
    BufferRam ram(0x08800000u, 0x01400000u);
    write_items(ram);
    const auto bytes = item_record(ram, kUpgrades[1]);
    check(bytes[3] == 1u && bytes[4] == 0x20u && bytes[5] == 0x24u, "an upgrade is the Backpacker Guide's record");
    check(bytes[kBuyPrice] == (5000u & 0xFFu) && bytes[kBuyPrice + 1u] == (5000u >> 8u), "at its own price");
    check(bytes[kSellPrice] == 0u && bytes[kSellPrice + 1u] == 0u, "sold for nothing");
}

void test_refund() {
    BufferRam ram(0x08800000u, 0x01400000u);
    ram.store32(kSys + kPoints, 100u);
    refund(ram, kSys, 2u, kUpgrades[0]);
    check(ram.load32(kSys + kPoints) == 7100u, "two extra Bugnet Pouches give 7000 points back");
    refund(ram, kSys, 0u, kUpgrades[0]);
    check(ram.load32(kSys + kPoints) == 7100u, "none extra, nothing back");
    ram.store32(kSys + kPoints, kMostPoints - 10u);
    refund(ram, kSys, 1u, kUpgrades[1]);
    check(ram.load32(kSys + kPoints) == kMostPoints, "a refund stops at the most points");
}

void test_store(const std::filesystem::path &data) {
    const std::filesystem::path path = data / "tool_pouches_store.txt";
    {
        Store store(path);
        check(store.tiers("Cid") == Tiers{}, "a hunter not in the file owns nothing");
        store.set("Cid", Tiers{1u, 0u, 2u});
        store.set("Two Words", Tiers{2u, 2u, 2u});
        store.set("Cid", Tiers{2u, 0u, 2u});
        check(store.save(), "the file is written");
    }
    {
        std::ofstream out(path, std::ios::app);
        out << "3 0 0 TooHigh\n1 1\n0 0 0 \nnot a line\n";
    }
    Store again(path);
    check(again.tiers("Cid") == Tiers{2u, 0u, 2u}, "a hunter's tiers are read back, the last set");
    check(again.tiers("Two Words") == Tiers{2u, 2u, 2u}, "a name with a space");
    check(again.tiers("TooHigh") == Tiers{}, "a tier past the last is not read");
    Store missing(data / "none" / "tool_pouches.txt");
    check(missing.tiers("Cid") == Tiers{}, "no file, nothing owned");
}

int stand_in_calls = 0;
void stand_in(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ++stand_in_calls;
    ctx.gpr[2] = 7u;
    ctx.pc = ctx.gpr[31];
}

void set_hunter(psprecomp::GuestMemory &memory, const std::u16string &name) {
    for (std::uint32_t i = 0; i < 8u; ++i)
        memory.store16(kSys + 0x4A0u + i * 2u, i < name.size() ? static_cast<std::uint16_t>(name[i]) : 0u);
}

void put_lobby(psprecomp::GuestMemory &memory, bool loaded) {
    const std::uint32_t slot = mhfu::game::layout::kTaskSlot;
    memory.store32(slot, loaded ? mhfu::game::layout::kOverlayMagic : 0u);
    memory.store32(slot + 8u, slot);
    const std::string name = "lobby_task.ovl";
    for (std::size_t i = 0; i <= name.size(); ++i)
        memory.store8(
            slot + 32u + static_cast<std::uint32_t>(i), i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0u);
}

// The exchange's eight lists, as lobby_task has them (shortened), and the
// code shop_stock checks.
void write_exchange(psprecomp::GuestMemory &memory) {
    constexpr std::uint32_t kFirstList = 0x09B07008u;
    for (std::uint32_t i = 0; i < shop_stock::kExchangeLists; ++i) {
        const std::uint32_t list = kFirstList + i * 8u;
        memory.store32(shop_stock::kExchangeTable + i * 4u, list);
        memory.store16(list, 223u);
        memory.store16(list + 2u, 152u);
        memory.store16(list + 4u, 0xFFFFu);
    }
    for (const shop_stock::CodeWord &word : shop_stock::kSignature) memory.store32(word.address, word.word);
}

Ids exchange_list(psprecomp::GuestMemory &memory, std::uint32_t i) {
    constexpr std::array<shop_stock::Stocking, 1> kStockings{&stock};
    mhfu::game::GuestRam ram(memory);
    (void)shop_stock::keep(ram, kStockings);
    Ids ids;
    const std::uint32_t list = memory.load32(shop_stock::kExchangeTable + i * 4u);
    for (std::uint32_t at = list; memory.load16(at) != 0xFFFFu && ids.size() < 40u; at += 2u)
        ids.push_back(memory.load16(at));
    return ids;
}

void test_frame(const std::filesystem::path &data) {
    auto &settings = mhfu::settings::current();
    settings.tool_pouches = true;
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    memory.store32(backpack::kGameSysPointer, kSys);
    write_items(memory);
    write_exchange(memory);
    put_lobby(memory, true);
    runtime.register_function(backpack::kGuideCheck, &stand_in, "guide_stand_in");
    runtime.register_function(backpack::kObtainedCheck, &stand_in, "obtained_stand_in");
    runtime.register_function(backpack::kPouchCount, &stand_in, "pouch_count_stand_in");
    memory.store32(backpack::kTextBlock + 3u * 4u, 0x40u);   // the names' table
    memory.store32(backpack::kTextBlock + 4u * 4u, 0x4000u); // the descriptions'
    constexpr std::uint32_t kStore = 0x08B30000u;
    memory.store32(kStore + kExchangeRows, 4u);
    const auto row = [](std::uint32_t i) { return kStore + kExchangeRow + i * kExchangeRowBytes; };
    memory.store16(row(0u) + kExchangeRowItem, 223u);
    memory.store16(row(1u) + kExchangeRowItem, 930u);
    memory.store16(row(2u) + kExchangeRowItem, 934u);
    memory.store16(row(3u) + kExchangeRowItem, 931u);
    memory.store16(row(3u) + kExchangeRowGreyed, 1u); // locked, as the list greys it
    const auto call = [&](std::uint32_t address, std::uint32_t item, std::uint32_t ra, std::uint32_t a0 = 0u) {
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[4] = 0x09B10100u;
        ctx.gpr[kExchangeListRegister] = a0;
        ctx.gpr[5] = item;
        ctx.gpr[31] = ra;
        runtime.function_at(address)(runtime, ctx);
        return ctx.gpr[2];
    };
    const auto put_pouch = [&](std::uint32_t slot, std::uint16_t item, std::uint16_t count) {
        memory.store16(kPouch + slot * 4u, item);
        memory.store16(kPouch + slot * 4u + 2u, count);
    };

    // No hunter yet: the game's limits, nothing listed.
    frame(runtime);
    check(exchange_list(memory, 0u) == Ids{223u, 152u}, "with no hunter in memory nothing is listed");

    set_hunter(memory, u"Cid");
    frame(runtime);
    check(current() == Tiers{}, "a new hunter owns no pouch");
    const Ids all{223u, 152u, 930u, 931u, 932u, 933u, 934u, 935u};
    check(exchange_list(memory, 0u) == all, "the exchange offers every upgrade");
    check(exchange_list(memory, 7u) == all, "in every list");
    check(memory.load32(record(930u) + kBuyPrice) == 3500u && memory.load32(record(931u) + kBuyPrice) == 5000u,
        "the frame writes the upgrades' records");
    {
        const mhfu::game::GuestRam ram(memory);
        const std::uint32_t names = backpack::kTextBlock + 0x40u;
        const std::uint32_t descriptions = backpack::kTextBlock + 0x4000u;
        check(read_string(ram, names + memory.load32(names + 934u * 4u)) == "Lg Knife Pouch", "the item is named");
        check(read_string(ram, descriptions + memory.load32(descriptions + 930u * 4u)) == kUpgrades[0].description,
            "and described");
        check(read_string(ram, names + memory.load32(names + 931u * 4u)) == "?????" &&
                read_string(ram, descriptions + memory.load32(descriptions + 931u * 4u)) ==
                    "Buy the Bugnet Pouch\nfirst to unlock it.",
            "a locked +1 is ?????, with what unlocks it");
    }
    check(limit(memory, 148u) == 3u && limit(memory, 58u) == 5u, "and the limits are the game's");

    stand_in_calls = 0;
    check(call(backpack::kGuideCheck, 930u, kExchangeListGuideReturn, kStore) == 1u && stand_in_calls == 0,
        "the list takes an upgrade for a Guide");
    check(call(backpack::kGuideCheck, 930u, 0x09A7FEF4u) == 7u &&
            call(backpack::kGuideCheck, 223u, kExchangeListGuideReturn) == 7u &&
            call(backpack::kGuideCheck, 930u, backpack::kListGuideReturn) == 7u,
        "the purchase's check, other items and the General Store's list get the game's answer");
    check(call(backpack::kObtainedCheck, 930u, 0u) == 0u && call(backpack::kObtainedCheck, 223u, 0u) == 7u,
        "not owned, an upgrade is not obtained");
    check(call(backpack::kObtainedCheck, 931u, 0u) == 1u, "a locked +1 counts as obtained, so it is greyed out");
    check(call(backpack::kPouchCount, 930u, kExchangePaneCountReturn) == 7u,
        "not owned, the pane counts what the pouch holds");

    // Bought: a Bugnet Pouch into the pouch.
    put_pouch(3u, 930u, 1u);
    memory.store32(kSys + kPoints, 6000u);
    frame(runtime);
    check(current() == Tiers{1u, 0u, 0u}, "bought, the hunter owns the Bugnet Pouch");
    check(memory.load32(kPouch + 3u * 4u) == 0u, "and the item is gone");
    check(memory.load16(row(1u) + kExchangeRowGreyed) == 1u && memory.load16(row(0u) + kExchangeRowGreyed) == 0u &&
            memory.load16(row(2u) + kExchangeRowGreyed) == 0u,
        "its row is greyed out at once, the others are not");
    check(memory.load16(row(3u) + kExchangeRowGreyed) == 0u, "and its +1, paid for by the points left, is buyable");
    {
        const mhfu::game::GuestRam ram(memory);
        const std::uint32_t names = backpack::kTextBlock + 0x40u;
        check(read_string(ram, names + memory.load32(names + 931u * 4u)) == "Bugnet Pouch+1" &&
                read_string(ram, names + memory.load32(names + 933u * 4u)) == "?????",
            "and named at once; the other kinds' stay locked");
    }
    check(memory.load32(kSys + kPoints) == 6000u, "one bought, nothing paid back");
    check(limit(memory, 148u) == 4u && limit(memory, 150u) == 6u && limit(memory, 144u) == 3u,
        "bugnets carry one more, pickaxes as before");
    check(call(backpack::kObtainedCheck, 930u, 0u) == 1u && call(backpack::kObtainedCheck, 931u, 0u) == 0u,
        "obtained, not its +1");
    check(call(backpack::kPouchCount, 930u, kExchangePaneCountReturn) == 1u &&
            call(backpack::kPouchCount, 930u, 0x09A84A38u) == 7u,
        "the pane counts one; the pouch's count elsewhere is the game's");
    check(exchange_list(memory, 0u) == all, "the exchange's list is the same length as before");

    // A locked +1 bought anyway is paid back whole.
    put_pouch(4u, 933u, 1u);
    frame(runtime);
    check(current() == Tiers{1u, 0u, 0u} && memory.load32(kSys + kPoints) == 6000u + 5000u,
        "a locked Pickaxe Pouch+1 is paid back whole");
    check(memory.load32(kPouch + 4u * 4u) == 0u, "and taken back");
    memory.store32(kSys + kPoints, 1000u);

    // The open store now shows the Lg Knife Pouch+1, still locked.
    memory.store16(row(3u) + kExchangeRowItem, 935u);
    memory.store16(row(3u) + kExchangeRowGreyed, 1u);

    // Two Lg Knife Pouches into the box: one is paid back.
    memory.store16(kSys + backpack::kBox + 7u * 4u, 934u);
    memory.store16(kSys + backpack::kBox + 7u * 4u + 2u, 2u);
    frame(runtime);
    check(current() == Tiers{1u, 0u, 1u}, "the Lg Knife Pouch is the hunter's");
    check(memory.load32(kSys + backpack::kBox + 7u * 4u) == 0u, "both are taken from the box");
    check(memory.load32(kSys + kPoints) == 1000u + 3500u, "and the second one's points are paid back");
    check(memory.load16(row(3u) + kExchangeRowGreyed) == 1u, "its +1 stays greyed out without 5000 points");
    for (std::uint16_t item = 58u; item <= 62u; ++item) check(limit(memory, item) == 7u, "every knife carries 7");

    // One already owned, bought again from a list opened before: paid back whole.
    put_pouch(5u, 930u, 1u);
    frame(runtime);
    check(current() == Tiers{1u, 0u, 1u} && memory.load32(kSys + kPoints) == 1000u + 2u * 3500u,
        "an upgrade already owned is paid back whole");
    check(memory.load32(kPouch + 5u * 4u) == 0u, "and taken back");

    // Outside lobby_task nothing counts as bought.
    put_lobby(memory, false);
    put_pouch(6u, 935u, 1u);
    frame(runtime);
    check(current()[2] == 1u && memory.load16(kPouch + 6u * 4u) == 935u, "outside the village nothing is bought");
    put_lobby(memory, true);
    frame(runtime);
    check(current()[2] == 1u, "and what was there before is no purchase");
    memory.store32(kPouch + 6u * 4u, 0u);
    frame(runtime);

    // Saved, then loaded: the file keeps what was saved.
    game_saved();
    check(Store(data / "tool_pouches.txt").tiers("Cid") == Tiers{1u, 0u, 1u}, "the game's save writes the tiers");
    put_pouch(2u, 931u, 1u);
    frame(runtime);
    check(current() == Tiers{2u, 0u, 1u}, "the Bugnet Pouch+1 bought");
    check(limit(memory, 148u) == 5u && limit(memory, 150u) == 7u, "bugnets carry two more");
    game_loaded();
    frame(runtime);
    check(current() == Tiers{1u, 0u, 1u}, "a save loaded without saving brings back what was saved");
    check(limit(memory, 148u) == 4u, "and its limits");

    // Another hunter.
    set_hunter(memory, u"Ana");
    frame(runtime);
    check(current() == Tiers{} && limit(memory, 58u) == 5u, "another hunter has her own pouches");
    set_hunter(memory, u"Cid");
    frame(runtime);
    check(current() == Tiers{1u, 0u, 1u}, "and the first his again");

    // Off: the game's limits, nothing listed or answered, and nothing lost.
    settings.tool_pouches = false;
    frame(runtime);
    check(game_limits(mhfu::game::GuestRam(memory)), "off, every limit is the game's");
    check(exchange_list(memory, 0u) == Ids{223u, 152u}, "and the exchange's lists are the game's");
    stand_in_calls = 0;
    check(call(backpack::kObtainedCheck, 930u, 0u) == 7u && stand_in_calls == 1, "and the store's checks the game's");
    put_pouch(4u, 932u, 1u);
    frame(runtime);
    check(memory.load16(kPouch + 4u * 4u) == 932u, "off, an upgrade is not taken");
    memory.store32(kPouch + 4u * 4u, 0u);
    settings.tool_pouches = true;
    frame(runtime);
    check(current() == Tiers{1u, 0u, 1u} && limit(memory, 148u) == 4u, "on again, the hunter's pouches are back");

    // An item table that is not the game's is left alone (a fresh runtime
    // cannot be had for the state, so the check is on the functions).
    BufferRam other(0x08800000u, 0x01400000u);
    write_items(other);
    other.store8(record(149u) + kCarryByte, 9u);
    check(!game_limits(other), "a table with other limits is not the game's");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_tool_pouches_tests";
    std::error_code stale;
    std::filesystem::remove_all(data, stale);
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_TRACE_TOOL_POUCHES", "1");
    test_upgrades();
    test_limits();
    test_stock();
    test_items();
    test_refund();
    test_store(data);
    test_frame(data);
    std::filesystem::remove_all(data, stale);
    if (failures != 0) {
        std::cerr << failures << " tool pouch check(s) failed\n";
        return 1;
    }
    std::cout << "tool pouch tests passed\n";
    return 0;
}
