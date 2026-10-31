# MHFU profile

This profile builds `Pokee` for **Monster Hunter Freedom Unite** (USA, `ULUS-10391`, disc version 1.01). It recompiles that PSP executable into a native program. No executable, game data or code generated from them is part of the repository — you supply your own copy of the game. The installer accepts this release only.

## Status

The game boots, loads its overlays, creates a character or loads a save, walks the village, plays hunts alone or in ad hoc multiplayer and saves, with sound, music, movies and lighting. It can be played with fully rebindable keyboard and mouse controls or a gamepad. The simulation remains at the PSP's 30 frames per second, with optional presentation at 45, 60, 90, 120 or the display's refresh rate through frame interpolation. Taking an item from the box in a quest does not wait for the icon to fly to the pouch.

| Area | State |
| --- | --- |
| Code | The whole executable (178 138 instructions, 67 units) and all 290 code overlays are recompiled ahead of time; an interpreter covers anything they miss |
| Kernel | Threads with a deterministic virtual clock, semaphores, event flags, mutexes, callbacks, VTimers, partition memory, VBlank interrupts, file I/O straight from the disc image |
| Imports | 244 of 296 implemented; the rest are logging stubs that return 0 |
| Graphics | Vulkan: textures (palettes, DXT, swizzle), skinning, per-vertex lighting (four directional, point or spot lights and the full material model) and fog, blending, depth and alpha test, sprites, per-framebuffer render targets, arbitrary window shapes and frame interpolation; PPSSPP-compatible HD texture packs are supported |
| Audio | `sceSasCore` voice mixing, `sceAudio` output and ATRAC3 music through `sceAtrac3plus` |
| Movies | PSMF playback through `sceMpeg` and `sceJpegCsc`: H.264 video and ATRAC3plus sound |
| Input | Control presets (Default, Modern, Left-handed, Classic keyboard, and the player's own) with every keyboard, mouse and gamepad control rebindable to any chord of up to four inputs, such as LB + ○ or △ + ○, and actions that press any PSP buttons together, such as Item left (L + □); SDL3 gamepads with an analog camera and proportional bow and bowgun aim on the second stick |
| Text | `sceLibFont` glyphs rasterized from a host TrueType font |
| Saves | The save-data utility, with saves in the PSP's own format: a save copied from a PSP loads, and one made here can be copied back; the menu imports, exports and backs up saves |
| Interface | Gamepad, keyboard and mouse driven first-run setup, file browser, in-game settings, performance statistics and on-screen keyboard |
| Multiplayer | Ad hoc play through PSP ad hoc servers: two instances have met in a gathering hall and started a quest together; play with PPSSPP and on public servers is still to be tested. See [Multiplayer](#multiplayer-ad-hoc) |

Not done yet:

- **Infrastructure networking** (`sceHttp`, `sceNetInet`): the game's download mode. Ad hoc multiplayer works.
- **Dialog screens.** The save-data and message dialogs work but draw nothing; each answers as if the player confirmed it ([#33](https://github.com/TeamGDB/Yakumo/issues/33)).

Tested on macOS (Apple Silicon, Vulkan through MoltenVK), on a Steam Deck in Game Mode, built with GCC in a Debian 13 container and running on native Vulkan, and on Windows 11 with MSVC; see [the compatibility table](../../docs/COMPATIBILITY.md).

## Supported executable

| Item | Value |
| --- | --- |
| Disc | USA `ULUS-10391`, version 1.01 |
| Module | `MonsterHunterPortable2ndG` 1.1 |
| Encrypted `SYSDIR/EBOOT.BIN` SHA-256 | `3354adb3ee40821f230c4055b9bf7cc5d4c864e42abb74ce8ac022933f9697a7` |
| Decrypted `EBOOT.ELF` SHA-256 | `3070954e2dc88fd0b0836c0c1dc526531aca4a213b0f18806e324963efb514f4` |
| Load image | `0x08804000`, 32 MiB guest RAM (user memory ends at `0x0A000000`) |
| Entry / `gp` | `0x08906A54` / `0x089D3BA0` |
| Imports | 272 |

The encrypted header uses tag `0xC0CB167C`. `generate.sh` translates the resident code (3095 functions, 67 C++ units) and stamps the executable SHA-256. Launch registers that corpus only when the prepared executable has the same hash. The ELF names overlay sections (`game_task.ovl`, `em01.ovl`, `stage000.ovl`, and the rest) but those sections have no file bytes, so they are not part of this corpus. The analog camera's follow driver, its bow and bowgun aim, lock-on, the projection, hiding the HUD and layered armor were read from this executable. The lock-on marker places the monster from the follow camera's eye and look-at. Armor names are read from the text block at `0x089CC560` (head, chest, arms, waist and legs in tables 7, 9, 11, 13 and 15). Portable 3rd, its PSP release (`ULJM-05800`), and other Freedom Unite regions are refused.

## Requirements

- CMake 3.20 or newer, Ninja and a C++20 compiler
- Optional: `ccache`, which the build uses automatically when it is installed
- Python 3
- SDL3, Vulkan (the loader and headers; MoltenVK on macOS) and `glslangValidator`
- `make` and a C compiler on macOS and Linux, to build FFmpeg (see below)

If SDL3, Vulkan or `glslangValidator` is missing, configuration still succeeds but builds the game **without a window**: CMake prints `mhfu: renderer disabled` and the program runs headless. Check for `mhfu: Vulkan renderer enabled` in the configure output.

The streamed music (ATRAC3) and the movies (H.264 with ATRAC3plus sound) are decoded by FFmpeg's shared `libavcodec` and `libavutil`. FFmpeg is part of the normal build; `MHFU_FFMPEG` chooses where it comes from (`cmake/FFmpeg.cmake` holds the pins):

- `bundled`, the default. The first configure of a build directory downloads FFmpeg 7.1.5, checks its SHA-256 and builds it with only the ATRAC3, ATRAC3plus and H.264 decoders, as LGPL-2.1-or-later shared libraries (configure stops if the result is not LGPL only). This takes a few minutes, once per build directory; a new version or configuration rebuilds it. The libraries and FFmpeg's licence go to `out/mhfu/bin/lib/`, which the executable finds through its rpath, so the game needs no FFmpeg on the system. On Windows, where FFmpeg's `configure` does not run with MSVC, it downloads a pinned, checksum-verified prebuilt LGPL shared FFmpeg 7.1.5 and puts its DLLs next to `Pokee.exe` (see [BUILDING.md](../../docs/BUILDING.md#ffmpeg)). Configuration reports `mhfu: bundled FFmpeg 7.1.5 …; music and movies enabled`. To build offline, put the archive in `out/mhfu/_deps/downloads/` (`MHFU_FFMPEG_DOWNLOAD_DIR`) first.
- `system` uses the FFmpeg that `pkg-config` finds, for example from `brew install ffmpeg` on macOS or `apt install libavcodec-dev libavutil-dev` on Debian and Ubuntu, and stops if there is none.
- `OFF` builds without FFmpeg. This is possible but not recommended: the game then has no music and skips its movies, and configure prints a warning saying so.

Expect a full build to need several gigabytes of memory and some time: the generated code is large. With Ninja, the build compiles at most `PSPRECOMP_GENERATED_JOBS` generated units at once, whatever `-j` you pass; the default is one per 4 GiB of memory, so 2 on an 8 GB machine. Set it when configuring, for example `-DPSPRECOMP_GENERATED_JOBS=1`.

## Quick start

```bash
# 1. Game data: link your disc image and decrypted executable into profiles/mhfu/game
#    (or let the program set itself up from the image; see "Game data" below)
profiles/mhfu/scripts/prepare_game.sh "/path/to/your.iso" /path/to/EBOOT.ELF

# 2. Recompile the executable
cmake -S . -B out/mhfu -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu
profiles/mhfu/scripts/generate.sh
cmake -S . -B out/mhfu                          # pick up the generated units
cmake --build out/mhfu --target Pokee -j 2

# 3. Recompile the code overlays (about 40 minutes, resumable)
profiles/mhfu/scripts/build_overlays.sh

# 4. Play
out/mhfu/bin/Pokee
```

Each step is described below. [`docs/BUILDING.md`](../../docs/BUILDING.md) covers the platforms, how long each stage takes, and working on the code without full rebuilds.

## Game data

The program needs two things from your own copy of the game: the disc image and the game's executable. It finds them in this order:

1. A directory given on the command line or in `MHFU_GAME_DIR`.
2. The **per-user data directory** that the installer fills, or a [portable copy](#portable-copy)'s own `data` folder.
3. `profiles/mhfu/game` in the checkout, set up by `prepare_game.sh`. A release build (`-DMHFU_RELEASE=ON`, see [Release builds](#release-builds)) has no checkout and skips this.

If neither the per-user directory nor `profiles/mhfu/game` holds game data, the program starts its installer instead of the game.

### Installer

The installer needs only your disc image. It runs as a few screens in the game's window, all of them usable with a gamepad alone, a keyboard or a mouse:

1. **Welcome**: what is needed and where the data goes.
2. **Choose the disc image** in Pokee's own file browser. It starts in your home folder (later in the folder where you last found an image) and lists folders and `.iso` files with their sizes; *Showing .iso only* switches to all files. The row of places above the list holds Home, Downloads, Desktop and Documents, and every removable drive: SD cards and USB drives under `/run/media` and `/media` on Linux (a Steam Deck's SD card among them), volumes under `/Volumes` on macOS, drive letters on Windows. Confirm opens a folder or picks a file; back goes up a folder, and from the top back to the welcome screen. On a gamepad, △ (Y) switches between .iso files and all files. A file dropped onto the window is taken as well, on this screen and on the welcome screen. *System dialog…* opens the system's file dialog instead; it is not offered under gamescope (Steam Deck Game Mode), where that dialog does not appear.
3. **Checks**: the image must be USA Freedom Unite `ULUS-10391` version 1.01 (the disc id in `PARAM.SFO` and the SHA-256 of the encrypted executable). A wrong release or region, including Portable 3rd, a modified image, a PlayStation 3 disc image, a compressed (`.cso`) image or a file that is no disc image at all each get a screen that says so plainly, with *Choose another file*. The accepted image is prepared and started on the 32 MiB layout. After `generate.sh` and a rebuild, launch registers that corpus when the executable hash matches the stamp.
4. **Copy or use in place**: copying (the default) puts the image (about 1.3 GB) into the per-user directory, so the game keeps working after the original is moved or deleted; the screen shows the free space and refuses the copy when there is not enough. Using the image where it is saves the space; the program then checks on every start that the image is still there and says so if it is not, offering to run the setup again.
5. **Progress**: a progress bar for the copy and for preparing the game's executable from the image, which is then checked against the hash in the table above. The work runs off the window's thread, so the window stays responsive; *Cancel* (or back) stops it and removes what it wrote.

The game then starts. Later starts go straight to the game. The in-game menu's *Set up game data again…* runs the same setup: the game closes and the program starts again with `--install`.

The per-user directory is SDL's preference path for `Pokee/MHFU`:

| System | Directory |
| --- | --- |
| macOS | `~/Library/Application Support/Pokee/MHFU/` |
| Linux | `~/.local/share/Pokee/MHFU/` (or under `$XDG_DATA_HOME`) |
| Windows | `%APPDATA%\Pokee\MHFU\` |

It holds `EBOOT.ELF`, `disc.iso` when the image was copied, `settings.ini`, which records where the image is and keeps the settings of the [in-game menu](#in-game-menu), `ms0`, the memory stick with the [saves](#saving-and-loading), `textures/ULUS10391` when you install an [HD texture pack](#hd-texture-packs), `pipeline_cache.bin`, the graphics pipelines compiled in earlier runs, and `pipeline_keys.bin`, the list of them the next run makes in the background from the start (deleting either only makes the next run compile them again). `MHFU_DATA_DIR` points the program at another directory. The Flatpak keeps this directory inside its own data directory, `~/.var/app/io.github.teamgdb.Pokee/data/Pokee/MHFU/`.

### Portable copy

A portable copy keeps everything in one folder next to `Pokee.exe` (or the Linux `Pokee`) instead of the per-user directory: the settings, the prepared game (`EBOOT.ELF`, and `disc.iso` when the image was copied), the saves in `ms0`, the pipeline caches, `logs`, `mods`, texture packs and save backups. The folder can then live on a USB drive, be copied to another computer or be deleted without leaving anything behind. The Windows release comes as a portable zip besides the normal one.

Portable mode is on when there is a file `portable.txt` or a folder `data` next to the executable, and the data then goes into that `data` folder. The first start without either uses the per-user directory as before. To choose for one run, `--portable` (or `MHFU_PORTABLE=1`) uses `data` next to the executable even without the marker, `MHFU_PORTABLE=0` ignores the marker, and `--data-dir DIR` (or `MHFU_DATA_DIR`) uses any folder. The first line the program prints names the folder and why: `[data] D:\Pokee\data (portable)`.

- **Moving it.** Everything the portable folder keeps is found relative to it, so the folder works from another path or another drive letter, with one exception: a disc image the setup was told to *use in place* is recorded by its full path. Choose *Copy* in the setup to take the image along.
- **A folder Pokee cannot write to**, such as a portable copy unpacked into `Program Files` or on a read-only drive, stops the program with a message that names the folder. It never falls back to another location on its own, so settings and saves cannot end up somewhere the player does not expect.
- **From an installed copy.** When a portable copy starts with an empty `data` folder on a computer where Pokee already has data in the per-user directory, it offers to copy that data into the portable folder, with a progress bar. The per-user directory is only read, never changed or moved. Files already in the portable folder are never replaced, and a cancelled or failed copy removes what it had copied. `Pokee --portable --copy-user-data` (or `--data-dir DIR --copy-user-data`) makes the same copy from a terminal and exits.
- **Back to the installed copy.** Remove `portable.txt` and rename or move the `data` folder. Its contents can be copied into the per-user directory by hand.

Any of these folders, and the folder the disc image is in, may have names outside ASCII, such as a Windows user name in Cyrillic or Japanese: the program keeps paths as Unicode throughout and writes them to `settings.ini` as UTF-8. On Windows the console shows them only if its font has the characters.

Saves made before `ms0` moved here stay where they were, in `profiles/mhfu/game/ms0`: a developer build keeps using them, and says so at start, until the per-user directory has an `ms0` of its own. Move the folder there to switch.

The same setup runs without any screens from a terminal, for scripts and headless machines:

```bash
out/mhfu/bin/Pokee --install "/path/to/your.iso"             # copy the image
out/mhfu/bin/Pokee --install "/path/to/your.iso" --in-place  # use it where it is
out/mhfu/bin/Pokee --install                                 # run the setup screens again, then play
```

`--install` with an image prepares everything and exits. A build without generated code can already run it, and the `EBOOT.ELF` it writes into the per-user directory is the executable `generate.sh` needs.

When the window cannot be created, for example without a working Vulkan driver, the installer falls back to SDL3 message boxes and the system file dialog (on Linux through the desktop portal or `zenity`). A build without SDL shows neither and prints the `--install` command instead.

### Checkout directory

For development, `profiles/mhfu/game` works as before, with an executable decrypted by an external tool:

1. Decrypt `PSP_GAME/SYSDIR/EBOOT.BIN` from your own image with an external tool, or take the `EBOOT.ELF` that `--install` wrote into the per-user directory. `tools/extract_iso.py <iso> <dir> /PSP_GAME/SYSDIR/EBOOT.BIN` extracts the encrypted file; the table above gives the hashes to check both files against.
2. Populate `profiles/mhfu/game` (ignored by Git):

   ```bash
   profiles/mhfu/scripts/prepare_game.sh "/path/to/your.iso" /path/to/EBOOT.ELF
   ```

The image itself is not unpacked: `game/disc.iso` links to it and the host reads files, and raw `sce_lbn` sectors, from it directly. `game/ms0` backs `ms0:` for save data. If you move the image later, rerun the script — the links point at absolute paths.

When the per-user directory also holds an installation, it takes precedence; start with `profiles/mhfu/game` as an argument, or set `MHFU_GAME_DIR`, to use the checkout.

## Build

```bash
cmake -S . -B out/mhfu -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu
profiles/mhfu/scripts/generate.sh          # writes analysis/ and generated/
cmake -S . -B out/mhfu                     # pick up the generated units
cmake --build out/mhfu --target Pokee -j 2
```

`generate.sh` analyzes the executable and writes the recompiled C++ into `generated/`. That corpus is derived from your copy of the game, so it stays local and is never committed.

Rerunning `generate.sh` rewrites only the units whose code changed, so the build after it recompiles only those.

The build protects its incremental state:

- **One build at a time.** With Ninja, CMake runs builds through a wrapper (`ninja-locked` on macOS and Linux, `ninja-locked.cmd` on Windows) that locks the build directory. A second `cmake --build` of the same directory, from a script or by hand, prints `another build is running` and waits. The lock belongs to the running Ninja, so interrupting `cmake --build` or a script around it does not let a new build start next to the Ninja that is still running. Running `ninja` directly bypasses the lock. `-DPSPRECOMP_BUILD_LOCK=OFF` turns it off.
- **Damaged dependency log.** Before each build the wrapper checks `.ninja_deps` and repairs a damaged one with `ninja -t recompact`, which keeps every intact record. Do not delete `.ninja_deps` or `.ninja_log`: either costs a full rebuild.
- **Compiler cache.** If `ccache` is installed, every compile goes through it, so a rebuild of unchanged code takes seconds instead of minutes, also across checkouts at different paths. `-DPSPRECOMP_CCACHE=OFF` turns it off.

CMake prints a warning for Ninja 1.13.2, which cannot recover from a damaged dependency log by itself (upstream issue [#2703](https://github.com/ninja-build/ninja/issues/2703)). Building through `cmake --build` works around it; the fix is due in Ninja 1.14. [`docs/BUILD_SYSTEM.md`](../../docs/BUILD_SYSTEM.md) explains why incremental state gets lost and what the build does about it.

## Release builds

Players can use a prebuilt release instead of building: it contains the program with the recompiled code and all overlay libraries, but no game data, and sets the game up from the player's disc image on first start. `-DMHFU_RELEASE=ON` builds the executable for that: it reads nothing from the checkout, and on Linux it loads the libraries it ships from `lib/` next to itself and links the C++ runtime statically. [`docs/RELEASING.md`](../../docs/RELEASING.md) describes how a release is built and published; `scripts/release_linux.sh` builds the Linux artifacts, and `packaging/` holds their manifests, launcher and third-party notices.

## Code overlays

Beyond the main executable, the game loads 290 code overlays (`*.ovl`) from `USRDIR/DATA.BIN` into fixed slots at run time: mode tasks (`game_task`, `lobby_task`, …), maps (`P_m*`, `P_v*`), monsters (`em*m0`–`m3`) and weapons (`we*player00`–`03`). The ELF section table lists them as zero-sized sections, so their code is not part of `EBOOT.ELF`.

Build them all once:

```bash
profiles/mhfu/scripts/build_overlays.sh [build_dir] [jobs]
```

It extracts every overlay from `DATA.BIN` into `analysis/overlays`, recompiles each one that has no corpus yet into `overlays/`, and then builds the overlay libraries into `bin/overlays` in a single `cmake --build` with `jobs` parallel jobs (default 2). Recompiled overlays are not recompiled again and compiled ones are not compiled again, so an interrupted run resumes. The game is playable before this finishes: an overlay whose library is not linked yet runs through the interpreter, which works but is roughly twenty times slower.

The overlays of one slot are linked into one library, `ovlpack_<slot base>`: four libraries for the 290 overlays. Windows Defender scans every DLL the game loads, so fewer files start faster there. `-DMHFU_OVERLAY_PACKS=OFF` links one library per overlay, named after its corpus, as builds up to 1.0.0 did; the host loads both kinds, also from an older `MHFU_OVERLAY_DIR`. Each overlay's code is compiled once, into an object library (`overlay_<corpus>`), so switching relinks the libraries without compiling again. `bin/overlay_libraries.txt` lists every library and the corpora in it, and configuring removes libraries from `bin/overlays` that the build no longer makes, since the host would load them too.

### How the host picks an overlay

Many overlays share one address, so each has its own corpus and the host must use the one matching what is loaded right now. An overlay image starts with `MWo3` and a 64-byte header: id, load address, code size, data size, bss size, two end-of-image addresses and a 32-byte name. A corpus is identified by its slot base plus an FNV-1a hash of that header and the code after it — the part the game never writes to, unlike the data section a loaded overlay keeps modifying.

On a dispatch miss inside a slot, the host hashes those bytes in guest memory, unregisters whatever was installed there and registers the matching corpus. Every frame, and before a chained call into a slot, it compares each installed slot's header against the one it installed, which is enough to notice a swap without rehashing the code. The call check matters when the game jumps into a new image before it flushes the instruction cache: the previous image's compiled code is dropped first, and the jump is looked up again. An overlay may be larger than the gap to the next slot's base; the slot list records where images load, not how much room they have.

Each library resolves its framework symbols from the executable that loads it, so it only matches a host built from the same sources; `cmake --build out/mhfu` rebuilds all of them (or `--target mhfu_overlays` for the overlay libraries alone). `MHFU_OVERLAY_DIR` points the host at another library directory.

To build a single overlay by hand — for example one dumped from memory with `MHFU_DUMP_OVERLAYS`:

```bash
profiles/mhfu/tools/add_overlay.py out/mhfu /path/to/overlay_0A05E600.bin 0x0A05E600
```

It builds with 2 parallel jobs; `-j N` changes that. `--no-build` stops after recompiling and prints the target name, for building many overlays in one run. No reconfigure is needed: CMake notices the new overlay directory by itself.

## Running

```bash
out/mhfu/bin/Pokee [game_dir]       # see "Game data" for where it looks without game_dir
```

The window renders at twice the PSP resolution by default (960×544). Esc, or L3+R3 on a gamepad, opens the [in-game menu](#in-game-menu); quit from there, or close the window (Cmd+Q on macOS, Alt+F4 on most Linux desktops). When the game asks for a name, Pokee's [on-screen keyboard](#on-screen-keyboard) opens; the menu can switch to giving a fixed name at once instead.

### Keyboard and mouse

The game can be played with a keyboard and a mouse alone. Every control can be rebound in the menu (Controls, see [Rebinding](#rebinding) and [Control presets](#control-presets)); these are the defaults, the **Default** preset:

| Key or button | PSP | In the game |
| --- | --- | --- |
| W / A / S / D | Analog stick | Move |
| Mouse | The HD release's second stick | Camera; see below |
| Left mouse button | △ | Draw, attack |
| Right mouse button, F | ○ (confirm) | Attack, talk, gather, confirm |
| Space | ✕ (back) | Roll, back |
| E | □ | Use an item, sheathe |
| Q / Left Shift | L / R | Camera behind the hunter / guard, run, aim |
| Enter, Tab | START | |
| Backspace | SELECT | |
| Arrow keys | D-pad | |
| I / J / K / L | The second stick, fully | Camera without the mouse |
| Esc | In-game menu | Frees the pointer |
| F3 | Performance overlay on or off | |
| `` ` `` (the key under Esc) | [Fast-forward](#fast-forward) | Runs the game faster while held; single player only |
| F12, Print Screen | [Screenshot](#screenshots) | The game's picture at full size, as a PNG in `screenshots/` |
| F7 | [Hide HUD](#hiding-the-hud) on or off | |
| Mouse 4 / Mouse 5 (side buttons) | L + □ / L + ○ | Item bar left / right ([Item left and right](#item-left-and-right)) |
| T, middle mouse button | [Lock-on](#lock-on) | A tap locks the camera onto a large monster, moves to the next, or lets go |

A bow aims with Left Shift held and shoots with the left button; a bowgun fires with the right one.

The **Classic keyboard** preset brings back the keys of earlier versions, for play without a mouse: I / J / K / L move, X ○, Z ✕, A □, S △, Q / W L / R, Enter START, Right Shift or Backspace SELECT, and the arrow keys the D-pad. **Left-handed** has the mouse in the left hand: I / J / K / L move, H ○ (and the right button), U □, O L, Right Shift R, Enter START, Backspace SELECT, the arrow keys the D-pad and keypad 8 / 4 / 5 / 6 the camera. **Modern** is Default plus △ + ○ on the side mouse button (Mouse 4) and C. *Restore control defaults* returns to Default.

To rebind, see [Rebinding](#rebinding). Keys are bound by their place on the keyboard, so W A S D stay under the same fingers on an AZERTY or a Dvorak layout; the menu shows their US names. They are kept in `settings.ini` as `input.bind.<control>` (for example `input.bind.circle=Mouse Right / F`, or `input.bind.triangle_circle=Left Shift + F`; controls `stick_up`, `stick_left`, `stick_down`, `stick_right`, `triangle`, `circle`, `cross`, `square`, `l`, `r`, `start`, `select`, `dpad_up`, `dpad_left`, `dpad_down`, `dpad_right`, `camera_up`, `camera_left`, `camera_down`, `camera_right`, `triangle_circle`, `item_left`, `item_right`, and `fast_forward`, `screenshot`, `hide_hud`, `lock_on` and `focus_camera`, which are not PSP controls; an empty `focus_camera` leaves that tap on L, and an empty value leaves any other control unbound).

**The pointer.** While the game runs and the window has focus, Pokee captures the mouse: the pointer is hidden and its motion and buttons go to the game. It is given back whenever Pokee's menu, the on-screen keyboard or a setup screen is up, and when the window loses focus (switching to another window, Cmd+Tab or Alt+Tab, minimising). Buttons and keys still held when it is captured again reach the game only after they are released, and motion made while it was free is never replayed. *Mouse* in the menu (`input.mouse`, `MHFU_MOUSE=0`) turns all of this off, leaving the pointer alone.

**The mouse camera** feeds the same camera layer as the right stick, so it works where the [analog camera](#analog-camera) does: in a quest's ordinary camera, moving the mouse sideways turns the camera and moving it forward and back tilts it, by *Mouse sensitivity* degrees for each count of motion (0.10 by default), and it stops where the mouse stops. While a bow or a bowgun is in the aim action, the same motion aims, at *Aim speed*'s share of *Camera speed*. Where the port does not drive the camera (with *Analog camera* off, in the village or in a camera mode without a driver) the game's own camera turns at one fixed speed or not at all, so the mouse can only switch that turn on: moving it sideways turns the camera for as long as it moves, like holding the right stick, and vertical motion does nothing there (the second stick's up and down are the game's recentring commands). With *Camera stick* set to D-pad or off, the mouse does not turn the game's own camera, since D-pad presses would also move cursors in the game's menus.

The keyboard and a gamepad can be used together or in turns: both are read every frame and add up, so nothing is left pressed by switching.

### Gamepad

Any controller SDL3 recognises works, and it can be connected before or after the game starts. With several connected, the menu reads them all and the game reads the one used last: pressing a button on another pad hands the game to it (`MHFU_PAD_FOLLOW=0` keeps the first one, as before). That covers the two-port PS2-to-USB adapters too, which SDL may show as two controllers, one per port.

| Gamepad | PSP |
| --- | --- |
| South / East / West / North face buttons | ✕ / ○ / □ / △ |
| LB | L: the camera behind the hunter, and item use with □ or ○ |
| RB, RT (R1 / R2) past the trigger point | R |
| LT (L2) past the trigger point | Focus/Follow Camera for [Lock-on](#lock-on): in Focus, a tap centers the camera on the selected monster; in Follow, it turns tracking off and back on |
| Start / Back | START / SELECT |
| D-pad | D-pad |
| Left stick | Analog stick |
| Right stick | The HD release's second stick (camera) |
| L3 + R3 (both sticks pressed) | In-game menu |
| R3 (right stick pressed) | [Lock-on](#lock-on): select a large monster. Another press moves to the next, or lets go |
| R3 + D-pad left | [Screenshot](#screenshots) |
| Upper back paddles, where the pad has them (P3 / P1, L4 / R4) | Item left / Item right: L + □ / L + ○ |

That is the **Default** preset. Its buttons, triggers included, can be rebound like the keyboard's (Controls, [Rebinding](#rebinding)), to one button or to a combination such as LB + ○, and other presets change them: **Modern** attacks with RT (△, which also shoots a bow), does the second attack with RB (○, which also fires a bowgun) and guards and aims with LT (R), with LB as L and lock-on on R3; **Left-handed** mirrors the pad, moving with the right stick, the D-pad doing the face buttons and the face buttons the D-pad, and the shoulders and triggers changing sides, so L3 locks on, RT is the focus or follow camera and RB is L. The sticks stay sticks: *Move with the right stick* swaps them, and the other one is the *Camera stick*. L3 + R3 always opens the menu.

The face buttons are positional, so on a PlayStation pad circle is circle and confirms, exactly as the game's prompts say. The menu's *Confirm button* setting (or `MHFU_PAD_FACE=xbox`) moves confirm to the bottom button for pads labelled the other way round: it swaps the bottom and right buttons in every preset, and the menu shows the bindings as they are then pressed.

### Steam Deck

The Deck's controls are a gamepad to Pokee, with the Default preset's layout. Its back buttons (L4, L5, R4, R5) are not: under Steam Input the game sees a *Steam Virtual Gamepad*, which has no back paddles. Give them keyboard keys in Steam and bind those keys in Pokee:

1. In Steam, open the game's properties, then **Controller** and its layout. Set **L4** to **Keyboard** and a key nothing else uses, such as **F5**, and **R4** to another, such as **F8** (F7 hides the HUD). L5 and R5 likewise, if you want them.
2. In Pokee, open the menu (L3 + R3), **Controls**, and add the key to a control on the *Keyboard and mouse* side: select the *+* after, say, **Item left**, and press L4. It records F5, and L4 now scrolls the item bar left. R4 on **Item right** the same way.

The keyboard and the gamepad are read together, every frame, so the back buttons work beside the pad with no layout to switch, and any control takes them: Item left and right, △ + ○, fast-forward, a screenshot, or a combination of your own. Chords of face buttons and shoulders (LB + X, △ + ○) are bound on the *Gamepad* side as on any pad.

### Control presets

A control preset is a whole layout: every control on the keyboard and mouse and on gamepads, and which stick moves. Choose one at the top of Controls (*Preset*); it applies at once.

| Preset | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Default | W A S D and the mouse, as in [Keyboard and mouse](#keyboard-and-mouse) | LB is L (camera and items), R3 locks on, LT is the focus or follow camera, RT is R |
| Modern | Default, plus △ + ○ on Mouse 4 and C, and no item keys on the side buttons | RT △ (attack, shoot a bow), RB ○ (second attack, fire a bowgun), LT R (guard, aim), LB L; the face buttons as in Default |
| Left-handed | The mouse in the left hand, I J K L to move | Mirrored: the right stick moves, the D-pad is △ ○ ✕ □, the face buttons the D-pad, the shoulders and triggers change sides |
| Classic keyboard | The keys of earlier versions, without a mouse | As in Default |

Every preset has the same port binds on the keyboard: `` ` `` fast-forward, F12 or Print Screen a [screenshot](#screenshots), F7 [Hide HUD](#hiding-the-hud). On a gamepad a screenshot is R3 + D-pad left; Left-handed mirrors it, to L3 + □ (west). [Lock-on](#lock-on) is T on every keyboard preset, and also the middle mouse button in those that use the mouse (all but Classic keyboard); on a gamepad it is R3, and L3 in Left-handed. The default and classic gamepads put Focus/Follow Camera on LT, and Left-handed on RT. Modern leaves that button unbound, because LT guards, so L does the focus tap.

The shipped presets never change. Changing a binding while one of them is chosen makes a preset of your own from it, named *Custom* (then *Custom 2*, and so on), and chooses that; later changes go into it as you make them. *Save as a new preset* copies the current bindings into another one. Your presets can be renamed (*Name*) and deleted (*Delete this preset*, which asks first and goes back to Default); up to 32 are kept, in `settings.ini` as `input.user_preset.<n>.name`, `.bind.<control>`, `.pad.<control>`, `.move_stick` and `.base`, the shipped preset it was made from, which resetting a control goes back to (Default for presets made before it was kept). The chosen preset is `input.preset` (`default`, `modern`, `left_handed`, `classic`, or `user:` and a name of yours), and the bindings in use are also kept as `input.bind.<control>`, `input.pad.<control>` and `input.move_stick`.

### Rebinding

Under the preset, Controls lists every control in groups (movement, attacks, items, camera, system, and the port's own features such as fast-forward), each with its keyboard and mouse bindings and its gamepad bindings side by side. Every binding is a chip, and a control takes up to four on each device:

- **Rebind:** select a chip (confirm, Enter, a click or a tap) and press what should take its place.
- **Add:** select the *+* chip after a control's bindings. Nothing is ever replaced to make room.
- **Clear:** the pad's top face button (Y, △), Delete or Backspace, a right click, or the chip's ×. A control may be left with nothing.
- **Reset:** the round arrow at the end of a row, or the pad's View (Select) button on any of the row's chips, puts the control back as the shipped preset has it that the bindings came from. It shows only when the control differs.

While Pokee waits for the input, a box over the menu says what is being bound and shows what is held so far. Esc or a touch cancels it; on the gamepad, so does holding the menu's back button (B) for a second, with a bar that fills while it is held, while a short press binds it. A gamepad capture gives up after six seconds without a press, and a keyboard capture ends at any gamepad button. Everything works with a gamepad alone, as on a Steam Deck, and with a mouse or touch.

**Chords.** A binding is one input or a chord of up to four held together on one device, in any order: L1 + ○ or △ + ○ on a pad, Left Shift + F or Shift + Ctrl + F on the keyboard (the keyboard and the mouse are one device, so Q + Mouse Right is a chord too). To bind one, press everything together; the capture box shows the chord as it is built and keeps it when the last input is let go.

- **The longest chord wins.** With A bound to one control and A + B to another, pressing A + B does only the second, and its inputs do nothing else while it is held. What is left over is matched again: holding W to move while pressing Q + E still moves.
- **Letting go.** A chord let go of unevenly presses nothing left over (letting go of △ a moment before ○ does not press ○). A chord that grew from something held first goes back to it: hold LB for L, then B for LB + B, and letting go of B leaves L.
- **The chord window.** An input that begins a longer chord waits a moment for the rest of it: *Chord window* under *Combinations* (50 ms by default, up to 200, `input.chord_window`, `MHFU_CHORD_WINDOW`; 0 turns it off). A tap shorter than the window still presses its control, when it is let go. Only such inputs wait, and only where acting at once could do harm: △ before ○ is no combined attack, and □ before L uses the item. Everything else acts in the same sample it goes down, with no time added: inputs in no chord, L, R and moving held first (LB in LB + X for Item left, which only adds □ to L), a game button in a chord only the port reads (D-pad left in R3 + D-pad left for a screenshot, as before), and Fast-forward, which runs while its button is down and stops when a longer chord takes that button (L3 beside L3 + R3). L3 + R3 (the menu) is a chord of the port's own: it wins like any chord, so an input bound alone to R3 does nothing when it is pressed.

**△ + ○ (together)** is a control of its own: one key or button that presses △ and ○ in the same frame, for the combined attacks that two fingers do not always manage together. Default leaves it unbound.

#### Item left and right

**Item left** and **Item right** scroll the item bar, as L + □ and L + ○ do on a PSP, from one input or from a chord of your choice (LB + X and LB + B, a key, a back paddle). The game scrolls only once L has been held on its own for 8 of its frames, which is when the bar opens: L and □ in the same frame do nothing, and □ after fewer frames of L uses the item (traced in a quest, with the pad read once per game frame). So these actions send L alone first, for 10 of the game's reads of the pad (a third of a second), then □ or ○ with it for at least 3, even for a short tap. If L is held already, □ or ○ goes at once. Any action that presses L with other buttons, your own included, is sent the same way; R is not, since nothing was traced to need it. The presets put them on the mouse's side buttons (not in Modern, which has △ + ○ there) and on a pad's upper back paddles, which nothing else uses; binding LB + X or LB + B makes X or B wait the chord window, since □ or ○ alone must not go first.

#### Your own combinations

*Your combinations*, after the controls, holds actions of your own that press any PSP buttons together (△ ○ ✕ □, L, R, the D-pad, START, SELECT), such as ✕ + ○. *+ New combination* makes one and opens its buttons: select each to turn it on or off, then *Done*. Its name is its buttons; select the name to change them, and the × at the end of the row removes it. Bind it like any control, on both devices. Up to 8 are kept with the preset, in `settings.ini` as `input.combo.<n>.buttons` (`L + Square`), `.bind` and `.pad` (and `input.user_preset.<n>.combo.<m>.*` in a preset of yours). The game gets the buttons in the same frame, except L, which goes first as above.

**Conflicts.** A binding that clashes is drawn in red, with a line under its control that says with what: the same input or chord on another control (one press does both); a chord that is part of another one, so it waits the chord window and does only the longer one when that is complete (shown on both controls); or a chord that takes an input doing L, R or moving on its own, which the game reads held together with other buttons, so that control lets go while the chord is held. A chord that only adds to L (LB + X for Item left) is none. The line has the fix: *Remove from …* takes the input away from the other control, *Keep both* leaves them and hides the warning until Pokee starts again. Conflicts are allowed, and the ones not kept are counted under the preset. *Screenshot* is read during play as well and clashes like any other control.

**From earlier versions.** Settings written before chords and combinations read as they were: every binding is kept, and a chord of two inputs is the same chord in any order. The one difference is the longest match: before, the first input of a chord such as LB + B also kept doing its own control while the chord was held (LB still pressed L); now the chord alone acts, and a line under it says so. Item left, Item right and Lock on, which those settings did not have, take the preset's inputs where they clash with nothing. Settings without a preset become one the first time they are read: the Default or Classic keyboard preset if the bindings are exactly one of those, otherwise a preset of your own named *Custom* with your keyboard bindings. The **trigger profiles** of earlier versions are part of it: *Bows* became LT on R and RT on △, *Bowguns* LT on R and RT on ○, over the Default pad. `input.trigger_profile` is dropped at the next save, and `MHFU_PAD_TRIGGERS` is no longer read (`MHFU_CONTROL_PRESET` chooses a preset for a run).

### Controllers SDL does not know

SDL treats a device as a gamepad only when it has a *mapping* for it: which of the device's raw buttons, hats and axes is which. It ships mappings for most controllers; one it does not know stays a plain joystick that neither the game nor the menu reads. Pokee says so: a note over the game when such a controller connects or a button on it is pressed, and a warning on the Controls page.

**Controls → Controllers** lists every controller connected, with its name, USB vendor and product ids, SDL GUID, its buttons, axes and hats, and its mapping (SDL's own, yours, or none). *What it reports* shows every button, hat and axis of the selected one live, so each button can be checked; *Copy these details* puts them on the clipboard for a bug report.

*Set up this controller* asks for each control in turn: the four face buttons, the D-pad, L1 and R1, L2 and R2, SELECT and START, L3 and R3, then each stick right and down. Press it on the controller; answer the screen with the keyboard, the mouse or a touch, because the controller's own buttons are being recorded (*Skip this one* for a control the controller lacks, *Back one step*, *Cancel*, or Esc). A button, a hat direction or an axis all count, so a D-pad that reports axes and triggers that are axes work too. *Save and use this layout* writes the mapping to `gamecontrollerdb.txt` in the data directory and applies it at once: the controller is then a gamepad in the game and the menu, and at every later start. *Remove my layout* takes the line out again; SDL's own mapping, if it had one, comes back at once, otherwise at the next start.

`gamecontrollerdb.txt` has the format of SDL and of the community's [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB): one line per controller and platform, `GUID,name,a:b2,b:b1,...,platform:Windows,`. Lines from that database can be pasted into it; Pokee reads it at start (only the lines for the platform it runs on). Pokee does not ship that database. The `SDL_GAMECONTROLLERCONFIG` environment variable, SDL's own, holds mappings too, one per line, and wins over the file and over SDL's built-in ones.

Every controller is also logged when it appears, with `[pad]`: name, USB ids, GUID, buttons, axes and hats, and its mapping or the lack of one.

### Touch screen

On a touch screen (the Android app, or any screen SDL reports touches from) controls are drawn over the game once it is touched, and hidden again when a gamepad or the keyboard is used. Controls → *Touch screen* → *Layout* chooses between two:

- **PSP buttons** (the default): a stick that appears where the left thumb lands, the four face buttons in the PSP's diamond on the right, L and R at the top corners, a D-pad at the left edge (*D-pad*), and Select, the menu button and Start small at the top.
- **Action**: large buttons named for what they do, each pressing a PSP button or two in the same frame:

| Element | Where | Presses |
| --- | --- | --- |
| Stick | Low on the left, fixed | The analog stick; a thumb landing on or near it takes it |
| Item pouch | Above the stick, at the edge | L |
| D-pad Up, D-pad Down | Beside the stick | D-pad Up, Down |
| Attack | The large button low on the right | △ |
| Second attack | Above the attack | ○ |
| Combined attack | Left of the attack, high | △ and ○ in the same frame |
| Evade | Left of the attack, low | × |
| Use / sheathe | Small, further left | □ |
| Guard, Special | Right of the attack | R, both |
| Pause, Start, Select | Small, top right | Pokee's menu; START; SELECT |
| Swipe area | Above the middle | A quick swipe left or right presses D-pad Left or Right for a moment (the item bar), again for each further length of swipe |

  Every finger is independent: the stick, the buttons and a camera drag work at the same time, and buttons stay pressed while held. A thumb on the right-hand cluster can slide from one button to the next. A finger anywhere free turns the camera, as on the PSP-button layout; in the swipe area a finger that does not swipe within about a quarter of a second turns the camera too, with the motion it made, so a swipe never turns the camera and a slow drag is never a swipe. The icons are drawn by Pokee, simple shapes; each button shows what it presses beside it. Placements keep their distance from the nearer edge of the screen's safe area (the swipe area from its middle), in heights of the screen, so the layout fits phones and tablets of any shape and stays clear of a cutout.

**Editing the action layout.** *Edit the action layout…* shows it over the game. Drag an element to move it; tap one to choose it, and the panel then sets what it presses (one PSP button, or two such as △ + ○ or R + △), its size, whether it is shown, or resets it. With nothing chosen the panel sets the opacity, the size of all elements, haptic feedback and where the panel sits, or resets the whole layout. Every change is saved at once, in `settings.ini` on this device (`input.touch_action.<element>`, for example `input.touch_action.combo=right 0.540 0.620 0.060 0x3000 1`: the anchor, the distance from it and from the top in heights of the safe area, the radius, the PSP buttons and whether it is shown). Pause cannot be hidden: it is the way into this menu. **Haptic feedback** (on by default) gives a short vibration when an Action button is pressed or a swipe is taken, as the system does for a key; the system's own touch feedback setting decides whether it is felt.

## On-screen keyboard

When the game asks for text (the hunter's name at character creation), Pokee opens its own keyboard over the game. It works with a gamepad alone, and a physical keyboard types into it at the same time; the mouse can click its keys.

| Gamepad | Keyboard | Action |
| --- | --- | --- |
| D-pad, left stick | | Move over the keys |
| Confirm (the bottom button: A, or ×) | typing | Type the key |
| Back | Backspace | Delete the character before the cursor |
| □ (X) | | Shift: once, again for caps, again off |
| △ (Y) | Space | Space |
| Select / View | | Letters or symbols |
| L1 / R1 | ← / →, Home, End | Move the cursor |
| Start | Enter | OK |
| the *Cancel* key | Esc | Cancel |

The keys *Shift*, *#+=*, *Space*, *Delete*, *Cancel* and *OK* sit in the bottom row. A counter shows the length against the most the game takes (12 characters for a hunter name); it turns red when a key cannot be typed. Characters the game cannot take are dimmed. A hunter name may hold Latin letters, digits, space and `! # $ & ' ( ) + , - . / : ; = ? @ _ ~`. The game with the English patch draws every printable ASCII character in a name, but the asterisk comes out as a bullet, and the double quote, percent sign, asterisk, angle and square brackets, braces, backslash, caret, backquote and vertical bar are left out because text formatting may claim them.

The game keeps running behind the keyboard, as it does behind the PSP's: it keeps polling the keyboard and playing sound, and its animations go on. It reads a neutral pad until the keyboard closes, and buttons still held then reach it only after they are released. The game blanks its screen while the PSP's keyboard would cover it, so the window keeps the frame from just before the keyboard opened, dimmed. The in-game menu does not open over the keyboard.

Where SDL reports a system on-screen keyboard (such as Steam's in Big Picture or Game Mode), a *Steam* key appears in the bottom row and asks for it; what it types goes into the field like a physical keyboard. The built-in keyboard always works without it.

OK hands the text to the game as the PSP's keyboard would: UTF-16 in the field's output buffer, the field result *changed*, and the dialog status moving from visible to quit. Cancel leaves the buffer alone and reports *cancelled*; the game then keeps the name it had.

The menu's text fields (*Hunter name*, *Server*, *Nickname*) open the same keyboard when a gamepad activates them; with a keyboard or the mouse they are edited in place.

## In-game menu

Esc, or L3+R3 on a gamepad, opens Pokee's menu over the game; the same again, back at its top level or Start closes it. Esc never quits the game: Steam's desktop controller layout on a Steam Deck sends Esc with the B button, so an Esc that arrives together with a gamepad button is ignored.

The menu opens where it was last closed: the same page, scrolled as it was. No row is selected when it opens or when LB and RB switch to another page, so a button pressed right after changes nothing; the first press of a direction selects the row that was focused when the menu closed, or the top row of a page just switched to. The page is also kept for the next start (`ui.menu_tab` in `settings.ini`); the scroll position and the row only for the session. When that row is not there any more, the first press selects the top of the page.

By default the game is paused while the menu is open: no guest code runs, emulated time stands still, the audio device stops, and the last frame stays behind the menu, dimmed. On resume the kernel's clock picks up from real time again, so the game neither races to make up the pause nor counts it in the `[perf]` statistics.

Two settings in the Game section change that. With *Pause the game when the menu opens* off, the game keeps running behind the menu: it keeps drawing frames at its own pace, the sound keeps playing and its clock keeps running, and the menu is drawn over each frame. During ad hoc play (in a gathering hall, joining one, or hosting a session) the game keeps running behind the menu unless *Pause during multiplayer* is on, whatever the first setting says, because a paused game stops answering the other players and can drop a quest; this one is off by default. The menu's header says which applies: *Paused* or *Running*. Either way, input goes to the menu only, so moving through it never moves the hunter, and buttons still held when it closes reach the game only after they are released.

The menu always selects with the bottom face button (A on an Xbox pad or a Steam Deck, × on a PlayStation pad) and goes back with the right one (B, or ○), whatever *Confirm button* is set to for the game. `MHFU_MENU_CONFIRM=game` makes the menu follow *Confirm button* instead. The footer shows the buttons of the pad in use (PlayStation shapes or letters) or the keys, and a line explaining the focused setting. L1/R1 (LB/RB), or Q/W on the keyboard, switch between the sections. Left and right change a value; confirm steps it forward.

Every change applies at once and is saved to `settings.ini` in the per-user directory, next to the installer's `disc_image`. A setting whose environment variable is set is decided by that variable for the run: the menu shows it greyed with *Set by MHFU_…* and leaves the file's value alone. So the order is: environment variable, then `settings.ini`, then the default.

The Android app starts from other defaults where a phone differs, with the same keys and values: `video.aspect` is `fill` (a phone is wider than the PSP), `video.fullscreen` is on (there is no window) and `input.mouse` is off (a phone has no mouse to capture). Everything else starts as in the table. On Android, *Analog camera* alone decides whether a finger drag turns the camera: *Camera stick* is about a physical stick there.

| Section | Setting | Key in `settings.ini` | Variable | Values |
| --- | --- | --- | --- | --- |
| Video | Resolution | `video.internal_scale` | `MHFU_INTERNAL_SCALE` | Auto (`auto` or `0`: the window's own size, followed as it changes, at most 1632 lines) or ×1–×6 of 480×272 (the variable allows up to ×8); default ×2. See [Picture shape and size](#picture-shape-and-size) |
| Video | Display | `video.fullscreen` | | Window or fullscreen |
| Video | Window size | `video.window_scale` | | ×1–×4 of 480×272; default ×2 |
| Video | Aspect ratio | `video.aspect` | | `original` (the PSP's shape, black bars; the default), `stretch` (stretched to the window) or `fill` (the game's view takes the window's shape). Older versions wrote `video.keep_aspect`, which is still read and written |
| Video | Field of view | `video.field_of_view` | `MHFU_FIELD_OF_VIEW` | 30–90 vertical degrees on a hunt; default 50, the game's own. The village and the Guild Hall keep the game's view |
| Video | Scaling filter | `video.sharp_screen` | | Smooth or sharp scaling of the finished picture to the window |
| Video | Texture filter | `video.sharp_textures` | | Smooth (bilinear) or sharp (nearest) texture sampling |
| Video | Texture pack | `video.texture_pack` | `MHFU_TEXTURE_PACK` | On (default) or off: draw an installed [HD texture pack](#hd-texture-packs) instead of the game's textures. The footer shows how many textures the pack has and how many are on the GPU, or where the pack was looked for |
| Video | Import texture pack… | `video.texture_pack_folder` | `MHFU_TEXTURE_PACK` (a folder) | Empty (default): the pack in `textures/ULUS10391`. A folder: the pack [imported to be used where it is](#importing-a-texture-pack). *Stop using the pack folder* empties it |
| Video | Button icons | `video.texture_pack_buttons` | `MHFU_TEXTURE_PACK_BUTTONS` | `auto` (default), `pack`, `xbox`, `playstation` or `nintendo`: the [button icon set](#button-icons) a pack that comes with several draws. Shown only for such a pack |
| Video | Vsync | `video.present_mode` | | On (FIFO), or off through mailbox or immediate presentation where the driver offers them |
| Video | Frame rate | `video.frame_rate` | `MHFU_FRAME_RATE` | `30` (default: the game's own frames, as they are), `45`, `60`, `90`, `120` or `display` (the display's refresh rate): frames in between the game's, with blended movement. See [Frame rate](#frame-rate) |
| Video | Lower when behind | `video.frame_rate_auto` | `MHFU_FRAME_RATE_AUTO` | On (default): the frame rate steps down by itself rather than slow the game. Off: the chosen rate stays, and the game may run below full speed |
| Video | Hide HUD | | `MHFU_HIDE_HUD` | Off (default) or on, for this run: the game's HUD, name tags and prompts are left out of the picture; menus and dialogs stay. Same as the Hide HUD key. See [Hiding the HUD](#hiding-the-hud) |
| Video | UI Scaling | `video.hud.map`, `video.hud.health`, `video.hud.item_bar`, `video.hud.names` | `MHFU_HUD_MAP`, `MHFU_HUD_HEALTH`, `MHFU_HUD_ITEM_BAR`, `MHFU_HUD_NAMES` | 50–150 each, the game's size as a percent; default 100. UI Scaling opens to Map, Status, Items, and Names, each with its own size. See [UI scaling](#ui-scaling) |
| Video | Performance | `video.performance` | `MHFU_PERF` | Off, overlay, overlay and log, log only, compact, compact and log. Compact is one line in the bottom-left corner: frames shown, the game's frame rate and its speed, green at full speed, yellow from 90%, red below |
| Video | GPU compatibility | `video.gpu_compat` | `MHFU_GPU_COMPAT` | Auto (default: on only when the start-up self-test fails or a pipeline is refused), on or off; applies from the next start. See [GPU compatibility mode](#gpu-compatibility-mode) |
| Video | Font | `text.font` | `MHFU_FONT` | Default (a Japanese system font), or an installed font; see [Game text](#game-text) |
| Video | Weight | `text.weight` | | Regular, bold (default) or heavy: thickens the game's text by 0–2 pixel columns |
| Video | Sharp text | `text.crisp` | `MHFU_CRISP_TEXT` | Off (default) or on: above ×1 the game's text is drawn again at the internal resolution. See [Sharper text and 2D textures](#sharper-text-and-2d-textures) |
| Video | UI textures | `video.ui_textures` | `MHFU_UI_TEXTURES` | Off (default), Sharp bilinear or MMPX, for the 2D interface above ×1 |
| Audio | Volume | `audio.volume` | | 0–100% |
| Audio | Mute | `audio.mute` | | |
| Audio | Mute in background | `audio.background_mute` | | Off (default) or on: silent while the window is not the one in front |
| Controls | Confirm button | `input.confirm` | `MHFU_PAD_FACE` | Right (○, Japanese) or bottom (Western) |
| Controls | Stick dead zone | `input.dead_zone` | `MHFU_PAD_DEADZONE` | 0–50% |
| Controls | Trigger point | `input.trigger` | `MHFU_PAD_TRIGGER` | 5–100% |
| Controls | Gamepad in background | `input.background_gamepad` | | Off (default) or on: the game reads the gamepad while its window is not the one in front, for example beside a second instance. Another game open at the same time may read the same presses. Keyboard and mouse only reach the window in front. Off, a window that loses focus sees the pad released |
| Controls | Chord window (under *Combinations*) | `input.chord_window` | `MHFU_CHORD_WINDOW` | Off, 10–200 ms (50 by default): how long an input that begins a longer chord waits for the rest; see [Rebinding](#rebinding) |
| Controls | Preset | `input.preset` | `MHFU_CONTROL_PRESET` | `default`, `modern`, `left_handed`, `classic` or `user:<name>`; see [Control presets](#control-presets). The variable takes a shipped preset's id or the name of one of yours, for the run only |
| Controls | Camera stick | `input.right_stick` | `MHFU_PAD_RSTICK_DPAD` | Camera, D-pad or off, for the stick that does not move the hunter |
| Controls | Analog camera | `input.analog_camera` | `MHFU_ANALOG_CAMERA` | Proportional turn and continuous tilt in the ordinary quest camera; on by default |
| Controls | D-pad camera | `input.dpad_camera` | `MHFU_DPAD_CAMERA` | With the analog camera on, the D-pad also turns the game's camera and steps its height; off by default |
| Controls | Aim speed | `input.aim_speed` | `MHFU_AIM_SPEED` | Degrees a second a bow or bowgun aims at full deflection |
| Controls | Camera speed | `input.camera_speed` | `MHFU_CAMERA_SPEED` | 20–720 degrees per second at full deflection; default 190 |
| Controls | Invert camera horizontally / vertically | `input.invert_camera_x`, `input.invert_camera_y` | | For the right-stick camera |
| Controls | Camera stick D-pad point | `input.right_stick_zone` | `MHFU_PAD_RSTICK_ZONE` | 10–100%, for the D-pad mode |
| Controls | Move with the right stick | `input.move_stick` | | `left` (default) or `right`; part of the preset |
| Controls | A control's *Gamepad* bindings under [Rebinding](#rebinding) | `input.pad.<control>` | | Up to four buttons, triggers or combinations, such as `Pad North / Pad LB + Pad East`; part of the preset |
| Controls | Mouse | `input.mouse` | `MHFU_MOUSE` | On (default): the window captures the pointer while the game runs, and the mouse turns the camera and presses its bound buttons; off: the pointer is left alone |
| Controls | Mouse sensitivity | `input.mouse_sensitivity` | `MHFU_MOUSE_SENSITIVITY` | Degrees of camera turn per count of mouse motion, 0.01 to 0.99; default 0.10 |
| Controls | Invert mouse horizontally / vertically | `input.invert_mouse_x`, `input.invert_mouse_y` | | For the mouse camera and aim |
| Controls | On-screen controls | `input.touch_controls` | | On (default): a touch screen shows the on-screen controls once it is touched |
| Controls | Layout | `input.touch_layout` | `MHFU_TOUCH_LAYOUT` | `psp` (PSP buttons, the default) or `action`; see [Touch screen](#touch-screen) |
| Controls | Edit the action layout… | `input.touch_action.<element>` | | Where each element of the Action layout is, what it presses and whether it is shown |
| Controls | Haptic feedback | `input.touch_haptics` | | On (default): a short vibration for an Action button or swipe |
| Controls | D-pad | `input.touch_dpad` | | On (default): the PSP-button layout has a D-pad at the left edge, for the game's menus; off gives its place to the stick |
| Controls | Controls opacity | `input.touch_opacity` | | 10–100%; default 50% |
| Controls | Controls size | `input.touch_size` | | 60–160% of the default size; default 100% |
| Controls | Touch camera speed | `input.touch_camera_speed` | | Degrees the camera turns for a drag across the screen's height, 30 to 720; default 180 |
| Controls | A control's *Keyboard and mouse* bindings under [Rebinding](#rebinding) (Move forward … △ + ○, Fast-forward, Screenshot, Hide HUD, Lock on, Focus/Follow Camera) | `input.bind.<control>` | | Up to four keys, mouse buttons or combinations, see [Keyboard and mouse](#keyboard-and-mouse); part of the preset. Focus/Follow Camera with nothing bound is L |
| Game | Hold to gather | `game.gather_hold` | `MHFU_GATHER_HOLD` | On (default): holding ○ on a gather point or a carvable body, or □ on a mining point or a bug net, gathers or carves again each time the hunter is free. □ at a Pokke Farm mining outcrop or bug thicket mines or catches bugs again while it has uses left. Movement and every other button, R held to run, △ and × included, are kept from the hunter while that hold runs, so the gather comes first. △ or × with ○ as it starts is still an attack. `0` leaves each button as one press. L+○ or L+□ before a hold starts is left to the item bar |
| Game | Gather with ○ | `game.gather_circle` | `MHFU_GATHER_CIRCLE` | On (default): ○ on a mining point with a pickaxe in the pouch, or on a bug point with a bug net, mines or catches bugs with that tool, and the item bar stays on the item that was selected. Without the tool ○ is left alone there. A carvable body on the point keeps ○ a carve. ○ at a Pokke Farm outcrop or thicket mines or catches bugs too. `0`: only □ with the tool selected, as in the original game |
| Game | Empty item slot | `game.empty_item_slot` | `MHFU_EMPTY_ITEM_SLOT` | On (default) or off: the item bar gets an empty slot, *Nothing Selected*, before the first item, and each quest starts on it, so □ with the weapon sheathed uses nothing until an item is chosen, as in the later games. `0` leaves the game's own bar. See [Empty item slot](#empty-item-slot) |
| Game | True Attack Value | `game.true_attack` | `MHFU_TRUE_ATTACK` | Off (default): the game's own numbers. On: the equipment screens show a weapon's attack, element and status as the game counts them, as FUComplete's True Raw/Ele/Status does. Attack loses its class factor: a great sword shown as 672 shows 140. The screens multiply the weapon's attack by a factor per class (great sword and long sword 4.8, hammer and hunting horn 5.2, lance and gunlance 2.3, sword and shield and dual blades 1.4, bowguns and bow 1.2), kept as 11 words at `0x0893ACDC` that damage never reads; on, each is 1. Element and status lose their trailing zero: fire shown as 250 shows 25. The game keeps those values as it counts them and its text appends a literal zero (`:~C%02d%3d0` in English); on, that format reads `%4d` instead, in each language: a wrapper on the game's `sprintf` (`0x0880E8F0`), put in place the first time the switch is on, changes the text in memory at its first print, and turning the switch off puts it back. Damage is unchanged |
| Game | Lock-on | `input.lock_on` | `MHFU_LOCK_ON` | On (default) or off: what the [Lock on](#lock-on) bind does. Off, the bind does nothing and the monster icon is hidden |
| Game | Lock-on style | `input.lock_on_style` | `MHFU_LOCK_ON_STYLE` | `follow` (default): the camera stays on the selected monster. `focus`: the selection stays, and a tap centers the camera on the monster. The default pad uses LT for that tap, and R3 for Lock on. With nothing bound to Focus/Follow Camera in Controls, that tap is L. A second tap within a third of a second recenters behind the hunter |
| Game | Lock-on marker | `input.lock_on_marker` | | On (default) or off: a red ring, a cross through it, and four ticks, over the locked monster. Follow keeps it up while the camera is tracking. The right stick or Focus/Follow Camera turns it off with that tracking; the selection stays, and the same button turns both back on. In Focus, selecting leaves it off. The focus button shows it for a third of a second |
| Game | Monster Icon | `video.hunt_portrait` | `MHFU_HUNT_PORTRAIT` | 50–200, the monster icon as a percent of its usual size; default 100. Hidden while Lock-on is off |
| Game | Damage numbers | `game.damage_numbers` | `MHFU_DAMAGE_NUMBERS` | `off` (default), `mine` or `everyone`: the health a hit removes, drawn where it landed. Mine shows only your hunter's hits; Everyone shows every hit, those whose attacker is not known included. Yellow when the part's effective hitzone is 45 or more (after sharpness for a blade, the shot hitzone for a gun), white otherwise. A critical hit has a glowing pink sparkle above it and a negative critical a faded blue-gray one. A file from an earlier version with `1` reads as Everyone, and the variable takes the names or any on/off flag |
| Game | Damage breakdown | `game.damage_breakdown` | `MHFU_DAMAGE_BREAKDOWN` | `total` (default), `split` or `total+element`: the hit as one number; the raw part in the hitzone's colour with the element part beside it, smaller, in the element's colour after the element's glyph (a curled flame for fire, a drop for water, a zigzag bolt with an arrowhead for thunder, two hexagonal crystals for ice, a cluster of crystal spikes for dragon: the port's own drawings after the element icons of the later games); a hit of element alone is that part with its glyph; or the whole hit with the element part beside it. A hit with no element is one number in every mode. Locked while damage numbers are off |
| Game | Damage number size | `game.damage_number_size` | `MHFU_DAMAGE_NUMBER_SIZE` | 100 (default): damage numbers at 85% of the interface text; any whole percent from 50 to 300, in steps of 10 in the menu. Locked while damage numbers are off. Above or below 100, stacked hits step by about half the number's height |
| Game | Damage number thickness | `game.damage_number_thickness` | `MHFU_DAMAGE_NUMBER_THICKNESS` | 1 (default) or 2: 1 is the font's own strokes with a one-pixel dark outline, and 2 thickens both. A saved 3 or more from an earlier version becomes 2. Locked while damage numbers are off. Above 1, stacked hits step by about half the number's height, outline included |
| Game | Modern sharpness gauge | `game.sharpness_gauge` | `MHFU_SHARPNESS_GAUGE` | On (default) or off: the sharpness the game draws shortens as it is used, the colour on the blade stays solid and glows, and orange and yellow get a colour too. Off is the game's own drawing. See [Modern sharpness gauge](#modern-sharpness-gauge) |
| Game | Gather icon | `game.gather_icon` | `MHFU_GATHER_ICON` | On (default): a mark floats above the gather point ○ or □ would gather, with the picture of what it gives (a pickaxe for mining, a bug net for bugs), above each monster or cut tail in reach with something left to carve, with the monster's icon and ○ on the one ○ would carve, and above the fishing spot a bait would fish at, with the bait. Where two of them are in reach at one place, their marks sit side by side. `0` turns it off and the gather list is not read for the mark |
| Game | Gather icon size | `game.gather_icon_size` | `MHFU_GATHER_ICON_SIZE` | 100 (default): the mark at its usual size; any whole percent from 50 to 300, in steps of 10 in the menu. The frame, the picture and the button grow together. Locked while the gather icon is off |
| Game | Game speed | `video.unthrottled` | `MHFU_UNTHROTTLED` | Normal (held to real time) or unlimited |
| Game | Fast loading | `video.fast_loading` | `MHFU_FAST_LOADING` | On (default) or off: while the game loads, and only then, it runs ahead of real time. See [Fast loading](#fast-loading) |
| Game | Fast-forward | `video.fast_forward` | `MHFU_FAST_FORWARD` | `hold` (default): the game runs faster while the Fast-forward key is held; `toggle`: one press turns it on, the next off; `off`: the key does nothing. Single player only. See [Fast-forward](#fast-forward) |
| Game | Fast-forward speed | `video.fast_forward_speed` | `MHFU_FAST_FORWARD_SPEED` | 2×–8× real time; default 3× |
| Game | Pause the game when the menu opens | `ui.menu_pause` | `MHFU_MENU_PAUSE` | On (default) or off: the game keeps running behind the menu |
| Game | Pause during multiplayer | `ui.menu_pause_multiplayer` | `MHFU_MENU_PAUSE_MULTIPLAYER` | Off (default): during ad hoc play the game keeps running behind the menu; on: the setting above decides |
| Game | When the game asks for a name | `input.name_entry` | `MHFU_OSK_MODE` | `keyboard` (default): the on-screen keyboard; `fixed`: the name below at once |
| Game | Hunter name | `input.name` | `MHFU_OSK_TEXT` | Default `Hunter`; up to 12 characters. Setting the variable also answers at once unless `MHFU_OSK_MODE` says otherwise |
| Mods (Layered armor) | Layered armor | `look.layered_armor` | `MHFU_LAYERED_ARMOR` | Off (default) or on: the hunter is drawn in the pieces chosen below; stats, skills and the save keep the real armor. See [Layered armor](#layered-armor) |
| Mods (Layered armor) | Head, Chest, Arms, Waist, Legs | `look.layered_head`, `look.layered_chest`, `look.layered_arms`, `look.layered_waist`, `look.layered_legs` | | `real` (default): the piece worn; `0`: nothing; otherwise the id of the armor piece to show |
| Mods (Event quests) | Event quests | `game.event_quests` | `MHFU_EVENT_QUESTS` | On (default) or off: the Guild Hall and the Training School list the quests in `event_quests/` after the save's own event quests and challenges. See [Event quests](#event-quests) |
| Mods | Give Any Rarity Item | `game.give_any_rarity` | `MHFU_GIVE_ANY_RARITY` | Off (default): the game's own limit, Rare 1 to 3 only. On: in the Guild Hall, Start > Item > Give hands an item of any rarity to another hunter. The game's check (`0x08829928`) sees the selected item as Rare 1 for that call; its other checks stay the game's. `MHFU_TRACE_GIVE=1` prints each check |
| Mods | Change Equipment in the Guild Hall | `game.hall_equipment` | `MHFU_HALL_EQUIPMENT` | On (default) or off. Off: the item box in the Guild Hall greys out Change Equipment and refuses it, as the game does. On: it offers Change Equipment as the box at home does, online or not, except while the hunter is readied for a quest, when it is greyed out and refused again; the rest of the hall's box stays the game's. The new pieces load into a model slot the hall is not using; in a full hall, where all four are in use, they load into a block of 164 KiB taken once from free memory and kept. The other consoles keep a copy of each hunter, which the game sends again only when it refreshes it (`0x0889D6EC`), and its equipment screen does not: so when the equipment screen closes in an online hall, this hunter's entry takes the new equipment, that refresh runs, and the others load the hunter again, both those already in the hall and those who enter later. A quest started from the hall after a change has not been tested yet. The addresses are checked against the game's code before anything is wrapped, so a game whose code differs there keeps the game's box. `MHFU_TRACE_HALL_EQUIPMENT=1` prints each choice it lets through and each refresh |
| Mods | Item Sets | `game.item_sets` | `MHFU_ITEM_SETS` | On (default): the item box menu, in the village and the Guild Hall, has a seventh entry, *Item Sets*, after *Combine Items*, to register the pouch as one of 24 sets and load a set again from the box, in the game's own windows. `0` leaves the game's own six entries. See [Item sets](#item-sets) |
| Mods | Eat in the Guild Hall | `game.hall_meal` | `MHFU_HALL_MEAL` | On (default): seated at a table in the Guild Hall, △ opens your home Felyne Kitchen's menu, as *Order Food* in the house does. Off: △ does nothing at the table, as in the game. See [Eating in the Guild Hall](#eating-in-the-guild-hall) |
| Mods | Show Key Quests | `game.key_quests` | `MHFU_KEY_QUESTS` | Off (default): the game's own quest names. On: the quests that count towards the next Urgent Quest are named *[Key]* and their name, in the quest lists and wherever else the game shows a quest's name. See [Key quests](#key-quests) |
| Mods | Throwing Knives | `game.throwing_knives` | `MHFU_THROWING_KNIVES` | On (default): the Throwing Knife and its Poison, Sleeping, Paralyze and Tranq versions are items, not supply items, so they stay in the pouch after a quest and go in the box, and their descriptions lose *(Supply Item)*. The village and Guild Hall shops sell the Throwing Knife for 750z (it sells for 75z), after the Boomerang, and the combo list ends with two more combinations, Whetstone + Machalite Ore and Whetstone + Dragonite Ore, each a Throwing Knife at 90%. The coated knives are not sold; they are combined from a Throwing Knife as in the game. Off: supply items, as in the game. USA Freedom Unite only. `MHFU_TRACE_THROWING_KNIVES=1` prints each change |
| Mods | Bed Restores Stamina | `game.bed_stamina` | `MHFU_BED_STAMINA` | On (default): while a camp bed fills health, it also fills stamina below 100 back up to 100, at the same speed on the HUD (2 health and 2 stamina a frame), and from then it lasts the six minutes it does at the start of a quest before it drops again. A whole sleep fills it from any level; a sleep cut short keeps what it filled, as it does the health. More than 100, from a meal or a steak, is kept. Off: the bed restores health only, as in the game. USA Freedom Unite only. `MHFU_TRACE_BED_STAMINA=1` prints each sleep that raised it |
| Mods | Use mods | `[general] enabled` in `mods.ini` | `MHFU_NO_MODS` | On (default) or off: every mod off, the game's own files only. See [Mods](#mods) |
| Mods | A row per mod: On, Priority | `[mod <folder>] enabled`, `rank` in `mods.ini` | | Off (default) or on; a higher rank wins where two mods replace the same file |
| System | Add a timestamp to the backup name | `saves.backup_timestamp` | | On (default): each backup from *Back up saves…* is a new folder named by its time; off: plain folder names, replaced after asking |
| System | Remind me to back up after updates | `saves.backup_reminder` | `MHFU_BACKUP_REMINDER` | On (default): the first start of a new release asks you to back up your saves; see [Backups](#where-saves-live-and-how-to-back-them-up). `saves.backup_reminded` records the release that last asked |
| Network | Ad hoc play | `network.adhoc` | `MHFU_ADHOC` | Off (default) or on; off, the game reports the wireless switch as off |
| Network | Server | `network.server` | `MHFU_ADHOC_SERVER` | Host name or address of a PSP ad hoc server, optionally `host:port`; empty by default |
| Network | Nickname | `network.nickname` | `MHFU_ADHOC_NICKNAME` | The name other players see; empty uses the hunter name |

Everything applies without a restart, apart from mods that change a file's size (see [Mods](#mods)); the name settings take effect the next time the game asks for a name. The System section has *Resume*, *Take a screenshot* and *Open the screenshots folder* (see [Screenshots](#screenshots)), *Open the data folder*, *Set up game data again…* and *Quit game* (both of the last two ask first), the *Saves* rows described under [Saving and loading](#importing-a-save-from-a-psp), and the build version, the data and saves folders and the GPU. Each section but Mods has a button that restores its defaults.

The Network section also shows the connection and has the troubleshooting tools described under [Multiplayer](#multiplayer-ad-hoc). The file also keeps `network.mac`, the address other players know you by (made up the first time you go on line; `MHFU_ADHOC_MAC` overrides it), `ui.menu_hint_seen`, set once the menu has been opened (until then a hint at the bottom of the screen says how to open it during the first seconds of play), and `ui.last_folder`, where the setup's file browser opens.

The interface is drawn with [Dear ImGui](third_party/imgui/README.md). Its text uses a system font: San Francisco or Helvetica on macOS, Noto Sans, DejaVu Sans or Liberation Sans on Linux, Segoe UI on Windows, with a Japanese font merged in for file names; `MHFU_UI_FONT` names another `.ttf`. It scales with the window: about 27-pixel text on a Steam Deck's 1280×800 screen.

### Damage numbers

**Damage numbers** (Video, `game.damage_numbers`, `MHFU_DAMAGE_NUMBERS=mine` or `everyone`) are off by default. On, a hit that removes health from a monster shows that amount where the monster was hit: at the point the game itself found where the weapon or shot met the part, the point it puts the hit's spark on, read while the game works out the hit (`game_sub.ovl` `0x09C42750`). When that point is not on the monster, the number goes on the monster's bone nearest the attacking hunter's weapon hand when the hit lands, so a hit on the head shows on the head and one on the tail on the tail. A bowgun's shot or a bow's arrow shows where it struck, from the shot the monster records as the last to hit it; when no shot is recorded, an arrow shows where its flight path passed through the monster. A hit applied on another console, or one whose skeletons cannot be read, shows a little above the monster's feet instead. The number stays at that place in the world: the monster moves on without it, and only the camera moves it on the screen. It holds still for a little under a second, then fades out quickly as it rises a short way; several hits on the same spot stack upward from it and never step to the side; hits on another part start their own stack. The steps are small and the numbers may overlap a little; a larger, smaller or thicker number steps by about half its height. The closer the camera, the larger the monster on the picture and the smaller the number is drawn (down to about half its size), so it keeps to that point rather than spreading over the body. A hit off the picture is not drawn. Poison, which drains health on its own timer, shows each tick as a light purple number that rides the monster rather than staying in one place, since it is not a hit; it shows with Mine too, as a tick does not say who poisoned the monster. The place is the entity's position at `+0x200`, and the view is the perspective the world was drawn with. With frame interpolation (Video > Frame rate) each number is moved, before each present, from where the last game frame drew it towards this frame's place by the present's weight, the weight the world is blended by, so it follows the camera and rises smoothly rather than stepping 30 times a second. `MHFU_DAMAGE_NUMBERS_SMOOTH=0` draws the numbers where each game frame puts them, and `MHFU_DAMAGE_NUMBERS_FOLLOW=1` makes them ride the monster as earlier versions did.

The amount is the hit as the game computed it, read where it adds the hit to the part that was struck (`game_sub.ovl` `0x09C43558`): its raw and element parts, whether it was a critical or negative critical hit, the effective hitzone the game drew its spark from, and who attacked. The health routine then takes that much off, times the monster's own damage scale, so the number is what the health bar loses. A killing blow shows the whole hit, also when the monster had less health left. The raw and element parts follow **Damage breakdown**. A hit the game applies without computing it this way, such as a barrel bomb, or one written from the network, shows as the drop in health, white and without a breakdown; Mine hides it, since its attacker is not known. `MHFU_DAMAGE_NUMBERS_COMPUTED=0` shows only those drops, as before.

In ad hoc play every player sees every hit, their own and the others', on large and small monsters alike. Only one console works out and applies each hit: the one in charge of a large monster, whoever attacked, or the attacker's for a small one. The others receive the new health over the network, from a few milliseconds to several seconds later and often several hits at once. So the console that applies a hit sends it to the other players, with its breakdown, critical mark, attacker and point of contact, and theirs show it as soon as it arrives: about a frame and one trip through the server after it lands, even for your own hit when another console is in charge of the monster. Only hunters' hits and poison ticks on a large monster are sent. A small monster is not the same on every console: each works out its own copy, so a teammate's hit on it would show where your copy is not. The health drop that follows is counted against the hits received, and only what they do not explain is shown, as a white number, half a second after it arrives: a hit from a player on another emulator or an older version, or one the game does not compute, such as a barrel bomb. Your hits are sent whatever your own Damage numbers setting, so a player who shows none still lets the others see their hits. The hits travel as one small datagram a game frame on ad hoc port 30000 through the server, which the game itself does not use; a player without this has no socket there and receives nothing. `MHFU_DAMAGE_NUMBERS_SYNC=0` neither sends nor receives them, and every hit applied elsewhere is the drop in health again, late and white, as before. `MHFU_DAMAGE_NUMBERS_HEALTH_WATCH=0` shows only the hits this console applies.

The colour is the later games' hitzone rule, not the size of the number. Orange means the part's hitzone is 45 or more. A blade multiplies that hitzone by its sharpness (red 0.50, orange 0.75, yellow 1.00, green 1.05, blue 1.20, white 1.32, purple 1.45); a gun uses the hitzone alone. Any other known part is gray. Element is already inside the one drop, so it does not change the colour. A critical hit adds `!`, and a negative critical adds `~`. Until the part itself is known, the number is white.

### Modern sharpness gauge

**Modern sharpness gauge** (Video, `game.sharpness_gauge`, `MHFU_SHARPNESS_GAUGE=0` turns it off) is on by default. On, the colour the game already paints on the blade shortens as sharpness is used. The blade stays as the game drew it. The colours stay the ones the game drew. A fresh colour covers the blade, and one point of it takes one point of that colour off the right end. The colour shrinks toward where the blade meets the handle, so an empty colour ends there; the handle is not part of the gauge. A long sword also draws a bar under the knife; that bar ends at the sharpness that is left, and the cap at its right end moves with it.

With the gauge on, the colour also stays solid and glows. The game pulses it from nothing to a full pulse (alpha 0 to 128); the gauge keeps that pulse but moves it to alpha 128 to 192, so the colour never dims and its crest is half again as bright.

The gauge also fills a gap in the game. The game keeps a colour for the layer it paints on the blade for each sharpness, and leaves orange and yellow empty, so it paints no layer for them. Yellow's blade is the plain one the other colours tint, so it shows no colour at all, and orange's own orange blade does not pulse. With the gauge on, orange (F0 80 00) and yellow (F0 E0 00) get a layer like red, green, blue, white and purple. A colour a mod has already given them is kept.

Off, all of this is the game's own: its pulse, its empty orange and yellow, and its whole drawing.

Both lengths come from the sharpness the game itself keeps, the halfword at hunter +4338, after the game has applied its own rules: a weapon or move that costs less per hit, a guard that costs sharpness, Razor Sharp, and Handicraft's longer bar. The colour on the knife is how full the current colour is. The bar is that sharpness divided by the most the hunter can reach. The blade is the game's sprite at (60, 32). The colour on it is the sprite at (60, 29), and the bar is the 96 by 6 sprite at (66, 44). Each is told apart by the call that draws it in the weapon gauges' function `0x08841100` (the Weapon part in [UI scaling](#ui-scaling)): the bar at `0x08841204`, its right cap at `0x088411E8`, a long sword's spirit fill at `0x08841348` and the colour at `0x08841508`, so a menu or another weapon's gauge in the same place is never cut. With `MHFU_HUD_SCALE_SITES=0`, or when the game's code differs, they are told apart by those rectangles. They take the Status HUD size and hide with the HUD. A bow or a bowgun draws neither. The cuts are in the game's own draw, so a screenshot of the game's frame shows the shortened sharpness.

### Gather icon

**Gather icon** (Video, `game.gather_icon`, on unless `MHFU_GATHER_ICON=0`) draws a mark above the gather point the game's own search would accept for ○ or □. That search walks the area's list in order and keeps the first record whose height is at least 200 below the hunter and under 100 above, and whose horizontal distance is inside the record's radius. A point past that, or in another area, gets no mark. A body ○ would carve, a monster or a cut tail, gets a mark of its own: the carve table that *Hold to gather* reads (below), with the same reach. Every body in reach with something left to carve gets one, so two monsters that fell together, or a monster and the tail cut beside it, have a mark each. The carve search (EBOOT `0x0886CEA8`) stops at the first body in reach in the table's order, and ○ carves that one or, when it has nothing left, nothing at all, so only its mark has the ○ badge. The others have no badge until the bodies before them are gone or the hunter steps off them. The game's circle handler tries the gather list first, and a plant in reach is gathered there, so a body over a plant gets no mark. Mining and a bug net start only from □ with the pickaxe or the net selected; ○ at such a point goes on to carve, so a monster that falls on a mining point is marked beside it. A body with nothing left to carve, or one the hunter cannot carve now, gets no mark. A fishing spot the hunter is at gets a mark as well, whatever else is in reach. Fishing is the item action, not ○: with a bait selected (items 136 to 143), using it asks the game's spot search, and the hunter fishes when the first spot in reach is a fishing spot (type 23). That search reads the loaded area's spot table, whose address the stage overlay's own getter holds; the port decodes it from the getter's three instructions and calls nothing. A spot is in reach within 50 above or below it and inside its radius. Camp spots such as the bed and the supply box are on the same list, so a fishing spot behind one of them gets no mark, as the game would not fish there. The mark is shown whether or not a bait is selected. The Pokke Farm's fishing pier is not marked. Marks whose frames would overlap on screen are set side by side in a row at their middle, the point's first, then the carves, then the fishing spot. The mark is the interface's, so it is left out of the game's own frames, and it is hidden while the menu is open. It sits under the game's HUD: where the health bar, the map or the item bar is drawn, they cover it (`MHFU_HUD_OVER_MARKS=0` puts it back on top).

The mark fades in over four game frames (about 130 ms) and grows a little as it does, and fades out where it was when the target goes. The point's, each body's and the fishing spot's marks fade and move on their own. Moving from one target to another within 600 units glides there, half the way each game frame; a farther one is jumped to. With frame interpolation (Video > Frame rate) the interface is built once per game frame, over the newer of the two frames a present blends, so a mark pinned to the world would stay where the newer frame has it while the picture under it is still on its way, and step 30 times a second. Before each present the mark is moved to where the last game frame drew it, blended towards this frame's place by the present's weight, the weight the world is blended by. A present that shows one of the two frames as it is uses that frame's place. `MHFU_GATHER_ICON_SMOOTH=0` draws the mark where each game frame puts it.

The mark is made only of the game's own pieces, the ones its item bar is drawn with, and Pokee draws nothing of its own in it. Its frame is the hexagon the item bar draws behind the selected item: sprite 36 of the quest bar's sheet, 40 by 40 texels at (152, 32), read from the game's sprite table (EBOOT `0x089392C8`, 12 bytes a sprite, which the HUD's sprite drawer `0x0882AF70` reads). The table's entry is checked before the frame is drawn. On the frame, where the item bar puts the item's icon (4 texels in from the left and 2 from the top, 32 texels across), is the picture of what it gathers, taken from the game's own item icons and drawn in the item's colour, as the item bar draws it. With a texture pack that replaces the sheet the frame is the pack's, like the button. A piece the game has not drawn yet in this session (the sheet, the item icons, a digit) is left out until it has, so the first moments of a quest can show a mark without its frame or button. A ○ point shows the item it mostly gives: each point keeps a list of items with weights, the gather picks one by weight, and the mark shows the heaviest, so an herb point shows an herb and a mushroom point a mushroom on every map. A mining point shows the pickaxe and a bug point the bug net. A bone pile always shows the plain white bone: a point whose heaviest item is a bone husk or a bone shows that one picture, so one pile does not show a husk and the next a bone. Herb points list the grey husk as filler, which does not count. A fishing spot shows the first bait's icon (item 136). A carve shows the icon of the dead monster lying there, large or small, from the sheet the quest's monster icon uses. Anything without a picture of its own, a cut tail, a monster the sheet has no picture for, or an item without an icon, shows the game's red ? from the same item icons. A tail keeps the ? because nothing found in memory names the monster it came from, and the nearest monster can be the wrong one. `MHFU_TRACE_GATHER` prints the item each point shows and where its icon is on the atlas.

**Gather icon size** (`game.gather_icon_size`, `MHFU_GATHER_ICON_SIZE`, 100) is that mark as a percent of its usual size, any whole percent from 50 to 300, in steps of 10 in the menu. The frame, the picture, the button and the count grow together, so a larger mark keeps its proportions. It is locked while the icon is off. Versions up to v0.3.6 also had **Gather icon thickness** (`game.gather_icon_thickness`); it is gone, and a saved value is ignored.

**Hold to gather** (Video, `game.gather_hold`, on unless `MHFU_GATHER_HOLD=0`) keeps gathering while ○ is held on a plant point, carves while ○ is held on a monster body, and gathers the same way while □ is held on a mining point or a bug net. Plants and carving are ○, B on an Xbox pad; mining and a bug net are □, X on an Xbox pad. The node's kind chooses the button: 0 is ○, 3 is mining and 4 is a bug net. The game starts either when the button is new, so a hold would otherwise be a single gather or a single carve. The hunter keeps its own copy of the pad: each frame EBOOT `0x088D61CC` (called at `0x088DB474` for the local hunter) copies the pad's buttons held, new and let go to hunter `+1152`, `+1156` and `+1160`, and the stick's magnitude to `+1182`, and clears the buttons itself while the hunter may not act, as in a menu. The hunter's actions read those words: a quest's ○ is bit `0x2000` new at `+1156` (game_task `0x09AA5424`). The hold works on them, after the copy, and leaves the PSP pad alone, so menus, the map and the item bar see the pad as the player holds it. While the hold runs, the button is marked new again every six frames, the way a player mashes it. The game starts the next gather on the first press after the hunter is free and ignores the presses during the animation, so it does not matter how the gather began, from standing or from a run. The hunter's action is not read. A hold starts from a press the hunter saw: walking onto the point with the button pressed and held presses at once, but a button held through a menu, whose press the copy cleared, is not a gather until it is pressed again, and neither is one still held after △ or × made it an attack. R is no attack: it is the run with the weapon away (RB on an Xbox pad), so ○ pressed while running starts the hold. Letting go stops the presses at once. Walking off the point, or off the body, stops them. While the hold is running the hunter is shown the gather button alone and a centred stick, so a push or another button, △, × and R included, does not walk it off or start another action: the gather comes first. Another button can drop the gather button from the pad for a few frames; that is still the hold until nothing is held. Away from a matching point or body, the words are unchanged. L+○ or L+□ before a hold has started is the item bar and is left alone. A body is one of up to twenty 32-byte records at the table whose pointer is the word at `0x09A4F1A0` (records start 4 bytes in). The same height window and radius as a gather point apply, the stage halfword has to match the hunter, an empty slot is `0xFFFF`, and bit 8 of the word at hunter `+1164` leaves the body alone. `MHFU_TRACE_GATHER=1` also prints `[gather] hold` on each repeated press.

**Gather with ○** (Game, `game.gather_circle`, on unless `MHFU_GATHER_CIRCLE=0`) lets ○ mine and catch bugs. The game mines only on □ with a pickaxe selected (items 144, 145, 146 and 1225) and catches bugs only with a bug net selected (148, 149, 150 and 1226): its circle handler (game_task `0x09AA6FB0`) checks the item in the selected pouch slot, the halfword at hunter `+1608`, and ○ at such a point goes on to the carve search. With the setting on, a new ○ on a mining point or a bug point, with that tool in the pouch, is □ in the hunter's words (see *Hold to gather*) until ○ is let go, and on the frame of that press the circle handler is given the tool's slot (the one already selected when it holds one, otherwise the first) in place of the selection. The handler reads the selection only to pass it to EBOOT `0x08856884`, which returns the slot's place in the pouch: at `0x09AA6FB8` for the pickaxe check, `0x09AA72B0` for the net's, and `0x09AA7234` for the tool's id, which the gather keeps at hunter `+1684` (`0x09AA7248`). Every call that takes the tool from the pouch afterwards uses that id. Only those three calls, from those return addresses, for that hunter and that frame, get the tool's slot, so the selection itself never changes: the item bar stays on the item the player chose and does not move. If the handler's instructions differ, ○ is left as it is. *Hold to gather* repeats ○ there as on a plant, and each press is □ for the hunter; with this setting on, □ itself is never a gather on a quest, so a □ beside a mining or bug point reaches the item bar and the map as it is (off, the hold repeats □ there as before). Without the tool ○ is left alone, so it never uses another item. A body ○ would carve keeps ○ a carve, as do L+○ (the item bar) and △ or × with ○ (attacks). Only the local hunter's words are changed. At a Pokke Farm outcrop or thicket, which needs no tool, ○ is □. With *Gather icon* on, a mining or bug point's mark is the pickaxe or net ○ would use, in its own tint (the selected one, else the first in the pouch), with how many of it the pouch holds at the frame's upper right, as x3, in the game's own letters: the x and the digits as the game drew them into the atlas it draws its text from, 7 by 14 of its pixels each, as the item bar's counts are; without one it is the plain pickaxe or net, with no count. Each mark has the button that does it at the frame's lower left: ○ for a plant, a carve, and mining or bugs this setting would take, □ for a fishing spot and for mining or bugs with the tool selected, and none at a mining or bug point without the tool. It is the game's own glyph for that button, the one its ~B prompts draw: a 16 by 16 cell along the top of the quest bar's sheet (the CLUT8 texture at `0x09156BA0`; ○, ×, △ and □ from the left, traced with `MHFU_TRACE_CAPTURE` in the item box). With a [texture pack](#hd-texture-packs) that replaces the sheet, it is cut from the pack's image instead, at that image's size, so it follows the pack's *Button icons* choice (the community pack's `QuestBar_(Xbox).png`, `QuestBar_(PS).png` or `QuestBar_(N).png`); a new choice is decoded on a thread of its own and shown once ready. Until the sheet has been drawn once, there is no badge. `MHFU_TRACE_GATHER=1` prints `[gather] ○ is □ with the tool in slot` on each such press.

At the Pokke Farm the hold works the same way with □ at a mining outcrop or a bug thicket. The village has no gather list. Each frame `lobby_task` stores the interaction point the hunter stands at, and □ starts it, so the hold reads that point and presses □ while it is an outcrop (type 15) or a thicket (type 16) with uses left. The uses are the game's own count in the save's farm block, so a hold at a spent outcrop leaves □ alone. While the hunter swings, the village is busy and the hold leaves □ as it is held; once the swing ends it presses □ again at once. The other farm spots, such as the fields and the fishing pier, are left alone. The village also has no carve table, and the gather mark is not drawn there. `MHFU_TRACE_GATHER=1` prints `[gather] farm` with the point's type, index and uses left when the spot in reach changes. If the instructions that hold the point, the handler or the farm count do not match, the farm hold stays off and that difference is logged once.

The 24-byte record holds the point, the radius, an id and a kind. It does not hold how many uses are left, and the search does not read them, so a spent point that is still on the list still gets the mark. The kind chooses the button shown beside the mark; the picture is still the ? for every kind. This has not been checked in a live quest, and the village is not part of the check: the list belongs to the quest overlay. `MHFU_TRACE_GATHER=1` prints the chosen point when it changes. If the instructions that do this search do not match, the mark stays off and that difference is logged once.

### Empty item slot

**Empty item slot** (Video, `game.empty_item_slot`, on unless `MHFU_EMPTY_ITEM_SLOT=0`) is on by default. L+○ past the last item on the item bar, or L+□ before the first, stops on an empty slot instead of wrapping round. The bar shows that slot as an empty frame before the first item (after the last), and the hunter starts each quest on it. Off, the bar is the game's own: it starts on the first item and wraps from the last item to the first. While it is selected the name under the bar reads *Nothing Selected* and □ with the weapon sheathed uses nothing. The next L+○ goes to the first item and the next L+□ to the last. A pouch with a single item goes back and forth between that item and nothing. The empty slot is not one of the pouch's 24, so a full pouch keeps it, and filling the pouch while it is selected leaves *Nothing Selected* selected. With 4 items or fewer, the open bar can leave the empty frame out beside a selected item, as it leaves out items it has no room for.

The empty slot is one the game already has: a pouch slot with no item, the selection a hunter with an empty pouch starts with. The game draws no item for it, and □ finds nothing to use and starts no action. Pokee picks the last empty slot in the pouch. An item that the game later puts into that slot is then the selected item. The selected slot is the halfword at hunter `+1608`. The pouch is 24 slots of an item id and a count at GameSys `+0x6AEA6`. While the hunter holds L, `game_task` steps the selection with `0x088D81FC`, which skips empty slots and items the item table leaves off the bar. Pokee answers those two calls, for ○ and □. The HUD lays out the slots around the selection with the same step and draws a place that holds nothing (255) as a frame without an icon; Pokee answers those calls from the same round, with 255 for the empty slot. For the name, Pokee answers the HUD's item check under the bar (`0x0882693C`) and the item name (`0x08851528`) for the empty slot, with a string it keeps in the host's reserved guest memory at `0x08803F00`. At the hunter's set-up, `game_task` selects the first item (`0x09A68128`); Pokee then selects the empty slot, and puts that wrapper back each time the overlay is installed. Packing the pouch (`0x088292AC`) first moves a selection with no item to the next item; Pokee selects the empty slot again afterwards. When an item leaves the pouch, the game moves the selection off a slot with no item; the empty slot the scroll chose stays. Every other call of the step is left to the game, such as the one after a gather. Turned off in a session, the scroll is the game's own again at once. `MHFU_TRACE_ITEM_BAR=1` prints each scroll and each change of the selected slot.

### Item sets

**Item Sets** (Mods, `game.item_sets`, on unless `MHFU_ITEM_SETS=0`) adds an entry of that name to the item box menu, after *Combine Items*. It does for the pouch what the game's *Equipment Set* does for equipment, and every screen is made of the game's own windows, text, button icons and sounds. It opens a small menu beside the box menu, as *Change Equipment* does:

- **Load Item Set**: the hunter's 24 sets, *Item Set 1* to *24*, eight to a page with the game's page arrows and number (L and R turn the page), and the highlighted set's items next to them in the pouch's own window, eight to a page (left and right turn that page). Choose a set and answer *Yes*: the pouch goes into the box, then each of the set's items comes out of the box into the pouch slot it was registered in, as many as the set says, as the box has and as the pouch can carry. The message window says what the box was short of. When the box has no room for the pouch, nothing moves.
- **Register Pouch**: the same list. Choose a set to keep what the pouch holds now, slot by slot, as that set; replacing a set that has items asks first.
- **△ Delete Set** on either list empties a set after a *Yes*. Deleting a set leaves the box and the pouch as they are.

Under the list, hints in the game's markup show the choose button with *Load Set* or *Register Set*, and △ with *Delete Set*. The sounds are the game's: the cursor, choose, back and refusal, the sound of items going in and out of the box when a set is loaded, and the equipment set's when the pouch is registered. Back goes from a list to the small menu, and from there to the box menu. The sets are Pokee's, not the game's: they are kept in `item_sets.txt` in the [data folder](#installer), by hunter name, so each character has its own and the save stays as the PSP game writes it. Two characters with the same name share their sets. The box and the pouch are changed the way the game's own *Put In Item* and *Take Out Item* change them, and are written to the save the next time the game saves.

How it fits in, read off the executable and `lobby_task`: the box is 1000 slots of an item id and a count at GameSys `+0x3428`, of which 800 are in use, plus 100 for each of the two Organizer Guides (items 829 and 830) the hunter has obtained. The game's deposit (`0x08853ABC`) tops up the stacks of the item first, then fills empty slots, 99 to a stack (255 for an item whose carry byte is 255); Pokee does the same, and marks deposited items obtained as the game does. The box's screens work on a copy of the pouch at GameSys `+0x43C8` and put it back into the pouch (`+0x6AEA6`) after each change, so a set is loaded into both. The box menu is the record at `0x09B0B0A8` with six rows and the list of their text ids at `0x09B05158` (the Small Item Box's at `0x09B05168`). Pokee sets seven rows, puts the entry's id after *Combine Items* in each list (the rows after it move into the list's spare halfword), answers that id in the text call (`0x09A7B890`) with *Item Sets*, kept at `0x08803F20`, and moves the window up by 12 pixels so its bottom clears the button hints under it. The box still numbers the entry 6 and its own rows as before; Pokee moves the box's cursor (`0x0881C2E4`) over the rows as they are shown and puts the menu's highlight (`0x0882F580` from `0x09AE66F0`) on the shown row. Confirming Item Sets would leave the box waiting, as it starts nothing for that row, and draw no menu in that frame; Pokee starts the screens in that frame instead. While they are open, Pokee takes the box's frame (`0x09ADCCD0`), which reads the buttons and would otherwise take back straight to the menu: it runs the box's update (`0x09AE38F8`), with the flag that makes the update show the message of the box's rows (bit 4 of the word at ui `+1512`, Put In Item's message for a row without one) cleared, and then draws the screens after it by calling the game's own functions in turn from a return stub at `0x08800010`, with their windows and words kept in the host's reserved guest memory at `0x08802000`. The windows are built and drawn as the box builds its own (`0x0882FA24`, `0x0882F580`); the set's items are shown by the pouch's window (`0x088198E4`), as *Put In Item* shows the pouch; the message, the Yes/No and the △ hint use *Put In Item*'s message window (`0x0882FC50`, the record at `0x09B0B128`). The changes are made again each time `lobby_task` is installed. Turned off, the menu has its six rows again at once. `MHFU_TRACE_ITEM_SETS=1` prints when the entry is put in place, chosen and left, and each load, registration and deletion.

### Eating in the Guild Hall

**Eat in the Guild Hall** (Mods, `game.hall_meal`, on unless `MHFU_HALL_MEAL=0`) lets a hunter eat without going home first. Sit down at one of the hall's tables with □ and press △: the menu of your own Felyne Kitchen opens, the one *Order Food* opens in the house, with *Today's Meals*, the ingredients your chefs offer, *Meal A* and *Meal B*, and the price. Choosing a meal pays for it and serves it as the kitchen does: the same effects, the same chance of each chef's kitchen skill, the chefs' levels, and the game's messages. The chefs' cooking scene does not play, and the hunter stays seated. The game's rule stays: one meal until the next quest, wherever it was eaten. △ again, or △ before the kitchen is open, plays the refusal. Backing out of the menu costs nothing. □ stands up again as before. While a meal can be ordered, the bar at the top left that says *:Stand Up* at a seat gets a second row under it, *:Order Food* in the game's own words, drawn as the game draws the first: its bar, the HUD's own △, blinking with the first row's button, and the text.

The meal is the game's own: Pokee runs the kitchen's menu (`0x09C1CC38` in `game_sub`) in place of the seated hunter's frame (`0x09AAC0A8` in `lobby_task`) while it is open, and once it is paid, the calls the kitchen's scenes make for the flavour, the skills, the meal's effects and the messages, and the flag that the hunter has eaten (GameSys `+0x69936`). Those are written to the save as a meal at home is. The second row runs the calls the bar makes for its first one (`0x08838B08` for the bar, `0x08838E0C` for the button, which shows it while the bar's blink is below 20, and the plain print) one row lower, after the bar's own button. The addresses are checked against the game's code before anything is wrapped. `MHFU_TRACE_HALL_MEAL=1` prints when the menu opens and closes and when a meal is served or refused. See `game/hall_meal.hpp` for what was traced.

### Key quests

**Show Key Quests** (Mods, `game.key_quests`, off unless `MHFU_KEY_QUESTS=1`) marks the key quests: on each level of the Village Chief's, Nekoht's and the Guild's quests, the few quests that must be cleared before that level's Urgent Quest is offered. Their names read *[Key]* and the name, for example *[Key] Battle of the Blos*, in the quest lists, the quest's details and wherever else the game shows the name, in every language. The other quests, the Urgent Quests themselves and training, treasure and event quests keep their names. A change applies to a list opened after it, and to one already open once it scrolls.

The game knows which quests are key: each quest's record in the quest packs (`DATA.BIN` entries 5342 to 5352) has bit `0x04` set in its fourth byte, and bit `0x02` on the Urgent Quest. The game loads a pack unchanged and reads a quest's name through offsets in it each time it draws the name. When the game has read a pack, Pokee finds it in memory, byte for byte as on the disc (or as a mod changed it), and points the names of its key quests at copies with the mark, kept in a block of guest memory of its own. Turning it off puts the game's offsets back. `MHFU_TRACE_KEY_QUESTS=1` prints each pack found and how many names were marked. See `game/key_quests.hpp` for what was traced.

### Fourth pouch page

**Fourth pouch page** (on unless `MHFU_POUCH_PAGE=0`) lets a hunter who owns it carry 32 items instead of 24, in a fourth page of eight in the pouch's windows, on the item bar, at the box and wherever else the game reads the pouch. The page is an item, the **Lg Backpack**: once the hunter reaches High Rank, at HR 4 in the Guild or with the Village Chief's 7★ quests open, the village General Store and the Guild Hall's store sell it for 100,000z, next to the Backpacker Guide (last in a list without the Guides). The village shopkeeper has news of it, once per hunter, as she has of her own new stock: her news marker shows when the hunter enters the village, and the next time she would only greet the hunter she tells of the Lg Backpack instead. That she has told it is written to `pouch_page.txt` at once, so loading the save again without saving does not bring the news back. Bought, it leaves the pouch or the box at once and the hunter has the page from then on; more than one bought into the box is paid back. The store greys it out at once, as it does the Organizer and Backpacker Guides a hunter has bought, and keeps it so, its pane showing the pouch holding 1 in red, the most there can be; the full-pouch message says 32 kinds of item. `MHFU_POUCH_PAGE_GRANT=1` gives every hunter the page, for testing. Without the page nothing changes.

How the store fits in, read off the executable and `lobby_task` and checked on a save at HR 9 with every village level open against a new hunter: Hunter Rank is the byte at GameSys `+0x44BC`, HR minus 1, which the Guild's quest counter reads per level (`0x09A88938`). A quest is cleared when its bit is set in the words at GameSys `+0x4428`, the bit being the quest id's place in the table at `0x0899C1C8` (`0x0884FE80`); the village counter (`0x09A87780`) opens 7★ once both of the Chief's 6★ Urgent Quests (10501 and 10509) or a later Urgent are cleared. The item is the unused id 929, given the Backpacker Guide's record at the new price; its name and description are entries of the game's item text tables pointed at strings in the host's reserved memory. The stores' stock is a list of item ids that `0x09A7FD50` picks by the store's type and progress from eight lists at `0x09B070A8` (the village) and five at `0x09B070D0` (the Guild Hall); from High Rank on those pointers lead to copies with it next to the Backpacker Guide. The store tells the Guides apart (`0x09A80230`) and greys one out once obtained (`0x0885C5CC`); Pokee answers the list's call of the first yes for the Lg Backpack and the second with whether the hunter owns the page; the list is built when the store opens, so on a purchase Pokee greys the item's row out in it as well (rows of 16 bytes from the store's `+0x6C`, greyed at `+8`). The pane beside the list counts a Guide as held once obtained, but asks inside its own unit too, so Pokee answers its call of the pouch's count (`0x0885AED0`, from `0x09A832B0`) with 1 for an owned Lg Backpack. The purchase asks inside its own unit, so it adds the item as any other, and Pokee counts as bought only what arrives while the item is offered: a save that already holds the unused id keeps it. The full-pouch message is `lobby_task` text 492, pointed at the host's own words the same way; the store's prompts to buy (491 and 495) stay its own, since they show the whole time the list is open. A talk asks the NPC type's lines for the line to begin with (`0x09A95D90`); the village store's (`0x09A98F20`) are its news, once per stage of village progress, or line 0, its greeting, and when checked as the village is entered, news sets the NPC's byte at `+0x484`, its marker. Pokee sets that byte while the news is due and answers the text of line 0 (`0x09A9F520`) with the news the first time a talk with the village store begins with it.

The game keeps four arrays of 24 slots, each with other data right after it: the pouch at GameSys `+0x6AEA6`, the pouch as saved at GameSys `+0x43C8` (which the box screens work on and hunter set-up copies into the pouch), the pouch kept aside while the Training School lends its own items at GameSys `+0x6ADFC`, and a copy in the local hunter at `+0x568`. Slot 24, just past the pouch, is a 25th the bow keeps its coatings in. None can grow in place, so a hunter with the page gets four arrays of 32 in the host's reserved guest memory at `0x08803000`, and the 197 instructions that reach them, listed in `config/site_hooks.txt`, are changed where they run: the recompiler (`psp_recomp --site-hooks`) has the generated code call the host after each of them while the page is on, and the host makes a loop over 24 slots go over 32, a count of 24 a count of 32, and an address of a slot the block's. The bow's slot becomes slot 32 and stays where the game keeps it. The executable, `lobby_task`, `game_task`, `arcade_task` (the Training School), and the item thieves `em20` and `em59` have sites; after editing the list, regenerate the executable and those overlays. Each frame the first 24 slots of each block are copied into the game's arrays, so a save holds them as on a PSP, and a change the game made to its own array (a save loaded, an item set) is taken into the block slot by slot. The page itself and whether the hunter owns it are kept by hunter name in `pouch_page.txt` in the data directory, written when the game saves, so quitting without saving keeps what was saved. Empty item slot, gather with ○, the gather icon and Give at any rarity read the pouch through the same blocks; item sets still hold 24 slots and leave the fourth page as it is. `MHFU_TRACE_POUCH_PAGE=1` prints the hunter's page as it is read and kept, and each site the first time it runs.

## Game text

The game draws its text with the PSP's system font, which lives in the console's flash and is not on the disc, so Pokee draws those glyphs from a font on your computer. *Font* in the menu's Video page lists the installed fonts that have every Latin letter, digit and punctuation mark, marked *Japanese* when they also have the kana and kanji the game still shows. Characters a font lacks come from the default font: Hiragino Sans on macOS, Noto Sans CJK on Linux and the Steam Deck (the `fonts-noto-cjk` package or its equivalent; a release falls back to the copy it ships), MS Gothic or Meiryo on Windows. To use a font that is not installed, put its `.ttf`, `.otf`, `.ttc` or `.otc` file into the `fonts` folder of the per-user directory (*Open the fonts folder* in the same section); those are listed first. A preview line under the setting shows the choice the way the game draws it.

A change applies at once: Pokee makes the game draw every character again the next time it shows it, so text already on screen changes within a frame or two.

How the text is laid out, as traced with `MHFU_TRACE_FONT=1`: the game sizes a glyph cell in a texture atlas from the font's maximum glyph size, renders each glyph into a 20×20 buffer and copies that whole buffer into the cell, and draws text as one sprite per cell, half a character wide for Latin letters and full width for Japanese ones. Pokee reports a 20×20 maximum so cells and buffer match, and fits every glyph inside its cell with a pixel of margin, shifting it and, when it is too large, scaling it down, so no font can spill into a neighbour or lose its edges. The size of the text is therefore fixed by the game; *Weight* is the adjustment that fits within it.

### Sharper text and 2D textures

Above ×1 the 3D world gets sharper with the internal resolution, but the game's own interface is made of textures sized for the PSP's 480×272 screen, so it was only magnified: blurred by bilinear filtering, or blocky with *Texture filter* on Sharp. Two settings change how it is drawn; neither changes anything the game reads, so its layout stays exactly as it was (the positions, sizes and texture coordinates of every draw are the same, checked draw by draw with `MHFU_TRACE_SPRITES`), and at ×1 both do nothing.

- **Sharp text** (Video → Font section, `text.crisp`, `MHFU_CRISP_TEXT`, off by default) draws the game's text again at the internal resolution, up to 4×. The game renders each glyph into a 20×20 buffer, copies it into a cell of its glyph atlas and draws text from those cells; the renderer draws the atlas pages again, cell by cell, from the same glyphs at the same pen positions, through the page's own palette. Before a page is used, every cell is drawn again at 1× and compared with the game's: a page where more than a quarter of them differ is left as it is (`[ui] glyph page ... differ` on the console).
- **UI textures** (Video, `video.ui_textures`, `MHFU_UI_TEXTURES`, `off` by default) for the rest of the 2D interface, meaning through-mode draws only; 3D is never touched:
  - *Sharp bilinear* (`sharp`): each texel keeps its colour across the pixels it covers and blends with its neighbour only over the last pixel at its edge, at any scale, whole or not. Done in the fragment shader; it costs nothing to keep.
  - *MMPX* (`mmpx`): each 2D texture is doubled once (×2, ×3) or twice (×4 and above) with [MMPX](https://jcgt.org/published/0010/02/04/), a pixel-art upscaler that keeps the exact palette, transparency and single pixels and rounds diagonals, curves and corners, then drawn with sharp bilinear. The copies are made on a separate CPU thread the first time a texture is drawn at that scale, and kept on the GPU (at most 48 of them and 64 MiB, the least recently drawn going first); until a copy is ready the original is drawn.
- An [HD texture pack](#hd-texture-packs)'s image wins over both: a texture the pack has an image for is drawn from the pack, and no copy is made of it. Packs do replace the 2D interface's textures: with the community pack for this game, the loading screen, the title pictures and the small menu pieces it has images for were drawn from the pack, and the atlas of buttons and window frames is one the pack itself marks to be left alone (`ignore/buttons`).

Sharp text was on by default in v0.6.5 and v0.6.6 and is off by default since, on every platform (issues #210 and #212). Those two versions wrote `text.crisp=1` into every `settings.ini` they saved, whether or not the player turned it on, so the value cannot tell a choice from the default: a file without `settings.version` (the versions up to v0.6.6) has its `text.crisp` dropped once, and Sharp text is off until it is turned on again. Empty item slot was off by default through `settings.version=2` and is on by default since; a file below version 3 has its `game.empty_item_slot` dropped once the same way. Give Any Rarity Item was on by default in v0.3.6 (`settings.version=3`) and is off by default since; a file below version 4 has its `game.give_any_rarity` dropped once. Every file saved now carries `settings.version=4`, and from then on all three are what the player chose.

`MHFU_TRACE_UI=1` logs each copy made, with its size and the milliseconds it took. See [Cost](#cost-of-the-sharper-ui) for what they cost.

#### Cost of the sharper UI

Measured on an M1 (MoltenVK) at ×5 with the village's start menu open for 45 seconds, one run at a time (`MHFU_PERF=log`):

| | Off (as main) | Sharp text | + Sharp bilinear | + MMPX |
| --- | --- | --- | --- | --- |
| GPU per frame | 8.02 ms | 8.09 ms | 8.15 ms | 8.14 ms |
| Render (CPU) per frame | 4.60 ms | 4.06 ms | 4.20 ms | 4.00 ms |

The steady cost is within the noise of the measurement: sharp bilinear adds a few instructions to the 2D interface's pixels only. What costs is making the copies, once per texture and scale: a glyph atlas page took 3.3 ms on average (8 ms at most) at ×4, and it is drawn again when the game adds glyphs to it; an MMPX copy took 6 ms on average and 16 ms at most (a 512×512 texture doubled to 1024×1024), made on a thread of its own so that no frame waits for it (the original is drawn meanwhile). The copies take at most 64 MiB of GPU memory together; a texture larger than 256 texels is only ever doubled once. Phones were not measured: expect the one-time costs to be several times higher there, and use *Sharp text* and *Sharp bilinear*, which cost next to nothing, if MMPX's copies arrive late.

## HD texture packs

Pokee loads HD texture packs made for PPSSPP, in its `textures.ini` format, without conversion. None is included or downloaded: install one from the menu (below), or copy the pack's folder, the one that holds `textures.ini`, to `textures/ULUS10391` in the [per-user directory](#installer) yourself:

| System | Pack folder |
| --- | --- |
| macOS | `~/Library/Application Support/Pokee/MHFU/textures/ULUS10391/` |
| Linux, Steam Deck | `~/.local/share/Pokee/MHFU/textures/ULUS10391/` |
| Flatpak | `~/.var/app/io.github.teamgdb.Pokee/data/Pokee/MHFU/textures/ULUS10391/` (a copy, not a link to a folder outside the sandbox) |
| Windows | `%APPDATA%\Pokee\MHFU\textures\ULUS10391\` |

*Open the textures folder* in the menu's Video section opens `textures`. A pack made for another release, such as Portable 3rd (`ULJM05800`), works only if its `textures.ini` lists `ULUS10391` under `[games]`; the import installs such a pack as `ULUS10391` by itself. *Texture pack* in the Video section turns the pack on and off while the game runs; off draws exactly the game's own textures again. The footer under that setting shows how many textures the pack lists, *No pack in …* with the folder Pokee looked in, or *Pack folder missing: …* when a pack used in place has moved.

### Button icons

Some packs come with button prompts for other controllers. The community MHFU pack draws the PSP's ○ ✕ △ □ by default and also has Xbox, PlayStation and Nintendo versions of the images that show buttons, named like the image they stand in for with `(Xbox)`, `(PS)` or `(N)` before `.png`: `QuestBar_fre(Xbox).png` for `QuestBar_fre.png`, and `QuestBar_(Xbox).png` for `QuestBar.png`. Pokee finds them anywhere in the pack, so nothing has to be renamed. *Button icons* in the Video section, under *Texture pack*, appears for such a pack:

- **Auto** (the default) follows the controller in use: Xbox letters for an Xbox pad or one SDL does not name, PlayStation shapes for a DualShock or DualSense, Nintendo letters for a Switch pad, and the pack's default with no pad. With *Confirm button* on the bottom button, ○ is on A, which none of the sets shows, so Auto keeps the pack's default.
- **Pack default**, **Xbox**, **PlayStation** or **Nintendo** draws that set whatever the controller.

A change applies within a frame or two, and so does plugging in another kind of controller with Auto. A set the pack does not have draws the pack's default. The footer shows the set drawn now. The naming is that pack's own, not part of PPSSPP's format; images of a pack named otherwise are drawn as `textures.ini` names them.

### Importing a texture pack

The in-game menu does it (Esc, or L3+R3 on a gamepad; Video section, under *Texture pack*), with a gamepad alone or with the keyboard and mouse:

1. Unpack the pack if it came as a `.zip`: zipped packs are not read.
2. Choose *Import texture pack…*. The file browser lists folders and starts in Downloads. Opening a folder that holds `textures.ini` chooses it; *Import from this folder* chooses the folder shown. Dropping the folder on the window chooses it too. The pack is found in any of these layouts:
   - the pack folder itself, the one holding `textures.ini`;
   - a folder holding `textures/ULUS10391/` (a copy of another Pokee data folder) or `ULUS10391/` (a `TEXTURES` folder);
   - PPSSPP's memory stick, or its `PSP` folder: `PSP/TEXTURES/ULUS10391/`;
   - a pack folder named for another release, such as `ULJM05800`, whose `textures.ini` lists `ULUS10391` under `[games]`, chosen itself or found in any of the places above. It is installed as `ULUS10391`.
3. The pack is checked, then shown beside the pack in use now. Nothing is copied until you choose. The review shows the pack's folder, its number of texture keys and hash, the number of image files and the size of the whole folder, and how many images `textures.ini` names that are not in the folder (those textures keep the game's own look). A pack is refused, with the reason, when its `textures.ini` cannot be read, names no hash, when it is `textures.zip`, or when it is named for another release and does not list `ULUS10391`.
4. Choose how to install it:
   - **Copy into Pokee's data folder** (*Copy and replace* when a pack is installed): copies the pack to `textures/ULUS10391`, leaving hidden files such as `.DS_Store` out. It needs the pack's size plus 64 MB free in the data folder and is refused otherwise. The copy runs in the background with a progress bar; the game keeps running (or stays paused) and the menu stays open until it ends. *Cancel* stops it at once.
   - **Use it where it is**: copies nothing and reads the pack from its folder from then on (`video.texture_pack_folder` in `settings.ini`), which saves the space of a large pack. The folder must stay where it is; if it moves, the Texture pack row says *Pack folder missing* and no pack is drawn. *Stop using the pack folder* goes back to the pack in the data folder.
5. The pack is turned on and applies at once: textures already on screen change within a frame or two, and the Texture pack row shows the new count.

**Nothing is deleted.** A copy goes to a hidden `textures/.incomplete-<date>_<time>` folder first; the pack in use stays in place until the copy is complete, so a cancelled or failed copy leaves it as it was (and removes only its own partial copy). Once the copy is complete, the pack it replaces is moved to `textures/.backup/<date>_<time>/ULUS10391/`, for example `.backup/2026-09-23_19-05-12/ULUS10391/`; import that folder to go back to it. A pack used in place is never moved or changed. `MHFU_TEXTURE_PACK` set to a folder still decides which pack is read for the run; the review says so.

How it works:

- **Keys.** Each texture is hashed once, when the renderer first uploads it, never per draw: `xxh64`, `xxh32` or the older `quick` over its bytes in guest memory, with the dimensions and, for paletted textures, the palette's hash in the key, as the pack's `[options]` choose. `ignoreAddress`, `reduceHash`, `[hashranges]`, `[reducehashranges]`, `[filtering]`, `[games]`, the wildcard keys that leave the address, palette or data hash out, empty entries that keep a texture as it is, and images in the pack's top folder named by their key are all honoured, except that `ignoreAddress` and `reduceHash` are ignored with `quick`, as PPSSPP ignores them. Zipped packs (`textures.zip`) and DDS, KTX2 and ZIM images are not supported; PNG is.
- **Loading.** Images are read and decoded on background threads; the original texture is drawn until its replacement is on the GPU, usually a frame or two after it first appears. Each image is uploaded with a full set of mip levels, so a large image stays smooth at a distance. Its size does not matter: texture coordinates address the whole texture, so a 4× image covers exactly what the original covered, 2D screens included. A file name a pack spells in another case than the file on disk is still found, which matters on Linux.
- **Memory.** Images on the GPU are kept within a budget (`MHFU_TEXTURE_PACK_MEMORY`): a quarter of a discrete GPU's own memory, at least 1 GiB and at most 8 GiB (6 GiB on a 24 GB card), and 1 GiB on an integrated GPU, which shares its memory with the system. When a new scene needs more, the images drawn least recently are dropped and loaded again when they are next drawn. The village with a full pack needs about 250 MB.
- **Loader threads.** A quarter of the CPU's hardware threads, from one to six, decode the pack's images (`MHFU_TEXTURE_PACK_THREADS`): one or two up to 11 hardware threads, six on a 32-thread desktop CPU. The log names both when the pack is loaded: `[texpack] 5120 textures from …; 6 loader threads, 6144 MB of GPU memory`.

`MHFU_TRACE_TEXTURE_PACK=1` logs each texture's key and what the pack does with it, each image decoded and uploaded, and once a second how many draws used a replacement. To make a pack, `MHFU_TEXTURE_DUMP=<folder>` writes every texture the game uploads, once, as a PNG named by its key; the folder gets a `textures.ini` that makes it a pack as it is, and edited images in it replace the game's.

## Mods

Pokee reads mods in the format the game's community already uses with its mod loader and mod manager, so a mod made for them works without changes. None is included, downloaded or linked here: install mods you downloaded yourself. Each mod is a folder in `mods` in the [per-user directory](#installer) (`~/Library/Application Support/Pokee/MHFU/mods` on macOS, `~/.local/share/Pokee/MHFU/mods` on Linux, `%APPDATA%\Pokee\MHFU\mods` on Windows):

```
mods/
  my_armour/        a mod in the mod manager's format
    mod.ini
    preview.png     optional, shown in the menu
    armour.pac
  some_files/       files named by the file id they replace, as in the community loader's files folder
    0601
    0602P           a patch for file 0602
```

A `mod.ini` looks like this:

```ini
[MOD INFO]
Name="My armour"
Author="Me"
Type="File"
Files="armour.pac;helm.pac"
Target="0601;05D2"
Description="First line.\Second line."
```

A **file id** is the index of a file in the game's `DATA.BIN`, in four hex digits ([DATA_BIN.md](../../docs/DATA_BIN.md#file-ids-and-mods)). What each `Type` does here:

| Type | Here |
| --- | --- |
| `File` | Replaces each `Target` file with the matching one of `Files`. A replacement may be larger than the original |
| `Patch` | Applies each of `Files` to its `Target` file as the game loads it; see below |
| `Pack` | Turns the mods named in `ModList` (their folder names) on and off together |
| `PseudoPack` | The `File` and `Patch` parts in its `SubModList` sections, as one mod |
| `Equip<type>`, `EquipSET`, `EquipCATSET` | A model you point at a piece of equipment: its screen in the menu asks for the file id each file replaces |
| `Code` | Listed, but cannot be turned on: code mods run their own PSP code inside the game, which recompiled code does not allow yet (#81) |

`Version`, `FilesHD` and `TargetHD` come from the Portable 3rd mod manager, which had a PSP release and an HD release with different file ids. Freedom Unite has one archive, so a file or patch mod is read from `Files` and `Target`, with or without `Version`. It can be turned on when every target is a file in this game's `DATA.BIN`. A target past the archive is refused, and an HD file list is left unused; the mod's screen says so. `Depends` names mods that are turned on with it. `Priority`, `Animation`, `Audio` and `Script` are read but do nothing here; the mod's screen says so.

**Patches.** A patch file is a list of blocks, each a 32-bit address, a 32-bit length and that many bytes, ending with the address `FFFFFFFF` (the format's last 8 bytes, `FFFFFFFF00000000`, which the mod manager strips when it installs a patch; either form works). A block for an address inside a code overlay changes the overlay's bytes as the game loads it, which is the same as changing them in memory right after the load: the game copies an overlay to its load address unchanged. A block for other memory is written right after that overlay has loaded, at the instruction cache flush the game makes after copying code. A block whose address is smaller than the file is an offset into the file, for files that load at no fixed address. A block marked to run as code (the top bit of its length) is skipped, and a patch that changes an overlay's code makes that overlay run in the interpreter; both are reported.

**Several mods on one file.** Where two mods that are on replace the same file, the one higher in the list wins. Patches apply on top of the file the game gets, the winning replacement included, lowest priority first. The mod loader refuses a file that is both replaced and patched; Pokee applies both, because patches are small edits at known places and a replacement usually keeps them where they were, and it lists the combination under *Conflicts* so it is easy to see which mod to turn off if it is not.

**The Mods page** of the menu lists the mods, highest priority first, with whether each is on. A mod's own screen shows its preview, author, type, the release it was made for, what it changes and its description, and has its switch, its place in the list (left and right move it) and, for equipment mods, the file id each file replaces. Those ids are typed by hand. The hunter shows the mod once the game loads those models again: at the item box, change to other armor and back, or restart. [EQUIPMENT_MODS.md](../../docs/EQUIPMENT_MODS.md) says what an equipment mod replaces. *Import mod…* copies a mod you downloaded and unpacked into the mods folder, turned off: choose its folder, the one with `mod.ini`, or a folder that holds several mods, in the file browser, or drop the folder on the window. A mod already installed under the same folder name moves to `mods/.backup` first; nothing is deleted. Unpack `.zip`, `.rar` and `.7z` archives before importing. *Open the mods folder*, *Read the folder again* (after adding or removing folders by hand) and *Use mods*, which turns every mod off for a clean comparison, are on the same page.

**When a change applies.** The game reads `DATA.BIN`'s directory once, at start. A change that keeps every file's size as the directory has it applies the next time the game loads each file: usually the next area, menu or piece of equipment shown; files the game loads once at start, such as the menus' textures, need a restart. A change that makes a file larger, or smaller where the directory has its exact size, applies at the next start; the page says so and offers *Restart now*.

The choices are kept in `mods.ini` next to `settings.ini`: `[general] enabled`, and per mod `enabled`, `rank` (higher wins) and the chosen targets of an equipment mod.

How it works: every file of the game comes from `DATA.BIN`, and the file I/O reads it for the game. The game reads its directory once, a table of where each file starts and a table of exact sizes, and then reads each file from its first block to exactly the size given, trusting the tables alone. With mods on, the file I/O serves an archive with the mods' files in it: a mod's file is encrypted for its place in the archive (the obfuscation is keyed by the block a file starts at), a file that no longer fits its blocks grows, every file after it moves up and is re-keyed on the way, and the directory says the same. With no mod on, not a byte changes. `host/mods/` holds the machinery, which knows no game (the mods folder, `mod.ini`, choices, conflicts, import, a file's bytes with mods), and the `mhfu_*` files this game's archive and format.

`MHFU_TRACE_MODS=1` logs what the mods change, each read they serve (`[mods] read 0FEE +0 131072 of 628736 bytes: replaced by …`) and each write made after an overlay loads; problems with a mod are logged whether it is set or not. `MHFU_TRACE_DATA_BIN=1` logs the id of every file the game reads while a mod is on: change the equipment on screen and the new ids are the files to target.

### Layered armor

**Layered armor** (Mods → Layered armor, `look.layered_armor`) draws your hunter in other armor than it wears, part by part, while everything else keeps the real armor: defense, resistances, skills, the Equipment screen, the save and what the game sends to other players. It is off by default, and off it changes nothing at all.

The page has the switch and a row per part (head, chest, arms, waist, legs). Each row opens a list: *Real equipment*, *Nothing* (the bare part, or the inner wear where the game shows it), then the pieces the hunter wears or keeps in the equipment box, under the names the game uses. A piece that hunter does not own, or that its sex cannot wear, shows the real armor. The choices are kept in `settings.ini`, not in the game's save, so the save stays exactly what a PSP or another emulator expects. They apply to whichever character is loaded. A choice can also be written there directly: `look.layered_chest=12` is piece id 12, `0` is nothing, `real` is the piece worn. An id the loaded hunter does not own is ignored. An older `look.layered_all` value in `settings.ini` is ignored too.

Turning the switch off or on releases the armor files once the hunter is loaded, and the game's own next load draws that look. Changing a piece in the equipment box does the same for the piece that changed: with the switch on, that change reads the chosen piece, and with it off, the piece just equipped. Standing still, before that load, does not rebuild the hunter. Defense and the save stay the real pieces. The display flip does not start the hunter's load: replacing the model crashed the quest, and loading one part from the flip froze the picture. A walk from the village to the Guild Hall, and the redraw after the switch was turned off and on, were not watched.

**Other players** receive your real armor. Layered armor changes what this game draws, and a quest does not keep your hunter in one fixed record, so every hunter it draws uses the chosen look. [LAYERED_ARMOR.md](../../docs/LAYERED_ARMOR.md) says what was traced and how. `MHFU_TRACE_LAYERED_ARMOR=1` logs each part it draws differently.

## Saving and loading

The game saves through the PSP's save-data utility, which the host implements. Saves live where a PSP keeps them, under the directory that backs `ms0:`: `ms0` in the [per-user data directory](#installer) for an installation, or `ms0` in the game directory when the game runs from one (`profiles/mhfu/game`, `MHFU_GAME_DIR` or the `game_dir` argument):

```text
game/ms0/PSP/SAVEDATA/ULUS10391/
    PARAM.SFO      titles, the file list and the hashes that protect the save
    MHP2NDG.BIN     the game data, encrypted
    ICON0.PNG      icon shown in the PSP's save list
    PIC1.PNG       background shown in the PSP's save list
```

`MHP2NDG.BIN` is encrypted and `PARAM.SFO` hashed exactly as the PSP's save-data utility does it, with the key the game supplies, so a folder can move between this port and a PSP's memory stick unchanged. The one field that cannot be reproduced is a hash made with a key unique to each PSP; the port writes a placeholder there. Copying a save made here onto a real PSP has not been tried yet.

Nothing is drawn for the save-data or message dialogs yet ([#33](https://github.com/TeamGDB/Yakumo/issues/33)): the game's own screens ask where to save and show the result, and the system dialogs answer as if the player confirmed them. The log shows each request and each message, for example `[savedata] AUTOSAVE (1) game="ULUS10391" …` followed by `[savedata] saved 1183744 bytes to …`.

### Importing a save from a PSP

The in-game menu does it (Esc, or L3+R3 on a gamepad; System section, *Saves*):

1. Copy the save to this machine, or connect the memory stick: on a PSP's memory stick the folder of *Monster Hunter Freedom Unite* is `PSP/SAVEDATA/ULUS10391`, with downloaded quests in `ULUS10391QST`. PPSSPP keeps the same folders in its `memstick/PSP/SAVEDATA`.
2. Choose *Import save…*. The file browser lists folders. Opening a save folder chooses it; *Import from this folder* chooses the folder shown, and every save in it, in its `SAVEDATA` or in its `PSP/SAVEDATA` is found, so a memory stick's root or a `SAVEDATA` folder holding several games works too.
3. Each save found is checked, then shown with its date and size beside the save it would replace. Nothing is copied until you choose *Import* (or *Replace and import*).
4. The game reads its saves at the title screen. *Restart now* closes the game and starts it again there; progress since your last save is lost. If you keep playing instead, do not save before restarting, or the game writes its own data over the imported save.

**Checks.** A folder is imported only when its `PARAM.SFO` names one of this game's folders (`ULUS10391`, `ULUS10391QST`, `ULUS10391DAT`), its own hashes match, and every data file it lists is present, matches its hash and decrypts with the key the game passes to the save-data utility. Other games' saves are left out (and counted), and a damaged one is refused with the reason. The key is taken from the game's own first save-data request, at boot; until then, saves cannot be checked and the menu says to try again at the title screen. A save stored without encryption, as early PPSSPP versions wrote them, has nothing to check and is imported as it is.

**Nothing is deleted.** A save that an import replaces is moved to `ms0/PSP/SAVEDATA/.backup/<date>_<time>/<folder>/`, for example `.backup/2026-09-19_19-05-12/ULUS10391/`; import that folder to go back to it. `.zip` files are not read: unpack them first.

By hand, the same works with the game closed: copy the folder into `ms0/PSP/SAVEDATA/` in the directory above (for example `~/.local/share/Pokee/MHFU/ms0/PSP/SAVEDATA/`), after keeping a copy of any folder of the same name, which holds all three character slots. *Open the saves folder* in the menu shows that folder.

### Exporting and backing up

- **Export save…** copies the game data and the downloaded quests to a folder you choose, as `MHFU saves <date>_<time>/PSP/SAVEDATA/ULUS10391…`, the layout of a memory stick: copy its `PSP` folder to the root of a PSP's memory stick, or import it on another machine. It always makes a new folder.
- **Back up saves…** copies every save folder of the game, the install data included, to the backups folder, `save-backups` in the [per-user data directory](#installer), or to a folder you choose. With *Add a timestamp to the backup name* (on by default; `saves.backup_timestamp` in `settings.ini`) each backup is a new folder named by its time, holding the save folders: `save-backups/2026-09-19_19-05-12/ULUS10391/`. With it off, the save folders go straight into the chosen folder (`save-backups/ULUS10391/`), and an earlier backup there is replaced only after you confirm. *Open the backups folder* shows it. To restore a backup, import it.
- **The reminder.** The first time a new release starts and finds saves, it pauses the game for a moment and asks you to back them up. *Back up now* makes a new timestamped folder in `save-backups` (on Android, in a folder you pick), whatever the timestamp setting says, so it never replaces an earlier backup; the next screen says where the backup went and offers to open that folder. *Continue without a backup* starts the game. Either way the reminder does not come back until the next release. The import's review asks the same before it writes anything, with its own *Back up now*. Turn the reminder off with *Remind me to back up after updates*.

### Where saves live and how to back them up

The saves are the folders in `ms0/PSP/SAVEDATA/` of the data directory: `ULUS10391` holds all three character slots, `ULUS10391QST` the downloaded quests, and `ULUS10391DAT` the install data, which the game can rebuild.

**Install data.** The game's *Data Install* copies most of `DATA.BIN` (about 600 MB) into `ULUS10391DAT`, and the game then reads those parts from the memory stick. Pokee needs no such copy: the game finds a complete install there as it starts, served from `DATA.BIN` as the [mods](#mods) leave it, so it runs as installed from the first start and mods still change what it loads. The game only reads an install while its own *Options → Data Install* is *ON*, which only its *Data Install* turns on, so each time the game loads the save Pokee sets that option *ON* (logged as `[install] Data Install option turned on`); the game keeps it in the save the next time it saves. With it, armor and weapon previews load by themselves, as on a PSP with the install. `MHFU_INSTALL_OPTION=0` leaves the option as the save has it. Nothing is written for the install itself, and the title menu leaves out *Data Install*, which has nothing left to do: the cursor goes from *Download* back to *New Game* (`MHFU_TRACE_TITLE_MENU=1` logs each move it changes). To use the game's own install instead, set `game.virtual_install=0` in `settings.ini` (or `MHFU_VIRTUAL_INSTALL=0`); from the next start the title menu has *Data Install* again. A `ULUS10391DAT` folder from a PSP or from an earlier version is left alone while the install is served.

| Platform | Saves folder |
| --- | --- |
| Windows | `%APPDATA%\Pokee\MHFU\ms0\PSP\SAVEDATA\` |
| macOS | `~/Library/Application Support/Pokee/MHFU/ms0/PSP/SAVEDATA/` |
| Linux and Steam Deck, release tarball | `~/.local/share/Pokee/MHFU/ms0/PSP/SAVEDATA/` (under `$XDG_DATA_HOME` when it is set) |
| Linux and Steam Deck, Flatpak | `~/.var/app/io.github.teamgdb.Pokee/data/Pokee/MHFU/ms0/PSP/SAVEDATA/` |
| Android | The app's private storage, which other apps and a computer cannot reach: use *Back up saves…* or *Export save…*, which copy to a folder you pick in Android's picker |
| A developer build | `ms0/` of `MHFU_DATA_DIR`, `MHFU_GAME_DIR` or `profiles/mhfu/game`, whichever the game runs from; the menu's *About* shows it as *Saves folder* |

To back up:

- **From the game:** *System → Back up saves…*, or *Back up now* when the reminder asks. Backups go to `save-backups/` beside `ms0/` (Android: a folder you pick). *Open the backups folder* shows them.
- **By hand:** with the game closed, copy the whole `SAVEDATA` folder somewhere else. Copying while the game runs can catch a save half written.
- **To restore:** *Import save…* from the backup's folder: the save it replaces is itself kept in `ms0/PSP/SAVEDATA/.backup/`. By hand, with the game closed, copy the save folders back into `ms0/PSP/SAVEDATA/`.

Pokee never deletes a save: an import moves the save it replaces to `ms0/PSP/SAVEDATA/.backup/<date>_<time>/`, and a backup only adds folders. The game's own *Delete previously saved character?* question, when a new character goes into a used slot, is the game's and replaces that slot as it would on a PSP.

### Downloadable content

The game keeps downloaded quests and equipment in the `ULUS10391QST` save folder and reads it through the save-data utility, like an ordinary save (AUTOLOAD of `ULUS10391QST` / `MHP2NDG.BIN`). The download servers are long gone, so the in-game download mode's network side stays unimplemented. To use DLC you already have, import your `ULUS10391QST` folder from the menu (or copy it into `ms0/PSP/SAVEDATA/`), then open the game's download menu to install the quests. The project does not host, bundle or link to DLC files.

This release (`ULUS-10391`) keeps the save in `ULUS10391`, with the game data in `MHP2NDG.BIN`. A folder written by a PSP running that release passes the checks with the key the game supplies. A save made here has not yet been loaded on a PSP.

To check a save folder without starting the game — for example one that the game reports as corrupted — run the save-data test program on it:

```bash
out/mhfu/bin/mhfu_savedata_tests --check profiles/mhfu/game/ms0/PSP/SAVEDATA/ULUS10391 MHP2NDG.BIN <key>
```

`<key>` is the 32-digit key the game passes to the save-data utility; a run with `MHFU_TRACE_SAVEDATA=1` prints it as `key=` on every request for the game's own save. The program reports whether the hashes in `PARAM.SFO` and the data file's hash match and whether the file decrypts. Without arguments it runs the self-tests, which need no game data.

### Event quests

The Guild Hall's event quests and the Training School's challenges come from the save: the game keeps six downloaded quests there, a challenge being one with a flag that keeps it out of the event list. Pokee also lists the quests in `event_quests/` of the data directory, after the save's, and they start like the save's own: event quests in the Guild Hall, challenges (ids from 61000) in the Training School, which has room for 14. The save is not changed, and a quest whose id the save holds too is the save's.

Mods → Event quests imports them. Choose a folder of quest files, or drop it on the window: each decoded quest file in it or in the folders directly inside it, whatever its name, and FUComplete's quest tables (`EventQstTbl`, `ArenaQstTbl`, files 6594 and 6598 of its `DATA.BIN`), are read. The page lists what it found and what it leaves out before anything is copied: quests only Japan's Portable 2nd G had, quests that are neither event quests nor challenges, and `.mib` files still encrypted as a PSP keeps them. Each quest goes into `event_quests/` as `m<id>.mib`, replacing a different quest of that id. The folder is read once a run, the first time the Guild Hall or the Training School loads, so quests imported after that are listed from the next start. FUComplete's quests carry English text only, and the game shows it in every language. The project does not host, bundle or link to quest files.

Only the lists and quests started offline have been tested. Posting a quest from the folder to other hunters online is not supported yet. The Guild Hall's getter and lookup (`0x09A88470`, `0x09A88500` in `lobby_task`) are called from inside their own code, so `lobby_task` is recompiled to dispatch those calls (`config/overlay_dispatch.txt`); with a library built without that, the list stays the game's. The Training School's getter (`0x09A6E2B8` in `arcade_task`) is called from another unit and needs no such list; its code is checked before it is replaced. `MHFU_TRACE_EVENT_QUESTS=1` logs the folder read, each list and each lookup. See `game/event_quests.hpp` for what was traced.

## Multiplayer (ad hoc)

The PSP game plays together through ad hoc wireless: up to four consoles in the same room. Pokee carries that over a network through a **PSP ad hoc server**. One player can host a session from the game itself, with the server built in (see [Play together locally](#play-together-locally)), or everyone can use a public server that PSP and PPSSPP players use, so you can hunt with other Pokee players and, as the protocol is the same, with players on PPSSPP (not tested yet). Nothing has to be forwarded on your router for a public server: all game traffic goes through the server.

### Play together locally

On one home network, or over a VPN such as Tailscale or ZeroTier, one player hosts and the others join; nothing else needs to be installed and nothing needs to be forwarded.

1. **Host.** Open the menu (Esc, or L3+R3), go to **Network** and choose **Host a session**. The server starts inside the game, and the screen lists the addresses the others can use, one per network (for example `192.168.1.20  Local network (en0)` and `100.101.102.103  Tailscale`). Selecting one copies it. Below them are the players connected to your session and the hall each is in; **Stop hosting** ends the session for everyone.
2. **Join.** The others open **Network** too. Under **Join a session** they see the sessions hosted on their network, with the host's name and player count, and choose **Join**. On a VPN that carries no broadcast, such as Tailscale or most WireGuard setups, the host's session is not listed by itself: type the host's VPN address into **Address** and press Enter. Joined addresses are remembered under the same heading.
3. **Play.** Everyone closes the menu, goes to the Gathering Hall entrance in Pokke Village and chooses **Online Gathering Hall**, as with any server. A hall holds four hunters: with more, set the game's own *Online Gathering Hall* option to MANUAL and everyone chooses the same hall number.

Hosting and joining turn **Ad hoc play** on and fill in **Server** (the host's own game uses its server on `127.0.0.1`). Joining another session, or hosting, while you are in a hall takes you out of it, as a dropped connection would. While you play together, opening the menu does not pause the game unless *Pause during multiplayer* is on (System section), so the others keep hearing from you.

**Ports.** The built-in server listens on every network interface on TCP **27312** (matchmaking) and **27313** (relay); a host answers address checks on UDP 27312 and announces itself to UDP **27314** on the local network, by broadcast and on the multicast group 239.255.27.14. On a LAN or a VPN nothing needs to be forwarded, though a firewall on the host may ask to let Pokee accept connections: allow it (on Windows, for private networks). To host over the plain internet, forward TCP 27312 and 27313 on the host's router to the host, and give the others your public address; a VPN is usually simpler. `network.host_port` in `settings.ini` (or `MHFU_ADHOC_HOST_PORT`) moves the server to another port pair, for example when another server already uses 27312; others then join `address:port`.

**PPSSPP players** should be able to join a session hosted in Pokee, since the server speaks the same protocols (not tested yet): in PPSSPP, set the ad hoc server to the host's address and use the relay (*AemuPostoffice*) data mode.

**Joining from the command line.** `Pokee --join [address]` starts the game already set to join the session hosted at `address` (`host` or `host:port`), with **Ad hoc play** on, for that run only: `settings.ini` keeps its own server, and the address is not added to the recent ones. Without an address it joins the session hosted on this machine, `127.0.0.1`; add the port when the host moved it (`--join 127.0.0.1:27400`). A run that joins this machine takes a made-up player address for the run, since a second instance with the same data folder would otherwise have the host's address, and the server would take the two for one player; `MHFU_ADHOC_MAC` still chooses one. Then choose **Online Gathering Hall** in Pokke Village as usual. Both instances use the same saves unless the second has its own `--data-dir` (see [Two instances on one machine](#two-instances-on-one-machine)): saving in both writes the same file.

**A server without the game.** `Pokee --adhoc-server [port]` runs the same server alone in a terminal, announced on the local network, and prints the addresses to join; Ctrl+C stops it. It needs neither game data nor a window.

A server has two parts, both over TCP: the matchmaking service on port **27312**, which knows who is in which gathering hall, and a relay on port **27313**, which carries the game's own traffic between the players. Pokee needs both, so pick a server that runs the relay (servers list it as *AemuPostoffice* data mode).

### Setting it up

1. Open the menu (Esc, or L3+R3) and go to **Network**.
2. Turn **Ad hoc play** on.
3. Enter the **Server**: a host name or IP address, `host:port` if its matchmaking port is not 27312. There is no default; see [Choosing a server](#choosing-a-server).
4. Optionally set a **Nickname**; otherwise the other players see your hunter name.
5. Close the menu. In Pokke Village, go to the Gathering Hall entrance and choose **Online Gathering Hall** (✕). With the game's *Online Gathering Hall* option on MANUAL you then choose a hall: everyone who picks the same hall number on the same server meets there.

Server and nickname changes apply the next time the game goes on line: leave the hall and enter it again. Turning ad hoc play off while in a hall takes you out of it, as if the connection dropped. The address other players know you by (`network.mac` in `settings.ini`) is made up once and kept.

Everyone in a hall must play the same game: this release is USA Freedom Unite (`ULUS-10391`), so players of that PSP version on PPSSPP can join.

### Choosing a server

PPSSPP's list of public ad hoc servers is in its [`assets/adhoc-servers.json`](https://github.com/hrydgard/ppsspp/blob/master/assets/adhoc-servers.json); its entries say which games each server's community plays and which data mode it runs. Choose one that runs the relay and whose players play Monster Hunter, near you if you can, and agree on it with the people you want to play with. Each server has a status page (usually on port 8888) that shows who is on line in which game.

Public servers are run by volunteers. Pokee keeps one connection to the matchmaking service and one to the relay per game socket, and pings the matchmaking service every two seconds, like the other clients.

### Running your own server

The simplest is **Host a session** in the game, or `Pokee --adhoc-server` (see [Play together locally](#play-together-locally)). You can also run [aemu_postoffice](https://github.com/Kethen/aemu_postoffice), the server most public servers use. It is a separate program under its own licence; nothing of it is part of Pokee.

Natively, on macOS or Linux (a C++20 compiler is all it needs):

```bash
git clone --recursive https://github.com/Kethen/aemu_postoffice
cd aemu_postoffice/server_cpp
bash build_linux.sh
./aemu_postoffice          # config.json and game_db.json must be next to it
```

In a container (Docker or Podman), from the same `aemu_postoffice` checkout:

```bash
docker run --rm -it -p 27312:27312 -p 27313:27313 -p 8888:8888 \
  -v "$PWD":/src -w /src/server_cpp debian:stable \
  bash -c 'apt-get update && apt-get install -y g++ && bash build_linux.sh && ./aemu_postoffice'
```

Then use `127.0.0.1` as the server on the same machine, or the machine's LAN address on the others. Open TCP 27312 and 27313 in its firewall for other machines. The server log shows every login, group join and relay session, and `http://<server>:8888/` lists who is on line.

### Two instances on one machine

Each instance needs its own settings (for its own address and nickname) and its own copy of the save:

```bash
# once: a game directory per instance with its own save
mkdir -p ~/pokee-b/ms0/PSP/SAVEDATA
ln -s /path/to/disc.iso ~/pokee-b/disc.iso
ln -s /path/to/EBOOT.ELF ~/pokee-b/EBOOT.ELF
cp -R profiles/mhfu/game/ms0/PSP/SAVEDATA/ULUS10391 ~/pokee-b/ms0/PSP/SAVEDATA/

# each instance: its own data directory, window title, server and nickname
MHFU_DATA_DIR=~/pokee-a-data MHFU_WINDOW_TITLE="Pokee A" MHFU_ADHOC=1 \
  MHFU_ADHOC_SERVER=127.0.0.1 MHFU_ADHOC_NICKNAME=HunterA out/mhfu/bin/Pokee profiles/mhfu/game
MHFU_DATA_DIR=~/pokee-b-data MHFU_WINDOW_TITLE="Pokee B" MHFU_ADHOC=1 \
  MHFU_ADHOC_SERVER=127.0.0.1 MHFU_ADHOC_NICKNAME=HunterB out/mhfu/bin/Pokee ~/pokee-b
```

Two characters from one save are fine in one hall, since the game tells players apart by their address, and each instance makes up its own.

### When something goes wrong

The menu's **Network** section shows what the connection is doing, updated live:

| Row | Shows |
| --- | --- |
| Connection | Off line, connecting, reconnecting (with the attempt and the last error), or on line with how long and the server connection's round trip |
| Server | While you host: its ports, uptime, players, halls, relay connections and streams, and what it has relayed and dropped |
| Discovery | Whether this instance listens for hosted sessions on UDP 27314 and how many it hears, and while you host, on how many interfaces it announces yours and how many address checks it answered |
| One row per host | Each session heard on the network: its address, players, game and when it was last heard |
| You | Your address and nickname |
| Group | The hall's ad hoc group, as the game names it (`MHP2Q0` and two more characters), and how many players are in it, or that it is being rejoined after a dropped connection |
| One row per player | Their address and how long ago their last packet arrived |
| One row per socket | Each ad hoc socket the game has open: its kind (PDP datagrams, PTP streams), port, state and peer |
| Relay links | How many of the sockets' relay connections are up |
| Per second, Since start | Packets and bytes in and out |
| Problems | Datagrams dropped, calls that timed out, reconnections |

Below it:

- **Network overlay** shows the connection, the group, its players and the traffic in the top-right corner while you play. `MHFU_ADHOC_OVERLAY=1` turns it on at start.
- **Log every call and packet** is the same as `MHFU_TRACE_ADHOC=1`: every ad hoc call the game makes, with its arguments and result, and every packet header goes to the console and to the network log.
- **Save network log** writes the recent network log and the section's state to `logs/adhoc-<date>-<time>.log` in the data folder (menu: System, *Open the data folder*). Attach it to a problem report, ideally with the log turned on before the problem happens.
- **Reconnect now** drops the server connection and connects again; the hall is joined again. **Disconnect** leaves the hall as if the other players were lost. The game reacts to both as to a real dropped connection.

Common problems:

- *The game says the wireless switch is off*: ad hoc play is off in the menu.
- *Join a session lists nothing*: the host is on a VPN without broadcast (type its address), on another network, or a firewall blocks UDP 27314 on your side. **Discovery** says whether this instance listens.
- *Host a session says the port is in use*: another server runs on this machine; stop it, or set `network.host_port`.
- *Connecting never finishes*: the server name is wrong, the server is down, or a firewall blocks TCP 27312. The console says `cannot reach the ad hoc server` or `cannot resolve`.
- *You are in a hall but see nobody*: the other player is on another server, in another hall, or playing a game the server does not group with this one. The server's status page shows where everyone is.
- *Players see each other but a quest cannot be joined or the hall drops*: the server has no relay (TCP 27313), or it is blocked. **Relay links** stays below its total.
- *The connection drops in a quest*: when the server connection comes back within ten seconds, the hall is rejoined; the quest itself usually ends, as it would on a PSP. After ten seconds the game is told the connection is lost.

### How it works

The game uses the PSP's ad hoc libraries (`sceNetAdhocctl`, `sceNetAdhoc`, and the network configuration dialog `sceUtilityNetconf`). Entering the Online Gathering Hall, it scans for halls, then asks the network dialog to join the hall's group (`MHP2Q0`, or `MHP2A0` in a mode not yet traced, and two more characters; the executable writes the prefix at `0x08899874`, and which hall each name is has not been traced); Pokee joins it on the server and the dialog finishes when the server confirms. In the hall every console broadcasts its state over PDP (datagrams on port 10000), which Pokee sends through the relay to each player in the group. A quest is a PTP stream: the host listens on port 20001 and each joining player connects to it, also through the relay.

One network thread owns every connection to the server, so the game never waits for the network except where a PSP call itself blocks, and then no longer than the call's own timeout. During ad hoc play the game keeps running behind the menu by default (see [In-game menu](#in-game-menu)); if it is paused, the connection stays up, but the other players stop hearing from the game until the menu closes.

## Configuration

The settings a player needs are in the [in-game menu](#in-game-menu). Environment variables remain for everything else, and override the menu's settings for the run where they overlap.

### Game and paths

| Variable | Default | Effect |
| --- | --- | --- |
| `MHFU_GAME_DIR` | unset | Directory holding `EBOOT.ELF`, `disc.iso` and `ms0/` (the saves); skips the per-user directory |
| `MHFU_DATA_DIR` | SDL's preference path | Per-user data directory the installer fills, with the saves in its `ms0/` |
| `MHFU_PORTABLE` | unset | `1`: keep the data in `data` next to the executable, as `portable.txt` does ([Portable copy](#portable-copy)); `0`: ignore `portable.txt` and `data` there |
| `MHFU_OVERLAY_DIR` | `overlays/` next to the executable (`Contents/Frameworks/overlays` in the macOS app) | Directory of overlay libraries |
| `MHFU_MODS_DIR` | `mods/` in the data directory | The [mods](#mods) folder |
| `MHFU_NO_MODS` | off | `1`: no mod applies this run, whatever `mods.ini` says; the menu still lists them |
| `MHFU_VIRTUAL_INSTALL` | on (`game.virtual_install`) | `0`: the game reads its own `ULUS10391DAT`, which *Data Install* writes, instead of the [install data](#where-saves-live-and-how-to-back-them-up) served from `DATA.BIN`, and the title menu shows *Data Install* again. Read at start |
| `MHFU_INSTALL_OPTION` | on | `0`: while the [install data](#where-saves-live-and-how-to-back-them-up) is served, the game's *Options → Data Install* stays as the save has it; otherwise Pokee turns it on each time the save is loaded, so the game uses the install. Read at start |
| `MHFU_EVENT_QUESTS` | on (`game.event_quests`) | `0`: the Guild Hall and the Training School list only the save's quests, not those in [`event_quests/`](#event-quests) |
| `MHFU_BACKUP_REMINDER` | unset | `0`: never ask to back up the saves this run; `1`: ask at every start, even in a scripted run or with the setting off. Unset, the reminder follows the menu setting and skips runs driven by `MHFU_INPUT_SCRIPT`, `MHFU_INPUT_LIVE` or `MHFU_AUTO_CONFIRM` |
| `MHFU_FONT` | a system CJK font | Font to draw the game's text with: a `.ttf`, `.otf`, `.ttc` or `.otc` file, with `#N` after the path for the Nth face of a collection. Glyphs it lacks come from the default, a Japanese system font (Hiragino on macOS, Noto Sans CJK on Linux, MS Gothic or Meiryo on Windows; inside a Flatpak, the host's Noto Sans CJK under `/run/host/fonts`), and last the font a release ships in `fonts/` next to the executable |
| `MHFU_UI_FONT` | a system font | TrueType font for Pokee's menu and setup screens |

### Video

| Variable | Default | Effect |
| --- | --- | --- |
| `MHFU_INTERNAL_SCALE` | `2` | Render resolution as a multiple of 480×272, or `auto` for the window's size (menu: Resolution) |
| `MHFU_NO_RENDER` | off | Run without a window; the installer shows no dialogs either. Emulated time is not held to real time |
| `MHFU_WINDOW_TITLE` | `Pokee` | Title of the game window, to tell instances apart |
| `MHFU_UNTHROTTLED` | off | Let emulated time run ahead of real time, so the game runs as fast as it can be drawn (menu: Game speed). The window is updated about 30 times a second, so Vsync does not hold it back, and the sound is muted |
| `MHFU_FAST_LOADING` | on | `0` keeps loads at the PSP's pace (menu: Fast loading). See [Fast loading](#fast-loading) |
| `MHFU_FAST_FORWARD` | `hold` | `toggle` makes the Fast-forward key a switch; `off` (or `0`) makes it do nothing (menu: Fast-forward). See [Fast-forward](#fast-forward) |
| `MHFU_FAST_FORWARD_SPEED` | `3` | Times real time while fast-forwarding, 2 to 8 (menu: Fast-forward speed) |
| `MHFU_FIELD_OF_VIEW` | `50` | Vertical degrees of the 3D view on a hunt, 30 to 90 (menu: Field of view). See [Field of view](#field-of-view) |
| `MHFU_FRAME_RATE` | `30` | `45`, `60`, `90`, `120` or `display`: present frames in between the game's 30 (menu: Frame rate). See [Frame rate](#frame-rate) |
| `MHFU_FRAME_RATE_AUTO` | on | `0` keeps the chosen frame rate even when the game falls behind (menu: Lower when behind) |
| `MHFU_NO_MATERIAL_COLOR` | off | Leave unlit geometry without vertex colours white instead of taking the material colour |
| `MHFU_NO_LIGHTING` | off | Draw lit geometry with the flat white stand-in used before lighting existed, and without fog, to compare a scene with and without them |
| `MHFU_NO_FOG` | off | Turn fog off and keep lighting |
| `MHFU_NO_FB_TEXTURES` | off | Decode every texture from guest memory, as before, instead of sampling the render target when the game textures from a framebuffer it drew, and stop writing framebuffers back to guest memory for the shown frame and for GE block transfers |
| `MHFU_TEXTURE_PACK` | unset | `0` turns the [HD texture pack](#hd-texture-packs) off, `1` on; a folder path loads the pack from that folder instead, ahead of an imported one (menu: Texture pack) |
| `MHFU_TEXTURE_PACK_BUTTONS` | `auto` | `pack`, `xbox`, `playstation` or `nintendo`: the pack's [button icon set](#button-icons) for this run (menu: Button icons) |
| `MHFU_TEXTURE_PACK_MEMORY` | a quarter of a discrete GPU's memory, 1024 to 8192; 1024 on an integrated GPU | Megabytes of GPU memory for texture pack images, at least 64; the least recently drawn are dropped above it |
| `MHFU_TEXTURE_PACK_THREADS` | a quarter of the hardware threads, 1 to 6 | Threads decoding texture pack images, 1 to 16; `2` is the most there were before |
| `MHFU_TEXTURE_DUMP` | unset | Write every texture the game uploads, once, as a PNG named by its texture pack key into this folder, to start a pack from |
| `MHFU_PICTURE_BESIDE_CUTOUT` | off | Put the picture beside a display cutout, moved off the cutout's side, as before [#170](https://github.com/TeamGDB/Yakumo/issues/170), instead of centring it. On Android, where the app has no environment, `adb shell setprop debug.pokee.picture_beside_cutout 1` does the same for the next start. Nothing changes on a screen without a cutout |
| `MHFU_CRISP_TEXT` | on | `0` draws the game's text as before, magnified from its 20-pixel glyphs (menu: Sharp text). See [Sharper text and 2D textures](#sharper-text-and-2d-textures) |
| `MHFU_UI_TEXTURES` | `off` | `sharp` or `mmpx`: how the 2D interface's textures are drawn above ×1 (menu: UI textures) |
| `MHFU_NO_SPRITE_CLAMP` | off | Let 2D tiles sample outside their own texels, as before; above ×1 this shows faint lines along the tile edges of 2D screens |
| `MHFU_SCREENSHOT_DIR` | unset | Write BMP frames into this directory |
| `MHFU_SCREENSHOT_EVERY` | `60` | Frames between screenshots |
| `MHFU_PERF` | off | `1` shows the performance overlay and logs frame statistics once per second; `log` only logs them; `compact` shows the one-line overlay and `compact+log` logs as well (menu: Performance). See [Performance statistics](#performance-statistics) |
| `MHFU_NO_CACHED_READBACK` | off | Keep the frame written back to guest memory in the first host-visible memory type, as before, instead of a host-cached one; on a Steam Deck the CPU copy out of it then takes ~3 ms a frame instead of a fraction of one |
| `MHFU_NO_CACHED_VERTICES` | off | Keep the vertex buffer in the first host-visible memory type, as before, instead of a host-cached one. A rainy area writes about 125 MiB of vertices a frame, and the uncached copy of that took long enough to run the game at half speed |
| `MHFU_NO_VERTEX_CACHE` | off | Decode every draw's vertices again each frame, as before, instead of reusing a span whose guest bytes and bones have not changed |
| `MHFU_NO_VERTEX_HOLD` | off | Copy those reused vertices into the per-frame buffer every frame, as before. Otherwise a span that has not changed is copied once into a buffer of 384 MiB (192 MiB on a device with less than 8 GiB of host-visible memory, or one that refuses the larger allocation) and drawn from there. One that did not fit is copied each frame until a later frame has room |
| `MHFU_NO_VERTEX_HOLD_RECLAIM` | off | Leave a span's bytes in that buffer after it stops being drawn, as the first version of the hold did. Otherwise the space returns once the frames still reading it have finished, so the next zone reuses it |
| `MHFU_OLD_VERTEX_CACHE` | off | Drop every cached span not drawn in the last three frames once a second, clear the whole cache when it passes 192 MiB, and keep a 192 MiB hold, as before. Otherwise spans stay cached until the cache or the hold is full, and then the least recently drawn give way, so an area walked back into or a building the camera turns back to is not decoded again; the memory of dropped spans is reused for the next ones |
| `MHFU_NO_MISS_TO_HOLD` | off | Convert a draw the vertex cache missed into memory of its own, copy it into the frame's buffer and then into the hold, as before. Otherwise a transformed draw without bone weights is converted once, straight into the hold, drawn from there in the same frame, and keeps no other copy; the first frame of a new area then writes its vertices once instead of three times. Implied by `MHFU_OLD_VERTEX_CACHE` |
| `MHFU_NO_DIRECT_VERTICES` | off | Expand every draw into a plain triangle list on the CPU, as before, instead of writing a transformed draw's decoded vertices once with an index list |
| `MHFU_NO_LOOKUP_CACHE` | off | Look every draw's pipeline and texture up in the renderer's caches, as before, instead of reusing the previous draw's and what the display list already resolved |
| `MHFU_NO_BUFFER_REUSE` | off | Allocate the texture decoder's working buffers, the staging buffer and command buffer of each texture upload, and the pixels of a framebuffer read back for a block transfer every time, as before, instead of keeping them for the next use |
| `MHFU_NO_DRAW_MERGE` | off | Record every draw with all of its state, as before, instead of setting only the state that changed and merging consecutive transformed draws with identical state into one draw call |
| `MHFU_NO_GPU_TIMESTAMPS` | off | Do not time the GPU with timestamp queries; the perf line reads `gpu n/a` |
| `MHFU_FRAMES_IN_FLIGHT` | `2` | `1` waits for the GPU to finish each frame before the next is recorded, as before, instead of recording a frame while the GPU draws the one before. The frame written back to guest memory reaches it at the same flip either way |
| `MHFU_SYNC_UPLOADS` | off | Copy each new texture with a command buffer of its own and wait for the queue to go idle, and wait again before destroying an evicted one, as before, instead of copying it ahead of the frame's commands in the same submission and destroying evicted ones once their frames have finished |
| `MHFU_NO_PIPELINE_CACHE` | off | Create every pipeline without the cache kept in `pipeline_cache.bin` |
| `MHFU_NO_ALPHA_VARIANTS` | off | Use the one fragment shader that tests alpha for every draw, as before, instead of pipelines without the test (and so without `discard`) for draws that have none |
| `MHFU_NO_CLEAR_LOAD` | off | Load the target's colour and depth at the start of every render pass, as before, even when the pass begins with a clear that writes all of them |
| `MHFU_NO_FAST_DECODE` | off | Decode every vertex with the general loop, as before, instead of a loop specialised for the run's formats. `MHFU_CHECK_DECODE` runs both and reports runs whose vertices differ |
| `MHFU_GPU_DECODE` | off | `1` hands the guest's own vertex bytes of every transformed draw to the vertex shader, which decodes and skins them, instead of decoding and skinning them on the CPU. Off by default: on the Steam Deck (radv) it hung the GPU (`ring gfx timeout`) at the character select screen. Through-mode draws, sprites and morphing vertices are decoded on the CPU either way |
| `MHFU_CHECK_GPU_DECODE` | off | Turns GPU vertex decode on and has the vertex shader write what it decoded to a buffer, decodes every checked draw on the CPU too, and compares them after the frame; a pair of `[gpu-decode-check]` lines every 300 frames, one for unskinned and one for skinned draws, counts vertices equal bit for bit, within rounding (skinned positions, whose bone terms the GPU sums with its own rounding) and different, and the first ten different vertices of each kind are printed. Each frame compares up to 32768 skinned vertices in the order drawn and 32768 unskinned ones from a window that moves through the frame from one frame to the next, so a busy scene's characters and all of its scenery are covered. Needs `vertexPipelineStoresAndAtomics` |
| `MHFU_NO_ROBUST_BUFFERS` | off | Turns off `robustBufferAccess`, which the renderer enables when the device offers it, so that a shader reading past a buffer gets zeros instead of whatever memory lies there |
| `MHFU_GPU_BREADCRUMBS` | off | For a GPU hang (`VK_ERROR_DEVICE_LOST`, or `amdgpu ... ring gfx timeout` in the kernel log): writes numbered markers into the command stream with `VK_AMD_buffer_marker`, and when the device is lost prints the last marker the GPU finished, the last it reached and the last queued, with what each one was (frame begin, render pass, uploads, replay, copy to the window). Needs a device with that extension (radv, AMD on Windows) |
| `MHFU_NO_TIGHT_MERGE` | off | Starts every draw's vertices on a 16-byte boundary, as before, which keeps about half the draws that could merge from merging |
| `MHFU_SYNC_TEXTURE_DECODE` | off | Decodes each new texture on the spot, as before, instead of copying its bytes and palette when it is drawn and decoding it on background threads until the frame is submitted |
| `MHFU_NO_FAST_TEXTURE_DECODE` | off | Decodes textures texel by texel with every palette entry read from guest memory again, as before. `MHFU_CHECK_TEXTURE_DECODE` decodes every texture both ways and compares them |
| `MHFU_NO_PIPELINE_PREWARM` | off | Makes each pipeline when a draw first needs it, instead of making the last run's pipelines (`pipeline_keys.bin`) on a background thread from the start |
| `MHFU_TRACE_RENDER` | off | A `[render-split]` line each second: the render thread's milliseconds per game frame running display lists (parsing, vertex decode, the renderer's handling of each draw with its texture and command recording) and on interpolation, replays, presents and the write-back. Timed with the CPU's own counter, so the frame barely changes |
| `MHFU_NO_FAST_STORE` | off | Convert the frame written back to guest memory pixel by pixel, as before, instead of a row at a time |
| `MHFU_TEXTURE_CACHE_LIMIT` | `1024` | Keep at most this many decoded textures on the GPU; a small number tests eviction |
| `MHFU_GPU_COMPAT` | `auto` | `1` or `on` forces [GPU compatibility mode](#gpu-compatibility-mode) on, `0` or `off` keeps it off even when the start-up self-test fails (menu: GPU compatibility) |
| `MHFU_NO_GPU_SELFTEST` | off | Skips the start-up self-test of the GE's pipelines |
| `MHFU_GPU_SELFTEST` | unset | `fail` counts the self-test's result with the specialized fragment shader as wrong, so Auto turns GPU compatibility mode on (with the setting Off, the retry with the plain shader runs); `fail-all` counts every result as wrong, so the note over the game shows. For testing the fallbacks on any GPU |
| `MHFU_DESCRIPTOR_POOL_SETS` | per pool | Makes every descriptor pool hold this many sets (textures 512, the lighting set 4, pre-rotation 8). Pools are counted exactly as declared, the way phone drivers count them, and a full one makes the next; a small number (`16`) exercises that growth on any GPU, and a `[render] texture sets: pool N …` line says each time |
| `MHFU_DESCRIPTOR_POOL_LIMIT` | per kind | At most this many pools of each kind (textures 32, the lighting set 4, pre-rotation 8). With a small `MHFU_DESCRIPTOR_POOL_SETS` it makes sets run out, to test that a texture, a sharper copy or a render-target copy that gets no set is drawn without it instead of stopping the game |
| `MHFU_GPU_FAIL_PIPELINES` | off | Refuses every pipeline with the specialized fragment shader, as a driver that cannot build one would, so the fallback to the plain shader (and under Auto, GPU compatibility mode) runs on any GPU. `play` refuses them only once the start-up self-test is done, as a driver that fails on a pipeline first needed in play would |
| `MHFU_MOLTENVK_ASYNC_SUBMITS` | off | macOS: `1` lets MoltenVK turn each submitted frame into Metal commands on a thread of its own instead of the game's, saving 1–2 ms of the game's thread a frame. Off by default: it crashed after minutes of play in v0.6.0-alpha.4. MoltenVK's own `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` does the same |

### GPU compatibility mode

Phones with MediaTek Helio chips and older Mali or PowerVR drivers showed a black screen with sound (issues #159 and #169). The renderer now checks the GPU before it relies on it and falls back where it can:

- At start the log gets `[gpu]` lines: the GPU, the driver (`r32p1` for a Mali driver), the limits, features and formats the renderer uses, the memory types and the surface's formats and capabilities. On Android they go to logcat as well. A GPU without something the renderer needs is named in the error instead of failing later; on Android the error is shown in a message box. Compressed texture formats are not needed: the game's DXT textures are decoded on the CPU.
- A self-test (`[gpu-selftest]`) draws a small picture with the game's own pipelines (alpha test, depth test, blending) and reads it back. If it comes out wrong, or the driver refuses one of its pipelines, Auto turns GPU compatibility mode on (below) and runs it again; with the setting Off, it is tried again with the fragment shader built without its specialization constant, which is then used for everything.
- A pipeline the driver refuses is logged with its `VkResult`, then retried without the pipeline cache and with the plain fragment shader.
- **GPU compatibility mode** (Video → GPU compatibility, `video.gpu_compat`) leaves out what old drivers are suspected of getting wrong: the specialization constant, robust buffer access, the pipeline cache and the background pipelines, render passes that skip loading the target, GPU timestamps, and a second frame in flight. The picture is the same, but it costs speed, most of all the single frame in flight, which keeps the CPU and the GPU from working on two frames at once.
  - **Auto**, the default, starts every GPU without it and turns it on only when the driver shows it is needed: when the start-up self-test draws wrong or the driver refuses one of its pipelines, and when a pipeline is refused in play. At start everything is left out except robust buffer access, which the device was made with; in play, from the next frame on, everything but that and the second frame in flight. The log says `[gpu-compat] on (Auto: …)`, and Video → GPU compatibility shows why.
  - v0.6.5 and v0.6.6 turned it on from the start for every Mali driver older than r38 and every PowerVR driver, which most MediaTek phones have; they lost speed for it (issues #210, #212), while the black screen it was meant for (#159, #169) very likely came from a descriptor pool running out, which the growing pools below fixed. The log still names such a driver: `[gpu-compat] off (Auto: …; Mali driver r32p1 is older than r38, which v0.6.5 and v0.6.6 started in it)`.
  - **On** leaves everything out from the start, robust buffer access included; **Off** never turns it on (a wrong self-test picture still switches to the plain fragment shader).
- If the picture is still known to be wrong, a note over the game says so and where to send the log, until the menu is opened. A lost GPU on Android is reported in a message box before the app closes.
- Descriptor pools are counted exactly as declared, the way phone drivers count them, and a full one makes the next instead of failing: a texture that still gets no descriptor set is drawn without it (white, or the original instead of a sharper copy) rather than stopping the game. Only the sets the renderer cannot draw anything without (the lighting set, the white texture, pre-rotation) stop it, with the pools and the driver's answer in the error. v0.6.0-alpha.5 stopped at start on a Mali phone with `vkAllocateDescriptorSets failed with VkResult -1000069000` (VK_ERROR_OUT_OF_POOL_MEMORY).
- When the Android app cannot go on (the renderer cannot start, the GPU is lost), its message box names the error, the GPU and driver (`Mali-G57 MC2, driver r32p1, Vulkan 1.1.177`), the phone and Android version, and the Pokee version, so a screenshot is a report. Its *Save the log…* button saves the logs through the document picker, as the menu's does, before the app closes. The log is flushed first and also kept as `pokee-fatal.log`, which later starts leave alone (`pokee.log` becomes `pokee-previous.log` at the next start and is gone at the one after); the menu's *Save the log…* includes it.

### Picture shape and size

**Aspect ratio** (Video) decides how the game's picture meets the window:

- **Original** keeps the PSP's 480×272 shape and adds black bars. This is the picture as it always was.
- **Stretch** stretches that picture over the whole window.
- **Fill** gives the game's 3D view the window's shape, with no bars and no stretching: the vertical field of view stays the game's and the horizontal one widens (16:9, 21:9, 32:9) or, in a narrower window such as the Steam Deck's 16:10, narrows a little. The 2D interface keeps the PSP's proportions, centred: at 21:9 it sits in the middle of the screen with the 3D view on both sides. Draws that cover the screen's width (fades, backdrops) and draws that sample the rendered picture (blur, the quest-reward background) spread with the 3D view. Movies keep the PSP's shape with black beside them.

**Resolution** Auto draws the game at the window's size in pixels (HiDPI included, at most 1632 lines) and follows the window: resizing it, fullscreen on or off, a move to another display. A new size is taken once the window has held it for a moment, so dragging an edge does not rebuild the picture on every frame. Under Original and Stretch, Auto picks the smallest multiple of 480×272 that covers the picture. ×1–×6 draw 272 lines per step; under Fill the width follows the window's shape.

Every change applies at the next frame; Original with a fixed resolution is exactly the picture of earlier versions.

### Field of view

**Field of view** (Video) sets how much of the world the 3D view takes in on a hunt, in vertical degrees from 30 to 90. 50 is the game's own view and changes nothing. Views the game itself makes wider or narrower during a quest, such as a bowgun's zoom, change in the same proportion, and the game's own easing between them is kept. The village and the Guild Hall always keep the game's view. Under *Fill* the horizontal view widens with the window as before, from this vertical one.

The game decides what to draw from the same field of view, so the edges of a wider view are not left empty. Lock-on's choice of which monster is on screen still assumes the game's 50 degrees.

On a phone with a display cutout (a camera notch or hole), the window covers the whole screen, the cutout's edge included, and the picture is always centred. It takes the whole screen when the game's 2D interface stays clear of the cutout there: under Fill and Original, whose interface keeps the PSP's shape in the middle, that is the case on most phones (at 20:9 the interface leaves a tenth of the width free on each side, more than a cutout takes). Otherwise (Stretch, or a narrower screen) the picture leaves the cutout's depth free on both sides, so it is letterboxed evenly and the interface stays clear. The touch controls and the performance overlay keep clear of the cutout on its own side. Each swapchain the Android app makes logs a `[render] layout` line with the window, surface and swapchain sizes, the transform, the cutout, SDL's safe area and where the picture goes, which is what to look for in a player's log when the picture is out of place.

How Fill works: the projection's parameters sit at the start of the object whose address is stored at `0x089CBD04` (the same object the follow camera reads the hunter from, at `+0xA84`): near plane 30, far plane 65000, aspect ratio and vertical field of view at `+0x0`, `+0x4`, `+0x8` and `+0xC`. The set-up (`0x08812A08`, and the reset at `0x08812B9C`) plants the aspect ratio as the immediate 480/272 and a field of view of about 0.8722. The projection (`0x08815348`, which calls the perspective builder `0x088144B4`) and the culling planes (`0x08815EC0`) read those fields, and the camera's update (`0x08815384`) builds the projection again only when the field of view differs from the one kept at `+0x10`. At each flip `host/camera/game_aspect.cpp` writes the target's shape into the live camera and makes the next update rebuild by changing `+0x10`. The set-up's immediates stay as the game compiled them: a camera created during a frame keeps 480/272 until that next update. The culling depth is the immediate 1.5 at `0x08815EC4`, also left as compiled, and it covers the view up to an aspect ratio of about 3. A wider picture, such as 32:9, can clip at the edge. Back at Original or Stretch the port writes the game's own aspect back, bit for bit, and then nothing more. The instructions this relies on are checked at start-up (listed in `game_aspect.cpp`); if any differs, the view keeps the PSP's shape. The renderer keeps the game's 480×272 coordinates everywhere (viewport, scissor, framebuffer textures, the frame written back to guest memory) and only spreads them over a target of the window's shape; the interface's through-mode draws into the shown framebuffer are pulled in about the centre, their scissor with them.

### Frame rate

The game makes 30 frames a second, and its logic is tied to that rate: animation, physics, attack windows, timers and the monsters' behaviour all advance by a fixed step per frame. **Frame rate** (Video) presents more frames than that without touching the game: between two of its frames the renderer draws the older one again with every moving object's transforms blended towards the newer one (frame interpolation). The game keeps its speed and its timing exactly; only the picture is smoother.

- **30** shows the game's frames as they are, when the game flips them. This is the picture of earlier versions.
- **45**, **60**, **90** and **120** present that many frames a second: 1.5, 2, 3 and 4 per game frame. 45 is half of a 90 Hz screen (the Steam Deck's) and 90 all of it; 120 is for desktop monitors of 120 Hz or more.
- **Match display** presents at the display's refresh rate.
- With **Vsync** on, the rate is never above the display's refresh: on a 60 Hz screen 90 and 120 run at 60. The row says so (*now 60*).
- If presenting that often would slow the game down, it steps down by itself to the fastest rate that fits (120, 90, 60, 45, 30), and tries a faster one again later once there is time to spare. It also steps down when the display has no image free for the presents (a screen that refreshes slower than the rate, such as a Steam Deck that gamescope switched to 60 Hz): a present waits at most 3 ms for one and is dropped otherwise. The row then shows *90 (running at 60)*. Loading screens and stalls while the game still has idle time do not count against it. **Lower when behind** (Video, on by default) turns this off: the chosen rate stays, capped only by Vsync, and the game may then run below full speed.
- The picture comes later than at 30: a frame is shown as it is one game frame after its own moment less the first in-between step, 16.7 ms later than at 30 at 60, 22.2 ms at 45 and 90, 25 ms at 120. The camera is still updated once per game frame. The pad is read when the game asks for it, at every Frame rate: the keyboard, mouse buttons and gamepad as they are then, not as they were at the last flip (issue #8), which takes up to a game frame off the input's own delay. `MHFU_PAD_AT_FLIP=1` reads it at the flip, as before.
- The presents keep the game's own clock: 29.97 frames a second, from the PSP's 59.94 Hz. A display refreshes at exactly 60 or 90 Hz, so with Vsync the two slip one refresh against each other about every 17 s at 60 and every 11 s at 90, which can show as a short stutter; at 30 the same happens every 33 s. Presenting on the display's own clock is left for later.

What is blended: 3D draws into the displayed picture that appear in both frames, recognised by where their vertices, indices and texture come from, their transforms, skinned characters' vertices (so bones move smoothly) and scrolling textures. The camera's motion between two frames is taken as one rigid motion of eye space (view times world), measured on the matched draw with the median turn. That sample is the rigid scenery when at least eight such draws matched, and every matched draw otherwise: a skinned character is left out, because in the hunter's house the hunter's parts outnumber the room and a turn in place was read as the camera's, so the room swung with the hunter. A draw of that scenery whose eye moved farther than any camera move that is not a cut (800 units) is not taken for the camera while at least a third of the sample moved less: a monster in the water in the Old Jungle's waterfall area adds about 6,000 splash draws with a single transform, which jumps 2,000 units or more whenever the game moves their origin, and as half the sample they were often taken for the camera, so many frames were taken for a camera cut and the picture fell back to 30 frames a second. A real cut moves nearly all the scenery and stays a cut (`MHFU_INTERPOLATION_NO_FAR_SAMPLES=1` keeps every draw in the sample). The motion is a turn about an axis through a centre, and a slide along it. In-between frames follow that motion along its arcs, and each draw's own motion is blended in the older frame's eye space, so scenery swings on the arc of a turn and the character the camera follows stays where it is on screen. What is not: the 2D interface and render-to-texture passes, shown as the older frame drew them, except the names over other hunters in a quest and in the gathering hall, with the icon beside each, which move with their hunter (`MHFU_INTERPOLATION_NO_NAMES=1` leaves them where each game frame put them); 3D draws in only one of the two frames (effects regenerated each frame, objects appearing or leaving), which move with the camera and nothing else; and flipbook animations such as the braziers' fire, whose texture offset jumps to the next cell of an atlas (a tenth of the texture or more in one game frame) and is held rather than swept across the cells between, so they animate at the game's own 30 frames a second. A pair whose draw would move more than 120 units in one game frame beyond what the camera's motion explains is not blended either: it is almost always two different objects, such as instances of one mesh drawn in a different order, or a buffer the game reused for other scenery, and blending them showed an object halfway to another for one present. Such a draw also moves with the camera only. `MHFU_INTERPOLATION_NO_MOTION_GUARD=1` and `MHFU_INTERPOLATION_NO_FLIPBOOK_GUARD=1` turn these two guards off. A mesh drawn more than once in a frame, such as the particles of an effect, has its instances paired by where they are rather than in drawing order: each with the nearest instance in the newer frame once the camera's motion is taken out, the nearest pairs first (for up to 64 instances of a mesh; more keep drawing order). A particle system draws its particles in an order that changes as they come and go, and paired in that order the hot spring's glints were blended towards each other, so every frame in between drew them away from their places and they flickered. `MHFU_INTERPOLATION_NO_NEAREST_INSTANCES=1` pairs instances in drawing order again. Two frames are not blended across a camera cut, a scene change or a loading screen: fewer than half of the draws match, the camera turned more than 30° or its eye moved more than 200 units in one game frame (the eye itself: a 7° orbit at 400 units moves it 49, whatever distant scenery does), or nothing 3D was drawn. A turn or a move that continues the previous frame's (the analog camera at up to 720° a second, 24° a game frame in yaw alone) may go past those limits, up to 75° and 800 units. Movies stay at 30.

How it is drawn. While the rate is above 30, each game frame is recorded as the renderer draws it: its draw calls (merged as usual) with their state, and a summary of every draw for matching it in the next frame. The vertex and index buffers keep three frames, the one being drawn and the two being blended, so drawing a frame again copies no vertices: an in-between frame records the older frame's draw calls again with blended matrices in their push constants. Only skinned draws get new vertices, blended on the CPU from copies of both frames' vertices, and only lit draws whose world matrix moved get a new lighting block. At each flip the frame is submitted and its picture copied; the presents follow a grid of evenly spaced moments aligned with the game's frames, timed by the real time of the vblank each frame started from (kept on a grid of two vblanks, since the game's code often runs past the vblank between two frames on a slower machine), and are made while the kernel waits for real time and, when due, while the game's code runs, up to half a game frame of the latter per frame. The delay allows for the game's code to take as long as the ninth longest of the last 60 frames; a slower frame holds the newest picture for a present or two. The flip no longer presents. The code is in `host/gpu/frame_interpolation.*` (matching, cuts, blending), `host/gpu/frame_pacing.*` (when to present, what each present shows, the rate governor) and `host/gpu/vulkan_renderer.cpp`.

Cost, Apple M1 at ×5 (2400×1360), in the village, vsync off (`MHFU_FRAME_RATE_CYCLE`, one run):

| Rate | fps | Speed | CPU per game frame (`render`) | GPU per game frame | One in-between frame: recording / whole present / GPU |
| --- | --- | --- | --- | --- | --- |
| 30 | 30.0 | 100% | 3.8 ms | 7.1 ms | |
| 45 | 45.0 | 100% | 5.4 ms | 12.7 ms | 0.54 / 1.5 / 5.9 ms |
| 60 | 59.8 | 100% | 5.5 ms | 11.3 ms | 0.55 / 1.7 / 5.3 ms |
| 90 | 89.9 | 100% | 6.5 ms | 15.2 ms | 0.59 / 1.6 / 4.7 ms |
| 120 | 119.8 | 100% | 7.1 ms | 18.1 ms | 0.56 / 3.0 / 4.3 ms |

An in-between frame records about 3,300 draw calls; the whole present includes acquiring, submitting and presenting the swapchain image, where MoltenVK waits for a drawable. No present was skipped at any rate.

Steam Deck (LCD, 90 Hz, Game Mode, ×6 under Fill, 2611×1632), village, Vsync on:

| Rate | fps | Speed | CPU per game frame (`render`) | GPU per game frame | One in-between frame: recording / whole present / GPU |
| --- | --- | --- | --- | --- | --- |
| 30 | 30.0 | 100% | 9.5 ms | 5.9 ms | |
| 45 | 45.0 | 100% | 13.8 ms | 11.1 ms | 2.7 / 3.1 / 4.8 ms |
| 60 | 59.9 | 100% | 14.1 ms | 11.2 ms | 2.9 / 3.4 / 4.8 ms |
| 90 | 89.9 | 100% | 16.5-17.0 ms | 16.0 ms | 2.7 / 3.0 / 4.9 ms |
| 120 | 89.3 (Vsync caps it at 90) | 100% | 16.7 ms | 16.1 ms | 2.7 / 3.1 / 4.9 ms |

At 90 the kernel still waits about 12 ms of every game frame, so the rate has room; 60 blended and 30 plain presents a second, none dropped, a delay of 26 ms (the game's code about 4 ms).

A game-side 60 fps patch was not used. The one community code for Portable 3rd (Saramagrean's CWCheat database, NPJB-40001 and ULJM-05800, "60 FPS Beta") takes the vblank handler at `0x0887669C` from starting a frame every other vblank to every one, then halves a quest timer, one counter and a few animation speeds back. The game has no time step to scale: its timers count frames and its animations take fixed steps (about 280 self-doubling `add.s` in the executable and the quest overlays), so everything else — monsters, their attacks, stamina, hit windows — runs at double speed, as its author and testers say. The overlay half of the NPJB code also targets the PSP release's addresses: our overlays load 0x800000 higher. 120 would be four times the game's own rate. Presenting in between keeps every one of those timings.

### Fast loading

Reading the disc costs nothing here, yet a load takes as long as on a PSP: the game's loader threads read, check and unpack its data a piece at a time and wait on the emulated clock in between, and the kernel holds that clock to real time. Measured with `MHFU_TRACE_LOAD`, the host sits idle for 80–95% of a load. **Fast loading** (Video, on by default) lets the clock run ahead while the game loads, up to 16 times real time, so a load takes as long as the work itself.

A load is recognised from what the game does rather than from a timer: it has read from the disc within the last half second of game time, and for a quarter of a second everything it has handed to `sceAudio` has been exact silence once the channel's volume is applied (a loading screen's sound is nothing but zeros). Anything else keeps real time, and ends a fast stretch at once:

- any sound, however quiet: the buffer that carries it plays in full, and so does everything after it. Only the buffers of zeros a fast stretch hands over are dropped, so no sound is ever cut or sped up, and the audio stays in step afterwards;
- a button or a D-pad direction held (moving a stick is fine), so a press never lasts longer in the game than on the pad;
- a movie, the in-game menu, and ad hoc play: once the game has started its ad hoc networking, or a session is going, time stays real for the other players;
- Game speed set to Unlimited, which already runs everything unpaced, and runs without a window.

On a Mac with an M1, from the button press to the new scene's first sound (for the Guild Hall and the farm, *before* is the game time the load took, which normal speed plays in real time):

| Load | Before | After |
| --- | --- | --- |
| Boot to the first logo | 1.0 s | 0.3 s |
| Title to character select (*Now Loading*) | 4.7 s | 2.5 s |
| Character select to the village | 9.2 s | 5.0 s |
| Village to the Guild Hall (offline) | 3.9 s | 0.7 s |
| Guild Hall to the farm | 5.6 s | 1.0 s |
| Village to a quest's base camp | 6.9 s | 1.7 s |
| Quest end to the village | 6.0 s | 1.4 s |

What is left is the game's own music fading out and its animations, which play at real time, the game's check of what it read (`sha1Thread`) and unpacking, and installing a code overlay. While a load runs fast, a flip reaches the window at most about 30 times a second and the others are drawn and not shown, so Vsync never holds it back; frame interpolation pauses and picks up again after it. Each fast stretch logs one line: `[load] fast 4922 ms of game time in 434 ms real, 4488 ms saved (sound; 18.4 s saved so far)`, with what ended it, and `, in a quest` after it when a quest was running as it began (an area change, or a quiet moment of play taken for a load). `MHFU_FAST_LOADING=0` or the menu's Off keeps every load at the PSP's pace.

### Fast-forward

Holding the **Fast-forward** key (by default `` ` ``, the key under Esc; Controls → *Keyboard and mouse*) runs the whole game faster than real time, 3× by default: long walks, gathering, cutscenes drawn by the game. Release it and the game is back at 100% at once. *Fast-forward* (Game) makes the key a switch instead (`toggle`), or turns it off; *Fast-forward speed* sets 2× to 8×. Nothing changes until the key is pressed.

It is the same clock as always, let go further. The kernel holds emulated time to real time; while fast-forwarding it lets emulated time run the chosen number of times as fast, the way [fast loading](#fast-loading) does during a load. Every vblank still comes one PSP vblank period (16.68 ms) of emulated time after the one before, so the game runs every one of its frames, its timers and its sound thread exactly as on a PSP; they only come sooner. Each stretch logs one line, with the vblanks the game saw: `[fast-forward] off (released): 20020 ms of game time, 1200 vblanks, in 6681 ms real (3.00x)`. `MHFU_PERF=log` reads `speed 300%` and three times the display lists while it runs. If the computer cannot keep up, the game runs as fast as it can instead, never skipping a frame.

- **Sound** is muted while fast-forwarding: the buffers the game hands to `sceAudio` are dropped, as a fast load drops its silence, and sound resumes, in step, as soon as the key is released. Sped-up sound would pile up faster than the device can play it, and dropping part of it would be choppy.
- **The picture**: the window shows a flip at most about 30 times a second and the others are drawn and not shown, so Vsync never holds it back; frame interpolation pauses and picks up again after it.
- **The indicator**, two arrowheads and the speed in the top-right corner, is drawn by Pokee's interface over the game. It is never part of the game's own frames (`MHFU_SCREENSHOT_DIR`); window captures show it, like the rest of the interface.
- **Single player only.** While the game has ad hoc networking on, or a session is going, the key does nothing: the other players' time is real. A press there says so once on the console, and the menu's rows show *Single player only*. A toggle made before going on line does not come back on its own.
- It also does nothing while the menu is open over the game, with Game speed set to Unlimited (which already runs unpaced), and without a window. A loading screen while fast-forwarding runs at fast loading's pace, whichever is faster.
- The camera keeps turning at real-time speed, so it stays controllable.
- There is no gamepad button for it yet. On a Steam Deck, Steam Input can put the key on a back button.

### Hiding the HUD

**Hide HUD** (Video, or its key, F7 by default: Controls, under *Port features*) leaves the game's HUD out of the picture, for screenshots and videos: the health and stamina bars, sharpness, the clock, the item bar, the map, the name plate, the name tags over hunters and the icons beside them, and the prompts in the corners (*Select Target*, *Hide List*, *Depart on your Quest*). Menus, dialogs, fades, cutscenes and loading screens stay, so the game can be played with it on. The same again brings the HUD back. It is off at every start (`MHFU_HIDE_HUD=1` starts with it on); no gamepad preset binds it, but a pad binding a player makes works. A short line at the top says *HUD hidden* or *HUD shown* for a moment; it is left out of window captures (`shot`), and the game's frames (`MHFU_SCREENSHOT_DIR`) never have it.

What stays: the menus' own hints, such as *Map Zoom In* under the quest menu, which the menu draws; and what the game draws in 3D, such as the red arrow over the person the hunter faces, which is part of the scene.

How the game's 2D draws were told apart (`host/gpu/game_hud.{hpp,cpp}`), read from this executable and from `lobby_task.ovl` and `game_task.ovl` (both load at `0x09A5F300`):

- Each 2D element is a packet of GE commands that the game files into its display list's ordering table with one function (`0x088596C8`: the list, the packet, its length, a bucket), and the buckets are drawn in order. The HUD shares buckets with menus and dialogs, so the bucket alone does not tell them apart.
- The code that files it does. In the village and the Guild Hall, `lobby_task` draws its text layer from `0x09AE38F8`, and the name over the hunter from `0x09A6C248`. In a quest, the HUD is a task callback: the health bar, the map, the item bar and their text are filed while `0x09A60298` is on the stack (through `0x09A62740` and `0x09A627B8`). `0x09A617A8` and `0x09A62400` file the same sprites for the quest's other HUD states, and the labels queue their own text from `0x09B62488`. In an ad hoc quest the names over the other hunters come from resident `0x088392F4`, which projects each character and queues its name through `0x0889198C`; that helper jumps into the queue function inside its own generated unit, so it is watched itself. The names count only while `game_task` is loaded. Menus are other callbacks. The port reads the guest's call stack when a packet is filed, the way the game's code builds it, and a packet filed from inside one of those is HUD. Each function's first two words are checked, because the two overlays replace each other in the same slot.
- Most text is queued and drawn at the end of the frame, away from the code that asked for it. `0x08A632C8` points at the text context, which keeps six queues. The four functions that append to a queue are watched, and the entries added until the next watched call or packet belong to the code on the stack then. When a queue is drawn, the entry each glyph comes from is read off the drawing function's stack frame, and so is the list entry of each button icon in the text (the **L** of *Hide List*).

The renderer then leaves out the HUD packets' draws, and nothing else: their other commands still run, so the GE state is the same either way. The watching starts once the HUD is first hidden, once any [UI scaling](#ui-scaling) size is set away from the game's size, or while Lock-on (for the monster icon) or the [Modern sharpness gauge](#modern-sharpness-gauge) is on, both on by default, and then stays for the run. It walks the guest's call stack for each 2D packet the game files, 150 to 270 of them in a quest frame; timed in the unit tests at under 0.1 ms a frame with every stack at its deepest. With the HUD shown, nothing is left out. If the game's code at those addresses is not what it was traced on, the switch is unavailable and says so on the console.

### UI scaling

**UI Scaling** (Video) opens under its row. Each part of the quest interface has its own size, from 50% to 150% in steps of 5. 100% is the game's size. Left and right, or a drag on the bar, change the focused part. Confirm on the UI Scaling row opens or closes it. Each part keeps its own size:

- **Map** (`video.hud.map`): the area map on the right of a quest, large or small, from the slot's top-right corner, with the marks, the camp icon and the player's arrow drawn on it.
- **Status** (`video.hud.health`): the quest clock, the hunter's name under it, the name and badge of the one cat that accompanies the player, health and stamina, the yellow eye shown once a monster is found, the status icons beside that name (poison, paralysis, sleep, stun, snowman, muddy, stench, defense down and fatigue), sharpness, the long sword's spirit gauge, a bowgun's loaded shots, a bow's coating display, a gunlance's shells and a hunting horn's notes. They grow from the clock's corner and stay together: sharpness, the spirit gauge, the loaded shots, the coatings, the shells and the notes stay under stamina. A quest can show four hunters. Each hunter's weapon badge stays on that name and takes Status from the clock, the same way as the first badge. The smaller icons in that column, such as the Felyne Comrade's face under its name, take Status from the clock with the plates, so they keep the place the game gives them beside the names. That holds whether the game draws the face with the HUD's sprites, with a name label, or as a small orthographic draw. A letter in the quest-details window stays where that window draws it.
- **Items** (`video.hud.item_bar`): the item bar, open or closed, from its bottom-right corner, including the item's name and the **L** prompt. A bowgun's ammo bar and a bow's coating bar sit on the item bar and take its size, and the selection menu each opens into is sized with it as one panel from the same corner. The item list and the shops stay at the size the game draws. The item bar is only sized while the quest clock is up, so a shop panel in that corner is left as drawn.
- **Names** (`video.hud.names`): the names over other hunters in a quest and in the gathering hall, and the mark beside each. Each name stays on the point the game places it. No other size reaches a name: one that crosses the party list, the map or the item bar keeps the Names size, and its letters never take Status, Map or Items. In the gathering hall a name is the weapon icon resident `0x08835470` files beside it while `lobby_task` is loaded, and the text queued after that icon, and only a draw the size of a name tag is sized. Such a label shrinks toward the middle of its letters and the weapon badge beside them, so it stays over its hunter; on the first frame it appears it shrinks toward the game's point, the first letter's top-left. The quest paper and the blinking ready mark over a hunter do not move that middle. The label files them after the name, with no point of its own or with the next hunter's, so each is given to the label it sits on: the paper starts 3 pixels right of its hunter's point and the ready mark 7, both ending a pixel above it. Each is sized toward that label's middle and moves with that label between frames, so a mark stays on its own name when hunters stand close together. A mark over no label drawn that frame is sized toward the middle of the label nearest it. A label keeps its middle for a second while it is not drawn, and as long as its point moves less than 96 pixels between frames, as when the camera turns. Those offsets and distances are the fallback. The label function `0x08835470` draws every hunter's label in one call, and the call in it names each piece: the badge at `0x088357D0`, the quest paper at `0x08835C50`, the ready mark at `0x08835D0C`. Traced with `MHFU_TRACE_CAPTURE`, every frame files a badge, that name's text and its marks, then the next badge, the player's own label first. Each piece takes the number of the label it follows, counted from 1 each frame, and the name's letters the number of the label that queued them. A label is then its own letters and badge, a mark goes to the label it follows whatever point it was filed with, and a label keeps its middle by its number, however far its point moves. The offsets apply only when the pieces cannot be told apart this way, or with `MHFU_HUD_SCALE_SITES=0`. `MHFU_TRACE_HALL_NAMES=1` prints each piece of these labels, where it was sized about and where it went. The same function draws the hunter's own name in the village, so that name takes the Names size too. Its caller, `0x088351F0`, also draws the prompt over the hunter (*Open Item Box*, *Enter House*) through `0x08835F7C`; the prompt, its button and the bar's end stay at the size the game draws, as do the shops and dialogs. `MHFU_HUD_HALL_NAMES=0` leaves the names in the village and the hall at that size too, as before. The rest of the village stays at the size the game draws: `lobby_task` files its labels from the same functions as the shops, prompts and dialogs (`0x09A6C248` calls through the mode at object+44). The hunter's own name under the quest clock follows Status, so it stays with the bars.

Only the quest HUD's own draws change size. Menus, the item box, shops and dialogs draw panels in the same corners at the same sizes as these parts, so each draw is first told apart by the code that filed it, the same way as for [Hiding the HUD](#hiding-the-hud): a draw changes size only when one of game_task's HUD functions (`0x09A60298`, `0x09A617A8`, `0x09A62400`, or the labels at `0x09B62488`) filed it. Nothing in the village or the gathering hall changes size, apart from the names over the hunters, and neither does a menu opened during a quest.

Within the quest HUD, the function that drew each part says which part it is. Traced with `MHFU_TRACE_CAPTURE` in two ad hoc instances, across the bowgun, bow, hunting horn, gunlance and long sword, every quest HUD packet is filed under the dispatcher `0x0883F270`, and each part is its own resident function below it:

| Function | Part | Size |
| --- | --- | --- |
| `0x0883F9F8`, and below it `0x08840338` (the clock), `0x088405E4` (health and stamina) and `0x08841908` (the party list) | The left column | Status |
| `0x08841100` | Sharpness, the spirit gauge, a horn's notes, loaded rounds, a bow's coatings, a gunlance's shells | Status, under stamina |
| `0x08832B98`, called from the party list | A name over a hunter | Names |
| `0x08842EE8`, and below it `0x08842F70` | The area map, its window and its marks. The call in `0x08842F70` tells the map's picture (the tile at `0x088432E8`, the window at `0x08843610`) from the marks on it (the camp icon at `0x088437B4`, the hunters' arrows at `0x0884398C` and `0x08843A2C`). The quest menu's Map Zoom In is drawn by `0x08842EE8` itself, at `0x08842F60`, and is sized from the same corner | Map |
| `0x08845A34`, with `0x088474DC` and `0x088471D8` (the ammo bar) and `0x08846FAC` (the ammo menu's arrow and highlight) | The item bar, the item's name, the ammo bar and the ammo menu | Items |
| `0x088172D8`, `0x08818C94`, `0x08818F74`, `0x08819128`, `0x08847CB0`, `0x08823A50`, `0x088DB068` | Combine's windows, the quest menu, its hint bar and description box, the item box, the banners (an item obtained, a teammate leaving) and the aim's reticle | As drawn |

The innermost function on the call stack that is in this table decides, and each must still begin with the instruction it was read from. A quest HUD draw from no function in the table stays as drawn. Text is drawn at the end of the frame, so each glyph takes the part of the code that queued its text. A part takes its size whatever its shape and whatever else is on screen, so a menu, a banner or the reticle in a part's corner is never taken for that part. The rectangles below only place each part: the corner it grows from. Which map draws are the map's picture, taken into the scissor so a larger map is not clipped, is the call that drew them. `MHFU_HUD_SCALE_SITES=0` goes back to telling the parts apart by their place and size on screen, to compare both. If the game's code is not what this was traced on, the sizes fall back to that too, and `MHFU_HUD_SCALE_GEOMETRY=1` goes further back, to the place and size alone without knowing which code filed each draw. Without the table, menus are still taken into account, so an open item list keeps the item bar under it at the game's size.

`MHFU_HUD_MAP`, `MHFU_HUD_HEALTH`, `MHFU_HUD_ITEM_BAR` and `MHFU_HUD_NAMES` set any whole percent in that range for one run. The earlier single size, `video.map_item_scale` or `MHFU_MAP_ITEM_SCALE`, still applies to Map, Status and Items when no separate size is set. It does not change Names.

Without the table, the parts are told apart by the rectangles in this paragraph; with it, only their corners are used. The pieces were measured with `MHFU_TRACE_SPRITES` in a quest's base camp and a hunting area (USA Freedom Unite). The area map is the tile at (316, 32)–(468, 184), or the window the game draws from the area-map texture (`0x0419C200`). Traced windows are about 34 by 40 and 40 by 40, at (425, 78)–(465, 118), at (432, 76)–(466, 116), and, once a monster is found, at (371, 59)–(411, 99). With the Map item a texel is drawn at (316, 32) plus that texel, so the picture stays put while the window slides over it as the hunter walks. The south of an area is drawn below the slot; on the bottom edge of Forest and Hills zone 1 the window sits under y 240, and it still takes the Map size. Without the Map item the window shows the hunter's zone the same way, one pixel per texel; no trace, with or without the Map item, has drawn it larger. A 176 by 120 panel at (280, 16) is not the map. Other areas draw that texture at other sizes; the bounds are the rectangle drawn. The window, the player's arrow and the camp icon all grow from the slot's top-right, (468, 32). The arrow and the camp icon stay at that size when the window moves off them. The camp icon is drawn from another texture at about the size of the window. The player's arrow is a turned quad, so its box crosses the edge of the map and still follows it. Pointing up or down, that box is about 10 by 12, the size of a letter, and it still takes the Map size; a letter is a different texture and stays as drawn. The shop and the return-to-town prompt draw panels of the same size in that corner from other textures, so those panels stay as the game drew them. The large tile is not drawn while that window is up. A mark of about 14 by 12 sits on the window and follows it. A bowgun's loaded shots are the rounds at (60, 32)–(132, 48) and their frame, which is not drawn in every frame, and its ammo bar is (312, 184)–(472, 200). A bow's coating display under stamina is a 34 by 12 plate at (60, 32)–(94, 44) without a coating, or a large icon at (60, 30)–(78, 46) standing for ten uses, with its count beside it. Once a shot is fired, the rest of those ten are a row of up to nine single coatings from (120, 30) to (244, 46). A gunlance draws no long sharpness bar, only the frame at (60, 29)–(104, 41), and five shells, 8 by 16, from (116, 30) to (156, 46), loaded or spent. A frame behind the shells, (116, 32)–(220, 44), is not drawn in every frame. A hunting horn's note staff is (118, 30)–(246, 46), with a mark at its left, drawn whenever the horn is out. A new note is drawn large at the first slot for two frames, (145, 21)–(168, 52) then (147, 23)–(166, 49), before it settles. Queued notes run from (150, 28) to (234, 50); the one being recited grows and the row shifts. A mark at (128, 56)–(140, 72) blinks once a note is queued. The ammo selection menu is everything inside (300, 120)–(480, 264) in a frame that draws the menu: its frame, (312, 128)–(472, 264), is the item bar's frame grown upward, and an arrow sits above it at (384, 124)–(400, 140). The menu is recognised from the first sprite it draws, so it is never drawn at full size for a frame when it opens or closes. In camp that tile is absent, and the supply icons in the same place stay as drawn. The long sword's spirit gauge is the red fill inside the sharpness bar, two pixels tall, from (66, 46) toward (162, 48). The clock is the gauge at (8, 8)–(60, 56), with its two hands drawn inside it as turned quads (four-vertex strips rather than sprites, as is the player's arrow on the small map), health is the bar at (56, 12)–(320, 20), or out to x 399 when maximum health is raised, and stamina is the bar at (56, 21)–(320, 29). Both bars are 2.64 pixels per point, so either one raised to 150 reaches x 452. Each end cap is 24 by 12 past the bar's end, so at 150 the caps reach x 476. In that band the status size takes only the bars, fills and frames, which start at the bars' left edge, those caps, and segments at most four pixels tall inside a bar. Other panels drawn there while the bars are up, such as the item box's top edge at (216, 28)–(456, 36) and a message window at (152, 8)–(328, 32), stay as the game drew them. That window is the notice shown while returning to camp and while carving. It then slides down the picture. A short line's right border is a 16-wide cap at x 312 or x 328. A long line reaches into the area map: the cap (344, 160)–(360, 168), the rows (360, 160)–(456, 168) and (376, 168)–(456, 176), and the untextured rows (350, 46)–(466, 61) and (350, 95)–(466, 112). The letters beside the border cross the map and the bowgun slot as well, and an 8-pixel piece of the border passes the ammo menu's corner. Those pieces stay as the game drew them. The menu's corner is the 16 by 8 tile at y 128 on its own; the notice's border is that tile with its other end 24 pixels below. The yellow eye shown once a monster is found is 12 by 16 at (104, 56)–(116, 72), just past the right end of the name plate, and it grows from the clock with that plate. The same eye beside a cat or a teammate is that size on their row and grows the same way. The status icons are the small marks past the plate, about 12 wide and 14 to 16 tall, and a cat's badge is that mark beside the cat's name, including one that sits left of x 112. Teammates and their cats draw those buffs on the rows below. They grow from the clock with the name. A cat's name is the letters beside a hunter, about 8 by 14 or one wider label, and a quest draws up to four hunters in the band left of the map, down to about y 148. Those follow the clock. A mark about 12 wide is a badge or a buff, not a name. The queued-note mark at (128, 56)–(140, 72) stays with the horn. The item bar is the sprites inside x 312–472 and y 200–264. A piece is scaled only while its own sprites are on screen, so the village, the item list ("Please select an item") and the menus stay as the game draws them.

### Audio

| Variable | Default | Effect |
| --- | --- | --- |
| `MHFU_NO_AUDIO` | off | Do not open a playback device; the game's audio timing is unchanged |
| `MHFU_AUDIO_DUMP` | unset | Write the mixed output to a 44100 Hz stereo WAV file |
| `MHFU_OLD_AUDIO_DRAIN` | off | A short buffer output behind longer ones still queued waits for them, as before. The game's main thread drains the music channel this way at each change of music segment, a few times a fight, and the wait of up to 46 ms made it miss a vblank. Otherwise such buffers wait only for each other and the next longer buffer waits for everything; the queue, and what is played when, stay the same |

### Input

| Variable | Default | Effect |
| --- | --- | --- |
| `MHFU_PAD_FACE` | positional | `xbox` puts confirm (○) on the south button (menu: Confirm button) |
| `MHFU_MENU_CONFIRM` | unset | `game` makes Pokee's menu follow the game's Confirm button setting; otherwise the menu confirms with the bottom button and goes back with the right one |
| `MHFU_PAD_DEADZONE` | `0.15` | Left-stick dead zone, as a fraction of travel (menu: Stick dead zone) |
| `MHFU_PAD_TRIGGER` | `0.25` | How far LT/RT travel before they press anything (menu: Trigger point) |
| `MHFU_CHORD_WINDOW` | `50` | Milliseconds an input that begins a longer chord waits for the rest, 0 to 200; `0` or `off` acts at once (menu: Chord window) |
| `MHFU_TOUCH_LAYOUT` | unset | The touch layout for this run: `psp` or `action` (menu: Layout) |
| `MHFU_CONTROL_PRESET` | unset | The control preset for this run: `default`, `modern`, `left_handed`, `classic`, or the name of one of the player's (menu: Preset). `MHFU_PAD_TRIGGERS`, which chose a trigger profile, is retired: its profiles are presets now |
| `MHFU_ANALOG_CAMERA` | on | Proportional yaw and continuous tilt in the ordinary quest camera, the tilt limited to −20°…70° before collision correction (the game keeps the eye no more than 30° below the point it looks at; a lower tilt made it raise that point and then let go at once, flipping the view). The tilt's height is measured from the hunter's feet, so where it would take the eye lower than 80 above them, the eye stays at that height and comes in along the same view line: the view looks up to about 36° with the eye above the floor. Stick deflection controls speed; release holds the angle. A recentre returns control to the game, and so does the physical D-pad when **D-pad camera** is on. Uses the camera update directly, without memory searches or renderer tracing. Off restores stock input and stops camera writes immediately (menu: Analog camera) |
| `MHFU_LOCK_ON` | on | `0` makes the [Lock on](#lock-on) bind do nothing and hides the monster icon (menu: Lock-on) |
| `MHFU_LOCK_ON_STYLE` | `follow` | `focus` keeps the selection and centers the camera on it when L is tapped. A second tap within a third of a second recenters behind the hunter (menu: Lock-on style) |
| `MHFU_HUNT_PORTRAIT` | `100` | The monster icon as a percent of its usual size, 50 to 200 (menu: Monster Icon) |
| `MHFU_LOCK_ON_HUNTERS` | off | `1` lets hunters, recognised by their vtable, onto the monster icon and lock-on again, as earlier versions did |
| `MHFU_HUNT_ICON_WITH_HUD` | on | `0` shows the monster icon whenever a quest runs, instead of only while the quest HUD is drawn: while its left column, where the clock is, was filed this frame or the one before (the Status part in [UI scaling](#ui-scaling)), or with `MHFU_HUD_SCALE_SITES=0` while the clock's rectangle is drawn |
| `MHFU_HUD_OVER_MARKS` | on | `0` draws the [gather icon](#gather-icon), damage numbers, the lock-on marker and hunt portraits over the game's HUD, as before. On, the frame is copied just before the game's first HUD draw, and wherever the finished frame differs from that copy the HUD is drawn again over the marks, so they pass under the health bar, the map and the item bar. Pokee's own windows and notes stay on top |
| `MHFU_GATHER_ICON_SMOOTH` | on | `0` draws the [gather icon](#gather-icon) where each game frame puts it: no fade, no glide between targets, and it does not follow frame interpolation |
| `MHFU_DAMAGE_NUMBERS_SMOOTH` | on | `0` draws [damage numbers](#damage-numbers) where each game frame puts them, without following frame interpolation |
| `MHFU_DAMAGE_NUMBERS_FOLLOW` | off | `1` makes [damage numbers](#damage-numbers) ride the monster as it moves, as earlier versions did, instead of staying where the hit landed |
| `MHFU_DAMAGE_NUMBERS_CONTACT` | on | `0` draws [damage numbers](#damage-numbers) a little above the monster's feet instead of on the bone nearest the attacker's weapon hand or where a shot struck or a bow's arrow passed |
| `MHFU_DAMAGE_NUMBERS_GAME_CONTACT` | on | `0` places [damage numbers](#damage-numbers) by the earlier rules (the shot, the bow's path, the bone nearest the weapon hand) instead of the point of contact the game itself found for the hit |
| `MHFU_DAMAGE_NUMBERS_COMPUTED` | on | `0` makes every [damage number](#damage-numbers) the drop in health, white, without the computed hit's breakdown, critical dot or attacker, as before |
| `MHFU_DAMAGE_NUMBERS_POISON` | on | `0` hides the light purple [damage numbers](#damage-numbers) of poison ticks, as before |
| `MHFU_CAMERA_SPEED` | `190` | Degrees a second at full deflection, 20 to 720 (menu: Camera speed) |
| `MHFU_AIM_SPEED` | `90` | Degrees a second a bow or bowgun aims at full deflection, 10 to 360 (menu: Aim speed) |
| `MHFU_AIM_PITCH_SHARE` | `0.34` | The share of Aim speed a bowgun's aim moves up and down at, above 0 and up to 4. The default is the game's own: its stick turns the pitch about a third as fast as the heading. `1` moves both at Aim speed, as before; a bow always does |
| `MHFU_MOUSE` | on | `0` leaves the pointer alone: no capture, no mouse camera and no mouse buttons (menu: Mouse) |
| `MHFU_MOUSE_SENSITIVITY` | `0.10` | Degrees of camera turn per count of mouse motion, 0.01 to 0.99 (menu: Mouse sensitivity) |
| `MHFU_PAD_RSTICK_DPAD` | off | Press D-pad bits from the right stick instead of feeding the HD release's second stick; enabling both would turn the camera twice (menu: Camera stick) |
| `MHFU_PAD_RSTICK_ZONE` | `0.5` | Right-stick threshold for that (menu: Camera stick D-pad point) |
| `MHFU_OSK_TEXT` | `Hunter` | Fixed name given when the game asks for one, at once and without the on-screen keyboard unless `MHFU_OSK_MODE=keyboard` (menu: Hunter name) |
| `MHFU_OSK_MODE` | `keyboard` | `keyboard` opens the on-screen keyboard; `fixed` gives the fixed name at once (menu: When the game asks for a name) |
| `MHFU_AUTO_CONFIRM` | off | Press ○ every N frames, to walk through menus unattended |

### Network

| Variable | Default | Effect |
| --- | --- | --- |
| `MHFU_ADHOC` | off | `1` turns ad hoc play on (menu: Ad hoc play) |
| `MHFU_ADHOC_SERVER` | none | PSP ad hoc server, `host` or `host:port` (menu: Server) |
| `MHFU_ADHOC_NICKNAME` | the hunter name | Name other players see (menu: Nickname) |
| `MHFU_ADHOC_MAC` | made up once | The address other players know you by, `xx:xx:xx:xx:xx:xx` |
| `MHFU_ADHOC_OVERLAY` | off | `1` shows the network overlay from the start |
| `MHFU_ADHOC_HOST_PORT` | `27312` | TCP port of the built-in server's matchmaking service; the relay uses the next one (`network.host_port`) |

### Analog camera

**Analog camera** (Controls) is on by default and drives both axes of the quest follow camera. Small right-stick deflections turn and tilt slowly; full deflection uses **Camera speed**. The input dead zone and inversion settings apply to both axes. The game retains terrain and wall collision handling. Its eye ease moves the eye's height an eighth of the way to the tilted offset each update and copies the sideways axes at once; the driver adds the player's change in height to the eye before that ease, so a tilt, looking up included, is not left for the ease to catch up. Releasing the stick holds the angle. Collision corrections still go through the ease. A wall the game's collision finds pulls the eye in at once, as the game does; with the analog camera on, the view then goes back out only a fifth of the remaining way each update, along the line the game put the eye on, so a collision that comes and goes from one update to the next (the back against a wall, orbiting past it) no longer snaps the view in and out. The game's own eye is put back before its next update, so its ease and collision are unchanged. When a wall blocks the eye, the game sweeps toward the eye it wants both from where the eye was, which stops it on the near side of the wall, and along the view line, which pulls it in; `0x0888FCE4` picks the pull-in only during a snap or with the camera in front of the hunter. Behind the hunter, with its back to a wall, the eye was held where the wall stopped it while the view turned and then snapped once the wanted eye came clear. With the analog camera on, the pull-in is picked from any side. While the camera is placing itself behind the hunter, and while a snap is in use, the driver leaves that update alone. The placement is the gate at camera +0x91 set non-negative, unless the game's option byte (the word at `0x089CC558`, plus 0x6ADE0) is 1: the game then skips the placement however the gate is set, and the stick keeps the camera. A gate of exactly 0 is not a placement but the game's soft follow, which steps the camera back behind the hunter whenever it is between 22.5 and about 146 degrees off; it can stay 0 for many seconds, after a bowgun's aim among others. While the stick drives the camera, that gate is held negative for the update and put back after it, so the stick keeps the camera. **D-pad camera** (Controls) is off by default: the D-pad's left, right, up and down are then cleared from the button halfwords the camera update copies (object `0x089CBD04` +0xA70 and +0xA72) for that update only and put back after it, so the D-pad neither turns the game's camera nor steps its height levels while the stick drives it. Two cameras on one view fought each other. Turned on, the D-pad turns and steps the game's camera as before, and the driver leaves those updates to the game. Village cameras and any other camera function stay stock, because this driver wraps only the follow camera's update.

While the hunter's action halfword at `+804` is 1001, the same option aims a bow or a bowgun. That action is the one the game's aim function (`0x09AA4DD0` in `game_task`) requires before it steps the aim heading at halfword `+800` and the pitch byte at `+1662`. A bow sets hunter byte `+1380`, and the function then steps from the stick direction in the high half of the button word, in three sizes from the left stick's magnitude. A bowgun leaves that byte clear, and the function steps only from the D-pad: left and right by 576 on the heading, up and down on the pitch byte. While R is held, a push on the right stick is shown to that call at full length, as whichever of the stick half and the D-pad the byte selects, so the function steps; the wrapper then replaces that step with one at Aim speed times how far each axis is pushed, before the aim camera reads the heading. The mouse is shown the same way and sized from its own degrees. The second stick the game reads stays centred, so the push is not also the camera's up or down command. The movement stick stays the one the player is holding. Action 1001 counts as aiming only while R is held. When R is released the game drops the reticle and glides the follow camera back, but the hunter stays in 1001 and the aim function keeps running for a second or more; the right stick goes back to the camera at the release instead of aiming on. With the right stick centred, a push on the left stick is resized from its magnitude the same way when the function stepped from it. A frame in which the game does not step (a roll, a shot, an axis it has locked, the D-pad with the stick centred) is left as the game made it. The body facing, the low 16 bits of the word at `+500`, moves with the aim heading while it still matches that heading. No separate scope flag was found in this executable; a scope that stays in action 1001 uses these same fields.

The option takes effect at run time, with no regeneration or rebuild. Turning it off stops all analog-camera writes, restores the game's vertical presets and passes the right stick through unchanged. Camera speed is in degrees per second of real time, measured between the game's flips, so it does not change when the game slows down.

The code is in two layers under `host/camera/`:

- `camera_input` is what the player asks for, independent of device and game. Rate sources (the stick, and the camera keys, which push it) hold a fraction of Camera speed; motion sources (the mouse, later a touch drag) add degrees. Every source adds together. New input devices only feed this layer.
- `game_camera` drives the game's camera from that input. The follow camera is the update at `0x08886E50`. Its argument is the parent object and the camera is that plus `0xA0`. The host wraps the update. It writes the target yaw and the filtered yaw together, so the game's quarter-step yaw filter has nothing left to close, and it rewrites the preset's eye-offset height and distance (`+4` and `+8` of the pointer at camera `+0x70`) for that call so collision sees the tilted offset. Those two floats are put back before the update returns, so the shared table is not left changed. The update then eases the eye's height an eighth of the way toward that offset (`0x08887D10`) and copies the other two axes at once. The player's change in height is added to the eye first, so that ease does not lag the stick; terrain and collision still use it. A non-negative byte at camera `+0x91` is the behind-the-hunter placement, and that update is left to the game. Some weapon checks in the same function take a fixed distance instead of the preset; the tilt does not apply on that path.

Safeguards: CMake finds the generated unit that holds the camera update, and without that unit the option stays off. At start-up the driver compares twenty-nine instructions of the game (listed in `game_camera.cpp`) with what it expects and stays out, saying which differs, if any does. The wrapper is installed only when the option is on, or when lock-on wants it (from the first frame, by default): a player who turns both off before starting keeps that unit on its direct calls, and the feature costs nothing. Guest RAM is never scanned.

### Lock-on

**Lock on** (T or the middle mouse button; R3 on a gamepad, L3 left-handed; Game → Lock-on) selects a large monster. **Lock-on style** chooses what the camera does with that selection. *Follow* (the default) keeps the camera on the monster while it moves. **Focus/Follow Camera** turns that tracking off and on again, and the reticle with it; the monster stays selected, and the right stick turns both off without dropping the portrait. *Focus* keeps the selection and leaves the camera where it is until Focus/Follow Camera is tapped, unless that button is also Lock on, when the tap that selects also starts the turn. The default pad has them apart: R3 selects and LT is Focus/Follow Camera. With nothing bound to **Focus/Follow Camera** in Controls, that button is L. LB stays L on the default pad, so the camera behind the hunter and item use stay on the shoulder. It works in the camera that follows the hunter in a quest; the hunter moves relative to the camera as always, so while *Follow* holds, pushing forward heads for the monster.

- **A tap** with nothing locked picks the nearest monster within 60° either side of where the camera looks, or the nearest one when none is in view. It reads the quest entity list and keeps an entry that is not a player and that has both current and maximum health above zero. A companion, whose maximum health is 0, is left out. A player is left out, and so is any hunter, yours or an ad hoc partner, recognised by its vtable (`0x089BB3CC`) because its list node is not always marked as a player. A hunter's `+0x1E8` byte is not a species, so it used to show up as a Lao-Shan Lung or Rathian icon and take the lock. `MHFU_LOCK_ON_HUNTERS=1` lets hunters in again, as earlier versions did. When any entry is marked large (a method in the em overlay, or class id 32, 34 or 36), only those are locked, and one in another area or dead still counts: a small monster does not take its place, a tap locks nothing, and no reticle is shown. A known small species is left out either way, including when it is marked large and when the quest marks nothing large, so it is not locked and it is not drawn on the hunt icon. That is the small monsters the icon sheet also has a cell for: Kelbi, Mosswine, Bullfango, Felyne, Aptonoth, Genprey, Velociprey, Vespoid, Melynx, Hornetaur, Apceros, Ioprey, Cephalos, Giaprey, Shakalaka, Blango, Conga, Remobra, Hermitaur, Anteka, Popo and Ceanataur. Their large relatives stay. A quest that marks nothing large can still lock any other entry on the list with a health pool.
- **Another tap** moves to the next one in that list; after the last one, a tap lets go. With one monster, the second tap lets go. When that button is also Focus/Follow Camera (a binding of your own; the presets keep them apart), a Focus tap centers instead of moving on, and a Follow tap while tracking is paused turns tracking on again.
- **It lets go by itself** when the monster dies or is captured (its health or its maximum health falls to zero), gets more than 6,000 units away, leaves for another area, or the quest ends. A monster in another area stays on the list with its health. Its zone is the halfword at entity `+0x29A`, and the loaded stage is the halfword at GameSys `+0x6AF0E` (the pointer is the word at `0x089CC558`), the same compare `0x08813630` makes. A different zone is not a target, wherever the monster stands, and a lock already on it lets go. With no GameSys pointer the zone is unknown and the monster stays selectable. A dead monster, health at or below zero while its maximum stays above zero, is not a target either. In *Follow* the right stick or Focus/Follow Camera turns tracking off and leaves the monster selected, portrait included, and turns the reticle off with the tracking. Another push of the stick does not turn either back on; Focus/Follow Camera does. The mouse or the camera keys, past about 6° so a nudge does not count, the D-pad's camera commands, or recentring with L still let go. Holding L for the item bar does not.
- **Focus** leaves the camera alone after the selection when Focus/Follow Camera is a different button. When one button is both, the tap that selects also starts the turn, and a later tap centers again instead of moving to the next monster. A tap of the focus button centers it on the selected monster and then leaves it there; the selection stays. With nothing bound to Focus/Follow Camera, that button is L. Binding it to something else makes that input the tap, and L recenters behind the hunter again. R does not center the camera unless Focus/Follow Camera is bound to it. The turn works from any position while the monster is selected, however close it stands, even with the camera facing straight away from it, and when the monster is already on screen. A second tap within a third of a second (ten camera updates) is left to the game, which recenters behind the hunter; the selection stays, so a later tap centers again. A tap after that third of a second, while the turn is still on its way, does not stop it. Once the camera has arrived, another tap centers again. The game's recenter writes the hunter's facing plus half a turn to the target yaw (`0x08887508`) when `0x0888e494` sees bit `0x200` set on the halfword at camera `+0x86` and on the hunter's button word at `+1152` (`0x0888e54c`). It also recenters when `0x09A6EE48` returns 1, which it does when the word at hunter `+1160` has L's bit. While the focus button is L, that press does not leave the game's snap in place: the port eases the camera itself, the same ease a Focus camera bind uses, and puts the yaw, the eye and the snap countdown (`+0x8D`) back before the update returns. A non-negative gate at camera `+0x91` would still step toward the hunter (`0x0888755C`), so that gate is held negative for the update only and then restored. L stays down, so the item bar is unchanged. A second tap, after the button has been released, is left to the game, which recenters behind the hunter. When the focus bind is R, the port replaces the hunter's facing for that update with the direction of the monster. `0x088874F4` loads that word and `0x08887508` stores it plus half a turn, then the facing is put back. A bind that is neither L nor R turns the camera itself. Follow leaves the facing alone. If the game recenters some other way, Focus puts the yaw, the eye and the snap countdown back before the update returns, except for that second tap. The reticle stays off after a selection. The focus button shows it, a red ring with a cross through it and four ticks, for a third of a second and then it goes; the selection stays. Turning the camera by hand or with the D-pad during that turn stops the turn and keeps the monster selected. With nothing selected, L still recenters behind the hunter. A Lock on button that is not also Focus/Follow Camera still only changes the selection.
- **While Follow holds**, the camera's yaw eases towards the monster by a fifth of the rest each camera update, at most 11.25° an update, so it follows a charge without jumping. Focus's tap takes three tenths of the rest each update. Off the picture one update is at most 22.5°, and 11.25° once the monster is inside it, so a monster already on screen is centered rather than left where it is, and a quarter turn hands the camera back in under half a second. It lands on the monster once within 2°, and gives up after one second if a moving monster keeps it further away. The picture's width comes from the game's vertical field of view, 0.872222 radians, at 480 by 272. When the monster stands on top of the hunter (within 300 units of the camera's look-at point) its direction means little and Follow's yaw holds, coming back in over the next 400 units (Focus's turn does not hold); with nearly half a turn to go, the turn keeps the way it was going instead of flipping between the two. Follow tilts by half the monster's elevation as seen from the hunter, at most 10° up or 20° down from where it was. Focus's tap tilts toward that elevation, down to −30° or up to 60°, and lands on that tilt when the yaw does. Either way, tilting with the [analog camera](#analog-camera) keeps that tilt for the rest of the lock. The second stick is kept from the game meanwhile, so its own turn and presets do not fight the lock.
- **While a bow or a bowgun aims**, the camera follows the aim as usual, and the lock resumes when the aim ends.
- **The marker** (*Lock-on marker*, on by default) is a red ring with a cross through it and four ticks, drawn over the locked monster a little above its feet. The view the world is drawn with has no translation, so the reticle is placed with a view built from the follow camera's eye (`+0`) and look-at (`+0x10`), through the projection and viewport of that draw. Follow keeps it on the monster while the camera is tracking, in the follow camera and in the aim camera that replaces it while a bow or a bowgun aims. The right stick or Focus/Follow Camera turns it off with the tracking; the selection stays, and the same button turns both back on. In Focus, selecting leaves it off when that button is not the focus button. The focus button shows it for a third of a second. When Lock on and the focus button are one button, the reticle comes up with the tap that selects. A monster behind the camera or off the picture is not marked. The reticle is left out of the game's own frames and screenshots, like the rest of the interface.
- **Monster Icon** sits at the bottom left of the picture, against its left margin, one for each large monster on that list, while Lock-on is on. It comes from the game's icon sheet, chosen by the species byte at entity `+0x1E8`, with no panel behind it. A species the sheet has no picture for takes no place in the row, so the icons after it stay against the margin; `MHFU_TRACE_LOCK_ON=1` prints its id once. *Monster Icon* (Video, 50–200%, 100 by default) changes its size. Each icon keeps the place it took the first time that monster was shown this quest. The game puts the newest entity-list node first and reinserts a monster when it changes area; that does not move the icon. Leaving the area, or dying, does not move it either. A new quest starts the order again. A monster in another area stays up: the icon is darkened and reads *Diff. Area*, inset inside the icon as *Monster Icon* changes its size, because lock-on cannot select it. A dead monster stays up too, darkened the same way and with no label, and lock-on will not select it. While lock-on has a living monster in this area selected, thin red brackets sit on that icon's corners. A loading screen shows no icon and no stand-in. The icons are shown only while the game draws its quest clock, in that frame or the one before, so they are gone once the screens after a quest begin, and during a cutscene. While *Hide HUD* is on the clock is not drawn, and the icons stay as before. `MHFU_HUNT_ICON_WITH_HUD=0` shows them whenever a quest runs, as earlier versions did. Turning Lock-on off hides the icons. They are part of the port's interface, so they are left out of the game's own frames.

The bind acts when it is let go. Other buttons may be held, so a sprint or an attack does not swallow the press. A longer chord that contains the bind does not lock on. Where lock-on is R3, as on the default pad, L3 + R3 opens the menu and R3 + D-pad left takes a screenshot. Sharing the button with Focus/Follow Camera is not a conflict: one button then both selects and starts the camera. *Lock-on* (Video) turns it off: the bind then does nothing, and the monster icon is hidden.

It is not a second camera. The [analog camera](#analog-camera)'s driver asks lock-on, on each update of the ordinary follow camera, for the yaw and pitch, and writes them where it writes its own, before the game's collision checks. The driver's hook is installed on the first tap, so with *Analog camera* off and lock-on never used, the game's camera code runs exactly as before. The code is `host/camera/lock_on.{hpp,cpp}`.

What it reads in this executable:

| What | Where | How it was found |
| --- | --- | --- |
| On a quest | `game_task.ovl` loaded at `0x09A5F300` | The overlay slot table in `mhfu_profile.hpp`; the header is the usual `MWo3` block |
| Entity list | The word at `0x09D19860` is the manager. The newest node is at manager `+0x19030`. Node `+4` is the next older node, `+12` the entity, `+36` a type byte (`1` is a player) | `game_sub` publishes the manager at `0x09C2BCF4` and inserts a node at `0x09C57CA0`. Large-monster overlays call that insert with type `0` (and some with `3` or `4`). The resident player constructor uses type `1` |
| Position | Entity `+0x200`, three floats, y up | The follow camera copies the hunter's `+512` and `+520` into the camera, and `em01` loads all three as floats |
| Health, most health | Entity `+0x2E4` and `+0x41E` (s16) | `em01` `0x09D1A398` stores the same value to both when the monster is created. A companion's most health stays 0, so it is skipped |
| Monster id | Entity `+0x1E8` (u8) | `0x088135D0` loads the byte and compares it with species constants. The health halfword at `+0x2E4` is read from the same pointer, so the id is still there when the monster is in another zone |
| Zone | Entity `+0x29A` (u16), compared with GameSys `+0x6AF0E` | `0x08813630` loads the object's halfword and the loaded stage. The GameSys pointer is the word at `0x089CC558` (`0x089D0000` minus 15016), and the stage halfword is 458752 minus 20722 bytes into that object |
| Camera eye, look-at | Camera structure `+0x0`, `+0x10` | The follow camera at `0x08886E50`; the camera is the parent plus `0xA0` |
| Camera yaw | `+0x80` target, `+0x82` filtered, in 65536ths of a turn | The same update; the host writes both before the game's quarter-step filter |

The marker does not project through the view a draw uploaded. That view has no translation, so the monster's world position misses the picture and the reticle is not drawn. The follow camera keeps its eye at `+0` and its look-at point at `+0x10`; the marker builds its view from those and uses the projection and viewport of the perspective draw with the most vertices. Portable 3rd kept a view and a projection on the camera object at `0x08A2F958`. This executable's camera object is the pointer at `0x089CBD04`, and the matrices stored on it do not place the monster on the picture. This has not been tried in a quest.

### Screenshots

**Screenshot** (F12 or Print Screen; R3 + D-pad left on a gamepad; *Take a screenshot* in the menu's System section) saves the game's picture as a PNG in `screenshots/` in the [data directory](#installer), or in the portable folder's `data/screenshots/` for a [portable copy](#portable-copy), named by the time, such as `Pokee_2026-09-27_14-03-22.png` (`_2`, `_3` for more in one second). It is the frame the game last drew, at the size it is drawn at: *Resolution* ×2 gives 960×544, ×4 1920×1088, and Auto the window's size, whatever the window shows it at. The picture is the game's own render target, so the port's interface is never in it: not the menu, the performance overlay or the note that says where the file went, which shows at the bottom of the window for three seconds. What the game draws itself, its HUD and the names over hunters, is in it, unless the [HUD is hidden](#hiding-the-hud). It works in play and from the menu (the game's picture behind the menu). The file is written on a background thread; the log says `[screenshot] <path> (<width>x<height>)` once it is.

### Diagnostics

| Variable | Effect |
| --- | --- |
| `MHFU_STRICT_HLE=1` | Do not bind logging stubs; stop at the first unimplemented import |
| `MHFU_TRACE_KERNEL=1`, `MHFU_TRACE_IO=1` | Trace thread and file activity, the memory stick's writes and renames included. Each kernel `block` and `wake` line gives the emulated time (`at=`); a `block` line also gives where the wait returns to in the game's code (`ra=`) and, for a timed wait, how long it asked for (`for=`). `MHFU_TRACE_KERNEL=user_main`, or another thread's name, traces that thread only, little enough to run through a hunt |
| `MHFU_TRACE_LOAD=1` | Four `[loadtrace]` lines a second: real and emulated time, flips, disc and memory stick reads, the loudest audio sample, time spent holding the game to real time, and which guest threads had the CPU. What [Fast loading](#fast-loading) was measured with |
| `MHFU_TRACE_MODS=1` | What the [mods](#mods) change at start and after each change, every `DATA.BIN` read they serve, and each write made after an overlay loads |
| `MHFU_TRACE_DATA_BIN=1` | While a mod is on, the id of every `DATA.BIN` file the game reads (`[mods] data 034B (32768 bytes)`): how to find the file behind a model on screen |
| `MHFU_TRACE_LAYERED_ARMOR=1` | Each armor part [layered armor](#layered-armor) draws differently (`[layered] Chest: model 22 instead of the worn piece's`). A change of the switch or the pieces logs that the next equipment-box change reads that look |
| `MHFU_TRACE_SAVEDATA=1` | Log every field of each save-data request and each status poll |
| `MHFU_TRACE_SYNC=1` | Trace semaphores, event flags and mutexes; `MHFU_TRACE_SYNC_LIMIT` caps the lines (default 4000) |
| `MHFU_STARVATION_INTERVAL` | Dispatches between virtual-clock advances in code that never calls an import |
| `MHFU_TRACE_GE=1` | Log the first draws of the run with their state |
| `MHFU_CHECK_DIRECT_VERTICES=1` | Expand each transformed draw as well and compare it, vertex by vertex and byte for byte, with what its index list names; prints `[direct-check] N draws compared, M differed` every 300 frames. Slow |
| `MHFU_TRACE_STALLS=1` | Where the render thread waits, once a second and for every slow frame; `MHFU_TRACE_STALLS_MS` sets what is slow (default 40). See [Where the render thread waits](#where-the-render-thread-waits) |
| `MHFU_TRACE_STALL_STACKS=1` | Linux: when the game thread has been inside one HLE call for more than 45 ms, interrupt it and print where it is (`[stall-stack]` and one address per frame), again every 15 ms up to four times a call. Names stall the stall trace can only place in a call; addresses in Pokee resolve with `addr2line -f -C -e Pokee` |
| `MHFU_TRACE_INTERPOLATION=1` | With a frame rate above 30, an `[interp]` line a second: the rate running and the one chosen, how many draws matched, the camera's largest turn and the eye's largest move, cuts by reason, presents, the time of a blended present and of recording its draw calls, the draw calls and GPU time of one in-between frame, the plain presents, skipped ones, the latest one, and the delay from a frame's moment to its present. A second line says why presents showed a frame as it is (`at the newest frame`: one per game frame at 60, 90 and 120, as it should be; `at the older`, `not blended (cut)`, `textures dropped`) and which were not made (`display busy`: no swapchain image within 3 ms; `over budget`: presents during the game's code had taken half a frame). It also says when the rate steps down or up, and why. `frames` adds a line per game frame, `presents` a line per present with its blend factor |
| `MHFU_CAMERA_GAME_FOLLOW=1` | Leave the game's soft follow (behind-the-hunter gate at 0) to the game, as before: the analog camera stands aside, and the right stick does nothing, for as long as the gate stays 0 |
| `MHFU_CAMERA_AIM_AFTER_R=1` | Count action 1001 as aiming whether or not R is held, as before: after R is released the right stick keeps aiming, and the camera does not turn, for as long as the hunter stays in 1001 |
| `MHFU_CAMERA_WALL_SNAP=1` | Show the eye where the game's wall collision put it, as before: a collision that comes and goes snaps the view in and out |
| `MHFU_CAMERA_WALL_SLIDE=1` | Leave the game's choice between holding a blocked eye where the wall stopped it and pulling it in along the view line, as before: behind the hunter it is held, then snaps |
| `MHFU_CAMERA_EYE_SWEEP=1` | Start the follow camera's first wall check at the eye, as before, instead of on the view line the game checks from near the hunter: an eye already through a wall or under the floor met nothing there and stayed, so the camera caught on scenery and showed the level from below |
| `MHFU_CAMERA_LOW_EYE=1` | Let a low tilt take the eye below 80 above the hunter's feet, as before, instead of bringing it in along the view line: below level the eye went under flat ground |
| `MHFU_CAMERA_LOW_TILT=1` | Let the analog camera tilt down to −60°, as before, instead of the game's own −30°: below −30° the game raises the point it looks at, and the view flips when it lets go |
| `MHFU_PAD_AT_FLIP=1` | Read the pad at each flip, as before issue #8, instead of when the game reads it || `MHFU_PAD_FOLLOW=0` | The game keeps the first gamepad connected, as before issue #147, instead of the one a button was last pressed on; other pads then drive only the menu |
| `SDL_GAMECONTROLLERCONFIG` | SDL's own: gamepad mappings, one per line, ahead of `gamecontrollerdb.txt` and SDL's built-in ones (see [Controllers SDL does not know](#controllers-sdl-does-not-know)) |
| `MHFU_INTERPOLATION_NO_MOTION_GUARD=1` | Blend every matched pair however far it moves, and leave 3D draws without a partner where the older frame drew them, as before the guard. The `[interp] guards:` line counts what the guard does: pairs given up and how many had the same mesh drawn more than once, the largest own motion kept, draw calls moved with the camera only |
| `MHFU_INTERPOLATION_NO_NEAREST_INSTANCES=1` | Pair the instances of a mesh drawn more than once in drawing order, as before, instead of each with the nearest instance of the newer frame; the `[interp] guards:` line counts the instances given a nearer partner than drawing order's |
| `MHFU_INTERPOLATION_NO_FAR_SAMPLES=1` | Keep scenery draws whose eye moved more than 800 units in the camera's sample, as before; with it, a monster in the water in the Old Jungle's waterfall area makes many frames a `camera moved` cut. An `[interp] frame` line says how many of the sampled draws the camera was taken from (`from 100 of 240 samples`) |
| `MHFU_INTERPOLATION_SLOW_DELAY=1` | Allow for the ninth longest code time of the last 60 frames alone, as before, instead of no more than the longest of the last 15. The game flips about 20 ms after a frame's moment in the town and 6 ms in a quest; with the old estimate, the first two seconds of a quest kept the town's longer delay, and many of its presents showed the older frame again instead of a blend |
| `MHFU_INTERPOLATION_NO_NAMES=1` | Draw the names over other hunters, in a quest and in the gathering hall, where each game frame puts them, as before, instead of moving them with their hunter between frames |
| `MHFU_INTERPOLATION_NO_FLIPBOOK_GUARD=1` | Blend texture offsets that jump up to half the texture, as before, instead of holding a flipbook's step; `[interp] guards:` counts the offsets held |
| `MHFU_CHECK_REPLAY=1` | Every 150 game frames, draw the older frame again from its recording without blending and compare it with its own picture pixel by pixel: `[interp] replay check … 0 of N pixels differ`. With `MHFU_SCREENSHOT_DIR` it also writes the older frame, the frame drawn again, the frame halfway to the newer one and the newer one as `replay_<frame>_*.bmp` |
| `MHFU_FRAME_RATE_CYCLE=30,60,90` | Switch the frame rate to the next one listed every `MHFU_FRAME_RATE_CYCLE_SECONDS` (default 10), to compare rates on one scene in one run |
| `MHFU_INTERPOLATION_EXTRA_MS=N` | Add N ms of busy CPU time to every blended present, to see the frame rate step down on a fast machine as it would on a slow one |
| `MHFU_TRACE_3D=1` | Per-frame counts of transformed draws, their targets and screen-space bounds |
| `MHFU_FIND_CAMERA=1` | Hunt guest memory for the words the camera is kept in, by what they do: one hunt against the yaw the view matrix reports and one against its pitch, trying every word as a float, a 32-bit and a 16-bit number, and as an angle, a rate, or a rate read a frame early. `MHFU_FIND_CAMERA_OUT` names a file the surviving list is written to |
| `MHFU_FIND_STEP=N` | Keep the 16-bit fields that move by exactly N between turning frames. The camera's own yaw moves by 1150, which is 1150/65536 of a turn |
| `MHFU_FIND_FLOAT=V`, `MHFU_FIND_INT32=N` | List every place in guest memory holding that value. `MHFU_FIND_INT16_WIDE=1` searches 16-bit fields instead of 32-bit |
| `MHFU_POKE_FOUND=V`, `MHFU_POKE_INT32=N` | Write a different value into what those found, once a frame. With more than one match, `MHFU_POKE_WHICH` must name an index or say `all`, since writing every place that held a number also writes whatever else held it |
| `MHFU_POKE_FLOAT=0xADDRESS:V[,...]` | Write floats into guest memory once a frame, to turn a guess about a constant into a measurement |
| `MHFU_TRACE_CAMERA_STATE=path.csv` | Trace follow-camera updates: frame, guest camera address, enabled state, stick axes, yaw target/current, preset height and distance, owned pitch, whether the update was left to the game, the snap byte and count, the behind-the-hunter gate, the turn and vertical button halfwords, the R latch, whether the stick stayed the driver's, whether the hunter was aiming and whether the game's option turns the placement off. Lines starting with `F` give each game frame's stick rate, whether the stick stayed the driver's, whether the hunter was aiming, the last follow update, and the hunter's R and action; lines starting with `P` give, after each follow update and its wall collision, the eye, the point looked at, their distance and horizontal distance, the preset's distance, the yaw, how the first wall check went (-1 from the eye, 0 or 1 a miss or hit from the view line) and the eye's height above the hunter's feet; lines starting with `U` mark an update that came back unfinished, with the address it stopped at, whose P line follows once it has finished. Does not enable memory searches or renderer tracing |
| `MHFU_TRACE_CAMERA_MODES=path.csv` | Every call of the camera update, and one line per game flip, with the parent object, the stick, the yaw fields and the whole camera structure in hex. Needs Analog camera turned on once in the session, which installs the hook the trace runs in |
| `MHFU_TRACE_AIM=path.csv` | While a bow or a bowgun is in the aim action: one line per camera update with the stick's and the mouse's degrees, the mouse degrees carried, the game's own step (yaw:pitch), the step taken off in advance and the step the driver made; and one line per frame the mouse shows the game a direction. For telling the game's steps from the driver's. It also prints `[aim]` lines: the aim wrapper put in place (`0x09AA4DD0`, which aims in action 1001); the hunter's action whenever R is held and it changes; and when an aim starts, ends, and once a second while it lasts, whether the wrapper is in place, whether it is the bow's aim (`bow`, where the right stick is pressed on the D-pad), whether the camera driver or lock-on holds the stick (`driving`), whether the right stick goes to the aim (`boost`), and the calls of the wrapped aim function |
| `MHFU_TRACE_WEAPON=1` | On each press of R, the followed hunter (1664 bytes) and the played hunter's record as hex: `[weapon] frame=… hunter=0x… entity=… record=…`. For finding where the weapon class and the drawn state are kept |
| `MHFU_TRACE_LOCK_ON=1` | One `[lockon] update` line per camera update while [lock-on](#lock-on) holds a monster: the monster's position, the camera's look-at point, its yaw before and after and the yaw wanted, the pitch before and after, and the hand-turn built up. On each tap, and whenever a new monster icon appears, one `[lockon]   node` line per entity on the quest list, players and left-out entities included: node type, class, vtable, the byte at `+0x1E8`, health, stage and position. On each tap, one `[lockon] candidate` line per healthy non-player entity on the quest list (its address, node type, class id, vtable and the method at `+8`, whether a method lies in the large-monster overlay, health and position) and whether any of them was marked large. Locking, moving to another monster and letting go are logged whether or not it is set |
| `MHFU_DAMAGE_NUMBERS_SYNC=0` | In ad hoc play, [damage numbers](#damage-numbers) neither send the hits this console applies nor receive the others': a hit applied on another console shows as its health drop, when that arrives, white, as before |
| `MHFU_DAMAGE_NUMBERS_HEALTH_WATCH=0` | [Damage numbers](#damage-numbers) show only the hits this console applies, without the per-frame reading of every monster's health that shows the hits other players' consoles applied in ad hoc play, so both can be compared |
| `MHFU_POUCH_PAGE=0` | Turns the [fourth pouch page](#fourth-pouch-page) off: the hooks never run and every hunter has three pages |
| `MHFU_POUCH_PAGE_GRANT=1` | Gives every hunter the [fourth pouch page](#fourth-pouch-page), for testing; kept in `pouch_page.txt` only when the game saves |
| `MHFU_TRACE_POUCH_PAGE=1` | One `[pouch page]` line when a hunter's page is read or kept, and one the first time each site in `config/site_hooks.txt` runs, with its image and address |
| `MHFU_FAST_ITEM_BOX=0` | Restores the game's one-second pause and fly-to-pouch icon after taking an item from the box in a quest, so both timings can be compared |
| `MHFU_TRACE_ITEM_BOX=1` | One `[item box]` line per quest HUD frame while the box's lock or the icon's flight is non-zero: the HUD and player addresses, the lock (30 frames) and the flight (31 frames), and what was left of them |
| `MHFU_TRACE_GIVE=1` | One `[give]` line per call of the game's Give check: the hunter, the selected item's rarity byte (0 is Rare 1), whether it was lifted, and the answer (1 is yes) |
| `MHFU_TRACE_ELEMENT=1` | One `[element]` line each time True Attack Value changes one of the game's element or status formats, with the return address of the `sprintf` call and the format before and after, and a line when they are put back |
| `MHFU_TRACE_HALL_EQUIPMENT=1` | With *Change Equipment in the Guild Hall* ([In-game menu](#in-game-menu), Mods) on, one `[hall equipment]` line each time Change Equipment is chosen in the Guild Hall's box, with the box's address, and one each time the closed equipment screen refreshes this hunter's entry for the hall |
| `MHFU_TRACE_ITEM_BAR=1` | With the empty item slot on, one `[item bar]` line per L+○ or L+□ scroll: the direction, the slot before and after, where the game alone would have gone, the empty slot, and the four bytes past the pouch. Also a line each time the selected slot changes |
| `MHFU_TRACE_ITEM_SETS=1` | One `[item sets]` line when the box menu gets its Item Sets entry, when Item Sets is chosen (with the box record's address), for each set loaded (with whether the box was full and how many items it was short of), registered or deleted, and when the box goes back to its menu |
| `MHFU_TRACE_TITLE_MENU=1` | Each cursor move in the title menu that is changed to skip the hidden *Data Install* while the [install data](#where-saves-live-and-how-to-back-them-up) is served (`[title menu] cursor 4 given as 5 (pressed 0x40)`) |
| `MHFU_TRACE_EVENT_QUESTS=1` | The [event quests](#event-quests) read from `event_quests/` and where they were placed, each Guild Hall event list and Training School challenge list with how many it added (`[event quests] list: 25 with 19 built in`), and each lookup of a quest by id with the info block found |
| `MHFU_TRACE_KEY_QUESTS=1` | With *Show Key Quests* on, one `[key quests]` line for each quest pack found in memory after the game read it (its entry, its address and how many names were marked), for one not found, and when the switch goes off and the names are put back |
| `MHFU_TRACE_HALL_MEAL=1` | With *Eat in the Guild Hall* on, one `[hall meal]` line when the kitchen's menu opens at a table in the Guild Hall (with the hunter and the kitchen object), when a meal is paid and served, when one is refused, and when the menu closes and why |
| `MHFU_TRACE_GATHER=1` | One `[gather]` line when the point under the [gather icon](#gather-icon) changes, or when there is none: its address, id, kind and position. `[gather] carve` gives a body's record address and its halfwords from `+16` when it comes into reach. `[gather] hold` is each repeated press of [Hold to gather](#gather-icon). `[gather] farm` gives the Pokke Farm mining outcrop or bug thicket in reach: its point address, type, index and uses left. `[gather] fishing` gives the fishing spot in reach, its record address and position, or `none` |
| `MHFU_TRACE_HITS=1` | Each call of the routine that takes health off a monster (the one [damage numbers](#damage-numbers) watch, `0x09AC7A08`) whose return finds the health changed: a `[hit] call` line with the arguments, the saved registers, `ra`, `sp`, the float arguments and the health; the chain of return addresses and the functions they lie in (`ra@start`); the 32 words on the stack, code-looking ones marked `*`; the 16 words behind each argument that points into RAM; a `[hit]   near` line listing every three floats, in the callers' stack frames, the records they point at and the monster, that lie on the monster above its feet and are not a hunter's position (a1's record holds the attacking hunter's position, not where the hit landed); and the health before and after. Each damage number also prints, when it starts to fade, where it sits and where the monster is by then. The monster's own update calls the routine every frame without changing anything; such a caller is named once and its calls are not printed. A drop in a monster's health between two frames that no call made is reported as written elsewhere. Each hit the game computed (see [damage numbers](#damage-numbers)) is a `[dmg] … computed` line: the monster, the part, the attacker and the player it counts as, the attack record, the total and its element part, the attack's element bits and the element they name, its critical marks (`80` critical, `40` negative), the effective hitzone, the monster's damage scale, and the number shown; then a `[dmg]   landing` line with the game's own point of contact and the point the earlier rules give, each with its distance from the monster's nearest bone, and how far apart the two are. To match two consoles' logs in ad hoc play, each `[dmg]` line also gives the host's wall clock in milliseconds (`wall=`, the same for two instances on one machine, where `t=` is each one's emulated clock) and is followed by a `[dmg]   hunters` line with each hunter, numbered as the player it counts as, and its position, `(camera)` marking the one the camera follows; and whenever the quest's hunters or monsters change, or the camera follows another hunter, a `[roster]` line per entity on the quest list gives its place from both ends, node type, vtable, what it is (`me`, `hunter`, `monster` or `other`), species, health and position. Works with the setting off. `MHFU_TRACE_HITS_LIMIT` caps the hits printed (default 200). How the point of contact is being looked for |
| `MHFU_TRACE_HITS_RAM=directory` | With `MHFU_TRACE_HITS`: user RAM (`0x08800000`–`0x0A000000`) as each of the first 12 printed hits begins, written as `hit_NN.bin`. For comparing a hit on one part with a hit on another, and for reading the callers' code; about 24 MB each, and each call of the routine is copied until then, so the game may slow while it runs |
| `MHFU_TRACE_BODIES=1` | How long a dead monster lies before the game removes it, and which of its fields count that time. A `[bodies] … died` line when a monster on the quest entity list reaches zero health, with its species and stage. When the body leaves the list, its slot comes back to life or changes species, a line with the frames and seconds it lay there, then up to 16 fields from the first 4 KiB of the entity that moved like a timer: in one direction only, at least 20 times, by exactly 1 for a byte or halfword and in even steps for a float, with the frame of its first step (`s16 +0x2A4: 900 -> 0 in 900 changes from frame 3`). A field's first change may be a jump, where the timer starts. Then up to 32 bytes that held still from the death and changed at most four times, the first of them in the body's last 120 frames, the earliest first (`flag +0x3C: 0x1 -> 0x5 at frame 470 of 477 (2 changes)`): a state or flag the removal may follow from. Leaving a quest forgets the bodies without a report. With `MHFU_TRACE_BODIES_WATCH=OFFSET` (such as `0x244`, where a Giaprey's body sinks) and `PSPRECOMP_WATCH_WRITE` set to any address, the first Velociprey, Genprey, Ioprey or Giaprey body has that field watched: every guest write to it is a `[watch-write]` line with the code address that made it. Arming the watch slows every store. How the despawn timer of small monsters is being looked for |
| `MHFU_TRACE_SHARPNESS=1` | One `[sharpness]` line whenever the [modern sharpness gauge](#modern-sharpness-gauge)'s reading changes: the points and the most, the colour as the gauge finds it and the game's own colour byte (+1522), the colour's start and end, and how full it is. Works with the setting off |
| `MHFU_TRACE_VIEWS=N` | Every N game frames, one line per distinct view and projection the frame's transformed draws used: how many draws and vertices, the eye position, yaw and pitch, the matrices and the render targets |
| `MHFU_TRACE_CAMERA=1` | One line per frame for the camera the game itself set: the second stick's offset from centre, the yaw and pitch read out of the frame's busiest view matrix, the turn since the previous frame, the view's up axis (a negative second number is an upside-down picture), and the camera's world position. Reads the game's own camera, so it tells a stepped turn from a continuous one |
| `MHFU_TRACE_FOV=1` | One line whenever the camera's field of view, the one its projection was built with or the one its preset asks for changes, in radians, with the camera's address and the scale *Field of view* applies |
| `MHFU_TRACE_WHITE_TEXTURES=1` | Each texture the decoder cannot decode, once, with its address, size, format, swizzle and palette: those draws are made with a white texture instead, so this is the first thing to check when something draws white |
| `MHFU_TRACE_FB_TEXTURES=1` | Each distinct texture that lies in a framebuffer the renderer drew (with both layouts), each large texture, `sceDmacMemcpy` copies into or out of VRAM, GE block transfers, new render targets, and the GE commands the renderer ignores. Add `PSPRECOMP_TRACE_VRAM_READS=1` to log game code reading VRAM with the CPU, per 64 KiB block and at most once a second |
| `MHFU_TRACE_SPRITES=N` | Every through-mode draw of presented frame N: sprites one by one, other primitives by their bounds, with positions, depth, texture coordinates and texture state, the address of the draw's PRIM command (`at=`), where the list's innermost CALL returns (`ret=`) and where its vertices are. `N/K` traces frame N and every Kth frame after it |
| `MHFU_TRACE_HUD=N/K` | From game frame N, every Kth frame: each 2D packet the game files into its display list (address, length, bucket), whether it is [HUD](#hiding-the-hud), and the guest call stack it was filed from (`return address@function start`); and each call into the game's text functions. The list built in one frame is drawn in the next, so pair it with `MHFU_TRACE_SPRITES=N+1/K` |
| `MHFU_HUD_SCALE_SITES=0` | [UI scaling](#ui-scaling) tells the quest HUD's parts apart by their place and size on screen, as before it knew which function drew each part. It still knows which draws the quest HUD filed, so menus filed by other code keep their size; for comparing the two |
| `MHFU_HUD_SCALE_GEOMETRY=1` | [UI scaling](#ui-scaling) tells the quest HUD's parts apart by their place and size on screen alone, as before it knew which code filed each draw. Menus in the same corners can then change size with the HUD; for comparing the two |
| `MHFU_TRACE_CAPTURE=1` | Every [screenshot](#screenshots) also traces the frames after it, so one run can catch moments that have no frame number in advance: the 2D packets and text calls of two frames, as `MHFU_TRACE_HUD` does, then the sprites those lists draw, as `MHFU_TRACE_SPRITES` does. Each sprite line adds `owner=` (`quest` for the quest HUD's draws, `other`, or `unknown` when the game's code differs), `site=` (the part of the quest HUD that drew it, as in [UI scaling](#ui-scaling): `status`, `weapon`, `map`, `items`, `ammo-bar`, `names`, `window`, `unlisted`, or `none` for a draw that is not the quest HUD's), `call=` (the return address into the part's function that led to the draw, 0 for text) and `name=1` with the point the game placed it when the draw takes the Names size. `[capture]` gives the frames |
| `MHFU_TRACE_HALL_NAMES=1` | Each draw of a name over a hunter in the village and the gathering hall: `[hallname]` lines with the letters or icon, the point the game placed it at, where it was drawn and where it went, the point it was sized about (`kept` for a label's middle, `point` for the game's) and the caller; and the 2D draws near a label that were not taken for part of it |
| `MHFU_HUD_OVERHEAD_NAME=0` | [UI scaling](#ui-scaling) stops telling apart the name over a teammate in an ad hoc quest. It then follows the shapes on screen again, as before: it takes the party list's size whenever it crosses a row of that list, and the Names size never reaches it. For comparing the two |
| `MHFU_HUD_HALL_NAMES=0` | [UI scaling](#ui-scaling) leaves the names over hunters in the village and the gathering hall at the size the game draws, as before; Names then applies only in a quest. Frame interpolation still moves them with their hunter. For comparing the two |
| `MHFU_TRACE_TEXT_FROM=addr[,addr…]` | Up to four guest functions, in hex. Each call that adds entries to one of the game's text queues while it runs, by any path, prints a `[textfrom]` line once per function, queue and caller: the queue, each new entry's address and point (where the text is drawn), whether the call stack is [HUD](#hiding-the-hud), and that stack. For finding which code queued a piece of text the watched text calls did not see |
| `MHFU_TRACE_HUD_SCALE=1` | Each distinct sprite the [UI scaling](#ui-scaling) sizes move, once: the piece it was sized as (map, status, items, sharpness, weapon, bowgun), the rule that took it (`site` for the part that drew it, or without one `shape`, `ammo-menu`, `map-texture`, `map-mark`), where the game drew it, its texture and framebuffer, and where it was drawn instead. `MHFU_TRACE_HUD_SCALE=all` adds each sprite left as drawn, with the rule that left it (`site` for a part left as drawn, `no-piece`, `obtain-banner`, `notice-border`, `reticle`, `piece-not-up`, `not-owner`) and, after `was`, the piece it was nearly taken for |
| `MHFU_TRACE_MAP_MARKS=1` | While the quest HUD is up (as for the monster icon), every draw near the area map, every frame: `[mapdraw]` for each draw whatever its primitive (screen bounds for 2D draws, untransformed bounds for small orthographic ones), with its texture, owner and colour, and `[mapmark]` for each quad the [UI scaling](#ui-scaling) decides, with the piece and rule, where it went, and whether the map, a window over the map, the item list and the clock were up. For marks that leave the map, such as a paintball's ring or a monster's ping. Produces a line per mark per frame. `=new` prints only the `[mapdraw]` lines, each kind of draw (primitive, vertex count, texture, caller) once, orthographic draws of any size included, and `[mapmark]` only for quads of 28 pixels or more from the map-icon atlas, such as a monster's ping, so it can stay on for a whole hunt |
| `MHFU_TRACE_MATERIAL=1` | Every distinct value the game writes to the GE material registers |
| `MHFU_TRACE_LIGHTING=1` | Every distinct value the game writes to the GE light and fog registers, and one line per distinct register state a lit draw is made with |
| `MHFU_TRACE_TEXTURE_PACK=1` | Each texture's texture pack key and what the pack does with it (its image, kept as it is, or not listed), each image decoded and uploaded, and once a second the draws that used a replacement and the images and megabytes on the GPU |
| `MHFU_SAMPLED_TEXTURE_KEYS=1` | Recognise changed textures of up to 64 KiB by one word in every 256 bytes, as for larger ones, instead of by all of their contents. Glyphs the game adds to its text atlas are then often missed, and text shows stale or missing characters |
| `MHFU_NO_CULL=1`, `MHFU_NO_DEPTH=1` | Disable face culling or the depth test, to bisect missing geometry |
| `MHFU_NO_THROUGH_DEPTH=1` | Leave 2D draws out of the depth buffer. Otherwise a 2D image that writes depth, such as a weapon icon in the shop, stays in front of a model drawn over it |
| `MHFU_TRACE_AUDIO=1` | One line per second of output: frames, peak, RMS, silence and drops |
| `MHFU_TRACE_AUDIO_CALLS=1` | An `[audio-call]` line for each audio channel reserved, released or given a new length or format, and for each output that waits longer than its own buffer lasts (one draining what the channel had queued, not pacing a stream), with the guest thread and the emulated time |
| `MHFU_TRACE_ATRAC=1` | Every `sceAtrac3plus` call with its arguments, result and decode position |
| `MHFU_TRACE_UI=1` | Each [sharper copy](#sharper-text-and-2d-textures) of a glyph atlas page or a 2D texture as it is made: its address, sizes and milliseconds, and how many of a page's cells were drawn again |
| `MHFU_TRACE_FONT=1` | Every `sceLibFont` call with its arguments: the font the game asks for, the font info and character metrics returned, and each glyph image's buffer and 26.6 position, with the caller's return address |
| `MHFU_TRACE_MPEG=1` | Every `sceMpeg` and `sceJpegCsc` call, and each call the ring buffer makes to the game's read callback |
| `MHFU_SAS_NO_ENV=1` | Hold every SAS voice at full envelope, to separate an envelope bug from a decoding one |
| `MHFU_TRACE_PAD=1` | Log the pad state whenever it changes (`[pad]`), and each chord pressed, waiting, tapped and let go, with when its last input went down (`[chord] <ms> pad pressed Pad LB + Pad West (last input down 0 ms before)`) |
| `MHFU_TRACE_PAD_READS=1` | Log every read the game makes of the pad, with the emulated time and the buttons (`[pad-read]`); the game reads it once a frame |
| `MHFU_TRACE_OSK=1` | Every keyboard utility call with the status it returns, and the words of the parameter block, its first field and the strings they point to |
| `MHFU_TRACE_ADHOC=1` | Every ad hoc, network dialog and wireless call with its arguments and result, and every packet header sent to or received from the ad hoc server (menu: Network, *Log every call and packet*) |
| `MHFU_INPUT_SCRIPT` | Scripted keys, mouse motion and buttons, virtual-gamepad buttons and axes, dropped files and window captures, for testing the menu, the setup, the on-screen keyboard and the game's controls without a person at the controls; the syntax is in `host/ui/input_script.hpp`. Its virtual gamepad also becomes the game's pad, in place of a real one that is connected; its keys reach the game through the bindings as well as the interface; with mouse steps the pointer counts as captured without taking the real one; its finger steps are a virtual touch screen, and `drag` moves the interface's pointer as a touch does; `joy` steps attach test joysticks SDL has no mapping for, like a controller it does not know (12 buttons, 4 axes and a hat), and press their buttons, hat and axes. Example: `300:key Escape;330:shot menu;360:pad leftstick+rightstick;400:key W 30;430:mouse 50 0` |
| `MHFU_CAPTURE_PRESENTS=N` | Each window capture (`shot NAME` in the input script) goes on for N presents in a row, written as `NAME.bmp`, `NAME_1.bmp` and so on: with a [frame rate](#frame-rate) above 30 it shows the frames in between the game's as well, which one capture per game frame never does. With `MHFU_TRACE_INTERPOLATION=presents`, the `[render] window` line of each capture comes just before the `[interp] present` line that says whether it was blended. Each capture waits for its frame, so the run is slower while it lasts |
| `MHFU_INPUT_LIVE` | A file read while the game runs; each line appended to it is an input-script step timed from when it is read, to drive two instances side by side |
| `MHFU_DUMP_OVERLAYS` | Directory to dump an overlay that has no library into |
| `MHFU_DUMP_RAM` | Directory to write the game's main memory into (`ram_<frame>.bin`, 32 MiB from `0x08000000`) each time the [screenshot](#screenshots) bind is pressed, for finding the game's structures |
| `PSPRECOMP_NO_INTERPRETER=1` | Stop at uncompiled code instead of interpreting it |
| `PSPRECOMP_MAX_DISPATCHES` | Stop after this many dispatches |
| `PSPRECOMP_HLE_HISTOGRAM=1` | Print import call counts on exit |

### Performance statistics

With `MHFU_PERF=1` the game draws a small overlay into the top-left corner of the presented image, so it appears in window and Steam screenshots and in `MHFU_SCREENSHOT_DIR` captures, and prints one line per second to stdout, flushed as it is written:

```text
[perf] fps 30.0 game 30.0 speed 100% | frame avg 33.4 max 34.7 ms | guest 4.1 render 9.8 wait 19.5 ms | lists 60/s draws 7712/2310 | FIFO 1440x816 90Hz | gpu 7.6 max 10.6 ms | overlay 0.05 ms
```

`MHFU_PERF=log` prints the line without the overlay. F3 shows or hides the overlay at any time, with or without the variable; there is deliberately no gamepad combination for it. The menu's *Performance* setting chooses the same modes, plus the overlay without the log. *Compact* replaces the overlay with one line in the bottom-left corner, `FPS 60.0 GAME 30.0 SPD 100%`, clear of the health bar and the quest clock: green at full speed, yellow from 90% and red below, so a slowdown shows at a glance. The statistics are collected all the time, so turning them on changes nothing else.

A frame runs from one guest flip (`sceDisplaySetFrameBuf`, where the renderer presents) to the next. With a [frame rate](#frame-rate) above 30 the renderer presents between flips instead: `fps`, `frame avg` and `max` and the overlay's graph then follow the presents, while `game`, `guest`, `render`, `wait` and `gpu` stay per game frame and include the in-between frames' work.

| Field | Meaning |
| --- | --- |
| `fps` | Frames presented per second of real time |
| `game` | Frames the game flips per second of *emulated* time: its own frame rate, 30 when it keeps up with its target |
| `speed` | Emulated time per real time: 100% when the game runs at PSP speed. The kernel holds its clock to real time, so it stays at 100% unless frames take longer than the game's frame time; below 100% the game runs slow |
| `frame avg`, `max` | Real time between presents over the last second |
| `guest` | The rest of the frame: recompiled code, HLE, the kernel, input and audio |
| `render` | CPU time turning display lists into Vulkan commands and recording the present, without the GPU waits inside it. The kernel's hold to real time happens outside it and does not reduce it |
| `wait` | Time blocked on the GPU: the frame fence, swapchain acquire, queue submit and present, the queue idle waits of texture uploads and evictions, and framebuffer read-backs for GE block transfers. With FIFO presentation, pacing to the display shows up here, and so does the time the kernel waits to hold the game to real time |
| `lists` | Display lists enqueued per second of real time |
| `draws` | Draws the GE made per frame / draw calls the renderer recorded for them; the second is smaller when consecutive draws with the same state are merged |
| `FIFO 1440x816 90Hz` | Present mode, swapchain size and the display's refresh rate as SDL reports it |
| `gpu`, `max` | GPU time per frame, averaged over the second, and the longest: from the first command of the frame to its last draw, measured with Vulkan timestamp queries and read back after the frame's fence, so it lags the frame by one. The copy to the window is not included. `gpu n/a` when the graphics queue has no timestamps (`timestampValidBits` 0) or `MHFU_NO_GPU_TIMESTAMPS` is set; the log says which at start-up |
| `overlay` | CPU time spent drawing the overlay, when it is shown |
| `interpolation` | The frame rate presented, when it is above 30; `interpolation 60 of 90` when it stepped down from the one chosen |
| `space` | The most of the vertex and index buffers one frame took in the second, in MiB, against 128 and 4 MiB a frame. What does not fit is not drawn (the interface goes first, as it is drawn last), and the log then says `[render] a frame ran out of vertex space` |
| `passes`, `cleared`, `copies` | Render passes a frame begins, presents between flips included; of those, the ones begun without loading what their first draw, a clear, overwrites; and full-size copies or blits of a render target (for sampling it as a texture, for frame interpolation's pictures, for the write-back to guest memory, to the window). On a phone's tiled GPU each pass reads and writes its target, and each copy moves a whole one |

The overlay shows the same numbers (GPU time on the second line, when there is one) and a graph of the last 192 frame times, from 0 to 50 ms, with guides at 16.7 and 33.3 ms: green up to 34 ms, yellow up to 50 ms, red beyond.

#### Comparing the old and new renderer paths

The renderer changes made for speed each have an off switch (see [Video](#video)). `MHFU_PERF_ALTERNATE=name[,name...]` instead turns the named ones off every other second, so a single run compares them under the same scene and load; each `[perf]` line then ends in `alt on` or `alt off`. Stand still in one spot for a minute and compare the `render` (CPU) and `gpu` numbers of the `on` and `off` lines. Names: `direct` (`MHFU_NO_DIRECT_VERTICES`), `lookup` (`MHFU_NO_LOOKUP_CACHE`), `reuse` (`MHFU_NO_BUFFER_REUSE`), `merge` (`MHFU_NO_DRAW_MERGE`), `store` (`MHFU_NO_FAST_STORE`), `decode` (`MHFU_NO_FAST_DECODE`), `alpha` (`MHFU_NO_ALPHA_VARIANTS`), `uploads` (`MHFU_SYNC_UPLOADS`), `clearload` (`MHFU_NO_CLEAR_LOAD`), `gpudecode` (GPU vertex decode, when `MHFU_GPU_DECODE=1` turns it on) and `texturedecode` (`MHFU_SYNC_TEXTURE_DECODE`).

#### Where the render thread waits

`MHFU_TRACE_STALLS=1` adds a `[stalls]` line after each `[perf]` line, and a `[slow-frame]` line for every frame longer than `MHFU_TRACE_STALLS_MS` milliseconds (default 40), so a spike can be matched to its cause:

```text
[stalls] ms per frame over 30 frames: fence 0.14 max 1.88 x30 acquire 0.08 max 0.10 x30 submit 3.15 max 3.37 x30 present 0.02 max 0.03 x30 pacing 21.68 max 7.48 x196 copy 0.02 max 0.02 x30 store 0.43 max 0.46 x30 | charged 0.40 max 2.00 x12
[slow-frame] 714 152.0 ms at 23812.4ms | guest 2.5 render 81.6 wait 67.9 ms | draws 2140 | charged 0.0 ms | gpu(prev) 2.0 ms | fence 2.02 x1 acquire 0.09 x1 submit 6.88 x1 present 0.03 x1 upload 43.93 x80 pacing 14.91 x4 copy 0.02 x1 store 0.51 x1
```

On Android, where no variable can be set, the `[stalls]` line comes with the `[perf]` line whenever Performance logs, so a log saved from the menu has it. Each kind that happened is listed with its time per frame averaged over the second, its longest single stall and how many there were; a `[slow-frame]` line gives that frame's totals, the emulated time it ended at (the clock of `MHFU_TRACE_KERNEL`'s `at=`), the draws the game made in it (a handful in a black fade or a loading screen, thousands in a frame of an area), and the GPU time of the frame before it, which is what a fence wait at the start of the frame waits for. Both lines also give the emulated time charged to the game's own CPU work: the kernel advances its clock 1 ms for every `MHFU_STARVATION_INTERVAL` dispatches a thread runs without an import call. On the `[stalls]` line it is the average per frame, the most in one frame and the number of charges. A slow frame that is mostly `pacing`, with several milliseconds charged, is one the game ran long in by its own clock, not one the host held up. A `[slow-frame]` line ends with the frame's slowest HLE call and its own host time, without the render and pacing time inside it (the hooks that run at the flip count towards `sceDisplaySetFrameBuf`), then the game thread's CPU time in the frame, its page faults (minor/major) and its context switches (voluntary/involuntary). CPU time well short of the frame's length means the thread was stopped rather than working: by a lock or a sleep it asked for (voluntary), or by the system taking the core away (involuntary). On macOS these numbers are the whole process's; on Windows they are 0.

| Kind | Where |
| --- | --- |
| `fence` | A frame's fence: before a frame slot is recorded again (the frame two back, with two frames in flight), and before the previous frame's picture is written back to guest memory at the next flip. The GPU is still busy with that frame |
| `acquire` | `vkAcquireNextImageKHR` |
| `submit` | The frame's `vkQueueSubmit`; MoltenVK waits for the next drawable here |
| `present` | `vkQueuePresentKHR` |
| `upload` | A texture upload that waits for the queue to go idle, and so for the frames before it: with `MHFU_SYNC_UPLOADS`, or one made outside a frame |
| `evict` | The queue idle wait before a cached texture is destroyed to make room, with `MHFU_SYNC_UPLOADS` |
| `readback` | A framebuffer read back for a GE block transfer (submits the frame so far and waits for it) |
| `idle` | Other device idle waits, such as a movie frame changing size |
| `pacing` | The kernel holding the game to real time (not a GPU wait, but part of `wait`) |
| `copy` | CPU time copying the written-back frame out of mapped memory, which may be uncached (part of `render`) |
| `decode` | Waiting at the frame's submit for textures decoded in the background |
| `pipeline` | CPU time creating graphics pipelines the frame needed (part of `render`). A burst of them, as in a new area, is also reported once it settles: `[render] 4 new pipelines in 12.3 ms, 57 so far; pipeline cache saved` |
| `store` | CPU time converting that frame into guest memory, `store_frame` (part of `render`) |
| `texture` | CPU time creating or destroying a texture's image, memory, view and descriptor: each texture seen for the first time, and each one evicted to make room (part of `render`) |
| `alloc` | `vkAllocateMemory` and `vkFreeMemory` calls for images and upload buffers, the costliest part of `texture`; the count says how many a frame made |
| `cache-save` | CPU time writing `pipeline_cache.bin` and `pipeline_keys.bin`, a few seconds after a burst of new pipelines (part of `render`) |
| `pack` | CPU time the HD texture pack spends on the game thread: hashing each new game texture to look it up, uploading a decoded image (its allocations, the copy into staging memory and its own submit), and freeing finished uploads or dropping images over the budget (part of `render`) |

After the kinds, a `[slow-frame]` line gives the frame's render split, in milliseconds, as `MHFU_TRACE_RENDER` times it: `| lists 52.3 (decode 4.1 draw 45.0: texture 3.2 vertices 30.1 pipeline 0.4 record 6.9 summary 0.4 other 4.0) interp 0.3 replay 0.0 present 0.6 writeback 0.2`. `lists` is running the display lists, with the vertex decode and the renderer's handling of each draw inside it. The parts of `draw` are its texture, writing its vertices into the frame's buffers (and keeping a span the vertex cache missed), finding its pipeline, recording its commands and what interpolation keeps of it; `other` is the rest of `draw`. The split is left out for the first quarter second, while its counter is calibrated.

#### A report from a phone

On Android, set Performance to *Log* in the menu, play to the place that is slow for a minute, then use *Save the log…*. The start of the log names the Vulkan driver (`[render] Vulkan …, driver …`), the memory the vertex buffer lives in, the swapchain's images and present mode, the internal resolution (`Renderer: … target WxH`) and how many frames are in flight. Each second then has a `[perf]` and a `[stalls]` line. Where to look:

- `gpu` close to or above the frame time (33 ms at 30 fps, 16.7 ms at 60): the GPU is the limit. Lower Resolution first, then Frame rate.
- `render` plus `guest` close to the frame time: the CPU is the limit; Frame rate's in-between frames add to `render`.
- `fence`, `acquire` or `present` large in `[stalls]`: waiting for the GPU or the display.
- `pipeline` and `[render] N new pipelines in X ms` lines: shader compiles. They should appear once per new area and not again in a later run, which loads them from `pipeline_cache.bin`.
- `upload` in `[stalls]`: texture uploads that waited for the GPU, which should not happen during play any more.
- `texture` and `alloc` with large counts on entering an area: textures created one allocation each; `cache-save` a few seconds after new pipelines: the cache written to disk.
- `pack` on entering an area: the texture pack's lookups and uploads; compare a run with `MHFU_TEXTURE_PACK=0`.
- Large `render` with none of the kinds above: the frame's split says which part of the renderer it is.
- A `[load] fast …, in a quest` line with no area change around it: fast loading took a quiet moment of play for a load.

## Host layout

```text
host/main.cpp                    Entry point: finding the game data, executable check, startup
host/app_paths.{hpp,cpp}         The executable's own location, and what a release ships next to it
host/install/                    First-run installer: per-user directory, image checks, executable preparation
host/settings/                   Player settings: settings.ini, environment overrides, defaults
host/camera/                     Camera input from every device, the driver for the game's own camera, and its view's shape
host/input/                      Bindings and control presets: names, settings.ini spelling, combinations, what held inputs press; the touch controls' logic
host/ui/                         Pokee's own interface (Dear ImGui): in-game menu, setup screens, file browser, on-screen keyboard
host/game/                       What the host reads from the running game: its memory, the character, the game's text, which file holds a piece of equipment's model; layered armor
host/debug/                      Developer tools (not in release builds): the game's money, boxes and quest state, cheats, command file
host/overlays.{hpp,cpp}          Overlay library loading and run-time installation
host/kernel/kernel.{hpp,cpp}     Scheduler, waits, virtual clock, interrupts, memory
host/kernel/iso_image.{hpp,cpp}  Read-only ISO 9660 view of the disc image
host/hle/hle_threadman.cpp       ThreadManForUser, Kernel_Library
host/hle/hle_sysmem.cpp          SysMemUserForUser, sceSuspendForUser, sceDmac
host/hle/hle_io.cpp              IoFileMgrForUser, sceUmdUser
host/hle/hle_system.cpp          Utils, LoadExec, Stdio, ModuleMgr, interrupts, power, RTC
host/hle/hle_media.cpp           sceDisplay, sceCtrl, sceGe_user, sceAudio, sceSasCore
host/hle/hle_atrac.cpp           sceAtrac3plus: ATRAC3 music decoded frame by frame, loops, positions
host/hle/hle_mpeg.cpp            sceMpeg and sceJpegCsc: the movie player's ring buffer, access units and decoding
host/hle/hle_font.cpp            sceLibFont over a host font
host/fonts/game_font.*           The game's text font: loading, fitting glyphs into the game's cells, fallback, installed fonts
host/hle/hle_utility.cpp         sceUtility on-screen keyboard and message dialog
host/hle/hle_savedata.cpp        sceUtility save-data dialog
host/hle/hle_adhoc.cpp           sceNet, sceNetAdhoc, sceNetAdhocctl, sceNetAdhocDiscover, sceWlanDrv, sceUtilityNetconf
host/adhoc/                      Client for PSP ad hoc servers: wire formats, network thread, diagnostics
host/hle/utility_dialog.hpp      Status life cycle shared by the dialogs
host/save_data/                  AES-128, PARAM.SFO, the save-data encryption and hashes, save folders
host/gpu/ge_state.{hpp,cpp}      GE command state machine: display lists to draw calls
host/gpu/vulkan_renderer.*       Vulkan backend, window and input
host/gpu/texture_pack.*          HD texture packs: textures.ini, texture keys, background image decoding, texture dumps
host/gpu/texture_pack_import.*   Texture pack import: finding a pack in a folder, its checks, the copy, swap and backup
host/gpu/replacement_textures.*  Texture pack images on the GPU: uploads with mip levels, memory budget
host/gpu/frame_interpolation.*   Frame rate above 30: matching draws between frames, cuts, blending transforms
host/gpu/frame_pacing.*          Frame rate above 30: when to present and what, the rate governor
host/gpu/shaders/                GLSL, compiled to SPIR-V and embedded at build time
host/perf/frame_stats.*          Frame timing, the per-second summary and the [perf] log line
host/perf/perf_overlay.*         Performance overlay drawn on the CPU with a built-in 5x7 font
host/audio/audio_sink.*          SDL3 playback device and the mixing ring buffer
host/audio/sas_core.*            Software SAS: VAG decoding, pitch, envelopes, 32 voices
host/audio/atrac_decoder.*       ATRAC3 and ATRAC3plus frames to PCM through FFmpeg's libavcodec
host/movie/psmf_demuxer.*        PSMF program stream packs to H.264 pictures and ATRAC3plus frames
host/movie/avc_decoder.*         H.264 pictures to planar YCbCr through FFmpeg's libavcodec
```

Every import runs at the outer dispatch level, so a blocking import saves the caller's context with `pc = $ra` and loads another thread's context; the runtime's thread identity check keeps generated code from resuming in the wrong thread.

## Directory layout

```text
config/       Executable identity and overlay slot map
host/         Bootstrap, kernel, HLE, graphics, audio
scripts/      prepare_game.sh, generate.sh, build_overlays.sh, bootstrap_overlays.sh, release_linux.sh
packaging/    Release packaging: third-party notices; linux/ holds the Flatpak manifest, launcher and SDK build
tools/        ISO and DATA.BIN extraction, overlay wrapping, shader and NID table embedding
tests/        Save-data self-tests and save checker (mhfu_savedata_tests)
third_party/  stb_truetype, tiny-AES-c (installer and saves), Dear ImGui (menu and setup screens)
game/         Local game data: EBOOT.ELF, disc.iso, ms0/ (ignored)
analysis/     Analyzer output and extracted overlays (ignored)
generated/    Recompiled executable (ignored)
overlays/     One directory per recompiled overlay (ignored)
```
