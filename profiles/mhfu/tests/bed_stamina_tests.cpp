// Bed stamina: the code check, the own hunter, the restore and the watch of a
// sleep, on a stand-in hunter and game_task. No game data.
#include "game/bed_stamina.hpp"

#include "game/guest_layout.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using namespace mhfu::game::bed_stamina;
namespace layout = mhfu::game::layout;

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
constexpr std::uint32_t kCamera = 0x09800000u;
constexpr std::uint32_t kHunter = 0x09900000u;
constexpr std::uint32_t kPartner = 0x09910000u;
constexpr std::uint32_t kGameSys = 0x09000000u;
constexpr std::uint8_t kMyConsole = 2u;

std::int16_t s16(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return static_cast<std::int16_t>(memory.load16(address));
}

void load_game_task(psprecomp::GuestMemory &memory) {
    memory.store32(layout::kTaskSlot, layout::kOverlayMagic);
    memory.store32(layout::kTaskSlot + 8u, layout::kTaskSlot);
    const std::string_view name = layout::kQuestOverlay;
    for (std::size_t i = 0; i < name.size(); ++i)
        memory.store8(layout::kTaskSlot + layout::kOverlayName + static_cast<std::uint32_t>(i),
            static_cast<std::uint8_t>(name[i]));
    memory.store8(layout::kTaskSlot + layout::kOverlayName + static_cast<std::uint32_t>(name.size()), 0u);
    for (const CodeWord &word : code_signature()) memory.store32(word.address, word.word);
}

void put_hunter(psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint16_t console) {
    memory.store32(hunter + layout::kVtable, layout::kHunterVtable);
    memory.store16(hunter + kConsole, console);
}

// A quest with this console's hunter followed by the camera.
void set_quest(psprecomp::GuestMemory &memory) {
    load_game_task(memory);
    memory.store32(layout::kGameSysPointer, kGameSys);
    memory.store8(kGameSys + kOwnConsole, kMyConsole);
    memory.store32(layout::kCameraObject, kCamera);
    memory.store32(kCamera + layout::kFollowedHunter, kHunter);
    put_hunter(memory, kHunter, kMyConsole);
}

void set_stamina(
    psprecomp::GuestMemory &memory, std::uint32_t hunter, std::int16_t most, std::int16_t left, std::int16_t count) {
    memory.store16(hunter + kMostStamina, static_cast<std::uint16_t>(most));
    memory.store16(hunter + kStamina, static_cast<std::uint16_t>(left));
    memory.store16(hunter + kStaminaCount, static_cast<std::uint16_t>(count));
}

void act(psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint8_t group, std::uint8_t action) {
    memory.store8(hunter + kGroup, group);
    memory.store8(hunter + kAction, action);
}

// One frame of the game's sleep: the step byte and the count it has reached.
void sleep(psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint8_t step, std::int32_t count) {
    act(memory, hunter, kBaseGroup, kSleep);
    memory.store8(hunter + kSleepStep, step);
    memory.store32(hunter + kSleepCount, static_cast<std::uint32_t>(count));
}

void test_code_check() {
    psprecomp::GuestMemory memory(kMemory);
    check(!code_matches(memory), "empty memory is not game_task");
    load_game_task(memory);
    check(code_matches(memory), "the traced words in game_task match");
    memory.store8(layout::kTaskSlot + layout::kOverlayName, 'l');
    check(!code_matches(memory), "another overlay in the task slot is not read");
    load_game_task(memory);
    memory.store32(code_signature()[8].address, 0x24060036u);
    check(!code_matches(memory), "one different word is another game_task");
}

void test_own_hunter() {
    psprecomp::GuestMemory memory(kMemory);
    check(!own_hunter(memory), "no camera, no hunter");
    set_quest(memory);
    check(own_hunter(memory) == kHunter, "the followed hunter of this console");
    memory.store16(kHunter + kConsole, kMyConsole + 1u);
    check(!own_hunter(memory), "a followed hunter of another console is not ours");
    put_hunter(memory, kHunter, kMyConsole);
    memory.store32(kHunter + layout::kVtable, 0x089BB000u);
    check(!own_hunter(memory), "an object that is not a hunter is not ours");
    put_hunter(memory, kHunter, kMyConsole);
    memory.store32(layout::kGameSysPointer, 0u);
    check(!own_hunter(memory), "no GameSys, no way to tell");
}

