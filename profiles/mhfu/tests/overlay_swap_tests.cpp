// A second overlay copied into an occupied slot must not run the first
// overlay's compiled body. The game does this before it flushes the
// instruction cache, so the swap has to be noticed on the call itself.
//
// Built twice: against one library holding both corpora and a corpus for
// another ABI the loader must skip (mhfu_overlay_swap_tests), and against a
// library per corpus as builds before packs made them
// (mhfu_overlay_single_library_tests).

#include "overlay_swap_fixture.hpp"
#include "overlays.hpp"

#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

void check(bool condition, const char *contract) {
    if (!condition) throw std::runtime_error(contract);
}

void outside_slot(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ctx.gpr[2] = 9u;
}

void swapped_slot_does_not_run_the_old_body() {
    if (setenv("MHFU_OVERLAY_DIR", MHFU_OVERLAY_FIXTURE_DIR, 1) != 0)
        throw std::runtime_error("could not point the overlay loader at the fixtures");

    psprecomp::Runtime runtime;
    mhfu::install_overlay_support(runtime);
    const auto first = overlay_fixture_image(1u);
    runtime.memory().copy_in(kOverlayFixtureBase, std::span<const std::uint8_t>(first.data(), first.size()));
    runtime.cpu().gpr[2] = 0u;
    runtime.run(kOverlayFixtureEntry, 1u);
    check(runtime.cpu().gpr[2] == 1u, "the first image's body did not run, or a refused corpus did");

    runtime.cpu().gpr[2] = 0u;
    runtime.cpu().pc = kOverlayFixtureEntry;
    check(runtime.invoke_chained_call(runtime.cpu()), "a call into the unchanged image was rejected");
    check(runtime.cpu().gpr[2] == 1u, "the unchanged image did not run its own body");

    const auto second = overlay_fixture_image(2u);
    runtime.memory().copy_in(kOverlayFixtureBase, std::span<const std::uint8_t>(second.data(), second.size()));
    runtime.cpu().gpr[2] = 0u;
    runtime.cpu().pc = kOverlayFixtureEntry;
    check(!runtime.invoke_chained_call(runtime.cpu()), "the replaced image still ran the previous body");
    check(runtime.cpu().gpr[2] == 0u, "the previous body executed after the slot was overwritten");
    check(runtime.cpu().pc == kOverlayFixtureEntry, "rejecting the stale body moved the guest PC");

    runtime.cpu().gpr[2] = 0u;
    runtime.run(kOverlayFixtureEntry, 1u);
    check(runtime.cpu().gpr[2] == 2u, "the image now in the slot did not run");

    runtime.cpu().gpr[2] = 0u;
    runtime.register_function(0x08804000u, &outside_slot, "recomp_unit_not_an_overlay");
    runtime.cpu().pc = 0x08804000u;
    check(runtime.invoke_chained_call(runtime.cpu()) && runtime.cpu().gpr[2] == 9u,
        "a call outside every overlay slot was treated as stale");
}

} // namespace

int main() {
    try {
        swapped_slot_does_not_run_the_old_body();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overlay swap tests passed\n";
    return 0;
}
