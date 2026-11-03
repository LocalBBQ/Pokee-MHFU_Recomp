// sceUtilitySavedata*: the PSP's save-data dialog, backed by PSP-layout
// folders on the host (see save_data/savedata_store.hpp).
//
// What the game asks for:
//   - at boot, AUTOLOAD of its own save; when there is none, AUTOLOAD of the
//     saves of two other games (ULJM05500, ULJM05710) and SIZES for its own,
//     and "no data" answers lead to a new game;
//   - to save, AUTOLOAD of its own save followed by AUTOSAVE;
//   - after the title screen, AUTOLOAD again to read the characters.
// No save-data UI is drawn: modes that would show a list act as if the player
// confirmed the first entry. Every mode, the failures included, reports QUIT
// after a few polls; see utility_dialog.hpp.
#include "hle_common.hpp"
#include "utility_dialog.hpp"

#include "game/install_data.hpp"
#include "game/pouch_page.hpp"
#include "save_data/savedata_crypto.hpp"
#include "save_data/savedata_store.hpp"
#include "save_data/save_transfer.hpp"

#include "platform/utf8_path.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace mhfu {
namespace {

// SceUtilitySavedataParam, after the common dialog header.
namespace param {
constexpr std::uint32_t kMode = 0x30u;
constexpr std::uint32_t kOverwrite = 0x38u;
constexpr std::uint32_t kGameName = 0x3Cu;     // char[13]
constexpr std::uint32_t kSaveName = 0x4Cu;     // char[20]
constexpr std::uint32_t kSaveNameList = 0x60u; // pointer to char[20] entries, "" terminated
constexpr std::uint32_t kFileName = 0x64u;     // char[13]
constexpr std::uint32_t kDataBuf = 0x74u;
constexpr std::uint32_t kDataBufSize = 0x78u;
constexpr std::uint32_t kDataSize = 0x7Cu;
constexpr std::uint32_t kTitle = 0x80u;          // char[128]
constexpr std::uint32_t kSavedataTitle = 0x100u; // char[128]
constexpr std::uint32_t kDetail = 0x180u;        // char[1024]
constexpr std::uint32_t kParentalLevel = 0x580u;
constexpr std::uint32_t kIcon0 = 0x584u; // {buf, bufSize, size, unknown}
constexpr std::uint32_t kIcon1 = 0x594u;
constexpr std::uint32_t kPic1 = 0x5A4u;
constexpr std::uint32_t kSnd0 = 0x5B4u;
constexpr std::uint32_t kFocus = 0x5C8u;
constexpr std::uint32_t kMsFree = 0x5D0u;
constexpr std::uint32_t kMsData = 0x5D4u;
constexpr std::uint32_t kUtilityData = 0x5D8u;
constexpr std::uint32_t kKey = 0x5DCu; // char[16], firmware 2.00 and later
constexpr std::uint32_t kSecureVersion = 0x5ECu;
constexpr std::uint32_t kMinimumSizeWithKey = 0x5ECu;
// The game fills these for FILES and GETSIZE (the install, and its check at
// start); the layouts below are the ones its code reads and writes.
constexpr std::uint32_t kFileList = 0x5F8u;
constexpr std::uint32_t kSizeInfo = 0x5FCu;
} // namespace param

// FILES: {maxSecure, maxNormal, maxSystem, resultSecure, resultNormal,
// resultSystem, secureEntries, normalEntries, systemEntries}, each entry 80
// bytes: {mode, attr, u64 size, ctime, atime, mtime, char name[16]}.
namespace file_list {
constexpr std::uint32_t kMaxCounts = 0u;
constexpr std::uint32_t kResultCounts = 12u;
constexpr std::uint32_t kEntries = 24u;
constexpr std::uint32_t kEntrySize = 80u;
constexpr std::uint32_t kEntrySizeField = 8u;
constexpr std::uint32_t kEntryTimes = 16u; // three ScePspDateTime of 16 bytes
constexpr std::uint32_t kEntryName = 64u;
constexpr std::uint32_t kRegularFileMode = 0x21FFu; // FIO_S_IFREG | 0777
constexpr std::uint32_t kRegularFileAttr = 0x20u;   // FIO_SO_IFREG
} // namespace file_list

// GETSIZE: {numSecure, numNormal, secureEntries, normalEntries, sectorSize,
// freeSectors, freeKB, freeString[8], neededKB, neededString[8], overwriteKB,
// overwriteString[8]}, each entry {u64 size, char name[16]}. The game treats
// a non-zero neededKB as "not enough space", so it is what is missing, not
// the total.
namespace size_info {
constexpr std::uint32_t kNumSecure = 0u;
constexpr std::uint32_t kNumNormal = 4u;
constexpr std::uint32_t kSecureEntries = 8u;
constexpr std::uint32_t kNormalEntries = 12u;
constexpr std::uint32_t kSectorSize = 16u;
constexpr std::uint32_t kFreeSectors = 20u;
constexpr std::uint32_t kFreeKb = 24u;
constexpr std::uint32_t kFreeString = 28u;
constexpr std::uint32_t kNeededKb = 36u;
constexpr std::uint32_t kNeededString = 40u;
constexpr std::uint32_t kOverwriteKb = 48u;
constexpr std::uint32_t kOverwriteString = 52u;
constexpr std::uint32_t kEntrySize = 24u;
} // namespace size_info

enum Mode : std::uint32_t {
    kAutoLoad = 0,
    kAutoSave = 1,
    kLoad = 2,
    kSave = 3,
    kListLoad = 4,
    kListSave = 5,
    kListDelete = 6,
    kDelete = 7,
    kSizes = 8,
    kAutoDelete = 9,
    kSingleDelete = 10,
    kFiles = 12,
    kMakeDataSecure = 13,
    kMakeData = 14,
    kWriteData = 18,
    kGetSize = 22,
};

const char *mode_name(std::uint32_t mode) {
    static const char *const names[] = {"AUTOLOAD", "AUTOSAVE", "LOAD", "SAVE", "LISTLOAD", "LISTSAVE", "LISTDELETE",
        "DELETE", "SIZES", "AUTODELETE", "SINGLEDELETE", "LIST", "FILES", "MAKEDATASECURE", "MAKEDATA",
        "READDATASECURE", "READDATA", "WRITEDATASECURE", "WRITEDATA", "ERASESECURE", "ERASE", "DELETEDATA", "GETSIZE"};
    return mode < std::size(names) ? names[mode] : "UNKNOWN";
}

// Result codes written to the parameter block's result field.
namespace result {
constexpr std::uint32_t kOk = 0u;
constexpr std::uint32_t kLoadDataBroken = 0x80110306u;
constexpr std::uint32_t kLoadNoData = 0x80110307u;
constexpr std::uint32_t kLoadParam = 0x80110308u;
// FILES, GETSIZE and the data modes on a folder that does not exist; the
// install takes it as "no install data yet".
constexpr std::uint32_t kRwNoData = 0x80110327u;
constexpr std::uint32_t kDeleteNoData = 0x80110347u;
constexpr std::uint32_t kSaveAccessError = 0x80110385u;
constexpr std::uint32_t kSaveParam = 0x80110388u;
constexpr std::uint32_t kSizesNoData = 0x801103C7u;
} // namespace result

constexpr std::uint32_t kClusterSize = 0x8000u;
constexpr std::uint32_t kFreeClusters = 0x8000u; // 1 GiB

struct SavedataState {
    std::filesystem::path memory_stick;
    DialogLifecycle dialog;
};

SavedataState &state() {
    static SavedataState s;
    return s;
}

bool trace_savedata() {
    static const bool enabled = std::getenv("MHFU_TRACE_SAVEDATA") != nullptr;
    return enabled;
}

std::string hex_bytes(const savedata::Block &block) {
    std::string text;
    char digits[3];
    for (const std::uint8_t b : block) {
        std::snprintf(digits, sizeof digits, "%02x", b);
        text += digits;
    }
    return text;
}

// "<>" stands for "no save name": the folder is the game name alone.
std::string save_name_at(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    std::string name = read_cstring(memory, address, 20u);
    return name == "<>" ? std::string{} : name;
}

std::vector<std::string> save_name_list(const psprecomp::GuestMemory &memory, std::uint32_t list) {
    std::vector<std::string> names;
    if (list == 0u) return names;
    for (std::uint32_t i = 0; i < 64u; ++i) {
        const std::uint32_t entry = list + i * 20u;
        if (memory.load8(entry) == 0u) break;
        names.push_back(save_name_at(memory, entry));
    }
    return names;
}

std::vector<std::uint8_t> read_guest(const psprecomp::GuestMemory &memory, std::uint32_t address, std::uint32_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::uint32_t i = 0; i < size; ++i) bytes[i] = memory.load8(address + i);
    return bytes;
}

