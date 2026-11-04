// Backpack Lg.: High Rank in the Guild and the village, the item's record and
// text, the stores' stock and line, and the purchase, on stand-ins for the
// game's memory. Run from pouch_page_tests.cpp. No game data.
#include "game/backpack.hpp"

#include "game/guest_layout.hpp"
#include "game/pouch_page.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mhfu::game::backpack;
using mhfu::game::BufferRam;

int *failures_ = nullptr;

void check(bool condition, const char *message) {
    if (!condition) {
        ++*failures_;
        std::cerr << "FAIL: " << message << '\n';
    }
}

constexpr std::uint32_t kSys = 0x09000000u;

template <typename Memory> void write_quests(Memory &ram, const std::vector<std::uint16_t> &ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) ram.store16(kQuestTable + static_cast<std::uint32_t>(i) * 2u, ids[i]);
    ram.store16(kQuestTable + static_cast<std::uint32_t>(ids.size()) * 2u, kQuestTableEnd);
}

template <typename Memory> void clear_quest(Memory &ram, std::uint32_t bit) {
    const std::uint32_t word = kSys + kCleared + (bit >> 5u) * 4u;
    ram.store32(word, ram.load32(word) | (1u << (bit & 31u)));
}

std::string read_string(const mhfu::game::Ram &ram, std::uint32_t address) {
    std::string text;
    for (std::uint8_t c; (c = ram.load8(address)) != 0u && text.size() < 256u; ++address) text += static_cast<char>(c);
    return text;
}

void test_high_rank() {
    BufferRam ram(0x08800000u, 0x01400000u);
    // 40 quests before the ones that count, so their bits are in the second word.
    std::vector<std::uint16_t> ids(40, 1u);
    ids.insert(ids.end(), {10501u, 10509u, 11005u, 11107u, 11201u});
    write_quests(ram, ids);
    check(quest_bit(ram, 10509u) == 41u && !quest_bit(ram, 4242u), "a quest's bit is its place in the table");
    check(!high_rank(ram, kSys), "a new hunter is not High Rank");
    ram.store8(kSys + kHunterRank, 2u);
    check(!guild_high_rank(ram, kSys), "HR 3 is not High Rank");
    ram.store8(kSys + kHunterRank, 3u);
    check(guild_high_rank(ram, kSys) && high_rank(ram, kSys), "HR 4 is High Rank");
    ram.store8(kSys + kHunterRank, 0u);
    clear_quest(ram, 40u);
    check(cleared(ram, kSys, 10501u) && !village_high_rank(ram, kSys), "one 6★ Urgent does not open 7★");
    clear_quest(ram, 41u);
    check(village_high_rank(ram, kSys) && high_rank(ram, kSys), "both 6★ Urgents open 7★");
    BufferRam later(0x08800000u, 0x01400000u);
    write_quests(later, ids);
    clear_quest(later, 43u);
    check(village_high_rank(later, kSys), "a later village Urgent means 7★ is open");
}

void test_item_and_text() {
    BufferRam ram(0x08800000u, 0x01400000u);
    const std::uint32_t from = kItemTable + kTemplateItem * kItemRecord;
    for (std::uint32_t i = 0; i < kItemRecord; ++i) ram.store8(from + i, static_cast<std::uint8_t>(i + 1u));
    write_item_record(ram);
    const std::uint32_t to = kItemTable + kItem * kItemRecord;
    check(ram.load8(to + 3u) == 4u && ram.load8(to + 5u) == 6u, "the item takes the Backpacker Guide's record");
    check(ram.load32(to + kBuyPrice) == kPrice && ram.load32(to + kSellPrice) == 0u, "it costs 100,000z, sells for 0");

    // A text block: table 1 at +0x40, entries as offsets from the table.
    constexpr std::uint32_t kBlock = 0x08900000u;
    const std::uint32_t table = kBlock + 0x40u;
    ram.store32(kBlock + 3u * 4u, 0x40u);
    ram.store32(table + kItem * 4u, 0x2000u);
    write_text(ram, table + 0x2000u, "dummy");
    write_text(ram, kNameText, kName);
    check(table_address(ram, kBlock, 1) == table && table_address(ram, kBlock, 0) == 0u, "a table is found by index");
    std::optional<std::uint32_t> original;
    check(point_entry(ram, kBlock, 1, kItem, kNameText, original), "the entry was pointed");
    check(read_string(ram, table + ram.load32(table + kItem * 4u)) == kName && original == 0x2000u,
        "the entry reads the host's name, and the game's word is kept");
    check(point_entry(ram, kBlock, 1, kItem, 0u, original) && ram.load32(table + kItem * 4u) == 0x2000u,
        "pointed back, the entry is the game's again");
    std::optional<std::uint32_t> none;
    check(!point_entry(ram, kBlock, 0, kItem, kNameText, none) && !none, "a missing table is left alone");

    // lobby_task's text: resource 4 in the list.
    constexpr std::uint32_t kList = 0x08A00000u;
    ram.store32(kResourcesPointer, kList);
    check(lobby_text_block(ram) == 0u, "no resource 4, no block");
    ram.store32(kList + kResources + 3u * kResourceBytes, kLobbyText);
    ram.store32(kList + kResources + 3u * kResourceBytes + 4u, kBlock);
    check(lobby_text_block(ram) == kBlock, "resource 4 is lobby_task's text");
}

