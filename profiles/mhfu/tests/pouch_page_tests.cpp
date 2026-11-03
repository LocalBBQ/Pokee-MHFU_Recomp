// Fourth pouch page: the site list and what each action does to the
// registers, the blocks kept in step with the game's arrays, the pages file,
// and the frame, save and load on a runtime with stand-ins for the game's
// memory. No game data.
#include "game/pouch_page.hpp"

#include "game/guest_ram.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace {
using namespace mhfu::game::pouch_page;
using mhfu::game::BufferRam;

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
    if (value.empty())
        unsetenv(name);
    else
        setenv(name, value.c_str(), 1);
#endif
}

constexpr std::uint32_t kSys = 0x09000000u;

// Encodings for the actions' instructions.
constexpr std::uint32_t i_type(std::uint32_t op, std::uint32_t rs, std::uint32_t rt, std::uint32_t immediate) {
    return (op << 26u) | (rs << 21u) | (rt << 16u) | (immediate & 0xFFFFu);
}
constexpr std::uint32_t addu(std::uint32_t rd, std::uint32_t rs, std::uint32_t rt) {
    return (rs << 21u) | (rt << 16u) | (rd << 11u) | 0x21u;
}

Site site_of(std::string_view line) {
    const auto parsed = parse_sites(line);
    if (!parsed || parsed->size() != 1u) {
        check(false, "a test site did not parse");
        return {};
    }
    return parsed->front();
}

void test_parse() {
    std::string error;
    const auto parsed = parse_sites("# comment\n\n"
                                    "eboot 0x08856884 0x28A20019 1 bound 33 # trailing\n"
                                    "eboot 0x0 0x0 2 set C-0x3F28\n"
                                    "eboot 0x0 0x0 3 set 0x80\n"
                                    "eboot 0x0 0x0 4 setreg s0 W1\n"
                                    "lobby_task 0x0 0x0 5 rebase s1 C-0x3F28\n"
                                    "lobby_task 0x0 0x0 6 rebase gamesys C-0x43C8\n"
                                    "game_task 0x0 0x0 7 direct\n"
                                    "eboot 0x0 0x0 8 pouch_slot\n"
                                    "game_task 0x0 0x0 9 cap_base s4\n"
                                    "game_task 0x0 0x0 10 cap_check s4 0x60\n"
                                    "em59 0x0 0x0 11 rebase sp W2-0x80\n"
                                    "em20 0x0 0x0 12 set W3+8\n",
        &error);
    check(parsed.has_value(), "a good list did not parse");
    if (!parsed) return;
    check(parsed->size() == 12u, "the list had the wrong number of sites");
    const Site &bound = (*parsed)[0];
    check(bound.image == "eboot" && bound.address == 0x08856884u && bound.word == 0x28A20019u && bound.number == 1u &&
            bound.action == Action::Bound && bound.value == Value{Base::Number, 33},
        "bound parsed wrong");
    check((*parsed)[1].value == Value{Base::Saved, -0x3F28}, "a block minus an offset parsed wrong");
    check((*parsed)[2].value == Value{Base::Number, 0x80}, "a hex number parsed wrong");
    check((*parsed)[3].action == Action::SetReg && (*parsed)[3].reg == 16u && (*parsed)[3].value.base == Base::List1,
        "setreg parsed wrong");
    check((*parsed)[4].action == Action::Rebase && (*parsed)[4].reg == 17u, "rebase from a register parsed wrong");
    check((*parsed)[5].reg == kGameSysOperand && (*parsed)[5].value == Value{Base::Saved, -0x43C8},
        "rebase from GameSys parsed wrong");
    check((*parsed)[6].action == Action::Direct && (*parsed)[7].action == Action::PouchSlot, "direct parsed wrong");
    check((*parsed)[8].action == Action::CapBase && (*parsed)[8].reg == 20u, "cap_base parsed wrong");
    check((*parsed)[9].action == Action::CapCheck && (*parsed)[9].limit == 0x60u, "cap_check parsed wrong");
    check((*parsed)[10].reg == 29u && (*parsed)[10].value == Value{Base::List2, -0x80}, "W2 parsed wrong");
    check((*parsed)[11].value == Value{Base::List3, 8}, "a block plus an offset parsed wrong");

    for (const char *bad : {"eboot 0x0 0x0 1 jump 3", "eboot 0x0 0x0 1 bound", "eboot 0x0 0x0 1 bound 3 4",
             "eboot 0x0 0x0 1 setreg q9 C", "eboot 0x0 0x0 1 setreg gamesys C", "eboot 0x0 0x0 1 set X",
             "eboot 0x0 0x0 1 set C*2", "eboot 0x0 0x0 0 set 1", "eboot 0x0 0x0 1 direct 3",
             "eboot 0x0 0x0 1 cap_check s4", "eboot 0x0 0x0 1 cap_check s4 0", "eboot 0x0 0x0 1 cap_base gamesys",
             "eboot 0x0 nope 1 set 1", "eboot 0x0 0x0"}) {
        std::string why;
        check(!parse_sites(bad, &why).has_value() && why.find("line 1") == 0u, bad);
    }
}

