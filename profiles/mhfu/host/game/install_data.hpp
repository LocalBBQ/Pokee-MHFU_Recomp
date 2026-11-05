#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace psprecomp {
class GuestMemory;
}

// The game's Data Install, served from DATA.BIN instead of stored.
//
// Data Install copies the raw (still obfuscated) bytes of most DATA.BIN
// entries into numbered files, ms0:/PSP/SAVEDATA/ULUS10391DAT/00000000 and
// on. An entry is installed when the executable keeps a SHA-1 of it (a table
// of 20 bytes per entry at 0x08979924; all zero for the others). The entries
// go in order, each taking its whole blocks, and a new file starts where the
// next entry would take a file past 0x370000 bytes. At start the game lists
// the folder (save-data FILES) and checks every file's size against that
// packing; from then on it reads each installed entry from its file. Traced
// on USA Freedom Unite: the game's own install matches this packing file for
// file, with each entry's bytes as on the disc.
//
// Stored, the copy would also hide the mods from the game, which read the
// archive only. Served, each file is read from DATA.BIN as the mods leave it,
// laid out by the directory the game sees.
namespace mhfu::game::install_data {

inline constexpr std::string_view kFolder = "ULUS10391DAT";
inline constexpr std::uint64_t kFileLimit = 0x370000u;
inline constexpr std::uint32_t kBlock = 2048u;

// A run of DATA.BIN, in bytes from its start.
struct Range {
    std::uint64_t offset{};
    std::uint64_t length{};
};

struct File {
    std::vector<Range> ranges; // in order; adjacent entries share one
    std::uint64_t size{};
};

// The files the game expects, from the archive's block table (one more than
// there are entries) and which entries are installed.
[[nodiscard]] std::vector<File> pack(
    std::span<const std::uint32_t> blocks, const std::function<bool(std::uint32_t entry)> &installed);

// Reads `out.size()` bytes of `file` from `offset` with `read_archive`, which
// reads DATA.BIN at an offset. Returns the bytes read: fewer at the file's end.
using ArchiveRead = std::function<std::size_t(std::uint64_t offset, std::span<std::uint8_t> out)>;
std::size_t read(const File &file, std::uint64_t offset, std::span<std::uint8_t> out, const ArchiveRead &read_archive);

// "PSP/SAVEDATA/ULUS10391DAT/00000012" (as the file I/O has it, without the
// device) to 12.
[[nodiscard]] std::optional<std::uint32_t> file_number(std::string_view path);

// The files served this run, from the executable in guest memory and the
// archive as the mods leave it; worked out once, at the game's first look.
// Null when the setting is off, or when the executable is not the one this
// was traced on.
[[nodiscard]] const std::vector<File> *served(const psprecomp::GuestMemory &memory);

// The game reads its install only while its own option, Options -> Data
// Install, is ON, and only the game's Data Install turns it on (then saves).
// With the install served, the title menu leaves that out, so a save that
// never ran it would never use the install.
//
// The game keeps its own layer of scrambling inside MHP2NDG.BIN: the data
// the save-data utility returns is not yet readable. Once the game has
// unscrambled it in its load buffer, the buffer starts with the options
// block (language, sound, BGM level, FX level, Quick Continue, Background
// Loading, Data Install, one byte each), and the game copies that block to
// 0x09A04B78, in the executable's .bss. It starts reading ULUS10391DAT once
// the Data Install byte there is 1, and saves the block with the game.
// Traced on USA Freedom Unite by flipping the option in the menu and dumping
// memory, and by the reads that follow a load with the option on.
inline constexpr std::string_view kSaveGame = "ULUS10391";
inline constexpr std::string_view kSaveFile = "MHP2NDG.BIN";
inline constexpr std::uint32_t kOptions = 0x09A04B78u;
inline constexpr std::uint32_t kOptionBytes = 7u;
inline constexpr std::uint32_t kInstallOption = 6u;

enum class OptionStep {
    Waiting,  // the game has not copied the loaded options yet
    Unchanged, // copied, and the option is not off
    TurnedOn,  // copied with the option off: it is on now
};
// After a load into `buffer`: once the options block holds what the buffer
// starts with, turns the Data Install option on if it is off.
OptionStep turn_option_on(psprecomp::GuestMemory &memory, std::uint32_t buffer);
// MHFU_INSTALL_OPTION=0 leaves the option as the save has it. Read once.
[[nodiscard]] bool option_wanted();
// The game read its save into `buffer` (the save-data utility).
void save_loaded(std::uint32_t buffer);
// Each frame: after a load, while the install is served, turns the option on
// once the game has copied it (turn_option_on). Gives up after 30 seconds.
void option_frame(psprecomp::GuestMemory &memory);

// The folder's other files, as the game's own install writes them: PARAM.SFO,
// and the icons it takes from DATA.BIN entries 14 and 13.
struct SystemFile {
    std::string_view name;
    std::uint64_t size;
};
inline constexpr SystemFile kSystemFiles[] = {{"ICON0.PNG", 28672u}, {"PARAM.SFO", 4912u}, {"PIC1.PNG", 256000u}};

} // namespace mhfu::game::install_data