void test_raise() {
    psprecomp::GuestMemory memory(kMemory);
    set_stamina(memory, kHunter, 75, 40, 9);
    check(raise(memory, kHunter, kStaminaPerStep), "25 stamina is raised");
    check(s16(memory, kHunter + kMostStamina) == 81 && s16(memory, kHunter + kStamina) == 46,
        "by one step, the bar with it");
    check(s16(memory, kHunter + kStaminaCount) == kStaminaPeriod, "and it lasts six minutes again");

    set_stamina(memory, kHunter, 297, 297, 100);
    check(raise(memory, kHunter, kStaminaPerStep) && s16(memory, kHunter + kMostStamina) == kBaseStamina &&
            s16(memory, kHunter + kStamina) == kBaseStamina,
        "just under 100 stops at 100");

    set_stamina(memory, kHunter, 150, 0, 100);
    check(raise(memory, kHunter, 1000) && s16(memory, kHunter + kStamina) == kBaseStamina,
        "an empty bar is raised, no further than the most");

    set_stamina(memory, kHunter, kBaseStamina, 120, 100);
    check(!raise(memory, kHunter, kStaminaPerStep), "100 is left alone");
    check(s16(memory, kHunter + kStamina) == 120 && s16(memory, kHunter + kStaminaCount) == 100,
        "and so are the bar and the count");

    set_stamina(memory, kHunter, 450, 450, 5000);
    check(!raise(memory, kHunter, kStaminaPerStep) && s16(memory, kHunter + kMostStamina) == 450,
        "150 from a meal is kept");

    set_stamina(memory, kHunter, 75, 75, 9);
    check(!raise(memory, kHunter, 0) && s16(memory, kHunter + kMostStamina) == 75, "nothing to add, nothing written");
    check(!raise(memory, 0x7FFFFFF0u, kStaminaPerStep), "a hunter outside memory is not written");
}

