# Sourced by the release scripts: checks a directory of overlay libraries
# against the build's bin/overlay_libraries.txt, which CMake writes with one
# line per corpus, "<library file> <corpus>".
#
#   check_overlay_libraries <manifest> <library directory> <profile directory> <suffix>
#
# The manifest must cover every corpus in the profile's overlays/, and the
# directory must hold exactly the libraries it lists, ending in <suffix>. Sets
# overlay_libraries and overlay_corpora to the counts. Prints the problem and
# returns 1 otherwise.
check_overlay_libraries() {
    local manifest="$1" directory="$2" profile="$3" suffix="$4"
    local corpora library found missing=()
    if [[ ! -f "$manifest" ]]; then
        echo "error: $manifest is missing: configure and build the overlay libraries (target mhfu_overlays)" >&2
        return 1
    fi
    corpora="$(find "$profile/overlays" -mindepth 2 -maxdepth 2 -name meta.txt 2> /dev/null | wc -l | tr -d ' ')"
    overlay_corpora="$(wc -l < "$manifest" | tr -d ' ')"
    if [[ "$corpora" -eq 0 ]]; then
        echo "error: no overlay corpora in $profile/overlays" >&2
        return 1
    fi
    if [[ "$overlay_corpora" -ne "$corpora" ]]; then
        echo "error: $manifest lists $overlay_corpora corpora, but $profile/overlays has $corpora:" \
            "build the overlay libraries again (target mhfu_overlays)" >&2
        return 1
    fi
    overlay_libraries=0
    while read -r library; do
        overlay_libraries=$((overlay_libraries + 1))
        [[ -f "$directory/$library" ]] || missing+=("$library")
    done < <(cut -d ' ' -f 1 "$manifest" | sort -u)
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "error: ${missing[*]} missing from $directory: build the overlay libraries (target mhfu_overlays)" >&2
        return 1
    fi
    found="$(find "$directory" -maxdepth 1 -name "*$suffix" | wc -l | tr -d ' ')"
    if [[ "$found" -ne "$overlay_libraries" ]]; then
        echo "error: $found libraries in $directory, but the build makes $overlay_libraries" >&2
        return 1
    fi
}