// {buf, bufSize, size, unknown}: the bytes the game supplies for an icon file.
std::vector<std::uint8_t> read_file_data(const psprecomp::GuestMemory &memory, std::uint32_t field) {
    const std::uint32_t buffer = memory.load32(field);
    const std::uint32_t size = std::min(memory.load32(field + 8u), memory.load32(field + 4u));
    if (buffer == 0u || size == 0u) return {};
    return read_guest(memory, buffer, size);
}

savedata::SaveFiles files_for(
    const psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    savedata::SaveFiles files;
    files.game_name = read_cstring(memory, params + param::kGameName, 13u);
    files.save_name = save_name;
    files.file_name = read_cstring(memory, params + param::kFileName, 13u);
    if (memory.load32(params + dialog_common::kSizeOffset) > param::kMinimumSizeWithKey) {
        savedata::Block key{};
        for (std::uint32_t i = 0; i < key.size(); ++i) key[i] = memory.load8(params + param::kKey + i);
        if (!savedata::is_zero(key)) {
            files.key = key;
            // The menu's Import checks saves with it.
            savedata::remember_game_key(files.game_name, key);
        }
    }
    return files;
}

void log_request(const psprecomp::GuestMemory &memory, std::uint32_t params) {
    const std::uint32_t mode = memory.load32(params + param::kMode);
    std::cerr << "[savedata] " << mode_name(mode) << " (" << mode << ")"
              << " game=\"" << read_cstring(memory, params + param::kGameName, 13u) << "\""
              << " save=\"" << read_cstring(memory, params + param::kSaveName, 20u) << "\""
              << " file=\"" << read_cstring(memory, params + param::kFileName, 13u) << "\"";
    if (trace_savedata()) {
        const std::uint32_t size = memory.load32(params + dialog_common::kSizeOffset);
        std::cerr << " size=" << psprecomp::hex32(size)
                  << " buf=" << psprecomp::hex32(memory.load32(params + param::kDataBuf))
                  << " bufSize=" << psprecomp::hex32(memory.load32(params + param::kDataBufSize))
                  << " dataSize=" << psprecomp::hex32(memory.load32(params + param::kDataSize))
                  << " overwrite=" << memory.load32(params + param::kOverwrite)
                  << " focus=" << memory.load32(params + param::kFocus)
                  << " icon0=" << memory.load32(params + param::kIcon0 + 8u)
                  << " icon1=" << memory.load32(params + param::kIcon1 + 8u)
                  << " pic1=" << memory.load32(params + param::kPic1 + 8u)
                  << " snd0=" << memory.load32(params + param::kSnd0 + 8u);
        if (size > param::kMinimumSizeWithKey) {
            savedata::Block key{};
            for (std::uint32_t i = 0; i < key.size(); ++i) key[i] = memory.load8(params + param::kKey + i);
            std::cerr << " key=" << hex_bytes(key)
                      << " secureVersion=" << memory.load32(params + param::kSecureVersion);
        }
        const auto names = save_name_list(memory, memory.load32(params + param::kSaveNameList));
        if (!names.empty()) {
            std::cerr << " list=[";
            for (std::size_t i = 0; i < names.size(); ++i) std::cerr << (i ? "," : "") << "\"" << names[i] << "\"";
            std::cerr << "]";
        }
    }
    std::cerr << "\n";
}

