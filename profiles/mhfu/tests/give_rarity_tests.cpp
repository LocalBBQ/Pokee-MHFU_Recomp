// Give at any rarity: the item lookup, the code check, and the wrapper
// around a stand-in for the game's check. No game data.
#include "game/give_rarity.hpp"

#include "game/pouch_slots.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
using namespace mhfu::game::give_rarity;

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

constexpr std::uint32_t kHunter = 0x09900000u;
constexpr std::uint32_t kGameSys = 0x09000000u;
constexpr std::uint32_t kReturn = 0x08816F90u;
int calls{};
std::uint8_t seen_rarity{};

std::uint32_t rarity_of(std::uint16_t id) {
    return kItemTable + id * kItemRecord + kRarity;
}

void put_item(psprecomp::GuestMemory &memory, std::uint8_t slot, std::uint16_t id, std::uint8_t rarity) {
    memory.store32(kGameSysPointer, kGameSys);
    memory.store8(kHunter + kSelectedSlot, slot);
    memory.store16(kGameSys + kPouch + slot * 4u, id);
    memory.store8(rarity_of(id), rarity);
}

// The game's check as far as rarity goes: yes below Rare 4.
void game_check(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++calls;
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t slot = memory.load8(ctx.gpr[4] + kSelectedSlot);
    const std::uint16_t id = memory.load16(memory.load32(kGameSysPointer) + kPouch + slot * 4u);
    seen_rarity = memory.load8(rarity_of(id));
    ctx.gpr[2] = seen_rarity < kGameLimit ? 1u : 0u;
    ctx.pc = ctx.gpr[31];
}

void write_signature(psprecomp::GuestMemory &memory) {
    for (const CodeWord &word : code_signature()) memory.store32(word.address, word.word);
}

std::uint32_t run_check(psprecomp::Runtime &runtime) {
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = kHunter;
    ctx.gpr[31] = kReturn;
    runtime.function_at(kCanGive)(runtime, ctx);
    check(ctx.pc == kReturn, "the check returns to its caller");
    return ctx.gpr[2];
}

void test_rarity_address() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    put_item(memory, 3u, 220u, 5u);
    check(rarity_address(memory, kHunter) == rarity_of(220u), "the selected slot's item record");
    memory.store8(kHunter + kSelectedSlot, 30u);
    memory.store16(kGameSys + kPouch, 12u);
    check(rarity_address(memory, kHunter) == rarity_of(12u), "a slot past the pouch reads slot 0, as the game does");
    memory.store16(kGameSys + kPouch, 0u);
    check(!rarity_address(memory, kHunter), "no item has no record");
    memory.store16(kGameSys + kPouch, 0x8001u);
    check(!rarity_address(memory, kHunter), "an id the game reads as negative has no record");
    check(!rarity_address(memory, 0x7FFFFFF0u), "a hunter outside memory has no record");
}

// With the fourth pouch page the slots are the host's block and 32 is the
// game's 25th word.
void test_rarity_fourth_page() {
    namespace slots = mhfu::game::pouch_slots;
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    memory.store32(kGameSysPointer, kGameSys);
    slots::fourth_page() = true;
    memory.store8(kHunter + kSelectedSlot, 28u);
    memory.store16(slots::kBlock + 28u * 4u, 220u);
    check(rarity_address(memory, kHunter) == rarity_of(220u), "a fourth page slot's item record");
    memory.store8(kHunter + kSelectedSlot, 32u);
    memory.store16(kGameSys + slots::kGameExtra, 14u);
    check(rarity_address(memory, kHunter) == rarity_of(14u), "slot 32 is the game's 25th word");
    memory.store8(kHunter + kSelectedSlot, 33u);
    memory.store16(slots::kBlock, 12u);
    check(rarity_address(memory, kHunter) == rarity_of(12u), "past slot 32 reads slot 0, as the game does");
    slots::fourth_page() = false;
}

void test_code_check() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    check(!code_matches(memory), "empty memory is not the executable");
    write_signature(memory);
    check(code_matches(memory), "the traced words are the executable");
    memory.store32(code_signature()[6].address, 0x28420006u);
    check(!code_matches(memory), "one different word is another executable");
}

void test_wrapper() {
    psprecomp::Runtime runtime(32u * 1024u * 1024u);
    psprecomp::GuestMemory &memory = runtime.memory();
    write_signature(memory);
    mhfu::settings::current().give_any_rarity = true;
    mhfu::game::give_rarity::frame(runtime);
    check(runtime.function_at(kCanGive) == nullptr, "nothing is put in place before the code is installed");

    runtime.register_function(kCanGive, &game_check, "game_check");
    mhfu::game::give_rarity::frame(runtime);
    check(runtime.function_at(kCanGive) != &game_check, "the check is wrapped");
    mhfu::game::give_rarity::frame(runtime);

    put_item(memory, 2u, 300u, 6u);
    calls = 0;
    check(run_check(runtime) == 1u, "a Rare 7 item can be given");
    check(calls == 1, "the game's check runs once");
    check(seen_rarity == 0u, "the game sees the lifted rarity");
    check(memory.load8(rarity_of(300u)) == 6u, "the record's rarity is put back");

    put_item(memory, 2u, 301u, kGameLimit);
    check(run_check(runtime) == 1u && memory.load8(rarity_of(301u)) == kGameLimit, "the first rarity over the limit");

    put_item(memory, 2u, 302u, 1u);
    check(run_check(runtime) == 1u && seen_rarity == 1u, "a rarity under the limit is not touched");

    auto &settings = mhfu::settings::current();
    settings.give_any_rarity = false;
    put_item(memory, 2u, 300u, 6u);
    check(run_check(runtime) == 0u, "turned off, the game's limit holds");
    check(memory.load8(rarity_of(300u)) == 6u, "and the record is left alone");
    settings.give_any_rarity = true;
    check(run_check(runtime) == 1u, "turned on again in the session, the next call lifts it");
}

void test_other_code() {
    psprecomp::Runtime runtime(32u * 1024u * 1024u);
    runtime.register_function(kCanGive, &game_check, "other_code");
    mhfu::game::give_rarity::frame(runtime);
    check(runtime.function_at(kCanGive) == &game_check, "code that does not match is not wrapped");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_give_rarity_tests";
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_GIVE_ANY_RARITY", "");
    set_environment("MHFU_TRACE_GIVE", "");
    check(!mhfu::settings::current().give_any_rarity, "giving at any rarity is off by default");
    test_rarity_address();
    test_rarity_fourth_page();
    test_code_check();
    test_other_code();
    test_wrapper();
    std::error_code ec;
    std::filesystem::remove_all(data, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "give rarity tests passed\n";
    return 0;
}
