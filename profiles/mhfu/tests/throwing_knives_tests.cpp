// Throwing knives: the lookups, the code checks, and the wrappers around
// stand-ins for the game's functions, over tables laid out as the game's are.
// No game data.
#include "game/throwing_knives.hpp"

#include "game/guest_ram.hpp"
#include "game/shop_stock.hpp"

#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mhfu::game::throwing_knives;
namespace shop_stock = mhfu::game::shop_stock;
using shop_stock::kListEnd;
using shop_stock::read_list;
constexpr std::uint32_t kShopLists = shop_stock::kLists;
constexpr std::uint32_t kShopTables = shop_stock::kTables;

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

constexpr std::uint32_t kMemory = 32u * 1024u * 1024u;
constexpr std::uint32_t kGameSys = 0x09000000u;
constexpr std::uint32_t kScreen = 0x09100000u;
constexpr std::uint32_t kReturn = 0x0881E3F8u;
constexpr std::uint16_t kSmMonsterBone = 281u;
constexpr std::uint16_t kHerb = 163u;
constexpr std::uint16_t kBlueMushroom = 177u;
constexpr std::uint16_t kToadstool = 180u;

std::uint32_t item_record(std::uint16_t item) {
    return kItemTable + item * kItemRecord;
}

void write_words(psprecomp::GuestMemory &memory, std::span<const CodeWord> words) {
    for (const CodeWord &word : words) memory.store32(word.address, word.word);
}

void write_record(psprecomp::GuestMemory &memory, std::uint32_t index, std::uint16_t other, std::uint16_t result) {
    const std::uint32_t record = kRecords + index * kRecordSize;
    memory.store16(record, other);
    memory.store16(record + 2u, result);
    memory.store8(record + 4u, 3u);
    memory.store8(record + 6u, static_cast<std::uint8_t>(index));
}

// Three combinations of the game's, grouped as its are: Herb's, Throwing
// Knife's, and Whetstone's one record. The combo list is the same three, and
// the rest of its 172 entries pairs nothing.
void write_combinations(psprecomp::GuestMemory &memory) {
    write_record(memory, 0u, kBlueMushroom, 7u);
    write_record(memory, 1u, kToadstool, 59u);
    write_record(memory, 2u, kSmMonsterBone, kBoomerang);
    memory.store8(kIndex + kHerb * 2u, 1u);
    memory.store8(kIndex + kHerb * 2u + 1u, 0u);
    memory.store8(kIndex + kThrowingKnife * 2u, 1u);
    memory.store8(kIndex + kThrowingKnife * 2u + 1u, 1u);
    memory.store8(kIndex + kWhetstone * 2u, 1u);
    memory.store8(kIndex + kWhetstone * 2u + 1u, 2u);
    const std::uint16_t pairs[3][2] = {
        {kHerb, kBlueMushroom}, {kThrowingKnife, kToadstool}, {kWhetstone, kSmMonsterBone}};
    for (std::uint32_t i = 0; i < 3u; ++i) {
        memory.store16(kPairs + i * 4u, pairs[i][0]);
        memory.store16(kPairs + i * 4u + 2u, pairs[i][1]);
    }
    for (std::uint32_t i = 3u; i < kGameCombos; ++i) {
        memory.store16(kPairs + i * 4u, 1u); // Book of Combos 1, which combines with nothing
        memory.store16(kPairs + i * 4u + 2u, 2u);
    }
    memory.store32(kGameSysPointer, kGameSys);
}

void write_items(psprecomp::GuestMemory &memory) {
    for (std::uint16_t knife : kKnives) {
        memory.store8(item_record(knife) + kFlags, kSupplyItem | 0x20u);
        memory.store8(item_record(knife) + 3u, 5u);
    }
}

// The descriptions' section at +0x1000 of the texts, as DATA.BIN entry 19
// lays them out: an offset per item, the texts after.
constexpr std::uint32_t kSection = 0x1000u;
std::uint32_t text_at(std::uint16_t item) {
    return kText + kSection + 0x400u + item * 0x60u;
}

