#include "game/shop_stock.hpp"

#include <map>
#include <utility>

namespace mhfu::game::shop_stock {

bool code_matches(const Ram &ram) {
    for (const CodeWord &expected : kSignature)
        if (!ram.contains(expected.address, 4u) || ram.load32(expected.address) != expected.word) return false;
    return ram.contains(kLists, kTablesEnd - kLists);
}

std::optional<std::vector<std::uint16_t>> read_list(const Ram &ram, std::uint32_t list) {
    std::vector<std::uint16_t> ids;
    for (std::uint32_t at = list; at + 2u <= kTables; at += 2u) {
        if (!ram.contains(at, 2u)) return std::nullopt;
        const std::uint16_t id = ram.load16(at);
        if (id == kListEnd) return ids;
        ids.push_back(id);
    }
    return std::nullopt;
}

bool village_or_hall(std::uint32_t slot) {
    return (slot >= kVillageTable && slot < kVillageTable + kVillageLists * 4u) ||
        (slot >= kHallTable && slot < kHallTable + kHallLists * 4u);
}

bool exchange(std::uint32_t slot) {
    return slot >= kExchangeTable && slot < kExchangeTable + kExchangeLists * 4u;
}

std::size_t keep(Ram &ram, std::span<const Stocking> stockings) {
    if (!code_matches(ram)) return 0u;
    // Each word's game list, read before any copy is written over the last
    // frame's. A copy's word before it is the game's list it was made from.
    constexpr std::uint32_t kSlots = (kTablesEnd - kTables) / 4u;
    std::array<std::uint32_t, kSlots> lists{};
    for (std::uint32_t i = 0; i < kSlots; ++i) {
        const std::uint32_t pointer = ram.load32(kTables + i * 4u);
        const bool copy = pointer >= kCopies + 4u && pointer < kCopiesEnd && (pointer & 3u) == 0u;
        lists[i] = copy ? ram.load32(pointer - 4u) : pointer;
    }
    // The copies made this frame, by the game's list and what it became.
    std::map<std::pair<std::uint32_t, std::vector<std::uint16_t>>, std::uint32_t> made;
    std::uint32_t next = kCopies;
    for (std::uint32_t i = 0; i < kSlots; ++i) {
        const std::uint32_t slot = kTables + i * 4u;
        const std::uint32_t pointer = ram.load32(slot);
        const std::uint32_t list = lists[i];
        if (list < kLists || list >= kTables) continue;
        const std::optional<std::vector<std::uint16_t>> game = read_list(ram, list);
        if (!game) continue;
        std::vector<std::uint16_t> ids = *game;
        for (const Stocking stocking : stockings) ids = stocking(slot, std::move(ids));
        std::uint32_t target = list;
        if (ids != *game) {
            auto key = std::make_pair(list, std::move(ids));
            if (const auto found = made.find(key); found != made.end()) {
                target = found->second;
            } else {
                const std::vector<std::uint16_t> &stocked = key.second;
                const std::uint32_t bytes = 4u + (static_cast<std::uint32_t>(stocked.size()) + 1u) * 2u;
                if (next + bytes <= kCopiesEnd) {
                    // Written only where it differs: the same copies every frame.
                    const auto put16 = [&](std::uint32_t at, std::uint16_t value) {
                        if (ram.load16(at) != value) ram.store16(at, value);
                    };
                    if (ram.load32(next) != list) ram.store32(next, list);
                    target = next + 4u;
                    for (std::size_t i = 0; i < stocked.size(); ++i)
                        put16(target + static_cast<std::uint32_t>(i) * 2u, stocked[i]);
                    put16(target + static_cast<std::uint32_t>(stocked.size()) * 2u, kListEnd);
                    next += (bytes + 3u) & ~3u;
                    made.emplace(std::move(key), target);
                }
            }
        }
        if (pointer != target) ram.store32(slot, target);
    }
    return made.size();
}

} // namespace mhfu::game::shop_stock