// The list built in: what the recompiler was given, checked against what the
// actions need of their instructions.
void test_built_in_list() {
    const auto &list = sites();
    check(list.size() == 192u, "the built-in list is not the 192 sites");
    std::set<std::uint32_t> numbers;
    std::set<std::pair<std::string, std::uint32_t>> places;
    for (const Site &site : list) {
        check(numbers.insert(site.number).second, "a site number is used twice");
        check(places.insert({site.image, site.address}).second, "an address is listed twice");
        const std::uint32_t op = site.word >> 26u;
        const std::uint8_t dest = destination(site.word);
        switch (site.action) {
        case Action::Bound:
            check(op == 0x0Au || op == 0x0Bu, "a bound is not on an slti or sltiu");
            check((site.word & 0xFFFFu) == 24u || (site.word & 0xFFFFu) == 25u, "a bound is not against 24 or 25");
            break;
        case Action::Rebase:
            check(op == 0u && (site.word & 0x3Fu) == 0x21u, "a rebase is not on an addu");
            check(site.reg == kGameSysOperand || site.reg != dest, "a rebase reads the register it writes");
            check(site.reg == kGameSysOperand || site.reg == ((site.word >> 21u) & 31u) ||
                    site.reg == ((site.word >> 16u) & 31u),
                "a rebase's register is not an operand");
            break;
        case Action::Direct:
            check(op == 0u && (site.word & 0x3Fu) == 0x21u, "a direct site is not on an addu");
            break;
        default:
            break;
        }
        if (site.action != Action::SetReg && site.action != Action::CapBase)
            check(dest != 0u, "a site writes register zero");
    }
    check(numbers.size() == list.size() && *numbers.begin() == 1u && *numbers.rbegin() == list.size(),
        "the site numbers are not 1 to the count");
}

void test_slot_address() {
    check(slot_address(kSys, 0u) == kPouch, "slot 0 is not the block's first");
    check(slot_address(kSys, 31u) == kPouch + 31u * 4u, "slot 31 is not the block's last");
    check(slot_address(kSys, kBowSlot) == kSys + kGameBowSlot, "the bow's slot is not where the game keeps it");
    check(slot_address(kSys, 33u) == 0u, "slot 33 has an address");
    check(destination(addu(3u, 4u, 5u)) == 3u && destination(i_type(0x09u, 0u, 7u, 1u)) == 7u,
        "the destination register is wrong");
    check(value_of({Base::Pouch, 4}) == kPouch + 4u && value_of({Base::Number, 32}) == 32u &&
            value_of({Base::Aside, 0}) == kAside && value_of({Base::HunterCopy, -0x568}) == kHunterCopy - 0x568u &&
            value_of({Base::List2, 0}) == kList2 && value_of({Base::List3, 0}) == kList3,
        "a value is wrong");
}

