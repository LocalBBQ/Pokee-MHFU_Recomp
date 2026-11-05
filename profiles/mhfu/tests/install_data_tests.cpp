// Checks for the served Data Install (game/install_data.hpp) that need no game
// data: the packing, reads that cross ranges, and the file names.
//
//   mhfu_install_data_tests
//   mhfu_install_data_tests --check <EBOOT.ELF> <DATA.BIN> <ULUS10391DAT folder>
//
// The second form packs the archive with the executable's entry hashes and
// compares every file's size with a Data Install the game made itself, and
// the bytes of each file with what the served install reads.
#include "game/install_data.hpp"
#include "game/title_menu.hpp"
#include "mods/mhfu_data_bin.hpp"

#include "psprecomp/guest_memory.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mhfu::game::install_data;

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    std::fflush(stdout);
    if (!condition) ++failures;
}

constexpr std::uint32_t kLimitBlocks = static_cast<std::uint32_t>(kFileLimit / kBlock);

// A block table from entry lengths in blocks, the directory taking 17.
std::vector<std::uint32_t> table(const std::vector<std::uint32_t> &lengths) {
    std::vector<std::uint32_t> blocks{17u};
    for (const std::uint32_t length : lengths) blocks.push_back(blocks.back() + length);
    return blocks;
}

void test_pack() {
    const auto all = [](std::uint32_t) { return true; };
    const auto files = pack(table({2u, 3u, 1u}), all);
    check(files.size() == 1u && files[0].size == 6u * kBlock, "small entries share one file");
    check(files.size() == 1u && files[0].ranges.size() == 1u && files[0].ranges[0].offset == 17u * kBlock,
        "adjacent entries are one range, from the first entry's block");

    const auto skipping = pack(table({2u, 3u, 1u}), [](std::uint32_t entry) { return entry != 1u; });
    check(skipping.size() == 1u && skipping[0].size == 3u * kBlock && skipping[0].ranges.size() == 2u &&
            skipping[0].ranges[1].offset == 22u * kBlock,
        "an entry left out splits the range and takes no room");

    const auto exact = pack(table({kLimitBlocks - 1u, 1u, 1u}), all);
    check(exact.size() == 2u && exact[0].size == kFileLimit && exact[1].size == kBlock,
        "a file fills to the limit exactly; the next entry starts a file");

    const auto large = pack(table({kLimitBlocks + 5u, 1u}), all);
    check(large.size() == 3u && large[0].size == 0u && large[1].size == (kLimitBlocks + 5u) * kBlock &&
            large[2].size == kBlock,
        "an entry larger than the limit leaves the file before it empty and has one to itself");

    check(pack(std::vector<std::uint32_t>{17u}, all).size() == 1u, "an archive without entries packs one empty file");
}

void test_read() {
    File file;
    file.ranges = {{100u, 4u}, {200u, 3u}};
    file.size = 7u;
    std::vector<std::uint64_t> asked;
    const ArchiveRead archive = [&asked](std::uint64_t offset, std::span<std::uint8_t> out) {
        asked.push_back(offset);
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>(offset + i);
        return out.size();
    };
    std::vector<std::uint8_t> out(5u);
    check(read(file, 2u, out, archive) == 5u, "a read crossing two ranges is whole");
    check(out == std::vector<std::uint8_t>{102, 103, 200, 201, 202}, "it reads each range at its place");
    check(asked == std::vector<std::uint64_t>{102u, 200u}, "one archive read per range");

    std::vector<std::uint8_t> tail(10u);
    check(read(file, 5u, tail, archive) == 2u, "a read past the end stops at the file's end");

    const ArchiveRead short_read = [](std::uint64_t, std::span<std::uint8_t> out) { return out.size() / 2u; };
    std::vector<std::uint8_t> partial(4u);
    check(read(file, 0u, partial, short_read) == 2u, "a short archive read ends the read");
}