std::uint32_t do_load(psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    const auto files = files_for(memory, params, save_name);
    const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
    const std::uint32_t capacity = memory.load32(params + param::kDataBufSize);
    if (files.game_name.empty() || files.file_name.empty() || buffer == 0u) return result::kLoadParam;

    const auto loaded = savedata::load_save(state().memory_stick, files);
    if (loaded.status == savedata::LoadStatus::NoData) {
        std::cerr << "[savedata] no save data in " << path_to_utf8(savedata::save_folder(state().memory_stick, files))
                  << "\n";
        return result::kLoadNoData;
    }
    if (loaded.status == savedata::LoadStatus::Broken) {
        std::cerr << "[savedata] broken save in " << path_to_utf8(savedata::save_folder(state().memory_stick, files))
                  << ": " << loaded.reason << "\n";
        return result::kLoadDataBroken;
    }
    const auto &data = loaded.contents.data;
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(data.size(), capacity));
    memory.copy_in(buffer, std::span<const std::uint8_t>(data.data(), count));
    memory.store32(params + param::kDataSize, count);
    write_cstring(memory, params + param::kTitle, loaded.contents.title, 128u);
    write_cstring(memory, params + param::kSavedataTitle, loaded.contents.savedata_title, 128u);
    write_cstring(memory, params + param::kDetail, loaded.contents.detail, 1024u);
    std::cerr << "[savedata] loaded " << count << " bytes from "
              << path_to_utf8(savedata::save_folder(state().memory_stick, files)) << (files.key ? " (decrypted)" : "")
              << "\n";
    if (data.size() > capacity)
        std::cerr << "[savedata] " << files.file_name << " holds " << data.size() << " bytes; the buffer takes "
                  << capacity << "\n";
    game::pouch_page::game_loaded();
    return result::kOk;
}

