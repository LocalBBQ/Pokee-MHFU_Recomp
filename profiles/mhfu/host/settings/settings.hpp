#pragma once

#include "input/bindings.hpp"
#include "input/chords.hpp"
#include "input/presets.hpp"
#include "input/touch_action.hpp"
#include "kernel/fast_forward.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The player's settings: what the in-game menu changes and settings.ini in the
// per-user data directory keeps.
//
// A value comes from its environment variable when that is set, otherwise from
// settings.ini, otherwise from the default below. A variable decides the value
// for the whole run: the menu shows it but cannot change it, and it is never
// written to settings.ini, so unsetting the variable brings the player's own
// choice back.
//
// Only the main thread reads or writes these.
namespace mhfu::settings {

enum class PresentMode { Fifo, Mailbox, Immediate };
// How the game's picture meets the window. Original keeps the PSP's shape with
// black bars, Stretch fills the window by stretching it, and Fill gives the
// game the window's shape: its view widens or narrows to match (the vertical
// field of view stays), and the 2D interface keeps the PSP's proportions.
enum class Aspect { Original, Stretch, Fill };
// Compact is one line of the overlay (frame rate, game rate and speed) in
// the bottom-left corner instead of the full panel in the top-left one.
enum class PerfDisplay { Off, Overlay, OverlayAndLog, Log, Compact, CompactAndLog };
// How the game's 2D textures are drawn at a high internal resolution
// (gpu/ui_textures.hpp): as they are, sharp bilinear, or doubled with MMPX.
enum class UiTextures { Off, Sharp, Mmpx };
// The button icons a texture pack that comes with several draws
// (gpu/texture_pack.hpp): following the pad in use, the pack's default, or one set.
enum class PackButtons { Auto, Pack, Xbox, PlayStation, Nintendo };
enum class RightStick { Camera, DPad, Off };
// Follow keeps the quest camera on the monster Lock on selected. Focus keeps
// that selection, and a tap of L centers the camera on the monster.
enum class LockOnStyle { Follow, Focus };
// Damage numbers (game/damage_numbers.hpp): none, this console's hunter's
// hits only, or every hit.
enum class DamageNumbers { Off, Mine, Everyone };
// What a damage number shows: the total, the raw share with the element
// share beside it, or the total with the element share beside it.
enum class DamageBreakdown { Total, Split, TotalAndElement };
// The on-screen controls' layout: the PSP's buttons (touch_controls.hpp), or
// the action-style one (touch_action.hpp).
enum class TouchLayout { Psp, Action };
// What answers the game when it asks for text such as the hunter's name.
enum class NameEntry { Keyboard, Fixed };
// Presents per second. The game makes 30 frames a second; the faster rates
// add frames in between with blended movement (frame interpolation), and
// Display follows the display's refresh rate. 30 presents the game's frames
// as they are, as before interpolation existed.
enum class FrameRate { Fps30, Fps45, Fps60, Fps90, Fps120, Display };
// GPU compatibility mode: the renderer leaves out what old mobile drivers
// have been seen to get wrong (gpu/device_report.hpp). Auto turns it on for
// those drivers only; it applies from the next start.
enum class GpuCompat { Auto, On, Off };

// The vertical field of view a quest's camera has in the game, in whole
// degrees (0.872222 radians), and how far the setting can move it.
inline constexpr std::uint32_t kGameFieldOfView = 50u;
inline constexpr std::uint32_t kMinFieldOfView = 30u;
inline constexpr std::uint32_t kMaxFieldOfView = 90u;

struct Settings {
    // Video
    std::uint32_t internal_scale{2u}; // render resolution, multiples of 480x272 (272 lines each); 0: the window's
    std::uint32_t window_scale{2u};   // windowed size, multiples of 480x272
    bool fullscreen{};
    PresentMode present_mode{PresentMode::Fifo};
    Aspect aspect{Aspect::Original};
    // The game's own 3D view, vertical degrees (camera/field_of_view.hpp);
    // other views (the village, zoom) are scaled in proportion.
    std::uint32_t field_of_view{kGameFieldOfView};
    bool sharp_screen{};             // nearest instead of linear scaling to the window
    bool sharp_textures{};           // nearest instead of linear texture sampling
    bool texture_pack{true};         // draw an installed HD texture pack's images instead of the game's
    std::string texture_pack_folder; // a pack used where it is instead of textures/<disc id>; empty: none
    PackButtons texture_pack_buttons{PackButtons::Auto}; // the pack's button icons
    bool unthrottled{};                                  // let emulated time run ahead of real time
    bool fast_loading{true};                             // ...but only while the game loads (kernel/fast_loading.hpp)
    // The fast-forward bind: held, toggled, or doing nothing (kernel/fast_forward.hpp).
    fast_forward::Mode fast_forward{fast_forward::Mode::Hold};
    std::uint32_t fast_forward_speed{fast_forward::kDefaultSpeed}; // times real time while it runs
    FrameRate frame_rate{FrameRate::Fps30};
    bool frame_rate_auto{true}; // lower the frame rate rather than slow the game
    PerfDisplay perf{PerfDisplay::Off};
    GpuCompat gpu_compat{GpuCompat::Auto};
    // Each quest interface piece, as a percent of the size the game draws it
    // (gpu/hud_scale.hpp). 100 leaves that piece where it is.
    struct InterfaceScale {
        std::uint32_t map{100u}; // the area map on the right
        // The quest clock, the name under it, health, stamina, the eye shown when a monster is found,
        // the status icons beside that name, sharpness, the long sword spirit gauge, a bowgun's loaded shots,
        // a bow's coatings, a gunlance's shells and a hunting horn's notes.
        std::uint32_t health{100u};
        // The item bar, the bowgun ammo bar, the bow coating bar and their menus.
        std::uint32_t item_bar{100u};
        // Names over other hunters, in a quest and in the gathering hall, and the mark beside each.
        std::uint32_t names{100u};
    };
    InterfaceScale hud{};

