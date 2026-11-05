#pragma once

// Fourth pouch page: a hunter who owns it carries 32 items instead of 24, in
// a fourth page of eight. The stores sell it as Backpack Lg. once the hunter
// is High Rank (game/backpack.hpp). `MHFU_POUCH_PAGE=0` turns it off;
// `MHFU_POUCH_PAGE_GRANT=1` gives it to every hunter, for testing.
// `MHFU_TRACE_POUCH_PAGE=1` prints each site the first time it runs.
//
// Read off the USA executable (ULUS-10391) and its overlays. The pouch is 24
// slots of a u16 item id and a s16 count, and the game keeps four arrays of
// that shape, each with other data right after it:
//
// - the pouch, GameSys + 0x6AEA6, reached through 0x08856884(GameSys, i),
//   which takes i up to 24: slot 24, GameSys + 0x6AF06, is a 25th the bow's
//   coatings use (0x088D7AD8 counts 25 slots for weapon class 10), and some
//   code reads GameSys + 4i + 0x70000 - 0x515A directly;
// - the pouch as saved, GameSys + 0x43C8, which is (GameSys + 0x4A0) + 0x3F28:
//   the box screens work on it (0x088D8900 copies the pouch in, 0x088D893C
//   back), and hunter setup (0x088D6924) copies it into the pouch;
// - the pouch kept aside, GameSys + 0x6ADFC, while the Training School lends
//   its own items (arcade_task) and in 0x08869F14 / 0x0887EAA0;
// - the hunter's copy, hunter + 0x568, filled by 0x088D6924 for this console's
//   hunter and read in a quest (game_task 0x09A72448).
//
// No array can grow in place, so a hunter with the page gets four arrays of
// 32 in the window the host reserves below the load image (kernel/kernel.hpp),
// and the code that reaches them is changed where it runs: config/site_hooks.txt
// lists 197 instructions, and the recompiler (psp_recomp --site-hooks) has the
// generated code call the host after each of them while the hooks are on.
// After each, the host changes the registers the instruction set:
//
// - bound N: an slti or sltiu against 24 (or 25 counting the bow's slot)
//   compares with N instead;
// - set V / setreg r V: the result, or another register, becomes V, a number
//   or a block's address plus an offset: a count of 24 becomes 32, an address
//   of the saved pouch becomes the 32-slot block's;
// - rebase r V: an address computed from r is computed from V instead, as
//   `addu rd, i*4, (GameSys + 0x4A0)` addressing the saved pouch;
// - direct: GameSys + 4i + 0x70000 becomes the address of slot i plus 0x515A;
// - pouch_slot: 0x08856884's result becomes slot i's address: the block for
//   0 to 31, the bow's slot (where the game keeps it) for 32;
// - cap_base / cap_check: at the end of a quest, 0x09B61B80 moves supply items
//   out of the pouch into a list of 24 entries; past 24 it leaves them.
//
// Off, nothing changes: the hooks cost one test, and every array is the
// game's own. On, the first 24 slots of each block are copied into the game's
// arrays every frame, so a save holds them as on a PSP, and slot by slot a
// change the game made to its own array (a save loaded, Item Sets) is taken
// into the block. The page itself (slots 24 to 31 of each block) and whether
// the hunter owns it are kept by hunter name in pouch_page.txt, written when
// the game saves, so a game quit without saving keeps what was saved.

#include "game/item_sets.hpp"
#include "game/pouch_slots.hpp"
#include "psprecomp/allegrex_context.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psprecomp {
class GuestMemory;
class Runtime;
} // namespace psprecomp

namespace mhfu::game {
class Ram;
}