// The titles and icons a save or MAKEDATA request carries, without the data.
savedata::SaveContents contents_for(const psprecomp::GuestMemory &memory, std::uint32_t params) {
    savedata::SaveContents contents;
    contents.title = read_cstring(memory, params + param::kTitle, 128u);
    contents.savedata_title = read_cstring(memory, params + param::kSavedataTitle, 128u);
    contents.detail = read_cstring(memory, params + param::kDetail, 1024u);
    contents.parental_level = memory.load8(params + param::kParentalLevel);
    contents.icon0 = read_file_data(memory, params + param::kIcon0);
    contents.icon1 = read_file_data(memory, params + param::kIcon1);
    contents.pic1 = read_file_data(memory, params + param::kPic1);
    contents.snd0 = read_file_data(memory, params + param::kSnd0);
    return contents;
}

std::uint32_t do_save(psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    const auto files = files_for(memory, params, save_name);
    const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
    const std::uint32_t size = memory.load32(params + param::kDataSize);
    if (files.game_name.empty() || files.file_name.empty() || buffer == 0u) return result::kSaveParam;

    savedata::SaveContents contents = contents_for(memory, params);
    contents.data = read_guest(memory, buffer, size);

    std::string error;
    if (!savedata::write_save(state().memory_stick, files, contents, error)) {
        std::cerr << "[savedata] save failed: " << error << "\n";
        return result::kSaveAccessError;
    }
    std::cerr << "[savedata] saved " << size << " bytes to "
              << path_to_utf8(savedata::save_folder(state().memory_stick, files)) << (files.key ? " (encrypted)" : "")
              << "\n";
    // The fourth pouch page is kept with each save the game writes.
    game::pouch_page::game_saved(memory);
    return result::kOk;
}

std::uint32_t do_delete(psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    const auto files = files_for(memory, params, save_name);
    if (!savedata::delete_save(state().memory_stick, files)) return result::kDeleteNoData;
    std::cerr << "[savedata] deleted " << path_to_utf8(savedata::save_folder(state().memory_stick, files)) << "\n";
    return result::kOk;
}

void write_size_string(psprecomp::GuestMemory &memory, std::uint32_t address, std::uint64_t kilobytes) {
    const std::string text = kilobytes >= 1024u * 1024u ? std::to_string(kilobytes / (1024u * 1024u)) + " GB"
        : kilobytes >= 1024u                            ? std::to_string(kilobytes / 1024u) + " MB"
                                                        : std::to_string(kilobytes) + " KB";
    write_cstring(memory, address, text, 8u);
}