void write_text(psprecomp::GuestMemory &memory, std::uint16_t item, const std::string &text) {
    memory.store32(kText + kSection + 4u * item, text_at(item) - (kText + kSection));
    for (std::size_t i = 0; i <= text.size(); ++i)
        memory.store8(text_at(item) + static_cast<std::uint32_t>(i), i < text.size() ? text[i] : 0u);
}

std::string read_text(const psprecomp::GuestMemory &memory, std::uint16_t item) {
    std::string text;
    for (std::uint32_t at = text_at(item); memory.load8(at) != 0u; ++at)
        text.push_back(static_cast<char>(memory.load8(at)));
    return text;
}

void write_texts(psprecomp::GuestMemory &memory) {
    memory.store32(kText + 4u * (kDescriptions + 2u), kSection);
    write_text(memory, 58u, "Stand-in text for the\nplain knife.\n~C05(Supply Item)");
    write_text(memory, 59u, "Stand-in text for the\npoison knife. ~C05(Supply Item)");
    write_text(memory, 60u, "Stand-in text for the\nsleep knife. ~C05(Supply Item)");
    write_text(memory, 61u, "Stand-in text for the\nparalysis knife. ~C05(Supply Item)");
    write_text(memory, 62u, "Stand-in text for the\ntranq knife. ~C05(Supply Item)");
}

// Two shop lists, the village's and the hall's, and one without the
// Boomerang; the village's table points to the first and third, the hall's to
// the second, and the first twice.
constexpr std::uint32_t kVillage = kShopLists;
constexpr std::uint32_t kHall = kShopLists + 0x20u;
constexpr std::uint32_t kFarm = kShopLists + 0x40u;
constexpr std::uint32_t kVillageTable = kShopTables;
constexpr std::uint32_t kHallTable = kShopTables + 0x50u;

void write_list(psprecomp::GuestMemory &memory, std::uint32_t list, const std::vector<std::uint16_t> &items) {
    for (std::size_t i = 0; i < items.size(); ++i) memory.store16(list + static_cast<std::uint32_t>(i) * 2u, items[i]);
    memory.store16(list + static_cast<std::uint32_t>(items.size()) * 2u, kListEnd);
}

void write_shop_code(psprecomp::GuestMemory &memory) {
    for (const shop_stock::CodeWord &word : shop_stock::kSignature) memory.store32(word.address, word.word);
}

void write_shops(psprecomp::GuestMemory &memory) {
    write_shop_code(memory);
    write_list(memory, kVillage, {7u, 11u, kBoomerang, 42u});
    write_list(memory, kHall, {7u, kBoomerang});
    write_list(memory, kFarm, {43u, 25u});
    memory.store32(kVillageTable, kVillage);
    memory.store32(kVillageTable + 4u, kFarm);
    memory.store32(kVillageTable + 8u, 0u);
    memory.store32(kHallTable, kHall);
    memory.store32(kHallTable + 4u, kVillage);
    memory.store32(kHallTable + 8u, 0u);
}

std::vector<std::uint16_t> list_at(psprecomp::GuestMemory &memory, std::uint32_t list) {
    const mhfu::game::GuestRam ram(memory);
    return read_list(ram, list).value_or(std::vector<std::uint16_t>{0xDEADu});
}

// A frame as present_frame runs it: the knives, then the stores' lists.
void step(psprecomp::Runtime &runtime) {
    frame(runtime);
    mhfu::game::GuestRam ram(runtime.memory());
    constexpr std::array<shop_stock::Stocking, 1> kStockings{&stock};
    (void)shop_stock::keep(ram, kStockings);
}

// The game's functions, as far as the wrappers rely on them.
int game_calls{};
std::uint16_t combined_result{};
bool combine_partway{};

void game_search(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    ctx.gpr[2] = game_record(runtime.memory(), ctx.gpr[5], ctx.gpr[6]);
    ctx.pc = ctx.gpr[31];
}

void game_known(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    ctx.gpr[2] = 7u;
    ctx.pc = ctx.gpr[31];
}

