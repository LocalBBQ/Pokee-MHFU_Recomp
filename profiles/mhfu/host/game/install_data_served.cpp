#include "game/install_data.hpp"

#include "mods/mhfu_mods.hpp"
#include "settings/settings.hpp"

#include "psprecomp/guest_memory.hpp"

#include <iostream>

// The served install for this run (install_data.hpp); the packing itself is
// in install_data.cpp, which needs no game.
namespace mhfu::game::install_data {
namespace {

// Where the executable keeps what this needs, on USA Freedom Unite: the
// install folder's name, which the check below compares to tell the
// executable apart, and the per-entry hashes, as many as the game's loops go.
constexpr std::uint32_t kFolderName = 0x089B4EF4u;
constexpr std::uint32_t kEntryHashes = 0x08979924u;
constexpr std::uint32_t kHashSize = 20u;
constexpr std::uint32_t kHashedEntries = 6657u;

bool traced_executable(const psprecomp::GuestMemory &memory) {
    for (std::uint32_t i = 0; i < kFolder.size(); ++i)
        if (memory.load8(kFolderName + i) != static_cast<std::uint8_t>(kFolder[i])) return false;
    return memory.load8(kFolderName + static_cast<std::uint32_t>(kFolder.size())) == 0u;
}

bool hashed(const psprecomp::GuestMemory &memory, std::uint32_t entry) {
    if (entry >= kHashedEntries) return false;
    for (std::uint32_t i = 0; i < kHashSize; ++i)
        if (memory.load8(kEntryHashes + entry * kHashSize + i) != 0u) return true;
    return false;
}

} // namespace

const std::vector<File> *served(const psprecomp::GuestMemory &memory) {
    static std::optional<std::vector<File>> files;
    static bool decided = false;
    if (!decided) {
        decided = true;
        if (!settings::current().virtual_install) {
            std::cout << "[install] served install data off: the game reads its own ULUS10391DAT\n";
        } else if (!traced_executable(memory)) {
            std::cout << "[install] served install data off: not the executable it was traced on\n";
        } else if (const std::vector<std::uint32_t> blocks = mods::data_bin_blocks(); blocks.size() < 2u) {
            std::cout << "[install] served install data off: DATA.BIN's directory is not available\n";
        } else {
            files = pack(blocks, [&memory](std::uint32_t entry) { return hashed(memory, entry); });
            std::uint64_t bytes = 0u;
            for (const File &file : *files) bytes += file.size;
            std::cout << "[install] serving ULUS10391DAT: " << files->size() << " files, " << bytes / (1024u * 1024u)
                      << " MiB of DATA.BIN\n";
        }
    }
    return files ? &*files : nullptr;
}

namespace {

struct OptionWatch {
    bool pending{};
    std::uint32_t buffer{};
    std::uint32_t frames{};
};

OptionWatch &watch() {
    static OptionWatch w;
    return w;
}

// Thirty seconds of frames at most: the game copies the options within a few.
constexpr std::uint32_t kWaitFrames = 30u * 60u;

} // namespace

void save_loaded(std::uint32_t buffer) {
    watch() = {true, buffer, 0u};
}

void option_frame(psprecomp::GuestMemory &memory) {
    OptionWatch &w = watch();
    if (!w.pending) return;
    if (!option_wanted() || served(memory) == nullptr) {
        w.pending = false;
        return;
    }
    switch (turn_option_on(memory, w.buffer)) {
    case OptionStep::Waiting:
        if (++w.frames >= kWaitFrames) w.pending = false;
        return;
    case OptionStep::TurnedOn:
        std::cout << "[install] Data Install option turned on: the install is served" << std::endl;
        break;
    case OptionStep::Unchanged:
        break;
    }
    w.pending = false;
}

} // namespace mhfu::game::install_data