// SIZES: free space on the stick, the space an existing save takes, and the
// space the requested save would need.
std::uint32_t do_sizes(psprecomp::GuestMemory &memory, std::uint32_t params) {
    std::uint32_t outcome = result::kOk;
    if (const std::uint32_t free = memory.load32(params + param::kMsFree); free != 0u) {
        memory.store32(free, kClusterSize);
        memory.store32(free + 4u, kFreeClusters);
        const std::uint64_t free_kb = static_cast<std::uint64_t>(kClusterSize) * kFreeClusters / 1024u;
        memory.store32(free + 8u, static_cast<std::uint32_t>(free_kb));
        write_size_string(memory, free + 12u, free_kb);
    }
    if (const std::uint32_t data = memory.load32(params + param::kMsData); data != 0u) {
        savedata::SaveFiles files;
        files.game_name = read_cstring(memory, data, 13u);
        files.save_name = save_name_at(memory, data + 16u);
        const std::uint64_t bytes = savedata::save_size(state().memory_stick, files);
        const std::uint32_t info = data + 36u;
        const auto clusters = static_cast<std::uint32_t>((bytes + kClusterSize - 1u) / kClusterSize);
        memory.store32(info, clusters);
        memory.store32(info + 4u, static_cast<std::uint32_t>((bytes + 1023u) / 1024u));
        write_size_string(memory, info + 8u, (bytes + 1023u) / 1024u);
        memory.store32(info + 16u, static_cast<std::uint32_t>((bytes + 1023u) / 1024u));
        write_size_string(memory, info + 20u, (bytes + 1023u) / 1024u);
        if (bytes == 0u) outcome = result::kSizesNoData;
    }
    if (const std::uint32_t needed = memory.load32(params + param::kUtilityData); needed != 0u) {
        std::uint64_t bytes = memory.load32(params + param::kDataSize) + 16u;
        for (const std::uint32_t field : {param::kIcon0, param::kIcon1, param::kPic1, param::kSnd0})
            bytes += memory.load32(params + field + 8u);
        bytes += 0x2000u; // PARAM.SFO and directory entries
        const auto clusters = static_cast<std::uint32_t>((bytes + kClusterSize - 1u) / kClusterSize);
        const std::uint64_t kb = static_cast<std::uint64_t>(clusters) * kClusterSize / 1024u;
        memory.store32(needed, clusters);
        memory.store32(needed + 4u, static_cast<std::uint32_t>(kb));
        write_size_string(memory, needed + 8u, kb);
        memory.store32(needed + 16u, static_cast<std::uint32_t>(kb));
        write_size_string(memory, needed + 20u, kb);
    }
    return outcome;
}

// MAKEDATA(SECURE): the folder with PARAM.SFO and the icons, and the data
// file when the request names one. The install makes its folder this way with
// no file and adds the files with WRITEDATA.
std::uint32_t do_make_data(
    psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name, bool secure) {
    auto files = files_for(memory, params, save_name);
    if (!secure) files.key.reset();
    if (files.game_name.empty()) return result::kSaveParam;
    savedata::SaveContents contents = contents_for(memory, params);
    const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
    if (!files.file_name.empty() && buffer != 0u)
        contents.data = read_guest(memory, buffer, memory.load32(params + param::kDataSize));
    std::string error;
    if (!savedata::write_save(state().memory_stick, files, contents, error)) {
        std::cerr << "[savedata] making the save failed: " << error << "\n";
        return result::kSaveAccessError;
    }
    std::cerr << "[savedata] made " << path_to_utf8(savedata::save_folder(state().memory_stick, files)) << "\n";
    return result::kOk;
}

