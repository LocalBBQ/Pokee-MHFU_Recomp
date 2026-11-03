#!/usr/bin/env bash
# Analyze the decrypted executable and regenerate the AOT corpus.
#
#   generate.sh [build_dir]
#
# Requires a framework build (psp_analyze/psp_recomp) in build_dir, default
# out/mhfu. Output: profiles/mhfu/analysis and profiles/mhfu/generated.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${1:-$repo_dir/out/mhfu}"
elf="$profile_dir/game/EBOOT.ELF"

if [[ ! -f "$elf" ]]; then
    echo "missing $elf; run scripts/prepare_game.sh first" >&2
    exit 1
fi

cmake --build "$build_dir" --target psp_analyze psp_recomp
mkdir -p "$profile_dir/analysis"
"$build_dir/psp_analyze" "$elf" "$profile_dir/analysis/report.json"
# Regenerate in place: psp_recomp rewrites only units whose text changed and
# removes units that no longer exist, so unchanged units keep their timestamps
# and are not recompiled. The site hooks are the host's (config/site_hooks.txt).
"$build_dir/psp_recomp" "$elf" --auto "$profile_dir/generated" \
    --site-hooks "$profile_dir/config/site_hooks.txt" --site-image eboot
if command -v sha256sum > /dev/null; then
    elf_sha256="$(sha256sum "$elf" | cut -d' ' -f1)"
else
    elf_sha256="$(shasum -a 256 "$elf" | cut -d' ' -f1)"
fi
cat > "$profile_dir/generated/corpus_identity.hpp" << EOF
#pragma once

// Written by profiles/mhfu/scripts/generate.sh. Launch registers this corpus
// only for an executable with this SHA-256.
namespace mhfu {

inline constexpr const char *kCorpusExecutableSha256 = "${elf_sha256}";

} // namespace mhfu
EOF
echo "corpus identity ${elf_sha256}"
