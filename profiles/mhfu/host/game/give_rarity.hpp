#pragma once

// Give at any rarity: in the Guild Hall, Start > Item > Give hands an
// item of any rarity to another hunter, not only Rare 1 to 3. On by default
// (Mods page, `game.give_any_rarity`; `MHFU_GIVE_ANY_RARITY=0` turns it
// off). Off, the game's own limit applies. The switch is read at each call.
//
// The executable (ULUS-10391) decides whether the selected pouch item can be
// given in 0x08829928(hunter). The item menu calls it to offer the entry
// (0x08816F88) and again when it is chosen (0x0881CF64, 0x0881D24C); 1 is yes.
// It reads the item id from the pouch slot in the hunter's byte at +1414
// (0x08826734, through 0x08856884: GameSys + 0x6AEA6 + 4 * slot, slot 0 for
// 25 or more; 33 or more with the fourth pouch page) and answers 0 when the rarity, byte 2 of the item's 24-byte
// record in the table at 0x0899D2B8, is not below 3 (0x08829988, slti 3).
// Rarity 0 is Rare 1. Its other checks (the record's byte 3 is not 255, the
// session and the stage) follow.
//
// The wrapper sets that one record's rarity to 0 for the call and puts it
// back after, so every other check stays the game's.

#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::give_rarity {

inline constexpr std::uint32_t kCanGive = 0x08829928u;
inline constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
inline constexpr std::uint32_t kPouch = 0x6AEA6u;
inline constexpr std::uint32_t kSelectedSlot = 1414u;
inline constexpr std::uint32_t kItemTable = 0x0899D2B8u;
inline constexpr std::uint32_t kItemRecord = 24u;
inline constexpr std::uint32_t kRarity = 2u;
inline constexpr std::uint8_t kGameLimit = 3u; // rarity bytes below this can be given

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};

// What the addresses above were read off. A mismatch means another
// executable, and nothing is wrapped.
[[nodiscard]] std::span<const CodeWord> code_signature();
[[nodiscard]] bool code_matches(const psprecomp::GuestMemory &memory);

// The address of the rarity byte of the item in the hunter's selected pouch
// slot, as 0x08829928 finds it; nothing for a pointer or id out of range.
[[nodiscard]] std::optional<std::uint32_t> rarity_address(const psprecomp::GuestMemory &memory, std::uint32_t hunter);

// At the flip: puts the wrapper in place once the executable's code is
// installed. Runs every frame; wrapping again is a no-op.
void frame(psprecomp::Runtime &runtime);

} // namespace mhfu::game::give_rarity