// Finds the record of its two items by itself, as the game's does inside its
// unit, and gives what it combines into.
void game_combine(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    const std::uint32_t record = game_record(runtime.memory(), ctx.gpr[7], ctx.gpr[8]);
    combined_result = record != 0u ? runtime.memory().load16(record + 2u) : 0u;
    ctx.gpr[2] = record != 0u ? 1u : 0xFFFFFFFFu;
    // Back to the dispatcher partway: it goes on from somewhere else.
    ctx.pc = combine_partway ? 0x0885B8A0u : ctx.gpr[31];
}

void game_step(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    ctx.gpr[2] = runtime.memory().load8(ctx.gpr[5]);
    ctx.pc = ctx.gpr[31];
}

// Lays the list out for the game's 172, rows of 8, the selection clamped.
void game_list_open(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++game_calls;
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t cursor = ctx.gpr[4] + kCursor;
    const std::uint32_t rows = memory.load8(cursor + kRows);
    std::uint32_t selected = memory.load16(cursor + kSelected);
    if (selected >= kGameCombos) selected = kGameCombos - 1u;
    memory.store16(cursor + kSelected, static_cast<std::uint16_t>(selected));
    memory.store8(cursor + kPages, static_cast<std::uint8_t>((kGameCombos + rows - 1u) / rows));
    memory.store8(cursor + kLastRows, static_cast<std::uint8_t>(kGameCombos % rows == 0u ? rows : kGameCombos % rows));
    memory.store8(cursor + kRow, static_cast<std::uint8_t>(selected % rows));
    memory.store8(cursor + kPage, static_cast<std::uint8_t>(selected / rows));
    ctx.gpr[2] = 1u;
    ctx.pc = ctx.gpr[31];
}

void install_game(psprecomp::Runtime &runtime) {
    runtime.register_function(kSearch, &game_search, "game_search");
    runtime.register_function(kKnown, &game_known, "game_known");
    runtime.register_function(kCombine, &game_combine, "game_combine");
    runtime.register_function(kStep, &game_step, "game_step");
    runtime.register_function(kListOpen, &game_list_open, "game_list_open");
}

void write_game(psprecomp::GuestMemory &memory) {
    write_words(memory, code_signature());
    write_combinations(memory);
    write_items(memory);
    write_texts(memory);
    write_shops(memory);
}

psprecomp::AllegrexContext call(psprecomp::Runtime &runtime, std::uint32_t function, std::vector<std::uint32_t> args) {
    psprecomp::AllegrexContext ctx{};
    for (std::size_t i = 0; i < args.size(); ++i) ctx.gpr[4 + i] = args[i];
    ctx.gpr[31] = kReturn;
    ctx.pc = function;
    runtime.function_at(function)(runtime, ctx);
    return ctx;
}

void test_lookups() {
    psprecomp::GuestMemory memory(kMemory);
    check(new_combo(kWhetstone, kMachaliteOre) == 0u, "Whetstone + Machalite Ore is the first new combination");
    check(new_combo(kDragoniteOre, kWhetstone) == 1u, "Dragonite Ore + Whetstone, in either order, the second");
    check(new_combo(0xFFFF0000u | kWhetstone, kMachaliteOre) == 0u, "the items are read as halfwords");
    check(!new_combo(kWhetstone, kSmMonsterBone), "the game's combinations are not new");
    check(!new_combo(202u, kWhetstone), "Iron Ore makes no knife");

    const std::array<std::uint8_t, kRecordSize> bytes = record_bytes(kNewCombos[1]);
    check(bytes[0] == (kDragoniteOre & 0xFFu) && bytes[1] == 0u, "the record holds the higher item");
    check(bytes[2] == kThrowingKnife && bytes[3] == 0u, "and makes a Throwing Knife");
    check(bytes[4] == kRateClass && bytes[5] == kYieldClass, "at the Boomerang's rate and yield");
    check(bytes[6] == 173u && bytes[7] == 0u && bytes[8] == 173u, "with its own known bit, flags clear");

    write_combinations(memory);
    check(game_record(memory, kBlueMushroom, kHerb) == kRecords, "a game record is found by its lower item");
    check(game_record(memory, kWhetstone, kSmMonsterBone) == kRecords + 2u * kRecordSize, "Whetstone's one record");
    check(game_record(memory, kWhetstone, kMachaliteOre) == 0u, "the game has no Machalite combination");
    check(game_record(memory, 0xFFFFu, kHerb) == 0u, "an item read as negative has no record");
    check(game_record(memory, kIndexItems, kIndexItems + 1u) == 0u, "nor an item past the index");

    check(pair_at(memory, 2) == std::array<std::int16_t, 2>{kWhetstone, kSmMonsterBone}, "the game's list entry");
    check(pair_at(memory, 172) == std::array<std::int16_t, 2>{kWhetstone, kMachaliteOre}, "entry 172 is new");
    check(pair_at(memory, 173) == std::array<std::int16_t, 2>{kWhetstone, kDragoniteOre}, "and 173");
    check(!pair_at(memory, 174) && !pair_at(memory, -1), "there are 174");
}