namespace mhfu::game::pouch_page {

using item_sets::Slot;

inline constexpr std::uint32_t kGameSlots = pouch_slots::kGameSlots;
inline constexpr std::uint32_t kSlots = pouch_slots::kSlots;
inline constexpr std::uint32_t kPageSlots = kSlots - kGameSlots;
inline constexpr std::uint32_t kSlotBytes = pouch_slots::kSlotBytes;
inline constexpr std::uint32_t kBowSlot = kSlots; // the bow's slot, as the hooks number it

// The game's arrays, from GameSys.
inline constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
inline constexpr std::uint32_t kGamePouch = pouch_slots::kGamePouch;
inline constexpr std::uint32_t kGameBowSlot = kGamePouch + kGameSlots * kSlotBytes;
inline constexpr std::uint32_t kGameSaved = 0x43C8u;
inline constexpr std::uint32_t kGameAside = 0x6ADFCu;
inline constexpr std::uint32_t kDirectBias = 0x70000u; // GameSys + 4i + this, read at -0x515A
inline constexpr std::uint32_t kDirectOffset = 0x515Au;

// The blocks, in the window the host reserves.
inline constexpr std::uint32_t kPouch = pouch_slots::kBlock;
inline constexpr std::uint32_t kSaved = 0x08803080u;
inline constexpr std::uint32_t kAside = 0x08803100u;
inline constexpr std::uint32_t kHunterCopy = 0x08803180u;
inline constexpr std::uint32_t kList1 = 0x08803200u; // 32 words each
inline constexpr std::uint32_t kList2 = 0x08803280u;
inline constexpr std::uint32_t kList3 = 0x08803300u;
inline constexpr std::uint32_t kBlocksEnd = 0x08803380u;

// Sites -----------------------------------------------------------------------

enum class Action { Bound, Set, SetReg, Rebase, Direct, PouchSlot, CapBase, CapCheck };
enum class Base : std::uint8_t { Number, Pouch, Saved, Aside, HunterCopy, List1, List2, List3 };

// A number, or a block's address plus an offset.
struct Value {
    Base base{Base::Number};
    std::int64_t offset{};
    friend bool operator==(const Value &, const Value &) = default;
};
[[nodiscard]] std::uint32_t value_of(const Value &value) noexcept;

inline constexpr std::uint8_t kGameSysOperand = 0xFFu; // rebase from GameSys rather than a register

struct Site {
    std::string image;
    std::uint32_t address{};
    std::uint32_t word{};
    std::uint32_t number{};
    Action action{};
    std::uint8_t reg{};  // setreg, rebase, cap_base, cap_check
    Value value{};       // bound (the limit), set, setreg, rebase
    std::uint32_t limit{}; // cap_check: bytes of list
};

// The lines of config/site_hooks.txt. None, with `error` set to the line at
// fault, when a line does not parse.
[[nodiscard]] std::optional<std::vector<Site>> parse_sites(std::string_view text, std::string *error = nullptr);
// The list built into the host.
[[nodiscard]] const std::vector<Site> &sites();

// The register an instruction writes: rd for the R-type ones, else rt.
[[nodiscard]] std::uint8_t destination(std::uint32_t word) noexcept;

// What a cap_base remembered, for the cap_check after it.
struct CapState {
    std::uint32_t base{};
};

// Changes the registers as the site says, after its instruction ran. `game_sys`
// is GameSys.
void apply(const Site &site, psprecomp::AllegrexContext &ctx, std::uint32_t game_sys, CapState &cap);

// Slot i of the pouch with the page: the block for 0 to 31, the bow's slot for
// 32 (where the game keeps it); 0 for anything else.
[[nodiscard]] std::uint32_t slot_address(std::uint32_t game_sys, std::uint32_t index) noexcept;

// The game's arrays and the blocks ---------------------------------------------

struct Array {
    std::uint32_t game{};  // offset from GameSys
    std::uint32_t block{}; // guest address of the block
};
inline constexpr std::array<Array, 3> kArrays{{
    {kGamePouch, kPouch},
    {kGameSaved, kSaved},
    {kGameAside, kAside},
}};

// The game's 24 slots of each array as they were last copied, to tell the
// game's own changes from the block's.
using Mirror = std::array<std::array<std::uint32_t, kGameSlots>, kArrays.size()>;

// The page's slots of each array, as kept for the hunter.
using PageSlots = std::array<Slot, kPageSlots>;
struct HunterPage {
    bool owned{};
    // The village shopkeeper has told the hunter of the Lg Backpack.
    bool announced{};
    std::array<PageSlots, kArrays.size()> slots{};
    friend bool operator==(const HunterPage &, const HunterPage &) = default;
};

// Fills the blocks from the game's arrays and the hunter's page, clears the
// hunter's copy and the lists, and returns the mirror.
Mirror fill_blocks(Ram &ram, std::uint32_t game_sys, const HunterPage &page);
// One frame: slot by slot, a change the game made to its own array goes into
// the block, and otherwise the block's slot goes into the game's array.
void merge(Ram &ram, std::uint32_t game_sys, Mirror &mirror);
// The page's slots as the blocks hold them now.
[[nodiscard]] HunterPage page_of(const Ram &ram);

// Every hunter's page, by name, in a text file.
class Store {
public:
    explicit Store(std::filesystem::path path) : path_(std::move(path)) {}
    [[nodiscard]] HunterPage page(const std::string &hunter);
    void set(const std::string &hunter, const HunterPage &page);
    // False when the file could not be written.
    [[nodiscard]] bool save();
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
    void load_file();
    std::filesystem::path path_;
    bool loaded_{};
    std::vector<std::pair<std::string, HunterPage>> hunters_;
};

// Hooks ----------------------------------------------------------------------

// At the flip: follows the hunter, keeps the blocks and the game's arrays in
// step and turns the hooks on for a hunter with the page.
void frame(psprecomp::Runtime &runtime);
// The game wrote its save: the page goes into pouch_page.txt with it.
void game_saved(psprecomp::GuestMemory &memory);
// The game read a save: the page is read again once the hunter is in memory.
void game_loaded();

// The hunter in memory has the page and the hooks are on.
[[nodiscard]] bool active();
// The hunter in memory owns the page (bought it, or MHFU_POUCH_PAGE_GRANT).
[[nodiscard]] bool owned();
// MHFU_POUCH_PAGE is not 0. Read at the first frame.
[[nodiscard]] bool enabled();
// Gives the hunter in memory the page, kept when the game next saves.
void grant(psprecomp::Runtime &runtime);
// The hunter in memory has heard the shopkeeper's news of the Lg Backpack;
// announce() records that and writes it to pouch_page.txt at once, so a
// reload without saving does not bring the news back.
[[nodiscard]] bool announced();
void announce();

} // namespace mhfu::game::pouch_page