void test_apply() {
    CapState cap;
    psprecomp::AllegrexContext ctx{};

    // slti v0, a1, 25 with the page: 32 (the bow's slot) is in, 33 is not.
    const std::string bound_line = "eboot 0x0 " + std::to_string(i_type(0x0Au, 5u, 2u, 25u)) + " 1 bound 33";
    const Site bound = site_of(bound_line);
    ctx.gpr[5] = 32u;
    apply(bound, ctx, kSys, cap);
    check(ctx.gpr[2] == 1u, "bound: 32 was not below 33");
    ctx.gpr[5] = 33u;
    apply(bound, ctx, kSys, cap);
    check(ctx.gpr[2] == 0u, "bound: 33 was below 33");
    ctx.gpr[5] = 0xFFFFFFFFu; // -1, signed
    apply(bound, ctx, kSys, cap);
    check(ctx.gpr[2] == 1u, "bound: slti did not compare signed");
    const Site unsigned_bound = site_of("eboot 0x0 " + std::to_string(i_type(0x0Bu, 5u, 2u, 24u)) + " 1 bound 32");
    apply(unsigned_bound, ctx, kSys, cap);
    check(ctx.gpr[2] == 0u, "bound: sltiu did not compare unsigned");

    // set / setreg.
    apply(site_of("eboot 0x0 " + std::to_string(i_type(0x09u, 0u, 7u, 24u)) + " 2 set 32"), ctx, kSys, cap);
    check(ctx.gpr[7] == 32u, "set did not write the count");
    apply(site_of("eboot 0x0 " + std::to_string(addu(16u, 2u, 0u)) + " 3 set C-0x3F28"), ctx, kSys, cap);
    check(ctx.gpr[16] == kSaved - 0x3F28u, "set did not write the block's address");
    apply(site_of("eboot 0x0 " + std::to_string(i_type(0x09u, 0u, 5u, 2u)) + " 4 setreg s0 W1"), ctx, kSys, cap);
    check(ctx.gpr[16] == kList1 && ctx.gpr[5] != kList1, "setreg wrote the wrong register");
    apply(site_of("eboot 0x0 0x0 4 setreg zero 9"), ctx, kSys, cap);
    check(ctx.gpr[0] == 0u, "register zero was written");

    // rebase: addu v1, v0, s1 with s1 = GameSys + 0x4A0 and v0 = 4 * 5.
    ctx.gpr[17] = kSys + 0x4A0u;
    ctx.gpr[3] = 20u + ctx.gpr[17];
    apply(site_of("lobby_task 0x0 " + std::to_string(addu(3u, 2u, 17u)) + " 5 rebase s1 C-0x3F28"), ctx, kSys, cap);
    check(ctx.gpr[3] + 0x3F28u == kSaved + 20u, "rebase did not move the address to the block");
    ctx.gpr[2] = kSys + 12u;
    apply(site_of("lobby_task 0x0 " + std::to_string(addu(2u, 3u, 2u)) + " 6 rebase gamesys C-0x43C8"), ctx, kSys, cap);
    check(ctx.gpr[2] + 0x43C8u == kSaved + 12u, "rebase from GameSys did not move the address");

    // direct: GameSys + 4i + 0x70000, read at -0x515A.
    const Site direct = site_of("game_task 0x0 " + std::to_string(addu(2u, 3u, 2u)) + " 7 direct");
    ctx.gpr[2] = kSys + 4u * 27u + kDirectBias;
    apply(direct, ctx, kSys, cap);
    check(ctx.gpr[2] - kDirectOffset == kPouch + 4u * 27u, "direct did not reach slot 27 of the block");
    ctx.gpr[2] = kSys + 4u * kBowSlot + kDirectBias;
    apply(direct, ctx, kSys, cap);
    check(ctx.gpr[2] - kDirectOffset == kSys + kGameBowSlot, "direct did not reach the bow's slot");
    ctx.gpr[2] = kSys + 2u + kDirectBias;
    apply(direct, ctx, kSys, cap);
    check(ctx.gpr[2] == kSys + 2u + kDirectBias, "direct changed an address that is not a slot");

    // pouch_slot: 0x08856884's v0 = a0 + 4i + 0x6AEA6.
    const Site pouch_slot = site_of("eboot 0x0 " + std::to_string(addu(2u, 2u, 1u)) + " 8 pouch_slot");
    ctx.gpr[4] = kSys;
    ctx.gpr[2] = kSys + kGamePouch + 4u * 30u;
    apply(pouch_slot, ctx, kSys, cap);
    check(ctx.gpr[2] == kPouch + 4u * 30u, "pouch_slot did not give slot 30 of the block");
    ctx.gpr[2] = kSys + kGamePouch + 4u * kBowSlot;
    apply(pouch_slot, ctx, kSys, cap);
    check(ctx.gpr[2] == kSys + kGameBowSlot, "pouch_slot did not give the bow's slot");

    // cap: past 0x60 bytes of list, the flag reads 0.
    const Site base = site_of("game_task 0x0 " + std::to_string(addu(16u, 0u, 0u)) + " 9 cap_base s4");
    const Site check_site =
        site_of("game_task 0x0 " + std::to_string(i_type(0x0Cu, 3u, 3u, 0x10u)) + " 10 cap_check s4 0x60");
    ctx.gpr[20] = 0x09000796u;
    apply(base, ctx, kSys, cap);
    ctx.gpr[20] += 0x5Cu;
    ctx.gpr[3] = 0x10u;
    apply(check_site, ctx, kSys, cap);
    check(ctx.gpr[3] == 0x10u, "cap cleared the flag before the list was full");
    ctx.gpr[20] += 4u;
    apply(check_site, ctx, kSys, cap);
    check(ctx.gpr[3] == 0u, "cap did not clear the flag once the list was full");
}