    // Text
    std::string font;              // the game's text font: path, "#face" for a collection; empty: the default
    std::uint32_t font_weight{1u}; // columns the game's glyphs are thickened by, 0 to kMaxFontWeight
    // Draw the game's glyph atlas again at the internal resolution. Off by
    // default on every platform since settings.version 2 (#210, #212).
    bool crisp_text{};
    UiTextures ui_textures{UiTextures::Off};

    // Audio
    std::uint32_t volume{100u}; // percent
    bool mute{};
    bool background_mute{}; // silent while the window does not have focus

    // Controls
    bool confirm_south{}; // confirm (circle) on the south face button
    float dead_zone{0.15f};
    float trigger{0.25f};
    // How long an input that begins a chord waits for the rest of it, in
    // milliseconds (input/chords.hpp); 0 acts at once.
    std::uint32_t chord_window{input::kDefaultChordWindowMs};
    RightStick right_stick{RightStick::Camera};
    float right_stick_zone{0.5f};
    // Drives the ordinary quest camera's yaw and pitch from how far the stick
    // is pushed, instead of the game's fixed-speed turn and vertical presets.
    // On by default. Off writes nothing at all, so the camera is exactly as
    // the game made it.
    bool analog_camera{true};
    // While the analog camera is on, the D-pad also turns the game's own
    // camera left and right and steps its height levels. Off by default: the
    // two cameras on one view fought each other.
    bool dpad_camera{false};
    // The Lock on bind selects a large monster (camera/lock_on.hpp). Nothing
    // happens until the bind is pressed; off, the bind does nothing at all.
    bool lock_on{true};
    // Follow keeps the camera on the selection. Focus/Follow Camera turns
    // that tracking off and on again without dropping the selection, and the
    // reticle goes with the tracking. The right stick turns both off and leaves
    // the portrait. Focus eases onto it when that button is tapped. The default
    // pad binds it to LT, and LB stays L. With the bind empty, the tap is L,
    // and the game's own recenter stays off so the press does not move the
    // camera twice. A second tap within a third of a second recenters behind
    // the hunter.
    LockOnStyle lock_on_style{LockOnStyle::Follow};
    // Ring, cross and ticks. Focus leaves it off until the focus button, then drops it shortly after.
    bool lock_on_marker{true};
    // The monster icon at the bottom left, as a percent of its usual size.
    std::uint32_t hunt_portrait_size{100u};
    // Degrees per second at full deflection, before the stick's own curve.
    float camera_speed{190.0f};
    // Degrees per second at full deflection while a bow or a bowgun aims.
    float aim_speed{90.0f};
    bool invert_camera_x{};
    bool invert_camera_y{};
    // Read the gamepad while the window does not have focus. Off, an
    // unfocused window sees a released pad; keyboard and mouse never reach it.
    bool background_gamepad{};
    // Keyboard and mouse. With the mouse on, the window captures the pointer
    // while the game runs and the mouse turns the camera; Esc frees it.
    bool mouse{true};
    float mouse_sensitivity{0.10f}; // degrees of camera turn per count of mouse motion
    bool invert_mouse_x{};
    bool invert_mouse_y{};
    // Control presets (input/presets.hpp): the layout in use, which preset
    // it is, and the player's own presets. The layout is kept whole, so it
    // stays what the player had even when a preset changes between versions.
    input::Layout controls{input::layout(input::Preset::Default)};
    input::PresetChoice control_preset{input::Preset::Default, {}};
    std::vector<input::UserPreset> user_presets;
    // On-screen controls for a touch screen, shown once the screen is touched
    // and hidden again when a gamepad or the keyboard is used.
    bool touch_controls{true};
    bool touch_dpad{true};            // the D-pad among them, for the game's menus
    float touch_opacity{0.5f};        // 0.1-1
    float touch_size{1.0f};           // 0.6-1.6 of the default size
    float touch_camera_speed{180.0f}; // degrees the camera turns for a drag across the screen's height
    TouchLayout touch_layout{TouchLayout::Psp};
    // The action layout's elements, kept on this device, and a short
    // vibration when one of its buttons is pressed.
    input::touch::ActionLayout touch_action{input::touch::default_action_layout()};
    bool touch_haptics{true};
    NameEntry name_entry{NameEntry::Keyboard}; // on-screen keyboard, or the name below at once
    std::string name{"Hunter"};                // the fixed name