void test_option() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t kBuffer = 0x093BA600u;
    // The options as a save that never ran Data Install has them, in the
    // load buffer once the game has unscrambled it.
    const std::uint8_t options[] = {1, 1, 2, 2, 0, 1, 0};
    for (std::uint32_t i = 0; i < kOptionBytes; ++i) memory.store8(kBuffer + i, options[i]);
    memory.store8(kOptions + kOptionBytes, 0x5Au);

    check(turn_option_on(memory, kBuffer) == OptionStep::Waiting, "options the game has not copied are left alone");
    check(memory.load8(kOptions + kInstallOption) == 0u, "waiting wrote nothing");
    for (std::uint32_t i = 0; i < kOptionBytes; ++i) memory.store8(kOptions + i, options[i]);
    check(turn_option_on(memory, kBuffer) == OptionStep::TurnedOn, "copied with the option off, it is turned on");
    check(memory.load8(kOptions + kInstallOption) == 1u, "the Data Install byte is 1");
    bool others_kept = memory.load8(kOptions + kOptionBytes) == 0x5Au && memory.load8(kBuffer + kInstallOption) == 0u;
    for (std::uint32_t i = 0; i < kInstallOption; ++i)
        others_kept = others_kept && memory.load8(kOptions + i) == options[i];
    check(others_kept, "only the Data Install byte changes");

    // Already on in the save: compared and left alone.
    memory.store8(kBuffer + kInstallOption, 1u);
    check(turn_option_on(memory, kBuffer) == OptionStep::Unchanged, "an option already on is left alone");
    // A buffer outside memory never matches.
    check(turn_option_on(memory, 0x10000000u) == OptionStep::Waiting, "a buffer outside memory is left alone");
}

void test_names() {
    check(file_number("PSP/SAVEDATA/ULUS10391DAT/00000012") == 12u, "a numbered file is recognised");
    check(file_number("psp/savedata/ulus10391dat/00000000") == 0u, "in any case");
    check(!file_number("PSP/SAVEDATA/ULUS10391DAT/PARAM.SFO"), "PARAM.SFO is not a numbered file");
    check(!file_number("PSP/SAVEDATA/ULUS10391DAT/0000001"), "seven digits are not a numbered file");
    check(!file_number("PSP/SAVEDATA/ULUS10391/00000001"), "another folder is not the install");
}

std::vector<std::uint8_t> file_bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The executable's entry hashes are at 0x08979924, 20 bytes each, for 6657
// entries; the ELF's first loadable segment maps its file offsets.
int check_install(const char *eboot_path, const char *data_bin_path, const char *folder) {
    const std::vector<std::uint8_t> elf = file_bytes(eboot_path);
    std::uint32_t program_headers = 0u;
    std::memcpy(&program_headers, elf.data() + 28u, 4u);
    std::uint32_t segment_offset = 0u, segment_address = 0u;
    std::memcpy(&segment_offset, elf.data() + program_headers + 4u, 4u);
    std::memcpy(&segment_address, elf.data() + program_headers + 8u, 4u);
    const std::size_t hashes = 0x08979924u - segment_address + segment_offset;

    std::ifstream archive(data_bin_path, std::ios::binary);
    const auto archive_bytes = std::filesystem::file_size(data_bin_path);
    std::vector<std::uint8_t> directory_bytes(512u * kBlock);
    archive.read(
        reinterpret_cast<char *>(directory_bytes.data()), static_cast<std::streamsize>(directory_bytes.size()));
    const auto directory = mhfu::mods::p3rd::Directory::parse(directory_bytes, archive_bytes);
    check(directory.has_value(), "DATA.BIN's directory parses");
    if (!directory) return 1;

    const auto files = pack(directory->blocks, [&](std::uint32_t entry) {
        if (entry >= 6657u) return false;
        for (std::size_t i = 0; i < 20u; ++i)
            if (elf[hashes + entry * 20u + i] != 0u) return true;
        return false;
    });
    std::size_t sizes = 0u, contents = 0u;
    char name[16];
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::snprintf(name, sizeof name, "%08u", static_cast<unsigned>(i));
        const std::vector<std::uint8_t> game = file_bytes(std::filesystem::path(folder) / name);
        if (game.size() != files[i].size) continue;
        ++sizes;
        std::vector<std::uint8_t> served(game.size());
        read(files[i], 0u, served, [&archive](std::uint64_t offset, std::span<std::uint8_t> out) {
            archive.clear();
            archive.seekg(static_cast<std::streamoff>(offset));
            archive.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
            return static_cast<std::size_t>(archive.gcount());
        });
        // The game pads an entry whose size is not whole blocks with what its
        // buffer held; the disc's padding is as good, as the game reads only
        // the entry. So count a file as matching where the entries' bytes do.
        bool same = true;
        std::uint64_t at = 0u;
        for (const Range &range : files[i].ranges) {
            for (std::uint32_t block = static_cast<std::uint32_t>(range.offset / kBlock);
                block < (range.offset + range.length) / kBlock;) {
                const auto entry = static_cast<std::uint32_t>(directory->entry_at(block));
                const std::uint64_t span = directory->span(entry);
                const std::uint64_t used = directory->size(entry);
                if (std::memcmp(game.data() + at, served.data() + at, static_cast<std::size_t>(used)) != 0)
                    same = false;
                at += span;
                block += static_cast<std::uint32_t>(span / kBlock);
            }
        }
        contents += same;
    }
    std::printf(
        "%zu files packed; %zu with the game's size, %zu with its entries' bytes\n", files.size(), sizes, contents);
    check(sizes == files.size(), "every file has the size of the game's own");
    check(contents == files.size(), "every file reads the game's own bytes");
    std::size_t listed = 0u;
    for (const auto &entry : std::filesystem::directory_iterator(folder))
        if (!file_number("PSP/SAVEDATA/ULUS10391DAT/" + entry.path().filename().string())) ++listed;
    check(listed == std::size(kSystemFiles), "the folder's other files are the three a served install lists");
    return failures == 0 ? 0 : 1;
}

} // namespace

