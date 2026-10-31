#pragma once

// ABI between Pokee and the recompiled overlay libraries it loads at run
// time. Keeping the corpora out of the executable means a new overlay costs a
// compile of its own sources and a relink of one library, nothing else. A
// library holds the corpora of one slot (see MHFU_OVERLAY_PACKS in
// profiles/mhfu/CMakeLists.txt), or of a single overlay.
//
// The libraries resolve psprecomp symbols against the executable that loaded
// them, so a library only ever matches the build it was produced with. The ABI
// version below guards the metadata layout; nothing else is checked.

#include <cstdint>

namespace psprecomp {
class Runtime;
}

namespace mhfu {

inline constexpr std::uint32_t kOverlayAbiVersion = 1u;

// An overlay image starts with "MWo3" and a 64-byte header: id, load address,
// code size, data size, bss size, two end-of-image addresses and a 32-byte
// name. The header and the code after it are the part the game never writes to,
// so they are what identifies the image; its data section drifts as it runs.
inline constexpr std::uint32_t kOverlayHeaderBytes = 64u;

// Identification of the image the corpus was generated from, exactly as the
// host recomputes it from guest memory: slot base plus the FNV-1a hash of the
// first kOverlayHeaderBytes + code_size bytes.
struct OverlayModuleInfo {
    std::uint32_t abi_version;
    std::uint32_t base;
    std::uint32_t size;
    std::uint32_t code_size;
    std::uint64_t hash;
    const char *name;
};

// One corpus in a library: its image, the function that registers its code,
// and the addresses every call to goes through the runtime, ending in 0
// (config/overlay_dispatch.txt).
struct OverlayCorpusEntry {
    const OverlayModuleInfo *info;
    void (*install)(psprecomp::Runtime &runtime);
    const std::uint32_t *dispatched_calls;
};

// Every corpus in a library.
struct OverlayPack {
    std::uint32_t abi_version;
    std::uint32_t count;
    const OverlayCorpusEntry *const *entries;
};

} // namespace mhfu

// Only the library defines these; the host resolves them by name and uses the
// declarations for the types alone.
#if defined(MHFU_OVERLAY_MODULE)
#if defined(_WIN32)
#define MHFU_OVERLAY_EXPORT __declspec(dllexport)
#else
#define MHFU_OVERLAY_EXPORT __attribute__((visibility("default")))
#endif
#else
#define MHFU_OVERLAY_EXPORT
#endif

extern "C" {
MHFU_OVERLAY_EXPORT const mhfu::OverlayPack *mhfu_overlay_pack();

// A library built before packs exports one corpus through these three instead.
// The host still loads it, so an older overlay directory keeps working.
MHFU_OVERLAY_EXPORT const mhfu::OverlayModuleInfo *mhfu_overlay_info();
MHFU_OVERLAY_EXPORT void mhfu_register_overlay(psprecomp::Runtime &runtime);
// The addresses every call to goes through the runtime, ending in 0
// (config/overlay_dispatch.txt). Optional: a library built before it existed
// has none, and the host takes that as an empty list.
MHFU_OVERLAY_EXPORT const std::uint32_t *mhfu_overlay_dispatched_calls();
}