    // Network (ad hoc play through a PSP ad hoc server)
    bool adhoc{};                          // wireless switch on: the game may go on line
    std::string adhoc_server;              // host or host:port of the server; empty: none
    std::string adhoc_nickname;            // shown to other players; empty: the hunter name
    std::string adhoc_mac;                 // this player's virtual MAC, made up on first use
    std::vector<std::string> adhoc_recent; // sessions joined lately, the latest first
    std::uint32_t adhoc_host_port{27312};  // the built-in server's adhocctl port; the relay is on the next

    // Interface
    bool menu_pause{true};         // opening the menu pauses the game
    bool menu_pause_multiplayer{}; // ...also during ad hoc play, where a paused game stops answering its peers
    // Damage numbers (game/damage_numbers.hpp): off, a hit is only what the
    // game already shows. Mine or Everyone, the health a hit removes is drawn
    // over the picture, for this console's hunter's hits or for all of them.
    DamageNumbers damage_numbers{DamageNumbers::Off};
    // How a number splits its raw and element shares. The total by default.
    DamageBreakdown damage_breakdown{DamageBreakdown::Total};
    // Sharpness gauge (game/sharpness_gauge.hpp). On by default. Off, the
    // game's full drawing.
    // On, the colour on the blade shortens as it is spent, never dims below a
    // full pulse and glows brighter, and orange and yellow get a colour too.
    // The blade stays.
    bool sharpness_gauge{true};
    // A ? over a gather node the game would accept (game/gather_icon.hpp).
    // On by default. Off, the list is not read for the mark.
    bool gather_icon{true};
    // Holding ○ gathers again each time the hunter is free on a node
    // (game/gather_icon.hpp). On by default. Off, ○ is one press.
    bool gather_hold{true};
    // ○ mines and catches bugs with a pickaxe or a bug net in the pouch,
    // selecting it for that gather (game/gather_icon.hpp, gather_circle). On
    // by default. Off: only □ with the tool selected, as the game has it.
    bool gather_circle{true};
    // The item bar gets an empty slot, Nothing Selected, before the first
    // item, and a quest starts on it, so □ uses no item
    // (game/empty_item_slot.hpp). On by default. Off: the game's own bar.
    bool empty_item_slot{true};
    // The Guild Hall's Give hands over items of any rarity, not only Rare 1
    // to 3 (game/give_rarity.hpp). Off by default: the game's limit.
    bool give_any_rarity{};
    // The equipment screens show a weapon's attack without its class factor
    // (game/true_attack.hpp) and its element and status without the trailing
    // zero (game/true_element.hpp). Off by default: the game's numbers.
    bool true_attack{};
    // The Guild Hall's box offers Change Equipment, as the house box does
    // (game/hall_equipment.hpp). On by default. Off: greyed out, as in the
    // game.
    bool hall_equipment{true};
    // The item box menu gets Item Sets, to register the pouch as a set and to
    // load a set from the box (game/item_sets.hpp). On by default. Off: the
    // game's own six entries.
    bool item_sets{true};
    // A hunter seated at a table in the Guild Hall orders from the home
    // kitchen's menu with △ (game/hall_meal.hpp). On by default. Off: △ does
    // nothing there, as in the game.
    bool hall_meal{true};
    // A key quest's name reads "[Key] " and its name wherever the game shows
    // it, the quest lists first (game/key_quests.hpp). Off by default: the
    // game's own names.
    bool key_quests{};
    // The game finds its Data Install on the memory stick, served from
    // DATA.BIN as the mods leave it (game/install_data.hpp). On by default.
    // Off: the game's own ULUS10391DAT, which Data Install writes. Read at
    // start; the game looks for the install as it starts.
    bool virtual_install{true};
    // The Guild Hall lists the quests in event_quests/ of the data directory
    // after the save's own event quests (game/event_quests.hpp). On by
    // default; without such quests nothing changes. Off: the save's alone.
    bool event_quests{true};
    // The five throwing knives are items, not supply items; the Throwing
    // Knife is sold in the shops for 750z and Whetstone + Machalite or
    // Dragonite Ore combines into one (game/throwing_knives.hpp). On by
    // default. Off: the game's supply items.
    bool throwing_knives{true};
    // The mark's size, in percent of the usual disc (kMinGatherIconSize to
    // kMaxGatherIconSize). A larger mark keeps the same proportions.
    std::uint32_t gather_icon_size{100};
    // Their size, in percent of the interface font's (kMinDamageNumberSize to
    // kMaxDamageNumberSize), and the weight of their strokes and dark outline
    // (1 to kMaxDamageNumberThickness, 1 the thinnest). Both scale with the
    // number as it shrinks near the camera.
    std::uint32_t damage_number_size{100};
    std::uint32_t damage_number_thickness{1};
    bool menu_hint_seen{};   // the "Esc / L3+R3 opens the menu" hint was shown
    std::string last_folder; // where the setup's file browser was last used
    std::string menu_tab;    // the menu's page when it last closed ("controls")

