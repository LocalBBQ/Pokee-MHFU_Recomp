#include "game/backpack.hpp"

#include "game/guest_layout.hpp"
#include "game/hooks.hpp"
#include "game/item_sets.hpp"
#include "game/pouch_page.hpp"
#include "game/pouch_slots.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace mhfu::game::backpack {
namespace {

std::uint32_t slot_word(std::uint16_t item, std::uint16_t count) {
    return static_cast<std::uint32_t>(item) | (static_cast<std::uint32_t>(count) << 16u);
}

// Reads a 0xFFFF-ended list of at most kMostStock ids; none when it does not
// end in that many.
std::optional<std::vector<std::uint16_t>> read_list(const Ram &ram, std::uint32_t list) {
    std::vector<std::uint16_t> ids;
    for (std::uint32_t i = 0; i < kMostStock; ++i) {
        if (!ram.contains(list + i * 2u, 2u)) return std::nullopt;
        const std::uint16_t id = ram.load16(list + i * 2u);
        if (id == 0xFFFFu) return ids;
        ids.push_back(id);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::uint32_t> quest_bit(const Ram &ram, std::uint16_t quest) {
    for (std::uint32_t i = 0; i < kMostQuests; ++i) {
        const std::uint32_t at = kQuestTable + i * 2u;
        if (!ram.contains(at, 2u)) return std::nullopt;
        const std::uint16_t id = ram.load16(at);
        if (id == kQuestTableEnd) return std::nullopt;
        if (id == quest) return i;
    }
    return std::nullopt;
}

bool cleared(const Ram &ram, std::uint32_t game_sys, std::uint16_t quest) {
    const std::optional<std::uint32_t> bit = quest_bit(ram, quest);
    if (!bit) return false;
    const std::uint32_t word = game_sys + kCleared + (*bit >> 5u) * 4u;
    return ram.contains(word, 4u) && ((ram.load32(word) >> (*bit & 31u)) & 1u) != 0u;
}

bool guild_high_rank(const Ram &ram, std::uint32_t game_sys) {
    return ram.contains(game_sys + kHunterRank, 1u) && ram.load8(game_sys + kHunterRank) >= kHighRank;
}

bool village_high_rank(const Ram &ram, std::uint32_t game_sys) {
    for (const std::uint16_t quest : kLaterUrgents)
        if (cleared(ram, game_sys, quest)) return true;
    return std::all_of(kSixStarUrgents.begin(), kSixStarUrgents.end(),
        [&](std::uint16_t quest) { return cleared(ram, game_sys, quest); });
}

bool high_rank(const Ram &ram, std::uint32_t game_sys) {
    return guild_high_rank(ram, game_sys) || village_high_rank(ram, game_sys);
}

std::array<std::uint8_t, kItemRecord> item_record(const Ram &ram) {
    std::array<std::uint8_t, kItemRecord> record{};
    const std::uint32_t from = kItemTable + kTemplateItem * kItemRecord;
    for (std::uint32_t i = 0; i < kItemRecord; ++i) record[i] = ram.load8(from + i);
    for (std::uint32_t i = 0; i < 4u; ++i) {
        record[kBuyPrice + i] = static_cast<std::uint8_t>(kPrice >> (8u * i));
        record[kSellPrice + i] = 0u;
    }
    return record;
}

void write_item_record(Ram &ram) {
    const std::uint32_t to = kItemTable + kItem * kItemRecord;
    if (!ram.contains(kItemTable + kTemplateItem * kItemRecord, kItemRecord) || !ram.contains(to, kItemRecord)) return;
    const auto record = item_record(ram);
    for (std::uint32_t i = 0; i < kItemRecord; ++i)
        if (ram.load8(to + i) != record[i]) ram.store8(to + i, record[i]);
}

std::uint32_t table_address(const Ram &ram, std::uint32_t block, int index) {
    if (block == 0u || index < 0) return 0u;
    const std::uint32_t at = block + static_cast<std::uint32_t>(index + 2) * 4u;
    if (!ram.contains(at, 4u)) return 0u;
    const std::uint32_t offset = ram.load32(at);
    if (offset < 8u) return 0u;
    const std::uint32_t table = block + offset;
    return ram.contains(table, 4u) ? table : 0u;
}

std::uint32_t entry_word(const Ram &ram, std::uint32_t block, int index, std::uint32_t entry) {
    const std::uint32_t table = table_address(ram, block, index);
    if (table == 0u || !ram.contains(table + entry * 4u, 4u)) return 0u;
    return table + entry * 4u;
}

bool point_entry(Ram &ram, std::uint32_t block, int index, std::uint32_t entry, std::uint32_t text,
    std::optional<std::uint32_t> &original) {
    const std::uint32_t table = table_address(ram, block, index);
    const std::uint32_t word = entry_word(ram, block, index, entry);
    if (word == 0u) return false;
    const std::uint32_t now = ram.load32(word);
    if (text == 0u) {
        if (original && now != *original) ram.store32(word, *original);
        return true;
    }
    const std::uint32_t wanted = text - table;
    if (!original && now != wanted) original = now;
    if (now != wanted) ram.store32(word, wanted);
    return true;
}

void write_text(Ram &ram, std::uint32_t address, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        ram.store8(address + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(text[i]));
    ram.store8(address + static_cast<std::uint32_t>(text.size()), 0u);
}

std::uint32_t lobby_text_block(const Ram &ram) {
    if (!ram.contains(kResourcesPointer, 4u)) return 0u;
    const std::uint32_t resources = ram.load32(kResourcesPointer);
    if (!layout::in_ram(resources) || !ram.contains(resources + kResources, kMostResources * kResourceBytes))
        return 0u;
    for (std::uint32_t i = 0; i < kMostResources; ++i) {
        const std::uint32_t entry = resources + kResources + i * kResourceBytes;
        if (ram.load32(entry) != kLobbyText) continue;
        const std::uint32_t block = ram.load32(entry + 4u);
        return layout::in_ram(block) ? block : 0u;
    }
    return 0u;
}

std::vector<std::uint16_t> with_item(std::vector<std::uint16_t> ids) {
    auto at = std::find(ids.begin(), ids.end(), kTemplateItem);
    if (at == ids.end()) at = std::find(ids.begin(), ids.end(), kOrganizerGuide);
    ids.insert(at == ids.end() ? ids.end() : at + 1, kItem);
    return ids;
}

std::uint32_t offer_stock(Ram &ram, bool offered, std::uint32_t free) {
    struct Table {
        std::uint32_t address;
        std::span<const std::uint32_t> lists;
    };
    const std::array<Table, 2> tables{{{kVillageStock, kVillageLists}, {kHallStock, kHallLists}}};
    for (const Table &table : tables) {
        // Where each list's copy goes, and whether the table is still the
        // game's or ours throughout.
        std::vector<std::uint32_t> copies;
        std::vector<std::vector<std::uint16_t>> lists;
        bool ours = ram.contains(table.address, table.lists.size() * 4u);
        for (std::size_t i = 0; ours && i < table.lists.size(); ++i) {
            const auto list = read_list(ram, table.lists[i]);
            if (!list) {
                ours = false;
                break;
            }
            const std::uint32_t copy = free;
            free += static_cast<std::uint32_t>((list->size() + 2u) * 2u + 3u) & ~3u;
            const std::uint32_t pointer = ram.load32(table.address + static_cast<std::uint32_t>(i) * 4u);
            if (pointer != table.lists[i] && pointer != copy) ours = false;
            copies.push_back(copy);
            lists.push_back(*list);
        }
        if (!ours || free > kWindowEnd) continue;
        for (std::size_t i = 0; i < table.lists.size(); ++i) {
            const std::uint32_t pointer_at = table.address + static_cast<std::uint32_t>(i) * 4u;
            if (!offered) {
                if (ram.load32(pointer_at) != table.lists[i]) ram.store32(pointer_at, table.lists[i]);
                continue;
            }
            const std::vector<std::uint16_t> ids = with_item(lists[i]);
            for (std::size_t j = 0; j < ids.size(); ++j)
                ram.store16(copies[i] + static_cast<std::uint32_t>(j) * 2u, ids[j]);
            ram.store16(copies[i] + static_cast<std::uint32_t>(ids.size()) * 2u, 0xFFFFu);
            if (ram.load32(pointer_at) != copies[i]) ram.store32(pointer_at, copies[i]);
        }
    }
    return free;
}

bool grey_out(Ram &ram, std::uint32_t store) {
    if (store == 0u || !ram.contains(store, kStoreRow + kMostRows * kRowBytes)) return false;
    const std::uint32_t rows = ram.load32(store + kStoreRows);
    if (ram.load8(store + kStoreMode) != 0u || rows > kMostRows) return false;
    for (std::uint32_t i = 0; i < rows; ++i) {
        const std::uint32_t row = store + kStoreRow + i * kRowBytes;
        if (ram.load16(row + kRowItem) == kItem) ram.store16(row + kRowGreyed, 1u);
    }
    return true;
}

bool village_store_talk(const Ram &ram, std::uint32_t talk) {
    if (!layout::in_ram(talk) || !ram.contains(talk, kTalkType + 1u) || ram.load8(talk + kTalkType) != kVillageStore)
        return false;
    const std::uint32_t record = ram.load32(talk + kTalkRecord);
    return layout::in_ram(record) && ram.contains(record, 4u) && ram.load32(record) == kVillageStore;
}

std::uint32_t held(const Ram &ram, const Places &places) {
    const auto count = [&](std::uint32_t first, std::uint32_t slots) {
        std::uint32_t total = 0u;
        if (first == 0u || !ram.contains(first, slots * 4u)) return total;
        for (std::uint32_t i = 0; i < slots; ++i)
            if (ram.load16(first + i * 4u) == kItem) total += ram.load16(first + i * 4u + 2u);
        return total;
    };
    return count(places.box, kBoxSlots) + std::max(count(places.pouch, places.slots), count(places.saved, places.slots));
}

void take(Ram &ram, const Places &places, std::uint32_t count) {
    // Up to `wanted` from the slots from `first`; how many were there.
    const auto take_from = [&](std::uint32_t first, std::uint32_t slots, std::uint32_t wanted) {
        std::uint32_t taken = 0u;
        if (first == 0u || !ram.contains(first, slots * 4u)) return taken;
        for (std::uint32_t i = 0; i < slots && taken < wanted; ++i) {
            const std::uint32_t at = first + i * 4u;
            if (ram.load16(at) != kItem) continue;
            const std::uint16_t here = ram.load16(at + 2u);
            const std::uint16_t out = static_cast<std::uint16_t>(std::min<std::uint32_t>(here, wanted - taken));
            taken += out;
            if (out == here)
                ram.store32(at, slot_word(0u, 0u));
            else
                ram.store16(at + 2u, static_cast<std::uint16_t>(here - out));
        }
        return taken;
    };
    const std::uint32_t from_pouch = take_from(places.pouch, places.slots, count);
    const std::uint32_t from_saved = take_from(places.saved, places.slots, count);
    const std::uint32_t from_either = std::max(from_pouch, from_saved);
    if (from_either < count) (void)take_from(places.box, kBoxSlots, count - from_either);
}

void refund(Ram &ram, std::uint32_t game_sys, std::uint32_t extra) {
    if (extra == 0u || !ram.contains(game_sys + kZenny, 4u)) return;
    const std::uint64_t zenny = ram.load32(game_sys + kZenny) + std::uint64_t{extra} * kPrice;
    ram.store32(game_sys + kZenny, static_cast<std::uint32_t>(std::min<std::uint64_t>(zenny, kMostZenny)));
}

namespace {

struct State {
    bool checked{};
    bool trace{};
    bool offered{};
    std::optional<std::uint32_t> name, description;
    std::uint32_t lobby_block{};
    std::optional<std::uint32_t> buy, buy_to_box, buy_full;
    // How many of the item the hunter had, to tell a purchase.
    std::string hunter;
    std::optional<std::uint32_t> had;
    // The store the list last asked about the item, to grey it out once bought.
    std::uint32_t store{};
    // The shopkeeper has news of the item, and the talk that is telling it.
    bool news{};
    std::uint32_t news_talk{};
    psprecomp::Runtime::RecompiledFunction guide_check{};
    psprecomp::Runtime::RecompiledFunction obtained_check{};
    psprecomp::Runtime::RecompiledFunction pouch_count{};
    psprecomp::Runtime::RecompiledFunction store_lines{};
    psprecomp::Runtime::RecompiledFunction talk_text{};
};

State &state() {
    static State s;
    return s;
}

void answer(psprecomp::AllegrexContext &ctx, std::uint32_t value) {
    ctx.gpr[2] = value;
    ctx.pc = ctx.gpr[31];
}

// 0x09A80230(store, item): a Guide. The list asks for each item it shows.
void guide_check(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if ((ctx.gpr[5] & 0xFFFFu) == kItem && ctx.gpr[31] == kListGuideReturn && pouch_page::enabled()) {
        state().store = ctx.gpr[4];
        answer(ctx, 1u);
        return;
    }
    ctx.pc = kGuideCheck;
    state().guide_check(runtime, ctx);
}

// 0x0885C5CC(?, item): obtained. The item is, once the hunter owns the page.
void obtained_check(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if ((ctx.gpr[5] & 0xFFFFu) == kItem && pouch_page::enabled()) {
        answer(ctx, pouch_page::owned() ? 1u : 0u);
        return;
    }
    ctx.pc = kObtainedCheck;
    state().obtained_check(runtime, ctx);
}

// 0x0885AED0(?, item): how many of the item the pouch holds. In the store's
// pane, the item counts one once the hunter owns the page, the most it can
// be, as an obtained Guide.
void pouch_count(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if ((ctx.gpr[5] & 0xFFFFu) == kItem && ctx.gpr[31] == kPaneCountReturn && pouch_page::enabled() &&
        pouch_page::owned()) {
        answer(ctx, 1u);
        return;
    }
    ctx.pc = kPouchCount;
    state().pouch_count(runtime, ctx);
}

// 0x09A98F20(talk, npc, check): the village store's news. Checked, the NPC
// shows it has some while the shopkeeper has news of the item.
void store_lines(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t npc = ctx.gpr[5];
    if (ctx.gpr[6] != 0u && state().news && pouch_page::enabled() && layout::in_ram(npc) &&
        runtime.memory().contains(npc + kNpcNews, 1u))
        runtime.memory().store8(npc + kNpcNews, 1u);
    ctx.pc = kStoreLines;
    state().store_lines(runtime, ctx);
}

// 0x09A9F520(talk, line): a line's text. The village store's greeting, said
// when it has no news of its own, is the item's news the first time; the
// talk that began with it keeps it until another begins.
void talk_text(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const std::uint32_t talk = ctx.gpr[4];
    if ((ctx.gpr[5] & 0xFFFFu) == kGreeting && pouch_page::enabled()) {
        GuestRam ram(runtime.memory());
        if (village_store_talk(ram, talk)) {
            const auto shown = static_cast<std::int32_t>(ram.load32(talk + kTalkShown));
            if (shown == 0 || shown == 1) { // the talk's first frame
                s.news_talk = s.news ? talk : 0u;
                if (s.news) {
                    pouch_page::announce();
                    s.news = false;
                    if (s.trace) std::cout << "[pouch page] the shopkeeper tells of the Lg Backpack" << std::endl;
                }
            }
            if (s.news_talk == talk) {
                answer(ctx, kNewsText);
                return;
            }
        }
    }
    ctx.pc = kTalkText;
    s.talk_text(runtime, ctx);
}

} // namespace

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    if (!s.checked) {
        s.checked = true;
        const char *text = std::getenv("MHFU_TRACE_POUCH_PAGE");
        s.trace = text != nullptr && *text != '\0' && std::string(text) != "0";
    }
    if (!pouch_page::enabled()) return;
    GuestRam ram(runtime.memory());

    // The item, its name and its description.
    write_item_record(ram);
    if (ram.load8(kNameText) != static_cast<std::uint8_t>(kName[0])) {
        write_text(ram, kNameText, kName);
        write_text(ram, kDescriptionText, kDescription);
        write_text(ram, kBuyText, kBuyLineText);
        write_text(ram, kBuyToBoxText, kBuyToBoxLineText);
        write_text(ram, kBuyFullText, kBuyFullLineText);
        write_text(ram, kNewsText, kNewsLineText);
    }
    point_entry(ram, kTextBlock, kNameTable, kItem, kNameText, s.name);
    point_entry(ram, kTextBlock, kDescriptionTable, kItem, kDescriptionText, s.description);
    if (runtime.function_at(kObtainedCheck) != nullptr && runtime.function_at(kObtainedCheck) != &obtained_check)
        (void)hooks::wrap(runtime, kObtainedCheck, &obtained_check, s.obtained_check, "mhfu_backpack_obtained");
    if (runtime.function_at(kPouchCount) != nullptr && runtime.function_at(kPouchCount) != &pouch_count)
        (void)hooks::wrap(runtime, kPouchCount, &pouch_count, s.pouch_count, "mhfu_backpack_pouch_count");

    const std::uint32_t sys = item_sets::game_sys(ram);
    const std::string hunter = sys != 0u ? item_sets::hunter_name(ram, sys) : std::string{};
    if (hunter.empty()) return;
    if (hunter != s.hunter) {
        s.hunter = hunter;
        s.had.reset();
    }
    const bool lobby = layout::overlay_loaded(runtime.memory(), layout::kTaskSlot, "lobby_task.ovl");
    const bool unlocked = high_rank(ram, sys);
    const bool offered = lobby && unlocked && !pouch_page::owned();

    // Bought: more of the item than the hunter had, while it is offered.
    const Places places{sys + kBox, pouch_slots::first(sys), pouch_page::active() ? pouch_page::kSaved : sys + kSavedPouch,
        pouch_slots::count()};
    const std::uint32_t now = held(ram, places);
    if (offered && s.had && now > *s.had) {
        const std::uint32_t bought = now - *s.had;
        take(ram, places, bought);
        refund(ram, sys, bought - 1u);
        pouch_page::grant(runtime);
        const bool greyed = grey_out(ram, s.store);
        if (s.trace)
            std::cout << "[pouch page] Lg Backpack bought: the fourth page is the hunter's"
                      << (greyed ? "" : "; no store list to grey it out in") << std::endl;
    }
    s.had = held(ram, places);

    if (!lobby) return;
    if (offered != s.offered && s.trace)
        std::cout << "[pouch page] Lg Backpack " << (offered ? "in stock" : "not on offer") << std::endl;
    s.offered = offered;
    s.news = offered && !pouch_page::announced();
    if (runtime.function_at(kGuideCheck) != nullptr && runtime.function_at(kGuideCheck) != &guide_check)
        (void)hooks::wrap(runtime, kGuideCheck, &guide_check, s.guide_check, "mhfu_backpack_guide");
    if (runtime.function_at(kStoreLines) != nullptr && runtime.function_at(kStoreLines) != &store_lines)
        (void)hooks::wrap(runtime, kStoreLines, &store_lines, s.store_lines, "mhfu_backpack_store_lines");
    if (runtime.function_at(kTalkText) != nullptr && runtime.function_at(kTalkText) != &talk_text)
        (void)hooks::wrap(runtime, kTalkText, &talk_text, s.talk_text, "mhfu_backpack_talk_text");
    // Listed from High Rank on, greyed out once bought, as the Guides are.
    offer_stock(ram, unlocked, kStockCopies);
    const std::uint32_t block = lobby_text_block(ram);
    if (block == 0u) return;
    if (block != s.lobby_block) {
        s.lobby_block = block;
        s.buy.reset();
        s.buy_to_box.reset();
        s.buy_full.reset();
    }
    point_entry(ram, block, kStoreTable, kBuyLine, offered ? kBuyText : 0u, s.buy);
    point_entry(ram, block, kStoreTable, kBuyToBoxLine, offered ? kBuyToBoxText : 0u, s.buy_to_box);
    point_entry(ram, block, kStoreTable, kBuyFullLine, pouch_page::active() ? kBuyFullText : 0u, s.buy_full);
}

} // namespace mhfu::game::backpack
