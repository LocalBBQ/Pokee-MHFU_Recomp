# Releasing

A release gives players the program ready to run: the executable with the recompiled game code built in and the overlay libraries, which hold the 290 recompiled overlays, one library per memory slot. Like the repository, it does not include any game assets or original game files: no disc image, no copy of the game's executable or data, and no textures, models, audio or video from the game. On first start it asks for the files from the player's own legally obtained copy, the disc image, checks it, accepts only the original release, and prepares the game from it (see [Installer](../profiles/mhfu/README.md#installer)).

Releases are built by maintainers, not by CI: the recompiled code is generated from the game's executable, so a build needs a copy of the game. The game data stays on the maintainer's machine. Every artifact is checked for it before it is published.

## Stable releases and test builds

There are two kinds of release, and players must be able to tell them apart at a glance.

| | Stable | Test build |
| --- | --- | --- |
| Tag | `vX.Y.Z`, e.g. `v0.7.0`, `v0.7.1` | `vX.Y.Z-test.N`, e.g. `v0.7.0-test.1` |
| GitHub | a normal release, marked **Latest** | always a **pre-release**, never Latest |
| Title | `Pokee vX.Y.Z` | `(test) Pokee vX.Y.Z-test.N` |
| Notes | what changed | start with the notice below, then what changed |
| Platforms | Windows and Linux | any subset, said in the notes |

Every test build's notes start with this notice, with the link pointing at the latest stable release:

```markdown
> ⚠️ **This is a test build, not a stable release.** The latest stable release is [Pokee vX.Y.Z](…). If you just want to play, stay on (or go back to) that one. Install this build only if you want to help test.
```

A test build becomes stable without being built again: when it has passed the checks below, the same commit is tagged `vX.Y.Z` and published with the same files (renamed to the stable version where the file name carries it, and re-checked against `SHA256SUMS`). So what was tested is exactly what players get.

A version is released as stable only when:

- [TESTING.md](TESTING.md) passes on Windows and Linux (the Steam Deck included), recorded in [COMPATIBILITY.md](COMPATIBILITY.md) with the commit;
- no issue reports a regression against the previous stable release (performance included) that is still open.

The pre-releases published before this policy (`v0.6.0-alpha.5`, shown as "v0.6.5", and `v0.6.6`) are test builds under the old naming; they are titled "(unstable)" and point players back to `v0.6.0-alpha.4`, the last stable release.

## Branches

All development happens on `main`, through pull requests merged by rebase.

- **Release branch.** When a version is being stabilised, cut `release/X.Y` from `main` and build its test builds from it. Only fixes go there: every fix is merged into `main` first and then brought to `release/X.Y` with `git cherry-pick`, so `main` always has everything and the release branch gets no new features.
- **No merging back.** A release branch is never merged into `main` (it has nothing `main` lacks). It stays for the patch releases of that version (`vX.Y.1`, …).
- **Tags** go on the release branch's commits. New features keep landing on `main` meanwhile and go out in the next `release/X.Y`.

Releases are for [Windows](#windows) and Linux. macOS and Android are not released for now. The [macOS](#macos) section describes how its build is packed, for when it is. Android has been tested on the emulator only ([#127](https://github.com/TeamGDB/Yakumo/issues/127)). The [Android](#android) section below describes how its build is packed, for when it is; a stable release that includes it also needs [TESTING.md](TESTING.md) to pass on a real Android phone.

## Linux

`profiles/mhfu/scripts/release_linux.sh` builds both Linux artifacts from a checkout:

| Artifact | Contents |
| --- | --- |
| `pokee-<version>-linux-x86_64.flatpak` | Flatpak bundle, app ID `io.github.teamgdb.Pokee`, on the `org.freedesktop.Platform` 25.08 runtime. The main download, and the one for the Steam Deck. |
| `pokee-<version>-linux-x86_64.tar.gz` | Portable tarball: the `pokee` launcher, `Pokee`, `overlays/`, `lib/` (SDL3, FFmpeg), `fonts/`, `licenses/` |
| `ffmpeg-<version>.tar.xz` | The unmodified source of the FFmpeg both artifacts contain, published next to them as the LGPL asks |
| `SHA256SUMS` | Checksums of the three files above |
| `BUILDINFO.txt` | Commit, build environment, glibc requirement and the FFmpeg configure line, for the release notes |

Flathub cannot host the Flatpak: its builders compile everything from source, and this build needs the game's executable.

### What the script does

1. Checks the game data a source build uses: `profiles/mhfu/game/EBOOT.ELF` (the SHA-256 in the profile README) and `profiles/mhfu/game/disc.iso`. `prepare_game.sh` sets both up; `Pokee --install` produces the executable from the image.
2. Starts the Steam Runtime 3 "sniper" SDK container (Debian 11, glibc 2.31), pinned by digest in `packaging/linux/sources.sh`, and runs `packaging/linux/build_in_sdk.sh` in it, which
   - builds SDL3 from a source archive pinned by version and SHA-256 in `sources.sh`;
   - configures Pokee with `-DMHFU_RELEASE=ON`, `-DMHFU_FFMPEG=bundled` and GCC 14. The bundled FFmpeg is built by `cmake/FFmpeg.cmake` with only `libavcodec` and `libavutil` and the ATRAC3, ATRAC3plus and H.264 decoders, and configuring stops unless FFmpeg reports the LGPL with no GPL or non-free parts. The build then generates the recompiled code, builds `Pokee` and runs the save-data self-tests;
   - builds all overlay libraries with `build_overlays.sh`;
   - stages the program with the libraries it needs, the fallback Japanese font and the license texts, strips it, and checks that every library resolves, that no binary needs `libstdc++.so`, and which glibc version it needs.
3. Packs the tarball from the staged tree, with the launcher, in a reproducible order and with fixed timestamps.
4. Builds the Flatpak with `flatpak-builder` (the `org.flatpak.Builder` Flatpak) from `packaging/linux/io.github.teamgdb.Pokee.yml`, which packs the same staged tree, and exports it as a single-file bundle.
5. Unpacks both artifacts again and refuses them if any file is named like game data (`EBOOT*`, `*.iso`, `*.cso`, `*.bin`, `*.prx`, `*.pbp`, `*.elf`, `DATA.BIN`, `PARAM.SFO`, `SAVEDATA`, `ms0`, …) or looks like it by content: a MIPS ELF, a PBP, an encrypted PSP module, a `PARAM.SFO` or an ISO 9660 image.
6. Prints the checksums.

### Requirements

- An x86-64 Linux machine, a Steam Deck in Desktop Mode included, with `podman` (or `docker`), `flatpak` with the Flathub remote, `curl` and `ostree`. The script installs `org.flatpak.Builder` and the Freedesktop 25.08 SDK for the user.
- The game data, as above.
- About 25 GB of free space. The first build takes one to two hours; later ones reuse the compiler cache, the built dependencies and the finished overlay libraries.

### Build

```bash
profiles/mhfu/scripts/release_linux.sh --version 0.2.0
```

Without `--version`, the artifacts are named after `git describe`. `--jobs N` sets the number of parallel compiles (default 4; each generated unit needs over a gigabyte of memory). `--skip-build` packs the staged build of a previous run again, `--no-flatpak` and `--no-tarball` leave one artifact out. Everything is kept in `out/release-linux/`, the artifacts in `out/release-linux/dist/`. Build from a clean checkout of the commit you release: the version shown in Pokee's menu comes from `git describe` of the checkout.

On a Steam Deck, keep it plugged in and stop it from sleeping during the build, which runs the CPU flat out for hours: a Deck that suspends or runs flat in the middle leaves the build to be resumed, and a hard power-off can damage the half-written files, so after one delete `out/release-linux/build` and let the compiler cache catch up. Over SSH, start the build as a user service so that it also survives the connection closing, and use three jobs to keep the temperature down:

```bash
systemd-run --user --unit=pokee-release --collect \
    systemd-inhibit --what=sleep:idle --why="Pokee release build" \
    "$PWD/profiles/mhfu/scripts/release_linux.sh" --jobs 3
journalctl --user -fu pokee-release     # follow it
```

### Check

On a machine with the game, before publishing.

**Never touch a player's own installation or data.** `~/.var/app/io.github.teamgdb.Pokee` is shared by every installation of the Flatpak, and `~/.local/share/Pokee` by every tarball: they hold the player's settings, copied disc image and saves. So on a machine where someone plays:

- never uninstall, reinstall or replace an installed Pokee, and never use `--delete-data`;
- never delete or overwrite anything in those directories, or in Steam's files;
- run every check with its data in a throwaway directory of your own, and clean up only what you created there.

```bash
check=$(mktemp -d)

# The tarball: set up from the disc image alone into a temporary data
# directory, then boot without a window.
tar -xzf out/release-linux/dist/pokee-*-linux-x86_64.tar.gz -C "$check"
export MHFU_DATA_DIR="$check/data"
"$check"/pokee-*/pokee --install /path/to/your.iso
MHFU_NO_RENDER=1 MHFU_NO_AUDIO=1 timeout 120 "$check"/pokee-*/pokee
unset MHFU_DATA_DIR

# The Flatpak, without installing it: run the committed tree from the build's
# repository with the same runtime, pointing its data at the temporary
# directory. This leaves any installed Pokee and its data alone.
ostree --repo=out/release-linux/flatpak/repo checkout --user-mode \
    app/io.github.teamgdb.Pokee/x86_64/stable "$check/app"
flatpak run --command=bash --filesystem="$check" --filesystem=/path/to/iso-folder:ro \
    --env=MHFU_DATA_DIR="$check/flatpak-data" --env=MHFU_NO_RENDER=1 --env=MHFU_NO_AUDIO=1 \
    org.freedesktop.Platform//25.08 -c \
    "$check/app/files/lib/pokee/pokee --install /path/to/your.iso &&
     timeout 120 $check/app/files/lib/pokee/pokee"

rm -rf "$check"
```

This checks the Flatpak's files on its runtime, not its sandbox permissions. Check those on a machine where Pokee is not installed, with `flatpak install --user` of the bundle and `flatpak run` using `--env=MHFU_DATA_DIR=` pointing at a temporary directory. Uninstall it afterwards without `--delete-data`, and delete only that temporary directory.

The boot should print the executable's SHA-256, `Functions:` with a non-zero count, and overlays being installed as the game loads them; the run then ends at the timeout. Then play the release once with a window, a gamepad, a save and a reload, following [`TESTING.md`](TESTING.md), on a Steam Deck in Game Mode as well.

### Publish

Create the release on GitHub with the tag of the commit that was built, and attach every file from `out/release-linux/dist/` except `BUILDINFO.txt`, whose contents go into the release notes. The FFmpeg source archive must stay attached to every release whose artifacts contain that FFmpeg: that is the source offer in [`THIRD_PARTY_NOTICES.md`](../profiles/mhfu/packaging/THIRD_PARTY_NOTICES.md).

### Updating a bundled component

Change its version and SHA-256 where it is pinned, `packaging/linux/sources.sh` for SDL3 and the font or `cmake/FFmpeg.cmake` for FFmpeg, and the matching entry in `THIRD_PARTY_NOTICES.md`; the script refuses to pack when they disagree. A new FFmpeg configure option goes into both as well. To move to a newer SDK, update its digest in `sources.sh`; to move to a newer Flatpak runtime, update `FLATPAK_RUNTIME_VERSION` and `runtime-version` in the manifest together.

## macOS

`profiles/mhfu/scripts/release_macos.sh` packages a finished build directory into the macOS artifacts, for Apple Silicon only:

| Artifact | Contents |
| --- | --- |
| `pokee-<version>-macos-arm64.dmg` | Disk image with `Pokee.app`, a link to Applications and `Read Me.txt` (the first start, Gatekeeper, where the data lives). The main download. |
| `pokee-<version>-macos-arm64.zip` | Only with `--zip`: the same app and note as a zip archive |
| `ffmpeg-<version>.tar.xz` | The unmodified source of the FFmpeg the app contains, as for Linux |
| `SHA256SUMS` | Checksums of the files above |
| `BUILDINFO.txt` | Commit, build environment, minimum macOS and the bundled versions, for the release notes |

`Pokee.app` holds:

```text
Contents/
    Info.plist                 io.github.teamgdb.pokee, version from git describe, LSMinimumSystemVersion
    MacOS/Pokee               the executable
    Frameworks/                libSDL3.0.dylib, libvulkan.1.dylib, libMoltenVK.dylib,
                               libavcodec.61.dylib, libavutil.59.dylib
    Frameworks/overlays/       the overlay libraries
    Resources/Pokee.icns      the icon, from docs/images/emblem.svg (packaging/macos/make_icon.sh)
    Resources/fonts/           Noto Sans CJK JP, the fallback Japanese font
    Resources/vulkan/icd.d/    MoltenVK's driver manifest, found by the bundled Vulkan loader
    Resources/licenses/        LICENSE, THIRD_PARTY_NOTICES.md and every bundled license
```

Inside an app bundle the host looks for the overlays in `Contents/Frameworks/overlays` and for fonts in `Contents/Resources/fonts` (`host/app_paths.cpp`); everywhere else they stay next to the executable.

The app is signed **ad hoc**: no Apple Developer ID, no notarization. Gatekeeper therefore rejects the downloaded app (`spctl --assess` says `rejected`) until the player allows it once in System Settings → Privacy & Security → *Open Anyway*, or removes the quarantine attribute; `packaging/macos/README.txt` and [`MACOS.md`](MACOS.md) walk players through it. It is signed without the hardened runtime, which would refuse to load the separately signed overlay libraries.

### What the script does

1. Checks the build directory: an arm64 `Pokee` built with `-DMHFU_RELEASE=ON` (a developer build, which names its checkout's game directory, is refused), every overlay library that `bin/overlay_libraries.txt` lists in `bin/overlays` and nothing else, the list covering every corpus in `profiles/mhfu/overlays`, and the bundled LGPL FFmpeg in `bin/lib` configured as `cmake/FFmpeg.cmake` pins it. Nothing in the build directory is rebuilt or changed.
2. Fetches the pinned sources in `packaging/macos/sources.sh` (which takes SDL3 and the font from `packaging/linux/sources.sh`) and builds SDL3 and the Vulkan loader for arm64 and the deployment target, macOS 13. MoltenVK is the Khronos release build, thinned to arm64.
3. Assembles `Pokee.app`, points every library reference at `@rpath` with the single rpath `@executable_path/../Frameworks`, and strips local symbols (the symbols the executable exports to the overlays stay).
4. Checks that every symbol the binaries import from the C and C++ runtime exists in the macOS 13 SDK, then marks the binaries that were built for a newer macOS (the build machine's) for macOS 13 with `vtool`. This is what lets a build made on the newest macOS run on older ones without recompiling the game code.
5. Signs every library, then the app, ad hoc, and checks the signature (`codesign --verify --deep --strict`), that no library reference or rpath leads outside the bundle, that no binary needs a newer macOS, and that no file in the app names the home directory, the user name, the checkout or the build directory.
6. Packs the disk image (APFS, LZMA-compressed) and, with `--zip`, the zip, opens them again, checks their signatures and refuses them if any file looks like game data, as the Linux script does.
7. Prints the checksums.

### Requirements

- A Mac with Apple Silicon, Xcode or the Command Line Tools, CMake, Ninja and curl.
- The macOS 13 SDK for the import check: the Command Line Tools keep older SDKs in `/Library/Developer/CommandLineTools/SDKs` (for example `MacOSX13.1.sdk`); `POKEE_CHECK_SDK=/path/to/MacOSX13.x.sdk` points at another one.
- A finished release build. From a checkout with the game data, as in the [build instructions](../profiles/mhfu/README.md):

  ```bash
  cmake -S . -B out/mhfu -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu \
      -DMHFU_RELEASE=ON -DMHFU_FFMPEG=bundled
  profiles/mhfu/scripts/generate.sh out/mhfu
  cmake --build out/mhfu -j 4 --target Pokee
  profiles/mhfu/scripts/build_overlays.sh out/mhfu 4
  ```

  `MHFU_RELEASE` changes only the executable, not the recompiled code or the overlays. A developer build directory can therefore be switched to it, `Pokee` rebuilt and copied away, and the directory switched back, which relinks only the executable:

  ```bash
  cmake -S . -B out/mhfu -DMHFU_RELEASE=ON && cmake --build out/mhfu -j 2 --target Pokee
  cp out/mhfu/bin/Pokee out/release-macos/Pokee
  cmake -S . -B out/mhfu -DMHFU_RELEASE=OFF && cmake --build out/mhfu -j 2 --target Pokee
  ```

  Build only the `Pokee` target there: the default target would relink every overlay library against the new executable.

### Build

```bash
profiles/mhfu/scripts/release_macos.sh out/mhfu                                   # a release build directory
profiles/mhfu/scripts/release_macos.sh --executable out/release-macos/Pokee out/mhfu   # a switched-back one
```

Without `--version`, the artifacts are named after `git describe`; commit first, since the version shown in Pokee's menu comes from `git describe` when `Pokee` was built. `--zip` adds the zip archive, `--no-dmg` leaves the disk image out, `--jobs N` sets the parallel compiles for SDL3 and the loader. Everything is kept in `out/release-macos/`, the artifacts in `out/release-macos/dist/`. The first run takes a few minutes for SDL3 and the loader; later runs reuse them.

### Check

The same rule as on Linux: **never touch a player's own data.** `~/Library/Application Support/Pokee/` holds the settings, the copied disc image and the saves of every Pokee on the Mac, the developer build included. Check the release with its data in a throwaway directory, from a copy of the app outside the checkout, with no `MHFU_*` or `DYLD_*` variable pointing at the build tree, and with no other Pokee running:

```bash
check=$(mktemp -d)
hdiutil attach -readonly -nobrowse -mountpoint "$check/dmg" out/release-macos/dist/pokee-*-macos-arm64.dmg
ditto "$check/dmg/Pokee.app" "$check/Pokee.app"
hdiutil detach "$check/dmg"

# Everything the process loads comes from the bundle or the system.
export MHFU_DATA_DIR="$check/data"
"$check/Pokee.app/Contents/MacOS/Pokee" --install /path/to/your.iso --in-place
DYLD_PRINT_LIBRARIES=1 VK_LOADER_DEBUG=driver "$check/Pokee.app/Contents/MacOS/Pokee" 2>&1 | tee "$check/run.log"
grep -E '/opt/homebrew|/usr/local|out/mhfu' "$check/run.log"      # should print nothing
unset MHFU_DATA_DIR

# What Gatekeeper does with a download: expect "rejected" for an ad hoc signature.
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$check/Pokee.app"
spctl --assess --type execute -vv "$check/Pokee.app"

rm -rf "$check"
```

`--install` without `--in-place` copies the 1.3 GB image into the throwaway directory instead. The window run should start the game, print `Overlay corpora: 290 from …/Pokee.app/Contents/Frameworks/overlays (4 libraries)`, and play the intro movie with sound. Then play the release once with a gamepad, a save and a reload, following [`TESTING.md`](TESTING.md), and open it once through Finder from a quarantined copy to see the Gatekeeper steps players will see.

### Publish

As for Linux: attach every file from `out/release-macos/dist/` except `BUILDINFO.txt` to the release of the tag that was built, and keep the FFmpeg source archive next to them. When Linux and macOS artifacts share a release, merge the two `SHA256SUMS` files.

### Updating a bundled component

SDL3 and the font are pinned in `packaging/linux/sources.sh` for both systems; the Vulkan loader, MoltenVK and the deployment target in `packaging/macos/sources.sh`. Update the matching entry in `THIRD_PARTY_NOTICES.md` as well; the script refuses to pack when they disagree. Raising the deployment target needs that release's SDK for the import check.

## Windows

`profiles/mhfu/scripts/release_windows.sh` packages a finished Windows build into two zips:

| Artifact | Contents |
| --- | --- |
| `pokee-<version>-windows-x86_64.zip` | The folder `pokee-<version>-windows-x86_64` with `Pokee.exe`, `overlays/` (the four overlay DLLs), `SDL3.dll`, the FFmpeg DLLs (`avcodec`, `avutil`, `swresample`), the Visual C++ runtime (`msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`), `licenses/`, `README.txt` and `BUILDINFO.txt`. It keeps its data in `%APPDATA%\Pokee\MHFU` |
| `pokee-<version>-windows-x86_64-portable.zip` | The same in a folder named `…-portable`, plus `portable.txt`, which keeps all data in `data\` next to `Pokee.exe` (see [Portable copy](../profiles/mhfu/README.md#portable-copy)) ([#152](https://github.com/TeamGDB/Yakumo/issues/152)) |
| `BUILDINFO-windows.txt` | Commit, compiler, SDL3 and FFmpeg versions and the overlay DLL count, for the release notes |

The FFmpeg DLLs are BtbN's prebuilt LGPL shared build, which the build downloads and checks against its pinned SHA-256 (see [FFmpeg](BUILDING.md#ffmpeg)); `licenses/FFmpeg-SOURCE.txt` names the build and its source.

### What the script does

1. Checks the build directory: configured with `-DMHFU_RELEASE=ON`, `bin/Pokee.exe` built, the overlay DLLs that `bin/overlay_libraries.txt` lists and no others, the list covering every corpus in `profiles/mhfu/overlays`, and the bundled FFmpeg next to the executable. Nothing in the build directory is rebuilt or changed.
2. Stages the program with `SDL3.dll` from the SDL3 the build found (`SDL3_DIR` in its CMake cache; the official `SDL3-devel-*-VC.zip` layout), the Visual C++ runtime from the toolchain's redistributable folder (`$VCToolsRedistDir`) and the license texts.
3. Checks with `dumpbin /dependents` that every DLL the executable and the overlays import is in the package or in `System32`. The Vulkan loader, `vulkan-1.dll`, comes with the graphics driver.
4. Packs each zip with Windows' own `tar.exe`, unpacks it again, and refuses it if any file is named like game data (`EBOOT*`, `*.iso`, `*.cso`, `*.bin`, `*.prx`, `*.pbp`, `*.elf`, `*.ovl`, `DATA.BIN`, `PARAM.SFO`, `ms0`, `SAVEDATA`) or looks like it by content (an ELF, a PBP, an encrypted PSP module, a `PARAM.SFO` or an ISO 9660 image), or if any file contains the checkout's path, the user profile's path, the user name or the computer name.
5. Prints the checksums.

### Requirements

The Windows build tools of [BUILDING.md](BUILDING.md#windows): the MSVC toolchain, CMake, Ninja, Python, Git for Windows, the Vulkan SDK and SDL3. Run the script in Git Bash started from an **x64 Native Tools Command Prompt**, which puts `dumpbin`, `cl` and `VCToolsRedistDir` in the environment.

### Build

Build a release configuration in a directory of its own, with the generated code and the overlay corpora in the checkout (copy them from a developer checkout as [BUILDING.md](BUILDING.md#several-checkouts) describes, or run `generate.sh` and `build_overlays.sh`). The compiler cache makes this quick when a developer build of the same sources exists:

```bash
cmake -S . -B out/release-windows/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu \
    -DMHFU_RELEASE=ON -DCMAKE_PREFIX_PATH="C:/path/to/SDL3"
cmake --build out/release-windows/build -j 4
profiles/mhfu/scripts/release_windows.sh --version 0.7.0 out/release-windows/build
```

`cmake --build` without a target builds the overlay DLLs as well. Without `--version`, the artifacts are named after `git describe`. `--no-portable` and `--no-installed` leave one zip out. The staged tree is kept in `out/release-windows/stage/`, the artifacts in `out/release-windows/dist/`.

### Check

On a machine with the game, before publishing. Never touch a player's own `%APPDATA%\Pokee\MHFU`: check each zip in a folder of your own.

```bat
rem The normal zip, with its data in a folder of your own
tar -xf pokee-<version>-windows-x86_64.zip -C C:\check
set MHFU_DATA_DIR=C:\check\data
C:\check\pokee-<version>-windows-x86_64\Pokee.exe --install C:\path\to\your.iso --in-place
set MHFU_DATA_DIR=

rem The portable zip: the first line must say "(portable)" and name data\ next to the executable
tar -xf pokee-<version>-windows-x86_64-portable.zip -C C:\check
C:\check\pokee-<version>-windows-x86_64-portable\Pokee.exe --install C:\path\to\your.iso
```

Then start each one with `MHFU_NO_AUDIO=1` and a time limit, play past the title screen, save once, and check that the portable copy wrote only into its `data` folder. On a machine where Pokee is set up already, the portable copy's first start offers to copy that data: choose *Start empty* unless the copy is what you are testing, and delete only the folders you created.

### Publish

Attach both zips to the GitHub release, and put `BUILDINFO-windows.txt` into the release notes.

## Android

`profiles/mhfu/scripts/release_android.sh <build-dir>` packs `pokee-<version>-android-arm64.apk` for 64-bit phones and handhelds with Android 10 or later and Vulkan 1.1, and a `SHA256SUMS` next to it, in `out/release-android/dist/`. There is no Play Store build (no AAB): players install the APK themselves, and on its first start the app asks for their `.iso` through Android's file picker and copies it into its own storage (about 1.3 GB, besides the app's 0.8 GB).

### Build directory

The script packs an Android build directory whose overlay libraries are already built, and rebuilds only `libmain.so`, the host. Such a directory takes the Android SDK with the NDK (r28c is the one tested), the generated corpus and the overlay corpora in the checkout, and a few hours at `-j2`:

```sh
cmake -S . -B out/android-app -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
  -DPSPRECOMP_PROFILE=mhfu -DMHFU_ANDROID_APP=ON \
  -DSDL3_DIR=<SDL3 built for Android>/lib/cmake/SDL3 -DCMAKE_FIND_ROOT_PATH=<SDL3 built for Android>
cmake --build out/android-app -j 2
```

`android-29` is the oldest system the APK installs on (Android 10); the script configures the directory for it every time and builds SDL3 for it, since code compiled for a newer platform may import functions Android 10 lacks. A directory configured for a newer platform from before keeps its overlay libraries: they call only the host and plain libc, and after packing the script checks that nothing any packed library imports is missing from Android 10's system libraries. (Ninja then counts those overlays as out of date, so a plain `cmake --build` of that directory would compile them again.)

The first configure needs an SDL3 built for Android to find; the release script builds the pinned one (`packaging/linux/sources.sh`) into `out/release-android/deps` and points the directory at it. The overlay libraries are reused as they are as long as `include/psprecomp/` has not changed since they were built; the script refuses them otherwise, and when any is missing.

### What the script does

1. Fetches the pinned SDL3 and the fallback font, checks their SHA-256 and builds SDL3 for Android 10 once.
2. Reconfigures the build directory as a release (`MHFU_RELEASE`, no checkout paths, `android-29`) against that SDL3 and rebuilds `libmain.so` if that changed it.
3. Checks that every overlay library `bin/overlay_libraries.txt` lists is there, and newer than the framework headers.
4. Packs the APK with `packaging/android/build_apk.sh`: the native libraries, SDL's Java activity and the app's own, the font, and under `assets/licenses` the third-party notices, SDL's licence, FFmpeg's LGPL with where its source is, and the font's licence. The version name is `git describe`; the version code is the number of commits, so a later release installs as an update.
5. Signs it with the release key, verifies the signature, refuses it if it holds any game file or if a library imports a function Android 10 lacks, and prints its size and SHA-256.

### The release key

Android installs an update only over an app signed with the same key. The key is a keystore outside the repository; the script reads it from `KEYSTORE`, `KEYSTORE_PASS` and `KEY_ALIAS`, or from a file that sets them, `KEY_ENV` (by default `keys/key.env` in the checkout, which `.gitignore` keeps out of Git).

**Back up the keystore and its password, and keep them private.** Losing them means no later release can install over the earlier ones: players would have to uninstall first, and uninstalling an Android app deletes its data, their saves included (unless they exported them). Anyone who has the key can publish an update players' phones will accept.

To move to a new key without stranding players, sign releases with both for a while using APK signature scheme v3 key rotation (`apksigner rotate` to make a lineage from the old key to the new one, then `apksigner sign --lineage`), and keep the old key backed up as well. A plain switch to a new key forces every player to uninstall.

### Check

`apksigner verify` and the game-data check run in the script. Install the APK on a device or the emulator (`adb install -r` keeps an earlier installation's data when the key matches), set up from an `.iso`, and play through [`TESTING.md`](TESTING.md) with the touch controls and with a gamepad.

### Publish

Attach the APK and `SHA256SUMS` to the GitHub release, and the FFmpeg source archive as for Linux: the APK carries the same LGPL FFmpeg.