void test_lists() {
    psprecomp::GuestMemory memory(kMemory);
    check(with_knife({7u, kBoomerang, 42u}) == std::vector<std::uint16_t>{7u, kBoomerang, kThrowingKnife, 42u},
        "the knife goes after the Boomerang");
    check(
        with_knife({7u, kBoomerang}) == std::vector<std::uint16_t>{7u, kBoomerang, kThrowingKnife}, "also at the end");
    check(with_knife({7u, 11u}) == std::vector<std::uint16_t>{7u, 11u}, "a list without the Boomerang stays");
    check(with_knife({kBoomerang, kThrowingKnife}) == std::vector<std::uint16_t>{kBoomerang, kThrowingKnife},
        "a list with the knife stays");

    write_list(memory, kVillage, {7u, kBoomerang});
    check(list_at(memory, kVillage) == std::vector<std::uint16_t>{7u, kBoomerang}, "a list up to its end");
    for (std::uint32_t at = kShopTables - 8u; at < kShopTables; at += 2u) memory.store16(at, 7u);
    check(list_at(memory, kShopTables - 8u) == std::vector<std::uint16_t>{0xDEADu},
        "a list that runs into the tables is not one");
}

void test_texts() {
    psprecomp::GuestMemory memory(kMemory);
    write_texts(memory);
    const std::uint32_t knife_break = static_cast<std::uint32_t>(read_text(memory, 58u).find("\n~C05"));
    const std::uint32_t poison_space = static_cast<std::uint32_t>(read_text(memory, 59u).find(" ~C05"));
    check(tag_start(memory, 58u) == text_at(58u) + knife_break, "the knife's tag starts at the line break before it");
    check(tag_start(memory, 59u) == text_at(59u) + poison_space, "a coated knife's at the space before it");
    write_text(memory, 60u, "Throwing knife.");
    check(!tag_start(memory, 60u), "a text without the tag is left");
    write_text(memory, 61u, "~C05(Supply Item)");
    check(!tag_start(memory, 61u), "a text that is only the tag is left");
    write_text(memory, 62u, std::string(300u, 'x'));
    check(!tag_start(memory, 62u), "nor is a text longer than any the game has");
    psprecomp::GuestMemory empty(kMemory);
    empty.store32(kText + 4u * (kDescriptions + 2u), 0x7FFFFFF0u);
    check(!tag_start(empty, 58u), "a section outside memory has no texts");
}

void test_code_checks() {
    psprecomp::GuestMemory memory(kMemory);
    check(!code_matches(memory), "empty memory is not the executable");
    write_words(memory, code_signature());
    check(code_matches(memory), "the traced words are the executable");
    memory.store32(code_signature()[11].address, 0x241300ADu);
    check(!code_matches(memory), "another count of combinations is another executable");
}