std::uint32_t slot_word(std::uint16_t item, std::uint16_t count) {
    return static_cast<std::uint32_t>(item) | (static_cast<std::uint32_t>(count) << 16u);
}

void test_blocks() {
    BufferRam ram(0x08800000u, 0x00900000u);
    for (std::uint32_t i = 0; i < kGameSlots; ++i) {
        ram.store32(kSys + kGamePouch + i * 4u, slot_word(static_cast<std::uint16_t>(100u + i), 1u));
        ram.store32(kSys + kGameSaved + i * 4u, slot_word(static_cast<std::uint16_t>(200u + i), 2u));
        ram.store32(kSys + kGameAside + i * 4u, slot_word(static_cast<std::uint16_t>(300u + i), 3u));
    }
    ram.store32(kSys + kGameBowSlot, slot_word(0x80u, 0xFFu));
    ram.store32(kHunterCopy, 0xDEADBEEFu);
    ram.store32(kList3 + 0x7Cu, 0xDEADBEEFu);
    HunterPage page;
    page.owned = true;
    page.slots[0][0] = {7u, 5u};
    page.slots[1][7] = {8u, 6u};
    page.slots[2][3] = {9u, 4u};
    Mirror mirror = fill_blocks(ram, kSys, page);
    check(ram.load32(kPouch + 5u * 4u) == slot_word(105u, 1u), "the pouch's slots did not fill the block");
    check(ram.load32(kSaved + 23u * 4u) == slot_word(223u, 2u), "the saved pouch did not fill its block");
    check(ram.load32(kPouch + 24u * 4u) == slot_word(7u, 5u), "the page did not fill the pouch's block");
    check(ram.load32(kSaved + 31u * 4u) == slot_word(8u, 6u), "the page did not fill the saved block");
    check(ram.load32(kAside + 27u * 4u) == slot_word(9u, 4u), "the page did not fill the aside block");
    check(ram.load32(kHunterCopy) == 0u && ram.load32(kList3 + 0x7Cu) == 0u, "the copy and lists were not cleared");
    check(ram.load32(kSys + kGameBowSlot) == slot_word(0x80u, 0xFFu), "the bow's slot moved");
    check(mirror[0][0] == slot_word(100u, 1u) && mirror[2][23] == slot_word(323u, 3u), "the mirror is wrong");
    check(page_of(ram) == page, "page_of did not read the page back");

    // The block's change goes to the game; the game's own change comes back.
    ram.store32(kPouch + 2u * 4u, slot_word(55u, 9u));
    ram.store32(kSys + kGameSaved + 4u * 4u, slot_word(66u, 1u));
    merge(ram, kSys, mirror);
    check(ram.load32(kSys + kGamePouch + 2u * 4u) == slot_word(55u, 9u), "the block's change did not reach the game");
    check(ram.load32(kSaved + 4u * 4u) == slot_word(66u, 1u), "the game's change did not reach the block");
    check(mirror[0][2] == slot_word(55u, 9u) && mirror[1][4] == slot_word(66u, 1u), "the mirror did not follow");
    // Both in one frame, different slots: each wins its own.
    ram.store32(kPouch + 3u * 4u, slot_word(1u, 1u));
    ram.store32(kSys + kGamePouch + 4u * 4u, slot_word(2u, 2u));
    merge(ram, kSys, mirror);
    check(ram.load32(kSys + kGamePouch + 3u * 4u) == slot_word(1u, 1u) &&
            ram.load32(kPouch + 4u * 4u) == slot_word(2u, 2u),
        "two changes in one frame were not both kept");
    // The page stays out of the game's arrays.
    check(ram.load32(kSys + kGamePouch + 24u * 4u) == slot_word(0x80u, 0xFFu), "the page reached the bow's slot");
}