std::vector<std::uint16_t> read_ids(const mhfu::game::Ram &ram, std::uint32_t list) {
    std::vector<std::uint16_t> ids;
    for (std::uint32_t i = 0; i < 300u && ram.load16(list + i * 2u) != 0xFFFFu; ++i)
        ids.push_back(ram.load16(list + i * 2u));
    return ids;
}

template <typename Memory> void write_stock(Memory &ram) {
    const auto put = [&](std::uint32_t table, std::span<const std::uint32_t> lists) {
        for (std::size_t i = 0; i < lists.size(); ++i) {
            ram.store32(table + static_cast<std::uint32_t>(i) * 4u, lists[i]);
            ram.store16(lists[i], static_cast<std::uint16_t>(7u + i));
            ram.store16(lists[i] + 2u, 11u);
            ram.store16(lists[i] + 4u, 0xFFFFu);
        }
    };
    put(kVillageStock, kVillageLists);
    put(kHallStock, kHallLists);
}

void test_stock() {
    BufferRam ram(0x08800000u, 0x01400000u);
    write_stock(ram);
    const std::uint32_t end = offer_stock(ram, true, kStockCopies);
    check(end > kStockCopies && end <= kWindowEnd, "the copies fit the window");
    const std::uint32_t village = ram.load32(kVillageStock + 2u * 4u);
    check(village != kVillageLists[2] && read_ids(ram, village) == std::vector<std::uint16_t>{kItem, 9u, 11u},
        "offered, the village store's list has the item first");
    check(read_ids(ram, ram.load32(kHallStock + 4u * 4u)) == std::vector<std::uint16_t>{kItem, 11u, 11u},
        "and so has the Guild Hall's");
    check(offer_stock(ram, true, kStockCopies) == end && ram.load32(kVillageStock + 2u * 4u) == village,
        "offering again keeps the same copies");
    offer_stock(ram, false, kStockCopies);
    check(ram.load32(kVillageStock + 2u * 4u) == kVillageLists[2] && ram.load32(kHallStock) == kHallLists[0],
        "not offered, the game's lists are back");
    ram.store32(kHallStock, 0x09B0FFF0u); // another overlay's data
    offer_stock(ram, true, kStockCopies);
    check(ram.load32(kHallStock) == 0x09B0FFF0u && ram.load32(kHallStock + 4u) == kHallLists[1],
        "a table that is not the game's is left alone");
    check(ram.load32(kVillageStock) != kVillageLists[0], "while the other table still gets the item");
}

