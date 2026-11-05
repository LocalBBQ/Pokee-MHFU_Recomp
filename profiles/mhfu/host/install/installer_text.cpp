// Text the installer's front ends show, kept apart from the dialogs so that
// it can be checked without SDL.

#include "install/installer.hpp"

#include "platform/utf8_path.hpp"

namespace mhfu::install {

std::string storage_question(
    const std::filesystem::path &image, const ImageInfo &info, const std::filesystem::path &data_dir) {
    const std::uint64_t tenths = (info.size_bytes + 50'000'000u) / 100'000'000u;
    const std::string size = std::to_string(tenths / 10u) + "." + std::to_string(tenths % 10u) + " GB";
    return "The image is " + std::string(info.title) + " (" + info.disc_id_display + ") and passed its checks.\n\n" +
        "Copy it into Pokee's data folder (recommended, " + size +
        "), so the game keeps working if the original is moved or deleted?\n\n"
        "Or use it where it is, to save space. The image must then stay at:\n" +
        path_to_utf8(image) + "\n\nData folder:\n" + path_to_utf8(data_dir);
}

} // namespace mhfu::install
