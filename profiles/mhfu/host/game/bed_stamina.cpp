#include "game/bed_stamina.hpp"

#include "game/guest_layout.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace mhfu::game::bed_stamina {
namespace {

constexpr std::array<CodeWord, 16> kSignature = {{
    {0x09A9A9E8u, 0x90820299u}, // lbu v0, 0x299(a0): group 0's action
    {0x09A9A9ECu, 0x2C410080u}, // sltiu at, v0, 128
    {0x09A9AD44u, 0x0E6A7F70u}, // jal 0x09A9FDC0: action 0x34, the sleep
    {0x09A9FDA0u, 0x24060034u}, // addiu a2, zero, 0x34: lying down, then the sleep
    {0x09A9FE20u, 0x24020078u}, // addiu v0, zero, 120: the sleep's frames
    {0x09A9FE24u, 0xAE020414u}, // sw v0, 0x414(s0)
    {0x09A9FFACu, 0x8F390068u}, // lw t9, 0x68(t9): health
    {0x09A9FFB4u, 0x24050002u}, // addiu a1, zero, 2
    {0x09A9FEA0u, 0x24060035u}, // addiu a2, zero, 0x35: getting up
    {0x09A9FEA4u, 0x0E699E1Eu}, // jal 0x09A67878: the action is set
    {0x09A6A7A4u, 0xA623062Eu}, // sh v1, 0x62E(s1): the most stamina
    {0x09A6A2A0u, 0x24022A30u}, // addiu v0, zero, 10800
    {0x09A6A2A4u, 0xA6420632u}, // sh v0, 0x632(s2): the count
    {0x09A9FDE8u, 0x908301D5u}, // lbu v1, 0x1D5(a0): the sleep's step
    {0x09A9FE68u, 0x2442FFFFu}, // addiu v0, v0, -1
    {0x09A9FE70u, 0xAE020414u}, // sw v0, 0x414(s0): one frame of the sleep
}};

bool trace_enabled() {
    static const bool on = [] {
        const char *text = std::getenv("MHFU_TRACE_BED_STAMINA");
        return text != nullptr && *text != '\0' && std::string(text) != "0";
    }();
    return on;
}

std::int16_t load_s16(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return static_cast<std::int16_t>(memory.load16(address));
}

} // namespace

std::span<const CodeWord> code_signature() {
    return kSignature;
}

bool code_matches(const psprecomp::GuestMemory &memory) {
    if (!layout::quest_running(memory)) return false;
    for (const CodeWord &expected : kSignature)
        if (!memory.contains(expected.address, 4u) || memory.load32(expected.address) != expected.word) return false;
    return true;
}

std::optional<std::uint32_t> own_hunter(const psprecomp::GuestMemory &memory) {
    const std::uint32_t hunter = layout::followed_hunter(memory, kHunterExtent);
    if (hunter == 0u || memory.load32(hunter + layout::kVtable) != layout::kHunterVtable) return std::nullopt;
    const std::uint32_t pointer = layout::kGameSysPointer;
    if (!memory.contains(pointer, 4u)) return std::nullopt;
    const std::uint32_t game_sys = memory.load32(pointer);
    if (!layout::in_ram(game_sys) || !memory.contains(game_sys + kOwnConsole, 1u)) return std::nullopt;
    if (memory.load16(hunter + kConsole) != memory.load8(game_sys + kOwnConsole)) return std::nullopt;
    return hunter;
}

bool raise(psprecomp::GuestMemory &memory, std::uint32_t hunter, int thirds) {
    if (thirds <= 0 || !memory.contains(hunter, kHunterExtent)) return false;
    const int most = load_s16(memory, hunter + kMostStamina);
    if (most >= kBaseStamina) return false;
    const int raised = std::min<int>(kBaseStamina, most + thirds);
    const int left = std::clamp<int>(load_s16(memory, hunter + kStamina) + thirds, 0, raised);
    memory.store16(hunter + kMostStamina, static_cast<std::uint16_t>(raised));
    memory.store16(hunter + kStamina, static_cast<std::uint16_t>(left));
    memory.store16(hunter + kStaminaCount, static_cast<std::uint16_t>(kStaminaPeriod));
    return true;
}

std::optional<Restore> Watch::frame(
    psprecomp::GuestMemory &memory, std::optional<std::uint32_t> hunter, bool on) {
    if (!on || !hunter || !memory.contains(*hunter, kHunterExtent)) {
        hunter_ = 0u;
        asleep_ = false;
        raised_.reset();
        return std::nullopt;
    }
    if (*hunter != hunter_) {
        hunter_ = *hunter;
        asleep_ = false;
        raised_.reset();
    }
    const bool sleeping = memory.load8(*hunter + kGroup) == kBaseGroup && memory.load8(*hunter + kAction) == kSleep;
    if (!sleeping) {
        asleep_ = false;
        std::optional<Restore> done = std::exchange(raised_, std::nullopt);
        if (done) {
            done->most_after = load_s16(memory, *hunter + kMostStamina);
            done->left_after = load_s16(memory, *hunter + kStamina);
        }
        return done;
    }
    const bool counting = memory.load8(*hunter + kSleepStep) == kAsleep;
    const auto count = static_cast<std::int32_t>(memory.load32(*hunter + kSleepCount));
    if (!asleep_) {
        // From its first frame the whole sleep is seen; from later on, only
        // what follows.
        asleep_ = true;
        count_ = counting ? std::clamp(count, 0, kSleepFrames) : kSleepFrames;
    }
    if (!counting) return std::nullopt;
    // The count only goes down in a sleep; anything else is a new one.
    const std::int32_t steps = count >= 0 && count < count_ ? count_ - count : 0;
    count_ = std::clamp(count, 0, kSleepFrames);
    // The step to 0 gets the hunter up instead of giving health.
    const std::int32_t healed = count > 0 ? steps : steps - 1;
    if (healed <= 0) return std::nullopt;
    const Restore before{*hunter, load_s16(memory, *hunter + kMostStamina), load_s16(memory, *hunter + kStamina),
        load_s16(memory, *hunter + kStaminaCount)};
    if (raise(memory, *hunter, healed * kStaminaPerStep) && !raised_) raised_ = before;
    return std::nullopt;
}

void frame(psprecomp::Runtime &runtime) {
    static Watch watch;
    psprecomp::GuestMemory &memory = runtime.memory();
    const bool matches = code_matches(memory);
    const std::optional<Restore> restored = watch.frame(
        memory, matches ? own_hunter(memory) : std::nullopt, matches && settings::current().bed_stamina);
    if (restored && trace_enabled())
        std::cout << "[bed stamina] hunter 0x" << std::hex << restored->hunter << std::dec << ": most "
                  << restored->most << ", left " << restored->left << ", " << restored->count
                  << " frames to the next drop -> " << restored->most_after << ", " << restored->left_after << ", "
                  << kStaminaPeriod << std::endl;
}

} // namespace mhfu::game::bed_stamina