void test_purchase() {
    BufferRam ram(0x08800000u, 0x01400000u);
    const Places places{kSys + kBox, kSys + 0x6AEA6u, kSys + kSavedPouch, 24u};
    ram.store16(kSys + kBox + 10u * 4u, kItem);
    ram.store16(kSys + kBox + 10u * 4u + 2u, 99u);
    ram.store16(kSys + kBox + 11u * 4u, kItem);
    ram.store16(kSys + kBox + 11u * 4u + 2u, 2u);
    ram.store16(places.pouch + 3u * 4u, kItem);
    ram.store16(places.pouch + 3u * 4u + 2u, 1u);
    ram.store16(places.saved + 3u * 4u, kItem);
    ram.store16(places.saved + 3u * 4u + 2u, 1u);
    ram.store16(places.pouch + 4u * 4u, 7u);
    ram.store16(places.pouch + 4u * 4u + 2u, 5u);
    check(held(ram, places) == 102u, "the box and the pouch count, the saved copy does not count twice");
    take(ram, places, 2u);
    check(ram.load32(places.pouch + 3u * 4u) == 0u && ram.load32(places.saved + 3u * 4u) == 0u,
        "taking starts with the pouch and its copy");
    check(ram.load16(kSys + kBox + 10u * 4u + 2u) == 98u && ram.load16(kSys + kBox + 11u * 4u + 2u) == 2u,
        "and takes the rest from the box, a stack at a time");
    check(
        ram.load16(places.pouch + 4u * 4u) == 7u && ram.load16(places.pouch + 4u * 4u + 2u) == 5u, "other items stay");
    take(ram, places, 100u);
    check(held(ram, places) == 0u, "taking stops when there is none left");
    ram.store32(kSys + kZenny, 1000u);
    refund(ram, kSys, 2u);
    check(ram.load32(kSys + kZenny) == 1000u + 2u * kPrice, "extra items are paid back");
    refund(ram, kSys, 0u);
    check(ram.load32(kSys + kZenny) == 1000u + 2u * kPrice, "none extra, nothing back");
    ram.store32(kSys + kZenny, kMostZenny - 5u);
    refund(ram, kSys, 3u);
    check(ram.load32(kSys + kZenny) == kMostZenny, "a refund stops at the most zenny");
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

// One frame on a runtime with lobby_task's stand-ins: offered at HR 4, the
// line and the stock change; bought, the page is the hunter's and the item
// is gone.
void test_frame() {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    memory.store32(kGameSysPointer, kSys);
    set_hunter(memory, u"Cid");
    write_quests(memory, {10501u, 10509u});
    write_stock(memory);
    // A save that holds the unused id already: never taken for a purchase.
    memory.store16(kSys + kBox + 5u * 4u, kItem);
    memory.store16(kSys + kBox + 5u * 4u + 2u, 99u);
    // lobby_task in the task slot, and stand-ins for the two checks.
    const std::uint32_t slot = mhfu::game::layout::kTaskSlot;
    memory.store32(slot, mhfu::game::layout::kOverlayMagic);
    memory.store32(slot + 8u, slot);
    const std::string name = "lobby_task.ovl";
    for (std::size_t i = 0; i <= name.size(); ++i)
        memory.store8(
            slot + 32u + static_cast<std::uint32_t>(i), i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0u);
    runtime.register_function(kGuideCheck, &stand_in, "guide_stand_in");
    runtime.register_function(kObtainedCheck, &stand_in, "obtained_stand_in");
    // Its text: table 0 with lines 491, 492 and 495.
    constexpr std::uint32_t kList = 0x08A00000u, kBlock = 0x08A80000u;
    memory.store32(kResourcesPointer, kList);
    memory.store32(kList + kResources, kLobbyText);
    memory.store32(kList + kResources + 4u, kBlock);
    memory.store32(kBlock + 2u * 4u, 0x40u);
    const std::uint32_t table = kBlock + 0x40u;
    for (std::uint32_t line : {kBuyLine, kBuyFullLine, kBuyToBoxLine}) memory.store32(table + line * 4u, 0x4000u);
    // The item text table, so the name can be pointed.
    memory.store32(kTextBlock + 3u * 4u, 0x40u);
    const auto call = [&](std::uint32_t address, std::uint32_t item, std::uint32_t ra) {
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[5] = item;
        ctx.gpr[31] = ra;
        runtime.function_at(address)(runtime, ctx);
        return ctx.gpr[2];
    };

    mhfu::game::pouch_page::frame(runtime);
    frame(runtime);
    check(memory.load32(kItemTable + kItem * kItemRecord + kBuyPrice) == kPrice, "the frame writes the item's record");
    check(memory.load32(kVillageStock) == kVillageLists[0] && memory.load32(table + kBuyLine * 4u) == 0x4000u,
        "below High Rank nothing is offered");
    check(memory.load16(kSys + kBox + 5u * 4u + 2u) == 99u, "the items the save held stay");

    memory.store8(kSys + kHunterRank, 3u);
    frame(runtime);
    check(memory.load32(kVillageStock) != kVillageLists[0], "at HR 4 the store sells the item");
    check(memory.load32(table + kBuyLine * 4u) == kBuyText - table &&
            memory.load32(table + kBuyToBoxLine * 4u) == kBuyToBoxText - table,
        "and the shopkeeper says it just came in");
    check(memory.load32(table + kBuyFullLine * 4u) == 0x4000u, "without the page the full pouch says 24");
    check(memory.load16(kSys + kBox + 5u * 4u + 2u) == 99u, "what the save held is still not a purchase");
    stand_in_calls = 0;
    check(
        call(kGuideCheck, kItem, kListGuideReturn) == 1u && stand_in_calls == 0, "the list takes the item for a Guide");
    check(call(kGuideCheck, kItem, 0x09A7FEF4u) == 7u && call(kGuideCheck, 829u, kListGuideReturn) == 7u &&
            stand_in_calls == 2,
        "the purchase and the real Guides get the game's answer");
    check(call(kObtainedCheck, kItem, 0u) == 0u && call(kObtainedCheck, 12u, 0u) == 7u,
        "not owned, the item is not obtained; other items are the game's");

    memory.store16(kSys + 0x6AEA6u + 2u * 4u, kItem);
    memory.store16(kSys + 0x6AEA6u + 2u * 4u + 2u, 1u);
    frame(runtime);
    check(mhfu::game::pouch_page::owned() && mhfu::game::pouch_page::active(), "bought, the page is the hunter's");
    check(memory.load32(mhfu::game::pouch_page::kPouch + 2u * 4u) == 0u &&
            memory.load16(kSys + kBox + 5u * 4u + 2u) == 99u,
        "the bought item is gone, the save's own stay");
    frame(runtime);
    check(memory.load32(kVillageStock) != kVillageLists[0], "owned, the store still lists it");
    check(call(kObtainedCheck, kItem, 0u) == 1u, "obtained, so the list greys it out");
    check(memory.load32(table + kBuyLine * 4u) == 0x4000u, "and the shopkeeper says what she said");
    check(memory.load32(table + kBuyFullLine * 4u) == kBuyFullText - table, "and the full pouch says 32");
}

} // namespace

void backpack_tests(int &failures) {
    failures_ = &failures;
    test_high_rank();
    test_item_and_text();
    test_stock();
    test_purchase();
    test_frame();
}
