# Pokee

Pokee is a native PC port of **Monster Hunter Freedom Unite** (USA, `ULUS-10391`, version 1.01) for Windows, Linux, the Steam Deck, macOS and Android. It is not an emulator: the game's PSP code is recompiled ahead of time into a native program and run on a reimplementation of the PSP system. On top of that it adds the things a PSP could not do: high resolutions, widescreen, a real analog camera, modern controls, HD textures and a set of optional quality-of-life changes.

> **This project does not include the game.** To install or build Pokee, use the files from your own legally obtained copy of that release.

## Legal disclaimer

**Pokee** is an independent, open-source project and is not affiliated with, authorized by, sponsored by, or endorsed by CAPCOM, Sony, or any of their affiliates.

Monster Hunter, Monster Hunter Freedom Unite, CAPCOM, PlayStation, PSP, and all related trademarks, game assets, artwork, audio, characters, and other intellectual property belong to their respective owners.

**Pokee** does not include any game assets or original game files: no disc image, no copy of the game's executable or data, and no textures, models, audio or video from the game. You must provide the files from your own legally obtained copy of Monster Hunter Freedom Unite (USA) to install or build **Pokee**; the installer checks that copy and accepts only `ULUS-10391` version 1.01.

To use **Pokee**, users must provide the required files from their own legally obtained copy of Monster Hunter Freedom Unite (USA) for PSP.

Users are solely responsible for obtaining, dumping, extracting, and using their game copy in accordance with the laws applicable in their jurisdiction.

**Pokee** does not support, provide, link to, or encourage the use of unauthorized or pirated copies of the game.

Any references to the original game or its trademarks are made solely for identification, compatibility, and interoperability purposes.

Screenshots and other depictions of the original game may be used solely to document or demonstrate **Pokee's** functionality. All depicted third-party game content remains the property of its respective rights holders.

The license covering **Pokee** applies only to the project's own original code and materials and does not grant any rights to third-party intellectual property.

**Pokee** provides the software, not the game. You must provide your own legally obtained copy.

## What Pokee adds to the PSP game