void test_store(const std::filesystem::path &dir) {
    const auto path = dir / "pages.txt";
    {
        Store store(path);
        check(!store.page("Nobody").owned, "an unknown hunter owns the page");
        HunterPage page;
        page.owned = true;
        page.slots[0][1] = {12u, 3u};
        page.slots[2][7] = {65535u, 99u};
        store.set("Ann Lee", page);
        store.set("Bo", HunterPage{});
        page.slots[1][0] = {4u, 4u};
        store.set("Ann Lee", page);
        check(store.save(), "the pages file was not written");
    }
    Store again(path);
    const HunterPage ann = again.page("Ann Lee");
    check(ann.owned && ann.slots[0][1] == Slot{12u, 3u} && ann.slots[1][0] == Slot{4u, 4u} &&
            ann.slots[2][7] == Slot{65535u, 99u},
        "a page did not come back from the file");
    check(!again.page("Bo").owned, "a hunter without the page came back with it");
    {
        std::ofstream out(path, std::ios::app);
        out << "stray line\nhunter Cy\nowned\npouch 1:2 bad 99999999:1 3:4\n";
    }
    Store damaged(path);
    const HunterPage cy = damaged.page("Cy");
    check(cy.owned && cy.slots[0][0] == Slot{1u, 2u} && cy.slots[0][1] == Slot{} && cy.slots[0][3] == Slot{3u, 4u},
        "a damaged line was not skipped");
    // A file where the folder should be: nothing can be written.
    {
        std::ofstream(dir / "blocker") << "x";
    }
    Store unwritable(dir / "blocker" / "pages.txt");
    unwritable.set("Ann", HunterPage{});
    check(!unwritable.save(), "a file that cannot be written was reported written");
}

void set_hunter(psprecomp::GuestMemory &memory, const std::u16string &name) {
    for (std::uint32_t i = 0; i < 8u; ++i)
        memory.store16(kSys + 0x4A0u + i * 2u, i < name.size() ? static_cast<std::uint16_t>(name[i]) : 0u);
}

