// One synthetic overlay corpus for tests/overlay_swap_tests.cpp. Marks 1 and 2
// are the two images of overlay_swap_fixture.hpp; mark 3 claims image 1 with
// another ABI version, so the host must refuse it. With
// MHFU_OVERLAY_FIXTURE_LEGACY the corpus is a library of its own, exported the
// way a build before packs did; otherwise a pack lists its entry,
// MHFU_OVERLAY_FIXTURE_ENTRY.
#define MHFU_OVERLAY_MODULE 1
#include "overlay_module.hpp"
#include "overlay_swap_fixture.hpp"

#include "psprecomp/runtime.hpp"

namespace {

constexpr std::uint32_t kImage = MHFU_OVERLAY_FIXTURE_MARK == 3u ? 1u : MHFU_OVERLAY_FIXTURE_MARK;

void fixture_body(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ctx.gpr[2] = MHFU_OVERLAY_FIXTURE_MARK;
    ctx.pc = kOverlayFixtureEntry + 4u;
}

mhfu::OverlayModuleInfo fixture_info() {
    const auto image = overlay_fixture_image(kImage);
    mhfu::OverlayModuleInfo info{};
    info.abi_version = MHFU_OVERLAY_FIXTURE_MARK == 3u ? mhfu::kOverlayAbiVersion + 1u : mhfu::kOverlayAbiVersion;
    info.base = kOverlayFixtureBase;
    info.size = static_cast<std::uint32_t>(kOverlayFixtureBytes);
    info.code_size = kOverlayFixtureCodeBytes;
    info.hash = overlay_fixture_hash(image.data(), image.size());
    info.name = kImage == 1u ? "stageA" : "stageB";
    return info;
}

const mhfu::OverlayModuleInfo kInfo = fixture_info();

void install(psprecomp::Runtime &runtime) {
    // Chained calls only enter bodies whose names start with recomp_unit_, which
    // is how generated overlay code is registered.
    const char *unit_name = kImage == 1u ? "recomp_unit_stageA" : "recomp_unit_stageB";
    runtime.register_function(kOverlayFixtureEntry, &fixture_body, unit_name);
}

} // namespace

#if defined(MHFU_OVERLAY_FIXTURE_LEGACY)
extern "C" MHFU_OVERLAY_EXPORT const mhfu::OverlayModuleInfo *mhfu_overlay_info() {
    return &kInfo;
}

extern "C" MHFU_OVERLAY_EXPORT void mhfu_register_overlay(psprecomp::Runtime &runtime) {
    install(runtime);
}
#else
namespace mhfu {
extern const OverlayCorpusEntry MHFU_OVERLAY_FIXTURE_ENTRY;
const OverlayCorpusEntry MHFU_OVERLAY_FIXTURE_ENTRY{&kInfo, &install, nullptr};
} // namespace mhfu
#endif