// WRITEDATA: one plain file into a folder MAKEDATA made.
std::uint32_t do_write_data(psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    auto files = files_for(memory, params, save_name);
    files.key.reset();
    const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
    const std::uint32_t size = memory.load32(params + param::kDataSize);
    if (files.game_name.empty() || files.file_name.empty() || buffer == 0u) return result::kSaveParam;
    std::string error;
    switch (savedata::write_data_file(state().memory_stick, files, read_guest(memory, buffer, size), error)) {
    case savedata::WriteStatus::Ok:
        if (trace_savedata()) std::cerr << "[savedata] wrote " << size << " bytes to " << files.file_name << "\n";
        return result::kOk;
    case savedata::WriteStatus::NoData:
        return result::kRwNoData;
    case savedata::WriteStatus::Failed:
        break;
    }
    std::cerr << "[savedata] write failed: " << error << "\n";
    return result::kSaveAccessError;
}

void write_date_time(psprecomp::GuestMemory &memory, std::uint32_t address, const std::tm &tm) {
    // ScePspDateTime: year, month, day, hour, minute, second, then microseconds.
    const int fields[] = {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
    for (std::size_t i = 0; i < std::size(fields); ++i)
        memory.store16(address + 2u * static_cast<std::uint32_t>(i), static_cast<std::uint16_t>(fields[i]));
    memory.store32(address + 12u, 0u);
}

// The served install as FILES lists it: the numbered files and the system
// files a real one has.
savedata::FileListing served_listing(const std::vector<game::install_data::File> &files) {
    savedata::FileListing listing;
    listing.exists = true;
    const auto now = std::filesystem::file_time_type::clock::now();
    char name[16];
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::snprintf(name, sizeof name, "%08u", static_cast<unsigned>(i));
        listing.normal.push_back({name, files[i].size, now});
    }
    for (const auto &file : game::install_data::kSystemFiles)
        listing.system.push_back({std::string(file.name), file.size, now});
    return listing;
}

// FILES: the folder's files, as many of each kind as the game has room for.
// The install's check at start compares the normal files' sizes, in name
// order, with what it would have written.
std::uint32_t do_files(psprecomp::GuestMemory &memory, std::uint32_t params, const savedata::FileListing &listing) {
    const std::uint32_t list = memory.load32(params + param::kFileList);
    if (list == 0u) return result::kLoadParam;
    const std::vector<savedata::ListedFile> *kinds[] = {&listing.secure, &listing.normal, &listing.system};
    for (std::uint32_t kind = 0; kind < 3u; ++kind) {
        const std::uint32_t room = memory.load32(list + file_list::kMaxCounts + 4u * kind);
        const std::uint32_t entries = memory.load32(list + file_list::kEntries + 4u * kind);
        const auto count =
            static_cast<std::uint32_t>(std::min<std::size_t>(kinds[kind]->size(), entries != 0u ? room : 0u));
        memory.store32(list + file_list::kResultCounts + 4u * kind, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const savedata::ListedFile &file = (*kinds[kind])[i];
            const std::uint32_t entry = entries + i * file_list::kEntrySize;
            memory.store32(entry, file_list::kRegularFileMode);
            memory.store32(entry + 4u, file_list::kRegularFileAttr);
            memory.store32(entry + file_list::kEntrySizeField, static_cast<std::uint32_t>(file.size));
            memory.store32(entry + file_list::kEntrySizeField + 4u, static_cast<std::uint32_t>(file.size >> 32u));
            const std::tm modified = savedata::local_time(savedata::to_system(file.modified));
            for (std::uint32_t t = 0; t < 3u; ++t)
                write_date_time(memory, entry + file_list::kEntryTimes + 16u * t, modified);
            write_cstring(memory, entry + file_list::kEntryName, file.name, 16u);
        }
    }
    if (trace_savedata())
        std::cerr << "[savedata] files: " << listing.secure.size() << " secure, " << listing.normal.size()
                  << " normal, " << listing.system.size() << " system\n";
    return listing.exists ? result::kOk : result::kRwNoData;
}