// The frame, a save and a load, on a runtime.
void test_runtime(const std::filesystem::path &dir) {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    memory.store32(kGameSysPointer, kSys);
    memory.store32(kSys + kGamePouch, slot_word(10u, 1u));

    frame(runtime);
    check(!active() && !runtime.site_hooks_active(), "the hooks were on with no hunter");

    set_hunter(memory, u"Ann");
    frame(runtime);
    check(!active() && !runtime.site_hooks_active(), "a hunter without the page has the hooks on");

    grant(runtime);
    check(active() && runtime.site_hooks_active(), "grant did not turn the hooks on");
    check(memory.load32(kPouch) == slot_word(10u, 1u), "grant did not fill the block");

    // A site through the runtime, as the generated code calls it.
    const Site *count = nullptr;
    for (const Site &site : sites())
        if (site.action == Action::Set && site.value == Value{Base::Number, 32}) {
            count = &site;
            break;
        }
    check(count != nullptr, "no count site in the list");
    if (count != nullptr) {
        psprecomp::AllegrexContext ctx{};
        const std::uint8_t dest = destination(count->word);
        ctx.gpr[dest] = 24u;
        runtime.call_site_hook(count->number, count->address + 4u, ctx);
        check(ctx.gpr[dest] == 24u, "a site at another address was applied");
        runtime.call_site_hook(9999u, count->address, ctx);
        check(ctx.gpr[dest] == 24u, "an unknown site was applied");
        runtime.call_site_hook(count->number, count->address, ctx);
        check(ctx.gpr[dest] == 32u, "the site did not set 32");
    }

    // A page slot, kept when the game saves.
    memory.store32(kPouch + 26u * 4u, slot_word(77u, 2u));
    frame(runtime);
    check(memory.load32(kSys + kGamePouch + 26u * 4u) != slot_word(77u, 2u), "the page reached the game's array");
    game_saved(memory);
    Store kept(dir / "pouch_page.txt");
    check(kept.page("Ann").owned && kept.page("Ann").slots[0][2] == Slot{77u, 2u}, "the save did not keep the page");

    // Changed and not saved, then loaded: the saved page comes back.
    memory.store32(kPouch + 26u * 4u, slot_word(88u, 1u));
    game_loaded();
    frame(runtime);
    check(memory.load32(kPouch + 26u * 4u) == slot_word(77u, 2u), "a load did not bring back the saved page");

    // Another hunter, then Ann again.
    set_hunter(memory, u"Bo");
    frame(runtime);
    check(!active() && !runtime.site_hooks_active(), "another hunter kept Ann's page");
    set_hunter(memory, u"Ann");
    frame(runtime);
    check(active() && memory.load32(kPouch + 26u * 4u) == slot_word(77u, 2u), "Ann's page did not come back");

    set_hunter(memory, u"");
    frame(runtime);
    check(!active() && !runtime.site_hooks_active(), "the hooks stayed on without a hunter");
}

} // namespace

int main() {
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_pouch_page_tests";
    std::error_code stale;
    std::filesystem::remove_all(data, stale);
    std::filesystem::create_directories(data);
    set_environment("MHFU_DATA_DIR", data.string());
    set_environment("MHFU_POUCH_PAGE", "");
    set_environment("MHFU_POUCH_PAGE_GRANT", "");
    set_environment("MHFU_TRACE_POUCH_PAGE", "1");
    test_parse();
    test_built_in_list();
    test_slot_address();
    test_apply();
    test_blocks();
    test_store(data);
    test_runtime(data);
    std::filesystem::remove_all(data, stale);
    if (failures != 0) {
        std::cerr << failures << " pouch page check(s) failed\n";
        return 1;
    }
    std::cout << "pouch page tests passed\n";
    return 0;
}
