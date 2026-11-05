#pragma once

// Sleeping in a camp bed restores stamina: while the bed fills the hunter's
// health, stamina below 100 fills with it, at the same speed on the HUD, up
// to 100, as at the start of the quest. More than 100, from a meal or a
// steak, is kept. On by default (Mods page, `game.bed_stamina`;
// `MHFU_BED_STAMINA=0` turns it off). Off, the bed restores health only, as in
// the game. `MHFU_TRACE_BED_STAMINA=1` prints each sleep that raised it.
//
// Read off the USA game_task overlay (ULUS-10391):
//
// Stamina. The hunter keeps it in thirds: the most stamina is the signed
// halfword at +0x62E and the stamina left the one at +0x55A, so 300 is 100
// on the bar (the HUD, 0x088405E4, scales both by 1/300 of the 100-stamina
// bar, and health, +0x2E4, by 1/100 of the same bar: one health is three
// thirds). 0x09A6A738(hunter, change) adds to the most stamina, only for the
// console's own hunter (+0x1E6 equal to the byte at GameSys +0x28), holds it
// between 75 and 450 (25 and 150) and lowers the stamina left to it. The
// halfword at +0x632 counts the frames until the most stamina next drops:
// 0x09A6A288 counts it down, and at 0 sets it to 10800 (six minutes) and takes
// 75 off through 0x09A6A738. The quest's start (0x09A65690) sets the most
// stamina to 300 plus the meal's, the stamina left to it and the count to
// 10800.
//
// The bed. Its interaction point (type 0x1E in 0x09AA64B0) puts the hunter
// beside it and starts action 0x33 of group 0 (the hunter's +0x298 and
// +0x299), lying down. 0x09A9A9E0 runs group 0's actions from a table of 128.
// 0x33 (0x09A9FCC0) moves on to 0x34 once lying down. 0x34 (0x09A9FDC0) is the
// sleep: its first frame sets the step byte +0x1D5 to 1 and the word at +0x414
// to 120. Each frame after, it takes one off +0x414, and while that is above
// 0 gives 2 health through the hunter's +0x68 method (0x088D6594, only for
// the console's own hunter, up to the most health at +0x41E); at 0 it gives
// the console's own hunter action 0x35, getting up.
//
// Each frame of a quest the hunter the camera follows is read, if it is the
// console's own. For each step +0x414 has gone down in its sleep, the most
// stamina below 300 goes up 6 (the 2 health's width on the bar), up to 300,
// the stamina left with it, and the count starts again at 10800. A whole
// sleep fills it from the least, 75, with frames to spare; a sleep cut short
// keeps what it filled, as it keeps the health. The other hunters' stamina is
// their own consoles' to restore. The words above are checked against the
// game's code before anything is read.

#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace psprecomp {
class Runtime;
}

namespace mhfu::game::bed_stamina {

// The hunter's fields.
inline constexpr std::uint32_t kGroup = 0x298u;
inline constexpr std::uint32_t kAction = 0x299u;
inline constexpr std::uint32_t kStamina = 0x55Au;
inline constexpr std::uint32_t kMostStamina = 0x62Eu;
inline constexpr std::uint32_t kStaminaCount = 0x632u;
inline constexpr std::uint32_t kConsole = 0x1E6u;
inline constexpr std::uint32_t kSleepStep = 0x1D5u;
inline constexpr std::uint32_t kSleepCount = 0x414u;
inline constexpr std::uint32_t kHunterExtent = kStaminaCount + 2u;
// GameSys +0x28: which hunter is this console's.
inline constexpr std::uint32_t kOwnConsole = 0x28u;

inline constexpr std::uint8_t kBaseGroup = 0u;
inline constexpr std::uint8_t kSleep = 0x34u;
inline constexpr std::uint8_t kAsleep = 1u; // the sleep's step once it counts
inline constexpr std::int32_t kSleepFrames = 120;

// 100 stamina, in the game's thirds, and the frames it lasts before it drops.
inline constexpr std::int16_t kBaseStamina = 300;
inline constexpr std::int16_t kStaminaPeriod = 10800;
// What one step of the sleep adds: 2 health, three thirds each.
inline constexpr std::int16_t kStaminaPerStep = 6;

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};

// The game_task words the addresses above were read off. A mismatch is
// another overlay or another release, and nothing is read.
[[nodiscard]] std::span<const CodeWord> code_signature();
[[nodiscard]] bool code_matches(const psprecomp::GuestMemory &memory);

// The console's own hunter, the one the camera follows, or nothing.
[[nodiscard]] std::optional<std::uint32_t> own_hunter(const psprecomp::GuestMemory &memory);

// What a sleep changed, for the trace.
struct Restore {
    std::uint32_t hunter{};
    std::int16_t most{};       // the most stamina before
    std::int16_t left{};       // the stamina left before
    std::int16_t count{};      // the frames that were left until it dropped
    std::int16_t most_after{}; // and after
    std::int16_t left_after{};
};

// Raises the most stamina by `thirds`, up to kBaseStamina, and the stamina
// left with it, and starts the count again. False, and nothing written, when
// the most stamina is already kBaseStamina or more.
bool raise(psprecomp::GuestMemory &memory, std::uint32_t hunter, int thirds);

// Follows one hunter's sleep from frame to frame.
class Watch {
public:
    // One frame. Raises the stamina by the sleep's steps since the last
    // frame; returns what the sleep changed on the frame it ends, if it
    // raised anything. Another hunter, no hunter, or `on` false starts again.
    std::optional<Restore> frame(psprecomp::GuestMemory &memory, std::optional<std::uint32_t> hunter, bool on);

private:
    std::uint32_t hunter_{};
    bool asleep_{};
    std::int32_t count_{}; // +0x414 on the last frame of the sleep
    std::optional<Restore> raised_;
};

// At the flip: one frame of the watch, while game_task's code matches.
void frame(psprecomp::Runtime &runtime);

} // namespace mhfu::game::bed_stamina