    // Saves
    bool backup_timestamp{true}; // a backup made from the menu goes to a new folder named by its time
    bool backup_reminder{true};  // remind to back up the saves the first time a new release starts
    std::string backup_reminded; // the release that last showed the reminder (savedata::release_of)

    // Layered armor (game/layered_armor.hpp): the hunter drawn in other armor
    // than it wears. Off, nothing of the game is touched. On, each armor part
    // with a piece chosen here is drawn as that piece, while the game keeps the
    // real one for everything else. The choices are the port's, never the save's.
    bool layered_armor{};
    bool layered_all{}; // kept so an older settings.ini still loads; the list is owned pieces
    // By part, in the game's order (chest, arms, waist, legs, head): the armor
    // piece's id, 0 for none (the bare part), or kLayeredReal for the piece worn.
    std::array<std::int32_t, 5> layered_pieces{-1, -1, -1, -1, -1};
};

inline constexpr std::int32_t kLayeredReal = -1;
inline constexpr std::int32_t kMaxLayeredPiece = 0xFFFF;
inline constexpr std::uint32_t kMaxInternalScale = 8u;
inline constexpr std::uint32_t kMaxWindowScale = 4u;
inline constexpr std::uint32_t kMinMapItemScale = 50u;
// 150% puts a normal health bar at the right edge of the 480-wide picture.
// Higher than that only carries the quest interface off the screen.
inline constexpr std::uint32_t kMaxMapItemScale = 150u;
inline constexpr std::uint32_t kMaxFontWeight = 2u;
inline constexpr std::uint32_t kMinHuntPortraitSize = 50u;
inline constexpr std::uint32_t kMaxHuntPortraitSize = 200u;
inline constexpr std::uint32_t kMinDamageNumberSize = 50u;
inline constexpr std::uint32_t kMaxDamageNumberSize = 300u;
// Past 3 the outline copies fill the digits in, so 3 is as thick as stays readable.
inline constexpr std::uint32_t kMaxDamageNumberThickness = 2u;
inline constexpr std::uint32_t kMinGatherIconSize = 50u;
inline constexpr std::uint32_t kMaxGatherIconSize = 300u;
inline constexpr float kMinMouseSensitivity = 0.01f;
inline constexpr float kMinTouchOpacity = 0.1f;
inline constexpr float kMaxTouchOpacity = 1.0f;
inline constexpr float kMinTouchSize = 0.6f;
inline constexpr float kMaxTouchSize = 1.6f;
inline constexpr float kMinTouchCameraSpeed = 30.0f;
inline constexpr float kMaxTouchCameraSpeed = 720.0f;
inline constexpr float kMaxMouseSensitivity = 0.99f;

// The platforms whose defaults differ. A phone plays full screen with a
// finger or a pad, so a few settings start otherwise there (defaults_for).
enum class Platform { Desktop, Android };
#if defined(__ANDROID__)
inline constexpr Platform kPlatform = Platform::Android;
#else
inline constexpr Platform kPlatform = Platform::Desktop;
#endif
// The defaults on `platform`: Desktop is Settings{} as declared above;
// Android differs in video.aspect (fill: a phone is wider than the PSP),
// video.fullscreen (on: there is no window) and input.mouse (off: a phone
// has no mouse to capture, and an emulator's pointer would turn the camera).
[[nodiscard]] Settings defaults_for(Platform platform);

// Loads the settings on first use, starting from defaults().
[[nodiscard]] Settings &current();
// This platform's defaults, for keys settings.ini lacks and for "Restore
// defaults".
[[nodiscard]] const Settings &defaults();
// Writes current() to settings.ini, leaving values set by environment
// variables at what the file had. Failures are reported on the console.
void save();

// Control presets. The player's preset called `name`, or null.
[[nodiscard]] input::UserPreset *find_user_preset(Settings &settings, const std::string &name);
// Chooses a preset and puts its layout in use. False if the player's preset
// named is not there.
bool choose_preset(Settings &settings, const input::PresetChoice &choice);
// Before the layout in use is changed: a shipped preset cannot be, so its
// layout is first copied into a new preset of the player's, which is then
// chosen. Returns that preset's name when one was made.
std::optional<std::string> prepare_controls_edit(Settings &settings);
// The shipped preset the layout in use is based on: the one chosen, or the
// one the player's preset was made from.
[[nodiscard]] input::Preset base_preset(const Settings &settings);
// After the layout in use was changed: the chosen preset of the player's
// keeps it.
void controls_edited(Settings &settings);

// settings.ini's keys and values, read over defaults() without the
// environment, and written back; for the tests. Controls written by earlier
// versions, which had keyboard bindings and a trigger profile but no
// presets, become a preset: a shipped one if they are one, otherwise one of
// the player's named "Custom".
// A file from before settings.version 2 has its text.crisp dropped, since
// those versions wrote it whether or not the player chose it (kVersion).
using Entries = std::map<std::string, std::string>;
[[nodiscard]] Settings from_entries(const Entries &entries);
[[nodiscard]] Entries to_entries(const Settings &settings);

// The environment variable that decides the setting stored under `key`
// (for example "video.internal_scale") for this run, or null.
[[nodiscard]] const char *overridden_by(const char *key);

} // namespace mhfu::settings