The game itself is unchanged: the same quests, monsters, damage and timings, running at the PSP's 30 frames per second. Everything below is added around it, and almost all of it can be switched off in Pokee's menu (Esc, or L3+R3 on a gamepad). Every setting is described in the [profile README](profiles/mhfu/README.md#in-game-menu).

### Picture

- **Any resolution.** The 3D world renders at the window's size or at ×1 to ×6 of the PSP's 480×272, instead of being stretched up from the PSP's screen.
- **Widescreen.** *Fill* gives the game's view the window's shape, so a 16:9 or 16:10 screen shows more of the world instead of black bars. *Original* (the PSP's shape) and *Stretch* are there too.
- **Higher frame rates.** Frame interpolation presents the game at 45, 60, 90, 120 fps or the display's refresh rate. The game still runs at its own 30, so attack timings and monster behaviour are exactly as on a PSP.
- **Field of view.** A wider (or narrower) view on hunts, 30–90°; the bowgun's zoom keeps its proportion.
- **HD texture packs.** Packs made for PPSSPP load as they are, with an importer in the menu and a choice of Xbox, PlayStation or Nintendo button prompts when the pack has them. See [Texture pack](#recommended-texture-pack).
- **Sharper interface.** Optional sharp text and upscaled 2D textures (sharp bilinear or MMPX), a choice of font and text weight.
- **HUD control.** Hide the HUD for screenshots (F7), and scale the map, status bars, item bar and name tags separately, 50–150%.
- **Fast loading.** Loads run ahead of real time, so area changes and quest starts take a fraction of the PSP's time. A fast-forward key (2×–8×) is there too.

### Controls

- **A real camera.** The right stick (or the mouse) turns and tilts the camera smoothly, instead of the PSP's D-pad "claw" camera. Bows and bowguns aim with the stick, in proportion to how far it is pushed.
- **Lock-on.** R3 (or T or the middle mouse button) locks the camera on a large monster, with a marker over it and a monster icon in the HUD; a follow camera keeps it in view, and a focus style centres on a tap.
- **Presets for every input.** Default, Modern (attacks on the triggers), Left-handed, Classic keyboard, or your own. Every control can be rebound to a key, button or a combination such as L1 + ○.
- **Complete keyboard and mouse play.** WASD to move, the mouse for the camera and its buttons to attack.
- **Gamepads just work.** Xbox, PlayStation, Nintendo and other SDL3 pads, with prompts that match the pad. A pad SDL does not know can be given a mapping.
- **Touch controls on Android**, in the PSP's layout or an action layout you can rearrange.

### Hunting

These change nothing about the game's balance; each one has an off switch.

- **Hold to gather.** Hold ○ at a gather point or on a carcass and it keeps gathering or carving.
- **Gather with ○.** Mine and catch bugs with ○, with no need to select the pickaxe or net on the item bar first; your selection comes back afterwards.
- **Gather icon.** A mark over each gather point shows what it gives and which button gathers it.
- **Empty item slot.** The item bar starts each quest on an empty slot, so pressing □ by accident uses nothing.
- **Item Sets.** Save the pouch's contents as a set and take it out of the box in one go, as *Equipment Set* does for armor.
- **Damage numbers** (off by default). The health each hit removes, drawn where it landed, split into raw and element if you like. In multiplayer, Pokee players can toggle to see each other's hits.
- **Modern sharpness gauge.** The sharpness colour visibly shortens as it is used.
- **True Attack Value** (off by default). Equipment screens show a weapon's real attack, element and status values, without the class multiplier.
- **Show Key Quests** (off by default). Quests that unlock the next Urgent Quest are marked *[Key]*.
- **Guild Hall comforts.** Eat from your Felyne Kitchen at the hall's tables and change equipment from the hall's item box. Giving items of any rarity to other hunters is there too, off by default.
- **Layered armor.** Wear one armor's looks over another's stats, part by part. Only your screen shows it; stats, skills, the save and what other players see keep the real armor.
- **Fast Item box taking** in a quest no longer waits for the icon to fly to the pouch.

### Saves, mods and multiplayer

- **PSP saves.** Saves are in the PSP's own format: copy your save from a PSP and carry on, or take one back. The menu imports, exports and backs up saves, and reminds you to back up after an update.
- **Downloadable content.** The download servers are long gone, but a `ULUS10391QST` folder you already have can be imported and is read by the game's download menu.
- **Mods.** Untested yet for importing and using mods, however in this section I've included some game changing features.
- **Multiplayer.** The game's ad hoc mode works between Pokee players on a home network, over a VPN such as Tailscale or ZeroTier, or through a PSP ad hoc server. See [Multiplayer](profiles/mhfu/README.md#multiplayer-ad-hoc).
- **Portable copy.** Keep the game, settings and saves in one folder next to the executable, on a USB stick if you like.

## Recommended texture pack

The recommended pack is Monkbreh's [**MHFU Texture Port**](https://github.com/Monkbreh/MHFU-Texture-Port). It is not an upscale: it swaps the PSP's textures for the higher-resolution originals of the same assets from the PS2 games, Frontier and the iOS release of Freedom Unite. It covers the interface, every map area, most monsters and most equipment, and comes with Xbox, PlayStation and Nintendo versions of the button prompts, which Pokee picks to match your controller. No pack is included with Pokee or downloaded by it.

To install it:

1. On the pack's GitHub page, choose **Code → Download ZIP** and extract it anywhere, for example to Downloads. Zipped packs are not read.
2. In the game, open Pokee's menu (Esc, or L3+R3) and go to **Video → Import texture pack…**.
3. Browse to the extracted folder and open its `ULUS10391` folder, the one with `textures.ini`. The `ULJM05500` folder beside it is for the Japanese release.
4. Pokee checks the pack and shows what it found. Choose **Copy into Pokee's data folder**, or **Use it where it is** to read it from where you extracted it and save the space (the folder must then stay where it is).

The textures change within a frame or two, without a restart. **Video → Texture pack** turns the pack off and on to compare; **Button icons** under it chooses the prompts (Auto follows your controller). Other PPSSPP packs for this release work the same way, and a pack made for Portable 3rd works if its `textures.ini` lists `ULUS10391`. Details: [HD texture packs](profiles/mhfu/README.md#hd-texture-packs).

## Play

Downloads for Windows and Linux (the Steam Deck included) are on the [releases page](https://github.com/LocalBBQ/Pokee/releases). On the first start, Pokee asks for your disc image (`.iso`) in its own window, checks it, prepares the game from it and keeps your settings and saves in a per-user folder (or in a [portable `data` folder](profiles/mhfu/README.md#portable-copy) next to the executable). This works with a gamepad alone.

- Linux and Steam Deck: [`docs/LINUX.md`](docs/LINUX.md)
- macOS: no release builds for now; it builds from source ([`docs/BUILDING.md`](docs/BUILDING.md))
- Where saves live and how to bring a PSP save across: [Saving and loading](profiles/mhfu/README.md#saving-and-loading)
- Every setting, control and environment variable: [profile README](profiles/mhfu/README.md)

### Windows: slow starts and Microsoft Defender

Microsoft Defender scans `Pokee.exe` and the overlay libraries in `overlays\` as the program loads them. Releases up to 1.0.0 shipped 290 overlay libraries, one per overlay, and the scans could make a start take far longer than it should. Later builds link the overlays into four libraries, one per memory slot. If a start is still slow, exclude Pokee's folders from scanning:

1. Open **Windows Security → Virus & threat protection → Manage settings** (under *Virus & threat protection settings*).
2. Scroll to **Exclusions** and choose **Add or remove exclusions → Add an exclusion → Folder**.
3. Choose the folder you extracted Pokee into, the one with `Pokee.exe`.
4. Optionally, add Pokee's data folder as well, `%APPDATA%\Pokee` (or the `data` folder of a portable copy), which holds the prepared game and its caches.

Or, in PowerShell run as administrator:

```powershell
Add-MpPreference -ExclusionPath "C:\Games\pokee-windows-x86_64", "$env:APPDATA\Pokee"
```

Use your own folder in place of `C:\Games\pokee-windows-x86_64`. Defender no longer scans anything in an excluded folder, so exclude only a Pokee you downloaded from the [releases page](https://github.com/LocalBBQ/Pokee/releases) or built yourself, and keep nothing else in that folder.

### Controls

On a gamepad the buttons are where you expect them: on a PlayStation pad ○ confirms and ✕ backs out, as the game's prompts say, and the right stick drives the camera and aiming. On the keyboard, WASD moves, the mouse turns the camera and its buttons attack. Esc, or both sticks pressed together (L3+R3), opens Pokee's menu. The full tables are in the [profile README](profiles/mhfu/README.md#running).

## Building from source

Building from source is a fully supported way to play. It needs:

- Your own copy of the game (see above)
- CMake 3.20 or newer, Ninja and a C++20 compiler
- Python 3
- SDL3, Vulkan and `glslangValidator`
- `make` and a C compiler on macOS and Linux: the build makes its own FFmpeg for the music and the movies. Nothing to install for it on Windows
- A few gigabytes of free memory for the build; the recompiled code is large

In short:

1. Prepare the game's executable from your disc image: build `Pokee` once without recompiled code and run it with `--install /path/to/image.iso`. No external decryption tool is needed. Then link the image and that executable into the profile with `profiles/mhfu/scripts/prepare_game.sh`.
2. Configure, generate the recompiled code with `profiles/mhfu/scripts/generate.sh`, and build `Pokee`.
3. Recompile the code overlays with `profiles/mhfu/scripts/build_overlays.sh` (about 40 minutes the first time; resumable).
4. Run `out/mhfu/bin/Pokee`.

The full instructions are in [`profiles/mhfu/README.md`](profiles/mhfu/README.md). Building on every platform, how long each stage takes, and how to work on the code without full rebuilds: [`docs/BUILDING.md`](docs/BUILDING.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md).

## Roadmap

- Save-data dialogs that draw
- Ad hoc play with PPSSPP and on the public servers PSP players use
- Android on real phones: device testing, graphics drivers and performance ([upstream #127](https://github.com/TeamGDB/Yakumo/issues/127), [#17](https://github.com/TeamGDB/Yakumo/issues/17))
- No stutters on area entry and in fights ([STUTTERS.md](docs/STUTTERS.md))
- One consistent visual style across the setup screens, menus and overlays ([upstream #33](https://github.com/TeamGDB/Yakumo/issues/33))

## How it works

The executable is analyzed and every instruction of its code is emitted as C++, which is compiled into the program. The game also loads 290 code overlays at run time into a handful of shared memory slots; each is recompiled, the overlays of one slot are linked into one library, and when the game loads an overlay, its recompiled code is installed between frames. An interpreter covers any code the recompiled set does not reach, so nothing stops the game — it only runs slower there.

Around that code sits a reimplementation of the PSP system: a kernel with threads, semaphores, event flags and timers; disc I/O read straight from the image; a Vulkan renderer for the PSP's graphics engine; software voice mixing for audio; and input from SDL3.

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describes the execution model and [`docs/DATA_BIN.md`](docs/DATA_BIN.md) the game's archive format. [`docs/TESTING.md`](docs/TESTING.md) has the smoke test and how to report results.

## Repository layout

```text
include/psprecomp/   Framework interfaces: runtime, memory, Allegrex state
src/                 Framework: ELF loading, decoder, runtime, interpreter
tools/               Framework: analyzer and C++ code generator
tests/               Framework regression tests
configs/             PSP NID data and generic examples
profiles/mhfu/       Everything specific to this game: host, kernel, renderer,
                     audio, input, configuration and build scripts
docs/                Architecture, archive format, profile guide, source rules
```

The recompiled code itself is generated locally from your copy of the game and is never committed.

## Built on PSPRecomp

**Pokee** is built on [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp), a static recompilation framework for PSP software. The framework is game-neutral and can be built on its own:

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE=""
cmake --build out/framework --config Release
ctest --test-dir out/framework -C Release --output-on-failure
```

To target another title, see [`docs/PROFILE_GUIDE.md`](docs/PROFILE_GUIDE.md). [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) sets out the rules for independently written code and third-party source.

[Pokee](https://github.com/LocalBBQ/Pokee) is built from [Yakumo](https://github.com/TeamGDB/Yakumo), the Portable 3rd port. That project and its contributors keep the credit for the host this tree starts from.

## License

The repository is distributed under the MIT License; see [`LICENSE`](LICENSE). Third-party files keep their own notices beside them; [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) lists their origins and licences.
