#pragma once

// The pouch as the game sees it now, for the host features that read it:
// 24 slots at GameSys + 0x6AEA6, or 32 in a block of the host's while the
// fourth pouch page is on (game/pouch_page.hpp turns it on and off). Either
// way one more index follows the slots: the game's 25th word, which the bow
// keeps its coatings in and Empty item slot stops on.

#include <cstdint>

namespace mhfu::game::pouch_slots {

inline constexpr std::uint32_t kGamePouch = 0x6AEA6u; // from GameSys
inline constexpr std::uint32_t kGameSlots = 24u;
inline constexpr std::uint32_t kSlots = 32u;           // with the page
inline constexpr std::uint32_t kSlotBytes = 4u;
inline constexpr std::uint32_t kGameExtra = kGamePouch + kGameSlots * kSlotBytes; // the 25th word
// The page's block, in the window the host reserves below the load image.
inline constexpr std::uint32_t kBlock = 0x08803000u;

// On while the hunter in memory has the page and its hooks run.
inline bool &fourth_page() {
    static bool on = false;
    return on;
}

// The slots: 24, or 32 with the page.
[[nodiscard]] inline std::uint32_t count() {
    return fourth_page() ? kSlots : kGameSlots;
}

// Slot `index`, 0 to count(), the last being the 25th word; 0 past it.
[[nodiscard]] inline std::uint32_t address(std::uint32_t game_sys, std::uint32_t index) {
    if (fourth_page()) {
        if (index < kSlots) return kBlock + index * kSlotBytes;
        return index == kSlots ? game_sys + kGameExtra : 0u;
    }
    return index <= kGameSlots ? game_sys + kGamePouch + index * kSlotBytes : 0u;
}

// Where slot 0 is: the slots before count() follow it four bytes apart.
[[nodiscard]] inline std::uint32_t first(std::uint32_t game_sys) {
    return fourth_page() ? kBlock : game_sys + kGamePouch;
}

} // namespace mhfu::game::pouch_slots