void test_frame() {
    psprecomp::Runtime runtime(kMemory);
    psprecomp::GuestMemory &memory = runtime.memory();
    auto &settings = mhfu::settings::current();
    settings.throwing_knives = true;
    write_game(memory);
    step(runtime);
    check(memory.load8(item_record(58u) + kFlags) & kSupplyItem, "nothing changes before the code is installed");

    install_game(runtime);
    step(runtime);
    check(runtime.function_at(kSearch) != &game_search, "the search is wrapped");
    check(runtime.function_at(kCombineStop) != nullptr, "and the stops are in place");
    for (std::uint16_t knife : kKnives) {
        check((memory.load8(item_record(knife) + kFlags) & kSupplyItem) == 0u, "a knife is no supply item");
        check(memory.load8(item_record(knife) + kFlags) == 0x20u, "and keeps its other flags");
    }
    check(memory.load32(item_record(58u) + kBuyPrice) == kKnifePrice, "the Throwing Knife costs 750z");
    check(memory.load32(item_record(58u) + kSellPrice) == kKnifeSellPrice, "and sells for 75z");
    check(memory.load32(item_record(59u) + kBuyPrice) == 0u, "a coated knife is not sold");
    check(read_text(memory, 58u) == "Stand-in text for the\nplain knife.", "the tag is cut");
    check(read_text(memory, 62u) == "Stand-in text for the\ntranq knife.", "on every knife");

    const std::uint32_t village = memory.load32(kVillageTable);
    check(village >= shop_stock::kCopies && village < shop_stock::kCopiesEnd, "the village's list is a copy");
    check(list_at(memory, village) == std::vector<std::uint16_t>{7u, 11u, kBoomerang, kThrowingKnife, 42u},
        "with the knife after the Boomerang");
    check(memory.load32(kHallTable + 4u) == village, "a list in two tables has one copy");
    check(list_at(memory, memory.load32(kHallTable)) == std::vector<std::uint16_t>{7u, kBoomerang, kThrowingKnife},
        "the hall's list has the knife too");
    check(memory.load32(kVillageTable + 4u) == kFarm, "a list without the Boomerang is the game's");
    check(list_at(memory, kVillage) == std::vector<std::uint16_t>{7u, 11u, kBoomerang, 42u},
        "and the game's lists are left as they are");

    step(runtime);
    check(memory.load32(kVillageTable) == village, "a frame later the tables are the same");
    // lobby_task is loaded again: its tables point to its lists.
    write_shops(memory);
    step(runtime);
    check(memory.load32(kVillageTable) == village, "a table loaded again gets the copies again");
    // Another overlay in the slot.
    memory.store32(shop_stock::kSignature[1].address, 0u);
    memory.store32(kVillageTable, kVillage);
    step(runtime);
    check(memory.load32(kVillageTable) == kVillage, "nothing is touched without lobby_task");
    write_shop_code(memory);
    step(runtime);

    settings.throwing_knives = false;
    step(runtime);
    check(memory.load8(item_record(58u) + kFlags) == (kSupplyItem | 0x20u), "off, the knife is a supply item again");
    check(memory.load32(item_record(58u) + kBuyPrice) == 0u, "with the game's prices");
    check(read_text(memory, 59u) == "Stand-in text for the\npoison knife. ~C05(Supply Item)", "and the game's text");
    check(memory.load32(kVillageTable) == kVillage && memory.load32(kHallTable) == kHall &&
            memory.load32(kHallTable + 4u) == kVillage,
        "and the shops' own lists");

    settings.throwing_knives = true;
    step(runtime);
    check((memory.load8(item_record(58u) + kFlags) & kSupplyItem) == 0u, "on again, the knife is an item");
    check(memory.load32(kVillageTable) != kVillage, "and in the shop");
    // The game reads its texts again.
    write_texts(memory);
    step(runtime);
    check(read_text(memory, 60u) == "Stand-in text for the\nsleep knife.", "texts read again are cut");
}