// GETSIZE: the free space, and how much more a save of the listed files would
// need than is free, both to write it new and to overwrite the existing one.
std::uint32_t do_get_size(psprecomp::GuestMemory &memory, std::uint32_t params, const std::string &save_name) {
    const std::uint32_t info = memory.load32(params + param::kSizeInfo);
    if (info == 0u) return result::kLoadParam;
    const auto files = files_for(memory, params, save_name);
    const auto cluster_bytes = [](std::uint64_t bytes) {
        return (bytes + kClusterSize - 1u) / kClusterSize * kClusterSize;
    };
    // The folder entry and PARAM.SFO, then the listed files and the icons.
    std::uint64_t needed = 2u * kClusterSize;
    for (const auto &[count_field, entries_field] : {std::pair{size_info::kNumSecure, size_info::kSecureEntries},
             {size_info::kNumNormal, size_info::kNormalEntries}}) {
        const std::uint32_t count = memory.load32(info + count_field);
        const std::uint32_t entries = memory.load32(info + entries_field);
        if (entries == 0u) continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = entries + i * size_info::kEntrySize;
            needed +=
                cluster_bytes(memory.load32(entry) | static_cast<std::uint64_t>(memory.load32(entry + 4u)) << 32u);
        }
    }
    for (const std::uint32_t field : {param::kIcon0, param::kIcon1, param::kPic1, param::kSnd0})
        needed += cluster_bytes(memory.load32(params + field + 8u));
    const std::uint64_t free_bytes = static_cast<std::uint64_t>(kClusterSize) * kFreeClusters;
    const std::uint64_t existing = savedata::save_size(state().memory_stick, files);
    const auto missing_kb = [&](std::uint64_t bytes) {
        return bytes > free_bytes ? (bytes - free_bytes + 1023u) / 1024u : 0u;
    };
    const std::uint64_t needed_kb = missing_kb(needed);
    const std::uint64_t overwrite_kb = missing_kb(needed > existing ? needed - existing : 0u);
    memory.store32(info + size_info::kSectorSize, kClusterSize);
    memory.store32(info + size_info::kFreeSectors, kFreeClusters);
    memory.store32(info + size_info::kFreeKb, static_cast<std::uint32_t>(free_bytes / 1024u));
    write_size_string(memory, info + size_info::kFreeString, free_bytes / 1024u);
    memory.store32(info + size_info::kNeededKb, static_cast<std::uint32_t>(needed_kb));
    write_size_string(memory, info + size_info::kNeededString, needed_kb);
    memory.store32(info + size_info::kOverwriteKb, static_cast<std::uint32_t>(overwrite_kb));
    write_size_string(memory, info + size_info::kOverwriteString, overwrite_kb);
    if (trace_savedata())
        std::cerr << "[savedata] getsize: " << needed / 1024u << " KB for the save, " << needed_kb
                  << " KB more than is free\n";
    return savedata::save_exists(state().memory_stick, files) ? result::kOk : result::kRwNoData;
}

// A request for the install folder while it is served (game/install_data.hpp):
// it is always there and complete, and the game's own Data Install writes
// nothing. Other folders, and the install when it is not served: nullopt.
std::optional<std::uint32_t> served_install_request(
    psprecomp::GuestMemory &memory, std::uint32_t params, std::uint32_t mode, const std::string &save_name) {
    if (read_cstring(memory, params + param::kGameName, 13u) + save_name != game::install_data::kFolder)
        return std::nullopt;
    const auto *files = game::install_data::served(memory);
    if (files == nullptr) return std::nullopt;
    switch (mode) {
    case kFiles:
        return do_files(memory, params, served_listing(*files));
    case kGetSize:
        (void)do_get_size(memory, params, save_name);
        return result::kOk;
    case kMakeDataSecure:
    case kMakeData:
    case kWriteData:
        if (trace_savedata()) std::cerr << "[savedata] the install is served; nothing written\n";
        return result::kOk;
    default:
        return std::nullopt;
    }
}

// Picks the save a list dialog would land on when the player just confirms:
// for loading and deleting the first listed save that exists, for saving the
// first listed name.
std::optional<std::string> pick_from_list(psprecomp::GuestMemory &memory, std::uint32_t params, bool must_exist) {
    const auto names = save_name_list(memory, memory.load32(params + param::kSaveNameList));
    for (const auto &name : names) {
        if (!must_exist || savedata::save_exists(state().memory_stick, files_for(memory, params, name))) return name;
    }
    if (!must_exist) return save_name_at(memory, params + param::kSaveName);
    return std::nullopt;
}

