#include "game/backpack.hpp"

#include "game/guest_layout.hpp"
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
            ram.store16(copies[i], kItem);
            for (std::size_t j = 0; j < lists[i].size(); ++j)
                ram.store16(copies[i] + static_cast<std::uint32_t>(j + 1u) * 2u, lists[i][j]);
            ram.store16(copies[i] + static_cast<std::uint32_t>(lists[i].size() + 1u) * 2u, 0xFFFFu);
            if (ram.load32(pointer_at) != copies[i]) ram.store32(pointer_at, copies[i]);
        }
    }
    return free;
}

Taken take_items(Ram &ram, std::uint32_t game_sys, std::uint32_t pouch, std::uint32_t saved, std::uint32_t slots) {
    Taken taken;
    const auto take = [&](std::uint32_t first, std::uint32_t count, std::uint32_t &total) {
        if (first == 0u || !ram.contains(first, count * 4u)) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t at = first + i * 4u;
            if (ram.load16(at) != kItem) continue;
            total += ram.load16(at + 2u);
            ram.store32(at, slot_word(0u, 0u));
        }
    };
    take(game_sys + kBox, kBoxSlots, taken.box);
    take(pouch, slots, taken.pouch);
    take(saved, slots, taken.saved);
    return taken;
}

void refund(Ram &ram, std::uint32_t game_sys, const Taken &taken) {
    // The pouch and the saved pouch are copies of each other.
    const std::uint32_t bought = taken.box + std::max(taken.pouch, taken.saved);
    if (bought <= 1u || !ram.contains(game_sys + kZenny, 4u)) return;
    const std::uint64_t zenny = ram.load32(game_sys + kZenny) + std::uint64_t{bought - 1u} * kPrice;
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
};

State &state() {
    static State s;
    return s;
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
    }
    point_entry(ram, kTextBlock, kNameTable, kItem, kNameText, s.name);
    point_entry(ram, kTextBlock, kDescriptionTable, kItem, kDescriptionText, s.description);

    const std::uint32_t sys = item_sets::game_sys(ram);
    if (sys == 0u || item_sets::hunter_name(ram, sys).empty()) return;

    // Bought: the item goes, the page comes.
    const std::uint32_t saved = pouch_page::active() ? pouch_page::kSaved : sys + kSavedPouch;
    const Taken taken =
        take_items(ram, sys, pouch_slots::first(sys), saved, pouch_slots::count());
    if (taken.box + taken.pouch + taken.saved > 0u) {
        refund(ram, sys, taken);
        pouch_page::grant(runtime);
        if (s.trace)
            std::cout << "[pouch page] Backpack Lg. bought (box " << taken.box << ", pouch " << taken.pouch
                      << "): the fourth page is the hunter's" << std::endl;
    }

    if (!layout::overlay_loaded(runtime.memory(), layout::kTaskSlot, "lobby_task.ovl")) return;
    const bool offered = high_rank(ram, sys) && !pouch_page::owned();
    if (offered != s.offered && s.trace)
        std::cout << "[pouch page] Backpack Lg. " << (offered ? "in stock" : "not in stock") << std::endl;
    s.offered = offered;
    offer_stock(ram, offered, kStockCopies);
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
