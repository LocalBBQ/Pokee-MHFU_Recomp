#!/usr/bin/env bash
# Extract every code overlay from DATA.BIN, recompile it and build the overlay libraries.
#
#   build_overlays.sh [build_dir] [jobs]
#
# The game loads its overlays into shared slots at run time, so each one needs
# its own corpus. This builds all of them up front instead of waiting for the
# game to reach them. First every overlay without a corpus is recompiled to
# C++, then the libraries (one per slot, or one per overlay with
# -DMHFU_OVERLAY_PACKS=OFF) are built in one Ninja run with `jobs` parallel
# jobs (default 2), so one scheduler and one limit govern the whole build.
# Recompiled overlays are not recompiled again, and Ninja skips what is already
# built, so an interrupted run picks up where it stopped. Expect roughly 40
# minutes for the whole set.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${1:-$repo_dir/out/mhfu}"
jobs="${2:-2}"
iso="$profile_dir/game/disc.iso"
extract_dir="$profile_dir/analysis/overlays"
library_dir="$build_dir/bin/overlays"

if [[ ! -e "$iso" ]]; then
    echo "error: $iso not found; run scripts/prepare_game.sh first" >&2
    exit 1
fi
if [[ ! -x "$build_dir/bin/Pokee" ]]; then
    echo "error: $build_dir/bin/Pokee not found; build the profile first" >&2
    exit 1
fi

mkdir -p "$extract_dir"
echo "extracting overlays from DATA.BIN into $extract_dir"
python3 "$profile_dir/tools/databin.py" "$iso" extract-overlays "$extract_dir" > /dev/null

images=("$extract_dir"/overlay_*.bin)
total=${#images[@]}
recompiled=0
failed=()

# Step 1: recompile every overlay that has no corpus yet. A corpus whose
# meta.txt exists is complete (add_overlay.py writes it last) and only needs
# building.
for image in "${images[@]}"; do
    stem="$(basename "$image" .bin)"   # overlay_<BASE>_<name>
    rest="${stem#overlay_}"
    base="${rest%%_*}"
    name="${rest#*_}"
    # A corpus generated with another list of dispatched calls
    # (config/overlay_dispatch.txt) is recompiled, and Ninja relinks its library.
    expected="$(python3 "$profile_dir/tools/add_overlay.py" --print-dispatch "0x$base" "$name")"
    for meta in "$profile_dir/overlays/ovl${base}_${name}_"*/meta.txt; do
        [[ -e "$meta" ]] || continue
        recorded="$(sed -n 's/^dispatch=//p' "$meta")"
        if [[ "$recorded" != "$expected" ]]; then
            echo "regenerating $name: its dispatched calls changed"
            rm -f "$meta"
        fi
    done
    if ! compgen -G "$profile_dir/overlays/ovl${base}_${name}_*/meta.txt" > /dev/null; then
        echo "[$((recompiled + ${#failed[@]} + 1))] recompiling $name at 0x$base"
        if ! python3 "$profile_dir/tools/add_overlay.py" --no-build "$build_dir" "$image" "0x$base" > /dev/null 2>&1; then
            failed+=("$name")
            continue
        fi
        recompiled=$((recompiled + 1))
    fi
done

# Step 2: build every library in one invocation. -k 0 keeps going past a
# failed overlay; the libraries that are still missing afterwards, which
# bin/overlay_libraries.txt lists with their corpora, are reported.
echo "building the overlay libraries with -j $jobs"
cmake --build "$build_dir" -j "$jobs" --target mhfu_overlays -- -k 0 || true
manifest="$build_dir/bin/overlay_libraries.txt"
libraries=0
corpora=0
if [[ -f "$manifest" ]]; then
    corpora="$(wc -l < "$manifest" | tr -d ' ')"
    while read -r library; do
        libraries=$((libraries + 1))
        if [[ ! -f "$library_dir/$library" ]]; then
            failed+=("$library ($(grep -c "^$library " "$manifest") corpora)")
        fi
    done < <(cut -d ' ' -f 1 "$manifest" | sort -u)
else
    failed+=("$manifest is missing")
fi

echo "$total overlays, $recompiled recompiled now; $corpora corpora in $libraries libraries; failed ${#failed[@]}"
if [[ ${#failed[@]} -gt 0 ]]; then
    printf '  failed: %s\n' "${failed[@]}"
    exit 1
fi
