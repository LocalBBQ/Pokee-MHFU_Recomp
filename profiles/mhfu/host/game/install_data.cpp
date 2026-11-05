#include "game/install_data.hpp"

#include "psprecomp/guest_memory.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace mhfu::game::install_data {
namespace {

constexpr std::string_view kFolderPath = "PSP/SAVEDATA/ULUS10391DAT/";

} // namespace

std::vector<File> pack(std::span<const std::uint32_t> blocks, const std::function<bool(std::uint32_t entry)> &installed) {
    std::vector<File> files(1u);
    if (blocks.size() < 2u) return files;
    for (std::uint32_t entry = 0; entry + 1u < blocks.size(); ++entry) {
        if (!installed(entry)) continue;
        const std::uint64_t offset = static_cast<std::uint64_t>(blocks[entry]) * kBlock;
        const std::uint64_t length = static_cast<std::uint64_t>(blocks[entry + 1u] - blocks[entry]) * kBlock;
        // As the game does it: an entry that does not fit starts the next
        // file even when this one is still empty.
        if (files.back().size + length > kFileLimit) files.emplace_back();
        File &file = files.back();
        if (!file.ranges.empty() && file.ranges.back().offset + file.ranges.back().length == offset)
            file.ranges.back().length += length;
        else
            file.ranges.push_back({offset, length});
        file.size += length;
    }
    return files;
}

std::size_t read(const File &file, std::uint64_t offset, std::span<std::uint8_t> out, const ArchiveRead &read_archive) {
    std::size_t done = 0u;
    std::uint64_t start = 0u; // where the range begins in the file
    for (const Range &range : file.ranges) {
        if (done == out.size()) break;
        const std::uint64_t at = offset + done;
        if (at < start + range.length) {
            const std::uint64_t into = at - start;
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(out.size() - done, range.length - into));
            const std::size_t got = read_archive(range.offset + into, out.subspan(done, count));
            done += got;
            if (got < count) break;
        }
        start += range.length;
    }
    return done;
}

std::optional<std::uint32_t> file_number(std::string_view path) {
    if (path.size() != kFolderPath.size() + 8u) return std::nullopt;
    for (std::size_t i = 0; i < kFolderPath.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(path[i])) != std::toupper(static_cast<unsigned char>(kFolderPath[i])))
            return std::nullopt;
    const std::string_view digits = path.substr(kFolderPath.size());
    std::uint32_t number{};
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (ec != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return number;
}

OptionStep turn_option_on(psprecomp::GuestMemory &memory, std::uint32_t buffer) {
    if (!memory.contains(kOptions, kOptionBytes) || !memory.contains(buffer, kOptionBytes)) return OptionStep::Waiting;
    for (std::uint32_t i = 0; i < kOptionBytes; ++i)
        if (memory.load8(kOptions + i) != memory.load8(buffer + i)) return OptionStep::Waiting;
    if (memory.load8(kOptions + kInstallOption) != 0u) return OptionStep::Unchanged;
    memory.store8(kOptions + kInstallOption, 1u);
    return OptionStep::TurnedOn;
}

bool option_wanted() {
    static const bool wanted = [] {
        const char *text = std::getenv("MHFU_INSTALL_OPTION");
        return text == nullptr || std::string_view(text) != "0";
    }();
    return wanted;
}

} // namespace mhfu::game::install_data