void test_title_menu() {
    using namespace mhfu::game::title_menu;
    // The five entries left wrap as the game's six do.
    check(
        cursor_before_input(0u, kUp) == kHiddenEntry, "up on NEW GAME is given the hidden entry, to land on DOWNLOAD");
    check(cursor_before_input(4u, kDown) == kHiddenEntry, "down on DOWNLOAD likewise, to wrap to NEW GAME");
    check(cursor_before_input(4u, kUp | kDown) == 4u, "up wins over down, as in the game");
    check(
        cursor_before_input(2u, kUp) == 2u && cursor_before_input(3u, kDown) == 3u && cursor_before_input(4u, 0u) == 4u,
        "other moves are the game's");
    check(cursor_before_input(kHiddenEntry, 0u) == 0u, "a cursor resting on the hidden entry goes to NEW GAME");
    check(cursor_before_input(kHiddenEntry, kUp) == kHiddenEntry &&
            cursor_before_input(kHiddenEntry, kDown) == kHiddenEntry,
        "and one moving off it is the game's to move");

    psprecomp::GuestMemory memory;
    const std::uint32_t table = kTextBlock + 0x100u;
    memory.store32(kTextBlock + 8u, 0x100u);
    const std::uint32_t entry = table + 4u * (kFirstLabel + kHiddenEntry);
    memory.store32(entry, 0x4000u);
    const char label[] = "DATA INSTALL";
    for (std::uint32_t i = 0; i < sizeof label; ++i)
        memory.store8(table + 0x4000u + i, static_cast<std::uint8_t>(label[i]));
    HiddenLabel hidden;
    check(hide_label(memory, hidden) && memory.load32(entry) == 0x4000u + 12u &&
            memory.load8(table + memory.load32(entry)) == 0u,
        "the label points at its own end while hidden");
    HiddenLabel again;
    check(!hide_label(memory, again), "an empty label is not hidden again");
    restore_label(memory, hidden);
    check(memory.load32(entry) == 0x4000u, "and is put back after");
    check(hide_label(memory, hidden), "hidden once more");
    memory.store32(entry, 0x5000u); // the game loads its text again
    restore_label(memory, hidden);
    check(memory.load32(entry) == 0x5000u, "a table loaded again since is left as the game has it");
    memory.store32(entry, 0x4000u);
    for (std::uint32_t i = 0; i < 300u; ++i) memory.store8(table + 0x4000u + i, 'x');
    check(!hide_label(memory, hidden) && memory.load32(entry) == 0x4000u, "a label with no end in reach is left alone");
    check(!code_matches(memory), "without demo_task's code nothing is wrapped");
}

int main(int argc, char **argv) {
    if (argc == 5 && std::strcmp(argv[1], "--check") == 0) return check_install(argv[2], argv[3], argv[4]);
    test_pack();
    test_read();
    test_names();
    test_option();
    test_title_menu();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