std::uint32_t run_request(psprecomp::GuestMemory &memory, std::uint32_t params) {
    const std::uint32_t mode = memory.load32(params + param::kMode);
    const std::string save_name = save_name_at(memory, params + param::kSaveName);
    if (const auto served = served_install_request(memory, params, mode, save_name)) return *served;
    switch (mode) {
    case kAutoLoad:
    case kLoad:
        return do_load(memory, params, save_name);
    case kAutoSave:
    case kSave:
        return do_save(memory, params, save_name);
    case kListLoad: {
        const auto chosen = pick_from_list(memory, params, true);
        if (!chosen) return result::kLoadNoData;
        write_cstring(memory, params + param::kSaveName, *chosen, 20u);
        return do_load(memory, params, *chosen);
    }
    case kListSave: {
        const auto chosen = pick_from_list(memory, params, false);
        write_cstring(memory, params + param::kSaveName, *chosen, 20u);
        return do_save(memory, params, *chosen);
    }
    case kListDelete: {
        const auto chosen = pick_from_list(memory, params, true);
        if (!chosen) return result::kDeleteNoData;
        write_cstring(memory, params + param::kSaveName, *chosen, 20u);
        return do_delete(memory, params, *chosen);
    }
    case kDelete:
    case kAutoDelete:
    case kSingleDelete:
        return do_delete(memory, params, save_name);
    case kSizes:
        return do_sizes(memory, params);
    case kFiles:
        return do_files(
            memory, params, savedata::list_files(state().memory_stick, files_for(memory, params, save_name)));
    case kMakeDataSecure:
    case kMakeData:
        return do_make_data(memory, params, save_name, mode == kMakeDataSecure);
    case kWriteData:
        return do_write_data(memory, params, save_name);
    case kGetSize:
        return do_get_size(memory, params, save_name);
    default:
        // Not used by this game. Report a parameter error so the guest takes
        // its failure path instead of reading results that were never written.
        std::cerr << "[savedata] mode " << mode << " (" << mode_name(mode) << ") is not implemented\n";
        return result::kLoadParam;
    }
}

} // namespace

void register_savedata(HleRegistrar &hle, const std::filesystem::path &memory_stick) {
    state().memory_stick = memory_stick;
    savedata::set_memory_stick(memory_stick);

    hle.add("sceUtility", "sceUtilitySavedataInitStart", [](Runtime &rt, AllegrexContext &ctx) {
        auto &memory = rt.memory();
        const std::uint32_t params = arg(ctx, 0);
        // A new request while one is still active replaces it rather than
        // failing: the earlier request's work is already done.
        if (state().dialog.active())
            std::cerr << "[savedata] InitStart while a dialog is active (status " << state().dialog.status() << ")\n";
        log_request(memory, params);
        const std::uint32_t outcome = run_request(memory, params);
        memory.store32(params + dialog_common::kResultOffset, outcome);
        if (outcome != result::kOk || trace_savedata())
            std::cerr << "[savedata] result " << psprecomp::hex32(outcome) << "\n";
        state().dialog.start();
        kernel().finish(ctx, 0u);
    });

    hle.add("sceUtility", "sceUtilitySavedataUpdate", [](Runtime &, AllegrexContext &ctx) {
        (void)state().dialog.poll();
        kernel().finish(ctx, 0u);
    });

    hle.add("sceUtility", "sceUtilitySavedataGetStatus", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t status = state().dialog.poll();
        if (trace_savedata()) std::cerr << "[savedata] status " << status << "\n";
        kernel().finish(ctx, status);
    });

    hle.add("sceUtility", "sceUtilitySavedataShutdownStart", [](Runtime &, AllegrexContext &ctx) {
        if (!state().dialog.active()) {
            kernel().finish(ctx, kErrorUtilityInvalidStatus);
            return;
        }
        if (!state().dialog.shutdown()) std::cerr << "[savedata] ShutdownStart before the dialog finished\n";
        kernel().finish(ctx, 0u);
    });
}

} // namespace mhfu