void test_watch() {
    psprecomp::GuestMemory memory(kMemory);
    Watch watch;
    set_stamina(memory, kHunter, 75, 40, 2000);

    act(memory, kHunter, kBaseGroup, 0x33u);
    check(!watch.frame(memory, kHunter, true), "lying down raises nothing");
    sleep(memory, kHunter, 0u, 77);
    check(!watch.frame(memory, kHunter, true) && s16(memory, kHunter + kMostStamina) == 75,
        "nor does the sleep's first frame, whatever +0x414 held");
    sleep(memory, kHunter, kAsleep, kSleepFrames);
    check(!watch.frame(memory, kHunter, true) && s16(memory, kHunter + kMostStamina) == 75,
        "the count set, no health yet");
    sleep(memory, kHunter, kAsleep, kSleepFrames - 1);
    check(!watch.frame(memory, kHunter, true), "the sleep returns nothing while it lasts");
    check(s16(memory, kHunter + kMostStamina) == 81 && s16(memory, kHunter + kStamina) == 46,
        "each frame of health raises the stamina one step");
    check(s16(memory, kHunter + kStaminaCount) == kStaminaPeriod, "and starts the count again");
    sleep(memory, kHunter, kAsleep, kSleepFrames - 1);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 81, "a frame where the count stays raises nothing");
    sleep(memory, kHunter, kAsleep, kSleepFrames - 4);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 81 + 3 * kStaminaPerStep, "frames run together add up");
    sleep(memory, kHunter, kAsleep, 1);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == kBaseStamina, "a whole sleep fills it to 100");
    act(memory, kHunter, kBaseGroup, 0x35u);
    const std::optional<Restore> done = watch.frame(memory, kHunter, true);
    check(done && done->most == 75 && done->left == 40 && done->count == 2000, "getting up reports the sleep");
    check(done && done->most_after == kBaseStamina && done->left_after == kBaseStamina, "and where it ended");
    check(!watch.frame(memory, kHunter, true), "once");

    sleep(memory, kHunter, kAsleep, kSleepFrames);
    (void)watch.frame(memory, kHunter, true);
    sleep(memory, kHunter, kAsleep, 2);
    (void)watch.frame(memory, kHunter, true);
    set_stamina(memory, kHunter, 150, 150, 2000);
    sleep(memory, kHunter, kAsleep, 0);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150 + kStaminaPerStep, "the step to 0 gets up and gives nothing");
    act(memory, kHunter, kBaseGroup, 0u);
    (void)watch.frame(memory, kHunter, true);

    set_stamina(memory, kHunter, 150, 150, 2000);
    sleep(memory, kHunter, kAsleep, kSleepFrames);
    (void)watch.frame(memory, kHunter, true);
    sleep(memory, kHunter, kAsleep, kSleepFrames - 10);
    (void)watch.frame(memory, kHunter, true);
    act(memory, kHunter, kBaseGroup, 0u);
    const std::optional<Restore> cut = watch.frame(memory, kHunter, true);
    check(cut && cut->most_after == 150 + 10 * kStaminaPerStep, "a sleep cut short keeps what it filled");

    set_stamina(memory, kHunter, kBaseStamina, 200, 2000);
    sleep(memory, kHunter, kAsleep, kSleepFrames);
    (void)watch.frame(memory, kHunter, true);
    sleep(memory, kHunter, kAsleep, 1);
    (void)watch.frame(memory, kHunter, true);
    act(memory, kHunter, kBaseGroup, 0x35u);
    check(!watch.frame(memory, kHunter, true) && s16(memory, kHunter + kStamina) == 200,
        "a sleep at 100 changes nothing and reports nothing");

    set_stamina(memory, kHunter, 150, 150, 2000);
    sleep(memory, kHunter, kAsleep, 5);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150, "a sleep first seen late counts only what follows");
    sleep(memory, kHunter, kAsleep, 90);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150, "a count going up is a new sleep, not health");
    sleep(memory, kHunter, kAsleep, -3);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150, "nor is a count below 0");
    act(memory, kHunter, kBaseGroup, 0u);
    (void)watch.frame(memory, kHunter, true);

    set_stamina(memory, kHunter, 150, 150, 2000);
    act(memory, kHunter, 1u, kSleep);
    memory.store8(kHunter + kSleepStep, kAsleep);
    memory.store32(kHunter + kSleepCount, 50u);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150, "the same number in another group is not the bed");

    sleep(memory, kHunter, kAsleep, kSleepFrames);
    (void)watch.frame(memory, kHunter, true);
    sleep(memory, kHunter, kAsleep, 50);
    check(!watch.frame(memory, kHunter, false) && s16(memory, kHunter + kMostStamina) == 150,
        "turned off, the sleep raises nothing");

    sleep(memory, kHunter, kAsleep, kSleepFrames);
    (void)watch.frame(memory, kHunter, true);
    set_stamina(memory, kPartner, 150, 150, 2000);
    sleep(memory, kPartner, kAsleep, 50);
    (void)watch.frame(memory, kPartner, true);
    check(s16(memory, kPartner + kMostStamina) == 150, "another hunter's count is not this sleep's");
    sleep(memory, kHunter, kAsleep, 100);
    (void)watch.frame(memory, kHunter, true);
    sleep(memory, kHunter, kAsleep, 90);
    check(!watch.frame(memory, std::nullopt, true), "with no hunter, nothing is raised");
    sleep(memory, kHunter, kAsleep, 80);
    (void)watch.frame(memory, kHunter, true);
    check(s16(memory, kHunter + kMostStamina) == 150, "and the sleep before it is forgotten");
}

void test_frame() {
    psprecomp::Runtime runtime(kMemory);
    psprecomp::GuestMemory &memory = runtime.memory();
    auto &settings = mhfu::settings::current();
    settings.bed_stamina = true;
    set_quest(memory);

    const auto night = [&] {
        for (std::int32_t count = kSleepFrames; count > 0; --count) {
            sleep(memory, kHunter, kAsleep, count);
            frame(runtime);
        }
        act(memory, kHunter, kBaseGroup, 0x35u);
        frame(runtime);
    };

    set_stamina(memory, kHunter, 75, 75, 30);
    night();
    check(s16(memory, kHunter + kMostStamina) == kBaseStamina, "in a quest, the own hunter's sleep restores");

    set_stamina(memory, kHunter, 75, 75, 30);
    settings.bed_stamina = false;
    night();
    check(s16(memory, kHunter + kMostStamina) == 75, "the setting off leaves the game's stamina");

    settings.bed_stamina = true;
    memory.store32(code_signature()[0].address, 0u);
    night();
    check(s16(memory, kHunter + kMostStamina) == 75, "code that does not match is not read");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_bed_stamina_tests";
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_BED_STAMINA", "");
    set_environment("MHFU_TRACE_BED_STAMINA", "1");
    check(mhfu::settings::current().bed_stamina, "the bed restores stamina by default");
    test_code_check();
    test_own_hunter();
    test_raise();
    test_watch();
    test_frame();
    std::error_code ec;
    std::filesystem::remove_all(data, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "bed stamina tests passed\n";
    return 0;
}