void test_combinations() {
    psprecomp::Runtime runtime(kMemory);
    psprecomp::GuestMemory &memory = runtime.memory();
    auto &settings = mhfu::settings::current();
    settings.throwing_knives = true;
    write_game(memory);
    install_game(runtime);
    step(runtime);

    // The search.
    game_calls = 0;
    psprecomp::AllegrexContext ctx = call(runtime, kSearch, {0u, kMachaliteOre, kWhetstone, 1u, 0u, 5u});
    check(ctx.gpr[2] == new_record(0) && ctx.pc == kReturn, "with bit 0 of mode, the new record");
    check(call(runtime, kSearch, {0u, kWhetstone, kDragoniteOre, 0u, 1u, 0u}).gpr[2] == new_record(1),
        "for t1 0, as lobby_task asks");
    check(call(runtime, kSearch, {0u, kWhetstone, kDragoniteOre, 0u, 0u, 0xFFFFFFFFu}).gpr[2] == new_record(1),
        "for t1 -1");
    check(call(runtime, kSearch, {0u, kWhetstone, kDragoniteOre, 0u, 0u, 1u}).gpr[2] == new_record(1), "for t1 1");
    check(call(runtime, kSearch, {0u, kWhetstone, kDragoniteOre, 0u, 0u, 2u}).gpr[2] == 0u, "not for t1 2");
    check(game_calls == 0, "a new pair never reaches the game's search");
    check(call(runtime, kSearch, {0u, kHerb, kBlueMushroom, 1u, 0u, 0u}).gpr[2] == kRecords && game_calls == 1,
        "the game's pairs are the game's");
    check(memory.load16(new_record(0)) == kMachaliteOre && memory.load16(new_record(0) + 2u) == kThrowingKnife,
        "the records are in the host's window");

    // Known.
    check(call(runtime, kKnown, {0u, kWhetstone, kMachaliteOre}).gpr[2] == 0u, "a new combination starts unknown");
    memory.store8(kGameSys + kKnownBits + 21u, 0x10u); // bit 172
    check(call(runtime, kKnown, {0u, kWhetstone, kMachaliteOre}).gpr[2] == 1u, "bit 172 makes the first known");
    check(call(runtime, kKnown, {0u, kWhetstone, kDragoniteOre}).gpr[2] == 0u, "and only the first");
    game_calls = 0;
    check(
        call(runtime, kKnown, {0u, kHerb, kBlueMushroom}).gpr[2] == 7u && game_calls == 1, "the game's asks the game");

    // The step: the new entries follow the game's 172.
    constexpr std::uint32_t kIndexByte = 0x09200000u;
    constexpr std::uint32_t kA = 0x09200010u;
    constexpr std::uint32_t kB = 0x09200012u;
    memory.store8(kIndexByte, 171u);
    ctx = call(runtime, kStep, {0u, kIndexByte, 1u, kA, kB, 1u});
    check(ctx.gpr[2] == new_record(0) && memory.load8(kIndexByte) == 172u, "after 171 comes 172");
    check(memory.load16(kA) == kWhetstone && memory.load16(kB) == kMachaliteOre, "with its pair");
    call(runtime, kStep, {0u, kIndexByte, 1u, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 173u && memory.load16(kB) == kDragoniteOre, "then 173");
    ctx = call(runtime, kStep, {0u, kIndexByte, 1u, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 0u && ctx.gpr[2] == kRecords, "then round to the first");
    ctx = call(runtime, kStep, {0u, kIndexByte, 0xFFu, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 173u && ctx.gpr[2] == new_record(1), "back from the first is the last");
    ctx = call(runtime, kStep, {0u, kIndexByte, 0u, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 173u && ctx.gpr[2] == new_record(1), "step 0 is the entry itself");
    memory.store8(kIndexByte, 2u);
    ctx = call(runtime, kStep, {0u, kIndexByte, 1u, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 172u, "entries without a record are passed over");
    memory.store8(kIndexByte, 5u);
    ctx = call(runtime, kStep, {0u, kIndexByte, 0u, kA, kB, 1u});
    check(memory.load8(kIndexByte) == 172u, "step 0 on an entry without a record goes on to the next");
    game_calls = 0;
    memory.store8(kIndexByte, 173u);
    ctx = call(runtime, kStep, {0u, kIndexByte, 1u, kA, kB, 0u});
    check(game_calls == 1 && memory.load8(kIndexByte) == kGameCombos - 1u,
        "another mode is the game's walk, from within its 172");

    // The Combine screen counts 174.
    memory.store8(kScreen + kCursor + kRows, 8u);
    memory.store16(kScreen + kCursor + kSelected, 173u);
    ctx = call(runtime, kListOpen, {kScreen});
    check(ctx.pc == kReturn && ctx.gpr[31] == kReturn, "the screen's count returns to its caller");
    check(memory.load8(kScreen + kCursor + kPages) == 22u, "174 entries in rows of 8 are 22 pages");
    check(memory.load8(kScreen + kCursor + kLastRows) == 6u, "the last with 6");
    check(memory.load16(kScreen + kCursor + kSelected) == 173u, "the selection stays on the last entry");
    check(memory.load8(kScreen + kCursor + kRow) == 5u && memory.load8(kScreen + kCursor + kPage) == 21u,
        "on the last page's last row");
    memory.store16(kScreen + kCursor + kSelected, 300u);
    call(runtime, kListOpen, {kScreen});
    check(memory.load16(kScreen + kCursor + kSelected) == 173u, "a selection past the end is the last entry");

    // Combining: the game's code sees the new record in Whetstone's place.
    const std::uint32_t whetstone = kRecords + 2u * kRecordSize;
    combined_result = 0u;
    ctx = call(runtime, kCombine, {0u, 0u, 0u, kWhetstone, kMachaliteOre, 0u});
    check(combined_result == kThrowingKnife && ctx.gpr[2] == 1u, "Whetstone + Machalite Ore makes a knife");
    check(ctx.pc == kReturn && ctx.gpr[31] == kReturn, "and returns to its caller");
    check(memory.load16(whetstone) == kSmMonsterBone && memory.load16(whetstone + 2u) == kBoomerang,
        "Whetstone's record is put back");
    ctx = call(runtime, kCombine, {0u, 0u, 0u, kWhetstone, kSmMonsterBone, 0u});
    check(combined_result == kBoomerang, "and the Boomerang is made as before");
    combine_partway = true;
    ctx = call(runtime, kCombine, {0u, 0u, 0u, kDragoniteOre, kWhetstone, 0u});
    check(combined_result == kThrowingKnife && memory.load16(whetstone) == kDragoniteOre,
        "partway, the new record stays in place");
    ctx.pc = kCombineStop;
    runtime.function_at(kCombineStop)(runtime, ctx);
    check(memory.load16(whetstone) == kSmMonsterBone && ctx.pc == kReturn, "until it reaches its stop");
    runtime.function_at(kCombineStop)(runtime, ctx);
    check(ctx.pc == kReturn, "a stop with no call waiting changes nothing");
    combine_partway = false;

    // Off, everything is the game's.
    settings.throwing_knives = false;
    game_calls = 0;
    check(call(runtime, kSearch, {0u, kWhetstone, kMachaliteOre, 1u, 0u, 0u}).gpr[2] == 0u && game_calls == 1,
        "off, the game finds no Machalite combination");
    call(runtime, kCombine, {0u, 0u, 0u, kWhetstone, kMachaliteOre, 0u});
    check(combined_result == 0u, "and makes nothing of it");
    memory.store16(kScreen + kCursor + kSelected, 10u);
    call(runtime, kListOpen, {kScreen});
    check(memory.load8(kScreen + kCursor + kPages) == 22u && memory.load8(kScreen + kCursor + kLastRows) == 4u,
        "and the screen counts 172");
    settings.throwing_knives = true;
}

void test_game_has_it() {
    psprecomp::Runtime runtime(kMemory);
    psprecomp::GuestMemory &memory = runtime.memory();
    mhfu::settings::current().throwing_knives = true;
    write_game(memory);
    write_record(memory, 2u, kMachaliteOre, kThrowingKnife);
    install_game(runtime);
    step(runtime);
    check(runtime.function_at(kSearch) == &game_search, "a game with the combination keeps its own");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_throwing_knives_tests";
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_THROWING_KNIVES", "");
    set_environment("MHFU_TRACE_THROWING_KNIVES", "1");
    check(mhfu::settings::current().throwing_knives, "throwing knives are items by default");
    test_lookups();
    test_lists();
    test_texts();
    test_code_checks();
    test_game_has_it();
    test_frame();
    test_combinations();
    std::error_code ec;
    std::filesystem::remove_all(data, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "throwing knives tests passed\n";
    return 0;
}
