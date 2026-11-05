// The in-game menu: Esc, or L3+R3 on a gamepad. It holds every player setting;
// each change applies at once and is saved to settings.ini straight away. It
// pauses the game, or, as the settings say, stays over the running game.

#include "ui/ui.hpp"

#include "camera/field_of_view.hpp"
#include "camera/hunt_icon.hpp"

#include "ui/bindings_editor.hpp"
#include "ui/controllers_screen.hpp"
#include "ui/font_menu.hpp"
#include "ui/input_script.hpp"
#include "ui/layer.hpp"
#include "ui/event_quests_screen.hpp"
#include "ui/layered_armor_screen.hpp"
#include "ui/mods_screen.hpp"
#include "ui/save_screen.hpp"
#include "ui/texture_pack_screen.hpp"
#include "ui/text_input.hpp"
#include "ui/touch_editor.hpp"
#include "ui/touch_overlay.hpp"
#include "ui/widgets.hpp"

#include "adhoc/client.hpp"
#include "adhoc/discovery.hpp"
#include "adhoc/session.hpp"
#include "audio/audio_sink.hpp"
#include "fonts/game_font.hpp"
#include "game/button_glyphs.hpp"
#include "game/damage_numbers.hpp"
#include "game/gather_icon.hpp"
#include "game/item_atlas.hpp"
#include "gpu/screenshot.hpp"
#include "gpu/game_hud.hpp"
#include "gpu/ui_motion.hpp"
#include "gpu/vulkan_renderer.hpp"
#include "hle/hle_common.hpp"
#include "input/bindings.hpp"
#include "install/game_identity.hpp"
#include "install/installer.hpp"
#include "install/user_data.hpp"
#include "kernel/fast_forward.hpp"
#include "kernel/fast_loading.hpp"
#include "platform/utf8_path.hpp"
#include "perf/frame_stats.hpp"
#include "save_data/save_transfer.hpp"
#include "settings/settings.hpp"
#include "pokee_version.hpp"
#if defined(MHFU_ANDROID_APP)
#include "platform/android_fatal.hpp"
#endif

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mhfu::ui {
namespace {

using Clock = std::chrono::steady_clock;

// Seconds the "how to open the menu" hint stays up at start, until the menu
// has been opened once.
constexpr double kHintSeconds = 12.0;
// The longest hunter name the game takes: its name buffer holds 12
// characters and a terminator.
constexpr std::size_t kHunterNameLength = 12u;
// Resolutions the menu offers. MHFU_INTERNAL_SCALE goes up to 8, but a
// setting that runs out of video memory would fail on every start.
constexpr int kMenuMaxInternalScale = 6;

std::string size_text(std::uint32_t scale) {
    return std::to_string(480u * scale) + "×" + std::to_string(272u * scale);
}

// Row options for the setting stored under `key`: when an environment
// variable decides it for this run, the row is shown but locked.
RowOptions options_for(const char *key, std::string description) {
    RowOptions options;
    options.description = std::move(description);
    if (const char *variable = settings::overridden_by(key)) {
        options.disabled = true;
        options.note = std::string("Set by ") + variable;
    }
    return options;
}

float font_gap() {
    return Layer::get().font_size() * 0.5f;
}

int cycle(int value, int delta, int count) {
    return ((value + delta) % count + count) % count;
}

// The menu's pages, in the order of kTabs. settings.ini keeps a page by
// name, so the order can change between versions.
enum class Page { Video, Audio, Controls, Game, Network, Mods, System };
constexpr const char *kTabs[] = {"Video", "Audio", "Controls", "Game", "Network", "Mods", "System"};
constexpr int kTabCount = static_cast<int>(std::size(kTabs));
static_assert(kTabCount == static_cast<int>(Page::System) + 1, "a name for every page");

// Where the menu was when it last closed (#189): its page, how far that page
// was scrolled and which row had the focus. Opening the menu again goes back
// there. The page is also kept in settings.ini for the next start; the rest
// only for this session, as rows come and go between runs.
struct MenuPlace {
    bool known{};
    int tab{};
    float scroll{};
    ImGuiID focus{};
};
MenuPlace &menu_place() {
    static MenuPlace place = [] {
        MenuPlace p;
        const std::string &saved = settings::current().menu_tab;
        for (int i = 0; i < kTabCount; ++i)
            if (!saved.empty() &&
                std::equal(saved.begin(), saved.end(), kTabs[i], kTabs[i] + std::strlen(kTabs[i]), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }))
                p.tab = i;
        return p;
    }();
    return place;
}

class Menu {
public:
    explicit Menu(bool paused) : paused_(paused), tab_(menu_place().tab) {}
    ~Menu() {
        // The page for the next start, in settings.ini, when it changed.
        std::string name = kTabs[menu_place().tab];
        std::transform(
            name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        settings::Settings &s = settings::current();
        if (s.menu_tab != name) {
            s.menu_tab = name;
            settings::save();
        }
    }
    Menu(const Menu &) = delete;
    Menu &operator=(const Menu &) = delete;
    // One frame; false once the menu closes.
    bool frame();
    [[nodiscard]] bool quit() const noexcept { return quit_; }

private:
    enum class Confirm { None, Quit, Setup, DeletePreset };

    void video();
    void audio();
    void controls();
    void preset_rows();
    void game();
    void network();
    void mods();
    void system();
    bool confirm_dialog();

    gpu::VulkanRenderer &renderer() { return Layer::get().renderer(); }
    [[nodiscard]] Page page() const noexcept { return static_cast<Page>(tab_); }

    bool paused_{}; // the game is paused behind the menu, rather than running
    int tab_{};
    bool first_frame_{true};
    bool unselected_{}; // opened with no row selected, until a move or a click
    bool close_{};
    bool quit_{};
    bool was_editing_{};
    bool back_{}; // the back button was pressed this frame
    Confirm confirm_{Confirm::None};
    bool confirm_opened_{};       // the confirmation was on screen last frame
    std::string preset_notice_;   // what the last preset change did, shown under Preset
    std::string screenshot_path_; // where Take a screenshot saved to
};

bool Menu::frame() {
    dropdown_new_frame();
    Layer &layer = Layer::get();
    // A text field's keyboard takes the whole screen until it closes.
    if (text_input_open()) {
        text_input_frame();
        return true;
    }
    // A texture pack copy keeps the menu open until it ends.
    texture_pack_import_tick();
    if (layer.take_menu_toggle() && !texture_pack_import_busy()) return false;
    const bool back = layer.take_back();
    // The pad's back button closes the menu too, once nothing is being edited.
    const ImGuiKey cancel = layer.confirm_south() ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
    const bool pad_back = ImGui::IsKeyPressed(cancel, false);
    const bool start = ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false);
    // Back closes the font list, or the save import and export, before it
    // closes the menu.
    bool font_list_was_open = (page() == Page::Video && (font_list_open() || texture_pack_screen_open())) ||
        (page() == Page::Controls && controllers_screen_open()) ||
        (page() == Page::Mods && (mods_screen_open() || layered_armor_screen_open() || event_quests_screen_open())) ||
        (page() == Page::System && save_screen_open());
    back_ = back || pad_back;

    // The touch layout's editor takes the whole screen until it is done.
    if (touch_editor_open()) {
        touch_editor_frame(back_);
        if (!touch_editor_open()) focus_next_row();
        return true;
    }

    begin_panel("##menu", "Pokee", paused_ ? "Paused" : "Running", true);
    // A page that is not there this run gives way to the first.
    if (tab_ >= kTabCount) tab_ = 0;
    static int dropdown_tab = -1;
    if (dropdown_tab != tab_) {
        dismiss_dropdowns();
        dropdown_tab = tab_;
    }
    const bool opening = first_frame_;
    const bool switched = tab_bar(kTabs, kTabCount, tab_) || first_frame_;
    first_frame_ = false;
    begin_content();
    MenuPlace &place = menu_place();
    bool restoring = false;
    // A click or a tap chose a row while none was selected.
    if (unselected_ && ImGui::GetCurrentContext()->NavId != 0u) unselected_ = false;
    if (opening && !font_list_was_open) {
        // No row is selected when the menu opens, so that a press right after
        // opening it cannot change a setting. The scroll is where the menu
        // closed, and the first move selects the row it closed on.
        const bool same_page = place.known && place.tab == tab_;
        ImGui::SetScrollY(same_page ? place.scroll : 0.0f);
        if (!same_page) place.focus = 0u;
        clear_focus();
        unselected_ = true;
    } else if (unselected_ && take_move_without_focus()) {
        unselected_ = false;
        if (place.focus != 0u) {
            ImGui::SetFocusID(place.focus, ImGui::GetCurrentWindow());
            ImGui::GetCurrentContext()->NavCursorVisible = true;
            restoring = true;
        } else {
            focus_next_row();
        }
    } else if (switched) {
        // A new page opens with no row selected too; the first move selects
        // its top row.
        ImGui::SetScrollY(0.0f);
        place.focus = 0u;
        clear_focus();
        unselected_ = true;
    }
    switch (page()) {
    case Page::Video:
        video();
        break;
    case Page::Audio:
        audio();
        break;
    case Page::Controls:
        controls();
        break;
    case Page::Game:
        game();
        break;
    case Page::Network:
        network();
        break;
    case Page::Mods:
        mods();
        break;
    case Page::System:
        system();
        break;
    }
    // The row that had the focus is gone (a row this platform or this state
    // does not have): the top of the page, as when the page is opened.
    if (restoring && !ImGui::GetCurrentContext()->NavIdIsAlive) {
        focus_next_row();
        ImGui::SetScrollY(0.0f);
    }
    // Where the menu is now, unless a screen of its own is over the page.
    if (!font_list_was_open && !restoring) {
        const ImGuiContext &g = *ImGui::GetCurrentContext();
        place.known = true;
        place.tab = tab_;
        place.scroll = ImGui::GetScrollY();
        if (g.NavWindow == ImGui::GetCurrentWindow() && g.NavId != 0u)
            place.focus = g.NavId;
        else if (switched && !unselected_)
            place.focus = 0u;
    }
    begin_footer();
    const BindingsFocus binding = page() == Page::Controls ? bindings_focus() : BindingsFocus::None;
    if (binding == BindingsFocus::Binding && bindings_focus_resettable())
        hints({{Control::Confirm, "Rebind"}, {Control::Clear, "Clear"}, {Control::Reset, "Reset action"},
            {Control::Back, "Back"}, {Control::Tabs, "Section"}});
    else if (binding == BindingsFocus::Binding)
        hints({{Control::Confirm, "Rebind"}, {Control::Clear, "Clear"}, {Control::Back, "Back"},
            {Control::Tabs, "Section"}, {Control::Menu, "Resume"}});
    else if (binding == BindingsFocus::Add)
        hints({{Control::Confirm, "Add"}, {Control::Back, "Back"}, {Control::Tabs, "Section"},
            {Control::Menu, "Resume"}});
    else if (binding == BindingsFocus::Reset)
        hints({{Control::Confirm, "Reset action"}, {Control::Back, "Back"}, {Control::Tabs, "Section"},
            {Control::Menu, "Resume"}});
    else if (page() == Page::Mods || page() == Page::System || binding == BindingsFocus::Fix)
        hints({{Control::Confirm, "Select"}, {Control::Back, "Back"}, {Control::Tabs, "Section"},
            {Control::Menu, "Resume"}});
    else
        hints({{Control::Confirm, "Select"}, {Control::Change, "Change"}, {Control::Back, "Back"},
            {Control::Tabs, "Section"}, {Control::Menu, "Resume"}});
    end_panel();
    bindings_capture_prompt();

    if (confirm_ != Confirm::None) {
        if ((back || pad_back) && confirm_opened_) confirm_ = Confirm::None;
    } else if (!font_list_was_open &&
        ((((back || pad_back) && !was_editing_ && !dropdown_swallowed_back()) || start))) {
        close_ = true;
    }
    if (close_) dismiss_dropdowns();
    if (close_ && !quit_ && texture_pack_import_busy()) close_ = false;
    if (!confirm_dialog()) return false;
    was_editing_ = ImGui::IsAnyItemActive();
    return !close_;
}

void Menu::video() {
    if (font_list(back_)) {
        dismiss_dropdowns();
        return;
    }
    if (texture_pack_screen(back_)) {
        dismiss_dropdowns();
        return;
    }
    settings::Settings &s = settings::current();
    section("Picture");
    {
        RowOptions o = options_for("video.internal_scale",
            "Auto draws the game at the window's own size and follows it (at most 1632 "
            "lines). ×1 to ×6 draw 272 lines per step, 480×272 times the step unless the "
            "aspect ratio is Fill. Higher is sharper and needs more from the GPU.");
        const std::array<std::uint32_t, 2> size = renderer().target_size();
        const std::string drawn = std::to_string(size[0]) + "×" + std::to_string(size[1]);
        const std::string value =
            (s.internal_scale == 0u ? std::string("Auto") : "×" + std::to_string(s.internal_scale)) + "   " + drawn;
        if (const int delta = choice_row("Resolution", value, o)) {
            // Auto, then ×1 up to the menu's largest (or a larger one a
            // variable once chose).
            const int limit = std::max<int>(kMenuMaxInternalScale, static_cast<int>(s.internal_scale));
            s.internal_scale = static_cast<std::uint32_t>(cycle(static_cast<int>(s.internal_scale), delta, limit + 1));
            renderer().set_internal_scale(s.internal_scale);
            settings::save();
        }
    }
#if !defined(__ANDROID__)
    // A phone is always full screen: no window to size.
    {
        const int delta = choice_row("Display", s.fullscreen ? "Fullscreen" : "Window",
            options_for("video.fullscreen", "Fill the screen, or play in a window you can resize."));
        if (delta != 0) {
            s.fullscreen = !s.fullscreen;
            renderer().set_fullscreen(s.fullscreen);
            settings::save();
        }
    }
    {
        RowOptions o = options_for("video.window_scale", "The window's size, in multiples of the PSP's screen.");
        if (s.fullscreen) {
            o.disabled = true;
            o.note = "Fullscreen";
        }
        const std::string value = "×" + std::to_string(s.window_scale) + "   " + size_text(s.window_scale);
        if (const int delta = choice_row("Window size", value, o)) {
            s.window_scale = static_cast<std::uint32_t>(
                cycle(static_cast<int>(s.window_scale) - 1, delta, static_cast<int>(settings::kMaxWindowScale)) + 1);
            renderer().set_window_scale(s.window_scale);
            settings::save();
        }
    }
#endif
    {
        static const char *const kAspects[] = {"Original", "Stretch", "Fill"};
        const int current = static_cast<int>(s.aspect);
        if (const int delta = choice_row("Aspect ratio", kAspects[current],
                options_for("video.aspect",
                    "Original keeps the PSP's shape with black bars at the sides or top. "
                    "Stretch fills the window by stretching the picture. Fill widens (or "
                    "narrows) the game's view to the window's shape, keeping its height, "
                    "and keeps the interface in the PSP's proportions."))) {
            s.aspect = static_cast<settings::Aspect>(cycle(current, delta, 3));
            renderer().set_aspect(s.aspect);
            settings::save();
        }
    }
    {
        RowOptions o = options_for("video.field_of_view",
            "How much of the world the game's 3D view takes in on a hunt, from top to bottom. 50, marked on the "
            "bar, is the game's own view; views the game makes wider or narrower during a quest, such as a bowgun's zoom, change in "
            "proportion. The village and the Guild Hall keep the game's view.");
        if (!camera::field_of_view_available()) {
            o.disabled = true;
            o.note = "Not with this game's code";
        }
        int degrees = static_cast<int>(s.field_of_view);
        if (slider_row("Field of view", degrees, static_cast<int>(settings::kMinFieldOfView),
                static_cast<int>(settings::kMaxFieldOfView), 1, "%d deg", o,
                static_cast<int>(settings::kGameFieldOfView))) {
            s.field_of_view = static_cast<std::uint32_t>(degrees);
            settings::save();
        }
    }
    if (choice_row("Scaling filter", s.sharp_screen ? "Sharp" : "Smooth",
            options_for("video.sharp_screen",
                "How the finished picture is scaled to the window: smooth "
                "(bilinear) or sharp (nearest pixel)."))) {
        s.sharp_screen = !s.sharp_screen;
        renderer().set_sharp_screen(s.sharp_screen);
        settings::save();
    }
    {
        struct Element {
            const char *name;
            const char *key;
            const char *help;
            std::uint32_t settings::Settings::InterfaceScale::*value;
        };
        static const Element kElements[] = {
            {"Map", "video.hud.map",
                "The area map on the right of a quest, from its top-right corner. 100% is the size the game draws.",
                &settings::Settings::InterfaceScale::map},
            {"Status", "video.hud.health",
                "The quest clock, the party list under it (up to four hunters and the Felyne Comrade, each with "
                "its name, badge, eye and buffs), health and stamina, sharpness, "
                "the long sword's spirit gauge, "
                "a bowgun's loaded shots, a bow's coatings, a gunlance's shells and a hunting horn's notes, from the "
                "clock's corner so they stay together. "
                "100% is the size the game draws.",
                &settings::Settings::InterfaceScale::health},
            {"Items", "video.hud.item_bar",
                "The item bar, open or closed, from its bottom-right corner, including the item's name, a bowgun's "
                "ammo bar and a bow's coating bar, and their menus. The item list and the shops stay as the game "
                "draws them. "
                "100% is the size the game draws.",
                &settings::Settings::InterfaceScale::item_bar},
            {"Names", "video.hud.names",
                "The names over other hunters in a quest and in the gathering hall, and the mark beside each. "
                "Each name stays on the point the game places it, and the map's size does not change it. "
                "The names over hunters in the village follow it too; the rest of the village and the hall stay at the "
                "size the game draws. "
                "The name under the quest clock follows Status. "
                "100% is the size the game draws.",
                &settings::Settings::InterfaceScale::names},
        };
        static bool open = true;
        disclosure_row("UI Scaling", open,
            {false, {},
                "Sizes for the quest interface. Each part below has its own size, from 50% to 150%. "
                "Menus stay as the game draws them."});
        if (open) {
            ImGui::Indent(ImGui::GetFontSize() * 0.75f);
            for (const Element &element : kElements) {
                int scale = static_cast<int>(s.hud.*(element.value));
                if (slider_row(element.name, scale, static_cast<int>(settings::kMinMapItemScale),
                        static_cast<int>(settings::kMaxMapItemScale), 5, "%d%%",
                        options_for(element.key, element.help))) {
                    s.hud.*(element.value) = static_cast<std::uint32_t>(scale);
                    settings::save();
                }
            }
            ImGui::Unindent(ImGui::GetFontSize() * 0.75f);
        }
    }
    {
        const std::string key = input::format(s.controls.keys[static_cast<std::size_t>(input::Action::HideHud)]);
        RowOptions o;
        o.description = "Hides the game's HUD for pictures and videos: health, stamina, sharpness, the clock, the "
                        "item bar, the map, name tags and prompts. Menus and dialogs stay. Its key (" +
            (key.empty() ? std::string("none: set it in Controls") : key) + ") does the same.";
        if (!gpu::hud::available()) {
            o.disabled = true;
            o.note = "Not with this game's code";
        }
        if (toggle_row("Hide HUD", gpu::hud::hidden(), o)) gpu::hud::toggle();
    }
    section("Textures");
    if (choice_row("Texture filter", s.sharp_textures ? "Sharp" : "Smooth",
            options_for("video.sharp_textures",
                "How the game's textures are sampled: smooth (bilinear) or "
                "sharp (nearest texel)."))) {
        s.sharp_textures = !s.sharp_textures;
        renderer().set_sharp_textures(s.sharp_textures);
        settings::save();
    }
    {
        static const char *const kModes[] = {"Off", "Sharp bilinear", "MMPX"};
        const int current = static_cast<int>(s.ui_textures);
        if (const int delta = choice_row("UI textures", kModes[current],
                options_for("video.ui_textures",
                    "Above x1, how the game's 2D interface (HUD, menus, icons) is drawn; the 3D world is "
                    "not touched. Off: as before. Sharp bilinear: crisp pixels at any scale, without blur. "
                    "MMPX: the textures doubled by a pixel-art upscaler that rounds curves and diagonals, "
                    "then drawn sharp."))) {
            s.ui_textures = static_cast<settings::UiTextures>(cycle(current, delta, 3));
            settings::save();
        }
    }
    {
        RowOptions o = options_for("video.texture_pack",
            "Draws an HD texture pack in PPSSPP's format from textures/ULUS10391 in the data "
            "folder instead of the game's textures.");
        // The footer shows the note under the description: what is loaded,
        // or where the pack was looked for.
        if (o.note.empty()) {
            const std::string status = renderer().texture_pack_status();
            o.note = status == "Not installed" ? "No pack in " + renderer().texture_pack_folder() : status;
            // A pack used in place that has moved: its path is on the "Pack
            // used from" row below; the footer has room for its name only.
            if (status.rfind("Folder missing: ", 0) == 0)
                o.note = "Pack folder missing: " +
                    install::path_to_utf8(install::path_from_utf8(status.substr(16)).filename());
        }
        if (choice_row("Texture pack", s.texture_pack ? "On" : "Off", o)) {
            s.texture_pack = !s.texture_pack;
            renderer().set_texture_pack(s.texture_pack);
            settings::save();
        }
    }
    if (s.texture_pack && renderer().texture_pack_has_button_icons()) {
        static const char *const kIcons[] = {"Auto", "Pack default", "Xbox", "PlayStation", "Nintendo"};
        const int current = static_cast<int>(s.texture_pack_buttons);
        RowOptions o = options_for("video.texture_pack_buttons",
            "The buttons the pack's prompts show, for a pack that comes with several sets. Auto follows the "
            "controller in use: Xbox letters for an Xbox pad, PlayStation shapes, Nintendo letters for a "
            "Switch pad, and the pack's default with no pad or with Confirm button on the bottom button.");
        if (o.note.empty()) o.note = "Showing " + renderer().texture_pack_button_icons();
        if (const int delta = choice_row("Button icons", kIcons[current], o)) {
            s.texture_pack_buttons = static_cast<settings::PackButtons>(cycle(current, delta, 5));
            settings::save();
        }
    }
    texture_pack_rows();
    section("Timing");
    {
        struct Mode {
            settings::PresentMode mode;
            const char *name;
        };
        std::vector<Mode> modes{{settings::PresentMode::Fifo, "On"}};
        if (renderer().supports_present_mode(settings::PresentMode::Mailbox))
            modes.push_back({settings::PresentMode::Mailbox, "Off (mailbox)"});
        if (renderer().supports_present_mode(settings::PresentMode::Immediate))
            modes.push_back({settings::PresentMode::Immediate, "Off (immediate)"});
        int current = 0;
        for (std::size_t i = 0; i < modes.size(); ++i)
            if (modes[i].mode == s.present_mode) current = static_cast<int>(i);
        if (const int delta = choice_row("Vsync", modes[static_cast<std::size_t>(current)].name,
                options_for("video.present_mode",
                    "On waits for the display's refresh and never tears. Off shows "
                    "each frame at once; the game's speed is the same either way."))) {
            s.present_mode =
                modes[static_cast<std::size_t>(cycle(current, delta, static_cast<int>(modes.size())))].mode;
            renderer().set_present_mode(s.present_mode);
            settings::save();
        }
    }
    {
        static const char *const kRates[] = {"30", "45", "60", "90", "120", "Match display"};
        RowOptions o = options_for("video.frame_rate",
            "Frames between the game's 30 a second, blending its movement. Steps down by "
            "itself rather than slow the game.");
        if (s.unthrottled && !o.disabled) {
            o.disabled = true;
            o.note = "Game speed is Unlimited";
        }
        const int current = static_cast<int>(s.frame_rate);
        std::string value = kRates[current];
        if (s.frame_rate == settings::FrameRate::Display && renderer().display_refresh() > 0.0f)
            value += "  " + std::to_string(static_cast<int>(std::lround(renderer().display_refresh()))) + " Hz";
        if (s.frame_rate != settings::FrameRate::Fps30 && !o.disabled) {
            // Vsync caps the rate at the display's, and the renderer steps
            // down rather than slow the game.
            const double now = renderer().frame_rate_now();
            const double chosen = s.frame_rate == settings::FrameRate::Display
                ? static_cast<double>(renderer().display_refresh())
                : std::stod(kRates[current]);
            if (now + 0.5 < chosen) {
                value += " (running at " + std::to_string(static_cast<int>(std::lround(now))) + ")";
                o.description = s.present_mode == settings::PresentMode::Fifo && renderer().display_refresh() > 0.0f &&
                        now + 0.5 >= static_cast<double>(renderer().display_refresh())
                    ? "With Vsync on, no faster than the display refreshes."
                    : "Lowered to keep the game at full speed; it tries the chosen rate again later.";
            }
        }
        if (const int delta = choice_row("Frame rate", value, o)) {
            s.frame_rate = static_cast<settings::FrameRate>(cycle(current, delta, 6));
            renderer().set_frame_rate(s.frame_rate);
            settings::save();
        }
    }
    {
        RowOptions o = options_for("video.frame_rate_auto",
            "On lowers the frame rate by itself when presenting that often would slow the "
            "game. Off keeps the chosen rate, and the game may then run below full speed.");
        if (s.unthrottled || s.frame_rate == settings::FrameRate::Fps30) o.disabled = true;
        if (choice_row("Lower when behind", s.frame_rate_auto ? "On" : "Off", o)) {
            s.frame_rate_auto = !s.frame_rate_auto;
            renderer().set_frame_rate_auto(s.frame_rate_auto);
            settings::save();
        }
    }
    {
        static const char *const kPerf[] = {
            "Off", "Overlay", "Overlay and log", "Log only", "Compact", "Compact and log"};
        constexpr int kCount = static_cast<int>(sizeof(kPerf) / sizeof(kPerf[0]));
        const int current = static_cast<int>(s.perf);
        if (const int delta = choice_row("Performance", kPerf[current],
                options_for("video.performance",
                    "Frame times and speed in the top-left corner, and a [perf] line "
                    "per second on the console. Compact is one line in the bottom-left corner: "
                    "frames shown, the game's frame rate and its speed, green at full speed. "
                    "F3 shows or hides the overlay."))) {
            s.perf = static_cast<settings::PerfDisplay>(cycle(current, delta, kCount));
            renderer().set_perf_overlay(perf::options().overlay);
            settings::save();
        }
    }
    {
        static const char *const kCompat[] = {"Auto", "On", "Off"};
        const int current = static_cast<int>(s.gpu_compat);
        RowOptions o = options_for("video.gpu_compat",
            "Leaves out what some older phone GPU drivers may get wrong: specialized shaders, robust buffer "
            "access, the pipeline cache and a second frame in flight. It costs speed, so Auto turns it on only "
            "when the start-up test picture comes out wrong or the driver refuses a shader. Applies when Pokee "
            "starts next. Now: " +
                renderer().gpu_compat_status() + ".");
        if (const int delta = choice_row("GPU compatibility", kCompat[current], o)) {
            s.gpu_compat = static_cast<settings::GpuCompat>(cycle(current, delta, 3));
            settings::save();
        }
    }
    font_rows();
    ImGui::Dummy({0.0f, font_gap()});
    if (button_row("Restore video defaults", {false, {}, "Every setting on this page back to how Pokee ships."})) {
        const settings::Settings &d = settings::defaults();
        const auto restore = [&](const char *key, auto &value, const auto &fallback) {
            if (settings::overridden_by(key) == nullptr) value = fallback;
        };
        restore("video.internal_scale", s.internal_scale, d.internal_scale);
        restore("video.fullscreen", s.fullscreen, d.fullscreen);
        restore("video.window_scale", s.window_scale, d.window_scale);
        restore("video.aspect", s.aspect, d.aspect);
        restore("video.field_of_view", s.field_of_view, d.field_of_view);
        restore("video.sharp_screen", s.sharp_screen, d.sharp_screen);
        restore("video.sharp_textures", s.sharp_textures, d.sharp_textures);
        restore("video.ui_textures", s.ui_textures, d.ui_textures);
        restore("video.texture_pack", s.texture_pack, d.texture_pack);
        restore("video.texture_pack_buttons", s.texture_pack_buttons, d.texture_pack_buttons);
        restore("video.gpu_compat", s.gpu_compat, d.gpu_compat);
        restore("video.present_mode", s.present_mode, d.present_mode);
        restore("video.frame_rate", s.frame_rate, d.frame_rate);
        restore("video.frame_rate_auto", s.frame_rate_auto, d.frame_rate_auto);
        restore("video.performance", s.perf, d.perf);
        restore("video.hud.map", s.hud.map, d.hud.map);
        restore("video.hud.health", s.hud.health, d.hud.health);
        restore("video.hud.item_bar", s.hud.item_bar, d.hud.item_bar);
        restore("video.hud.names", s.hud.names, d.hud.names);
        renderer().set_internal_scale(s.internal_scale);
        renderer().set_fullscreen(s.fullscreen);
        renderer().set_window_scale(s.window_scale);
        renderer().set_aspect(s.aspect);
        renderer().set_sharp_screen(s.sharp_screen);
        renderer().set_sharp_textures(s.sharp_textures);
        renderer().set_texture_pack(s.texture_pack);
        renderer().set_present_mode(s.present_mode);
        renderer().set_frame_rate(s.frame_rate);
        renderer().set_frame_rate_auto(s.frame_rate_auto);
        renderer().set_perf_overlay(perf::options().overlay);
        settings::save();
    }
}

void Menu::audio() {
    settings::Settings &s = settings::current();
    audio::AudioSink &sink = audio::AudioSink::instance();
    const bool device = sink.has_device();
    const auto locked = [&](const char *key, const char *description) {
        RowOptions options = options_for(key, description);
        if (!device) {
            options.disabled = true;
            options.note = std::getenv("MHFU_NO_AUDIO") != nullptr ? "Off: MHFU_NO_AUDIO" : "No audio device";
        }
        return options;
    };
    section("Output");
    int volume = static_cast<int>(s.volume);
    if (slider_row(
            "Volume", volume, 0, 100, 5, "%d%%", locked("audio.volume", "Loudness of everything the game plays."))) {
        s.volume = static_cast<std::uint32_t>(volume);
        sink.apply_settings();
        settings::save();
    }
    if (toggle_row("Mute", s.mute, locked("audio.mute", "Silence the game without losing the volume setting."))) {
        s.mute = !s.mute;
        sink.apply_settings();
        settings::save();
    }
    if (toggle_row("Mute in background", s.background_mute,
            locked("audio.background_mute", "Silence the game while its window is not the one in front."))) {
        s.background_mute = !s.background_mute;
        sink.apply_settings();
        settings::save();
    }
    info_row(
        "Device", device ? (paused_ ? "44100 Hz stereo, paused while this menu is open" : "44100 Hz stereo") : "None");
    ImGui::Dummy({0.0f, font_gap()});
    if (button_row("Restore audio defaults", {!device, {}, "Full volume, not muted, heard in the background."})) {
        s.volume = settings::defaults().volume;
        s.mute = settings::defaults().mute;
        s.background_mute = settings::defaults().background_mute;
        sink.apply_settings();
        settings::save();
    }
}

// A text field row. Returns true when an edit was committed; the new text is
// then in `value`. `id` keeps the field's state apart from other rows'.
// Activated with a gamepad, the field opens the on-screen keyboard; with a
// keyboard or mouse it is edited in place.
struct TextField {
    std::array<char, 132> buffer{};
    bool focused{};
    std::optional<std::string> entered; // from the on-screen keyboard
    bool refocus{};                     // the keyboard closed; focus the row again
};

bool text_row(const char *id, const char *label, std::string &value, std::size_t max_length, bool allow_empty,
    const std::function<bool(char32_t)> &allowed, const RowOptions &o) {
    static std::map<std::string, TextField> fields;
    TextField &field = fields[id];
    bool committed = false;
    if (field.entered) {
        if (allow_empty || !field.entered->empty()) {
            value = std::move(*field.entered);
            committed = true;
        }
        field.entered.reset();
    }
    if (!ImGui::IsAnyItemActive() || !field.focused)
        std::snprintf(field.buffer.data(), field.buffer.size(), "%s", value.c_str());
    const float row_height = std::round(Layer::get().font_size() * 1.9f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    // Highlighted like the other rows; the field reports its focus only after
    // it is drawn, so last frame's state is used.
    if (field.focused) draw_selection(draw, start, {start.x + width, start.y + row_height}, 235);
    draw->AddText(
        {start.x + std::round(16.0f * Layer::get().scale()), start.y + (row_height - Layer::get().font_size()) * 0.5f},
        o.disabled          ? colors::kTextDisabled
            : field.focused ? colors::kAccentBright
                            : colors::kText,
        label);
    const float field_width = std::min(width * 0.45f, Layer::get().font_size() * 12.0f);
    ImGui::SetCursorScreenPos({start.x + width - field_width - std::round(16.0f * Layer::get().scale()),
        start.y + (row_height - ImGui::GetFrameHeight()) * 0.5f});
    ImGui::SetNextItemWidth(field_width);
    if (o.disabled) ImGui::BeginDisabled();
    const auto filter = [](ImGuiInputTextCallbackData *data) {
        const auto *accepts = static_cast<const std::function<bool(char32_t)> *>(data->UserData);
        return (*accepts)(static_cast<char32_t>(data->EventChar)) ? 0 : 1;
    };
    const std::string widget_id = std::string("##") + id;
    ImGui::InputText(widget_id.c_str(), field.buffer.data(), std::min(max_length + 1u, field.buffer.size()),
        ImGuiInputTextFlags_CallbackCharFilter, filter, const_cast<std::function<bool(char32_t)> *>(&allowed));
    if (ImGui::IsItemActivated() && Layer::get().input_device() == InputDevice::Gamepad) {
        ImGui::ClearActiveID();
        TextInputRequest request;
        request.title = label;
        request.prompt = o.description;
        request.initial = value;
        request.max_length = max_length;
        request.allowed = allowed;
        open_text_input(std::move(request), [key = std::string(id)](std::optional<std::string> text) {
            TextField &closed = fields[key];
            closed.refocus = true;
            if (text) closed.entered = std::move(*text);
        });
    }
    // The menu was not drawn while the keyboard was up, so its focus is lost.
    if (field.refocus && !text_input_open()) {
        field.refocus = false;
        ImGui::FocusItem();
    }
    field.focused = ImGui::IsItemFocused();
    if (ImGui::IsItemFocused() || ImGui::IsItemHovered()) {
        std::string description = o.description;
        if (!o.note.empty()) description += "\n" + o.note;
        Layer::get().set_description(description);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && (allow_empty || field.buffer[0] != '\0')) {
        value = field.buffer.data();
        committed = true;
    }
    if (o.disabled) ImGui::EndDisabled();
    ImGui::SetCursorScreenPos({start.x, start.y + row_height});
    ImGui::Dummy({0.0f, 0.0f});
    return committed;
}

// Host names: printable ASCII without spaces.
bool host_character(char32_t c) {
    return c > 0x20u && c < 0x7Fu;
}

// What an address to join may hold: a host name or IPv4 address, a colon and
// a port, or an IPv6 address in brackets.
bool address_character(char32_t c) {
    return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'.' || c == U':' ||
        c == U'-' || c == U'[' || c == U']';
}

// An action's keys and pad buttons in one line, "F12 / PrintScreen; RS + D-pad
// Left", or a hint when neither device has any.
std::string binds_of(input::Action action) {
    std::string text = bindings_summary(action);
    return text.empty() ? "none: set it in Controls" : text;
}

// Characters a preset's name may hold: printable, without settings.ini's own.
bool preset_name_character(char32_t c) {
    return c >= 0x20u && c != 0x7Fu && c != U'=' && c != U'#';
}

void Menu::preset_rows() {
    settings::Settings &s = settings::current();
    section("Control preset");
    const RowOptions locked = options_for("input.preset", "");
    // The shipped presets, then the player's.
    const int shipped = static_cast<int>(input::kPresets);
    const int count = shipped + static_cast<int>(s.user_presets.size());
    int current = 0;
    if (s.control_preset.shipped) {
        current = static_cast<int>(*s.control_preset.shipped);
    } else {
        for (std::size_t i = 0; i < s.user_presets.size(); ++i)
            if (s.user_presets[i].name == s.control_preset.user) current = shipped + static_cast<int>(i);
    }
    const std::string current_name = current < shipped
        ? input::info(static_cast<input::Preset>(current)).name
        : s.user_presets[static_cast<std::size_t>(current - shipped)].name + "  (yours)";
    {
        RowOptions o = locked;
        o.description = current < shipped
            ? std::string(input::info(static_cast<input::Preset>(current)).description) +
                "\nChanging a binding below saves the change as a preset of your own."
            : "Your own preset: the bindings below change it, and it is kept as you leave it.";
        if (const int delta = choice_row("Preset", current_name, o)) {
            const int next = cycle(current, delta, count);
            const input::PresetChoice choice = next < shipped
                ? input::PresetChoice{static_cast<input::Preset>(next), {}}
                : input::PresetChoice{std::nullopt, s.user_presets[static_cast<std::size_t>(next - shipped)].name};
            settings::choose_preset(s, choice);
            preset_notice_.clear();
            settings::save();
        }
    }
    if (!preset_notice_.empty()) info_row("Saved", preset_notice_);
    if (!s.control_preset.shipped) {
        std::string name = s.control_preset.user;
        RowOptions o = locked;
        o.description = "The name of this preset.";
        if (text_row("preset_name", "Name", name, input::kMaxPresetName, false, preset_name_character, o)) {
            const std::string cleaned = input::clean_preset_name(name);
            bool taken = cleaned.empty();
            for (const input::UserPreset &other : s.user_presets)
                taken = taken || (other.name != s.control_preset.user && other.name == cleaned);
            for (std::size_t i = 0; i < input::kPresets; ++i)
                taken = taken || cleaned == input::info(static_cast<input::Preset>(i)).name;
            if (input::UserPreset *preset = settings::find_user_preset(s, s.control_preset.user); preset && !taken) {
                preset->name = cleaned;
                s.control_preset.user = cleaned;
                preset_notice_.clear();
                settings::save();
            } else if (taken && !cleaned.empty()) {
                preset_notice_ = "Another preset is called " + cleaned + "; the name was kept.";
            }
        }
    }
    {
        RowOptions o = locked;
        o.description = "A new preset of your own with the bindings above, to change without losing these.";
        if (s.user_presets.size() >= input::kMaxUserPresets && !o.disabled) {
            o.disabled = true;
            o.note = "At most " + std::to_string(input::kMaxUserPresets) + " presets";
        }
        if (button_row("Save as a new preset", o)) {
            const std::string base = s.control_preset.shipped ? std::string("Custom") : s.control_preset.user;
            const std::string name = input::unique_preset_name(s.user_presets, base);
            s.user_presets.push_back({name, s.controls, settings::base_preset(s)});
            s.control_preset = {std::nullopt, name};
            preset_notice_ = "The bindings are now also the preset " + name + ".";
            settings::save();
        }
    }
    if (!s.control_preset.shipped) {
        RowOptions o = locked;
        o.description = "Removes this preset for good and goes back to Default. Asks first.";
        if (button_row("Delete this preset", o, colors::kDanger)) confirm_ = Confirm::DeletePreset;
    }
    // Clashes anywhere in the layout.
    const std::size_t clashes = bindings_conflicts();
    if (clashes != 0u)
        info_row("Conflicts",
            std::to_string(clashes) + (clashes == 1u ? " binding clashes" : " bindings clash") +
                " with another; each is marked in red below, with a fix");
}

void Menu::controls() {
    if (controllers_screen(back_)) return;
    settings::Settings &s = settings::current();
    controllers_rows();
    preset_rows();
    bindings_editor(preset_notice_);
    section("Combinations");
    {
        int window = static_cast<int>(s.chord_window);
        if (slider_row("Chord window", window, 0, static_cast<int>(input::kMaxChordWindowMs), 10,
                window == 0 ? "Off" : "%d ms",
                options_for("input.chord_window",
                    "How long a button that begins a combination waits for the rest of it, such as "
                    "△ in △ + ○. Only those wait, and only where acting at once would press "
                    "something else first; every other button acts at once. Off: they act at "
                    "once, and a combination takes over when it is complete."))) {
            s.chord_window = static_cast<std::uint32_t>(window);
            settings::save();
        }
    }
    section("Gamepad");
    {
        SDL_Gamepad *pad = renderer().gamepad();
        const char *name = pad != nullptr ? SDL_GetGamepadName(pad) : nullptr;
        info_row("Connected",
            pad == nullptr ? "No gamepad; the keyboard and mouse drive the game"
                           : (name != nullptr ? name : "Gamepad"));
    }
    if (choice_row("Confirm button", s.confirm_south ? "Bottom (Western)" : "Right, ○ (Japanese)",
            options_for("input.confirm",
                "Which face button confirms in the game. The game's prompts show "
                "○ to confirm and × to go back. It swaps the bottom and right "
                "buttons in every preset. This menu always confirms with the bottom "
                "button and goes back with the right one."))) {
        s.confirm_south = !s.confirm_south;
        settings::save();
    }
    int dead_zone = static_cast<int>(std::lround(s.dead_zone * 100.0f));
    if (slider_row("Stick dead zone", dead_zone, 0, 50, 1, "%d%%",
            options_for("input.dead_zone",
                "How far the left stick moves before the hunter does. Raise it if "
                "the hunter drifts."))) {
        s.dead_zone = static_cast<float>(dead_zone) / 100.0f;
        settings::save();
    }
    int trigger = static_cast<int>(std::lround(s.trigger * 100.0f));
    if (slider_row("Trigger point", trigger, 5, 100, 5, "%d%%",
            options_for("input.trigger", "How far LT/RT (L2/R2) travel before they press anything."))) {
        s.trigger = static_cast<float>(trigger) / 100.0f;
        settings::save();
    }
    if (toggle_row("Gamepad in background", s.background_gamepad,
            options_for("input.background_gamepad",
                "Let the game read the gamepad while its window is not the one in front. Another "
                "game open at the same time may read the same presses. The keyboard and mouse "
                "only ever reach the window in front."))) {
        s.background_gamepad = !s.background_gamepad;
        settings::save();
    }
    {
        static const char *const kModes[] = {"Camera", "D-pad", "Off"};
        const int current = static_cast<int>(s.right_stick);
        if (const int delta = choice_row("Camera stick", kModes[current],
                options_for("input.right_stick",
                    "Camera turns the quest camera with the right stick. D-pad "
                    "presses the D-pad instead, like the PSP's camera controls. "
                    "It is the right stick unless the preset moves with it."))) {
            s.right_stick = static_cast<settings::RightStick>(cycle(current, delta, 3));
            settings::save();
        }
    }
    // On Android a finger drag drives the analog camera whatever the right
    // stick does, so the row stays open there.
    const bool camera =
        s.right_stick == settings::RightStick::Camera || settings::kPlatform == settings::Platform::Android;
    {
        RowOptions o = options_for("input.analog_camera",
            "Turn and tilt the quest camera as far as the stick is pushed, instead of the "
            "game's fixed-speed turn and vertical presets. Release holds the angle; a recentre "
            "returns to the game's camera. Off is the game's own camera, untouched.");
        if (!camera && !o.disabled) {
            o.disabled = true;
            o.note = "Camera stick is not the camera";
        }
        if (toggle_row("Analog camera", s.analog_camera, o)) {
            s.analog_camera = !s.analog_camera;
            settings::save();
        }
        o = options_for("input.dpad_camera",
            "Let the D-pad also turn the game's camera left and right and step its height, "
            "alongside the analog camera. Off, the D-pad leaves the camera to the stick.");
        if (!s.analog_camera && !o.disabled) {
            o.disabled = true;
            o.note = "Analog camera is off";
        }
        if (toggle_row("D-pad camera", s.dpad_camera, o)) {
            s.dpad_camera = !s.dpad_camera;
            settings::save();
        }
        o = options_for("input.camera_speed", "How fast the camera turns at full deflection, in degrees a second.");
        if (!s.analog_camera && !o.disabled) {
            o.disabled = true;
            o.note = "Analog camera is off";
        }
        int speed = static_cast<int>(s.camera_speed);
        if (slider_row("Camera speed", speed, 20, 720, 10, "%d deg/s", o)) {
            s.camera_speed = static_cast<float>(speed);
            settings::save();
        }
        o = options_for("input.aim_speed",
            "How fast a bow or a bowgun aims at full deflection, in degrees a "
            "second. The game's own aim uses three fixed speeds; this follows "
            "how far the stick is pushed.");
        if (!s.analog_camera && !o.disabled) {
            o.disabled = true;
            o.note = "Analog camera is off";
        }
        int aim = static_cast<int>(s.aim_speed);
        if (slider_row("Aim speed", aim, 10, 360, 5, "%d deg/s", o)) {
            s.aim_speed = static_cast<float>(aim);
            settings::save();
        }
    }
    {
        RowOptions o = options_for("input.invert_camera_x", "Turn the camera the other way left and right.");
        if (!camera && !o.disabled) {
            o.disabled = true;
            o.note = "Camera stick is not the camera";
        }
        if (toggle_row("Invert camera horizontally", s.invert_camera_x, o)) {
            s.invert_camera_x = !s.invert_camera_x;
            settings::save();
        }
        o = options_for("input.invert_camera_y", "Turn the camera the other way up and down.");
        if (!camera && !o.disabled) {
            o.disabled = true;
            o.note = "Camera stick is not the camera";
        }
        if (toggle_row("Invert camera vertically", s.invert_camera_y, o)) {
            s.invert_camera_y = !s.invert_camera_y;
            settings::save();
        }
    }
    {
        RowOptions o = options_for("input.right_stick_zone",
            "How far the camera stick moves before it presses the "
            "D-pad.");
        if (s.right_stick != settings::RightStick::DPad && !o.disabled) {
            o.disabled = true;
            o.note = "Camera stick is not the D-pad";
        }
        int zone = static_cast<int>(std::lround(s.right_stick_zone * 100.0f));
        if (slider_row("Camera stick D-pad point", zone, 10, 100, 5, "%d%%", o)) {
            s.right_stick_zone = static_cast<float>(zone) / 100.0f;
            settings::save();
        }
    }

    section("Keyboard and mouse");
    {
        RowOptions o = options_for("input.mouse",
            "While the game runs, the window takes the pointer: moving the mouse turns the "
            "camera and its buttons press what they are bound to. Esc opens this menu and "
            "gives the pointer back.");
        if (toggle_row("Mouse", s.mouse, o)) {
            s.mouse = !s.mouse;
            settings::save();
        }
        o = options_for("input.mouse_sensitivity",
            "Degrees the camera turns for each count of mouse motion. While a bow or a bowgun aims, "
            "the mouse is slowed as Aim speed is to Camera speed.");
        if (!s.mouse && !o.disabled) {
            o.disabled = true;
            o.note = "Mouse is off";
        }
        int sensitivity = static_cast<int>(std::lround(s.mouse_sensitivity * 100.0f));
        if (slider_row("Mouse sensitivity", sensitivity, 1, 99, 1, "0.%02d deg", o)) {
            s.mouse_sensitivity = static_cast<float>(sensitivity) / 100.0f;
            settings::save();
        }
        o = options_for("input.invert_mouse_x", "Turn the camera the other way when the mouse moves sideways.");
        if (!s.mouse && !o.disabled) {
            o.disabled = true;
            o.note = "Mouse is off";
        }
        if (toggle_row("Invert mouse horizontally", s.invert_mouse_x, o)) {
            s.invert_mouse_x = !s.invert_mouse_x;
            settings::save();
        }
        o = options_for("input.invert_mouse_y", "Push the mouse forward to look down instead of up.");
        if (!s.mouse && !o.disabled) {
            o.disabled = true;
            o.note = "Mouse is off";
        }
        if (toggle_row("Invert mouse vertically", s.invert_mouse_y, o)) {
            s.invert_mouse_y = !s.invert_mouse_y;
            settings::save();
        }
    }

    ImGui::Dummy({0.0f, font_gap()});
    if (button_row("Restore control defaults",
            {false, {},
                "Every gamepad, keyboard, mouse and touch setting back to how "
                "Pokee ships, with the Default preset. Your own presets and the Action layout's "
                "arrangement are kept."})) {
        const settings::Settings &d = settings::defaults();
        const auto restore = [&](const char *key, auto &value, const auto &fallback) {
            if (settings::overridden_by(key) == nullptr) value = fallback;
        };
        restore("input.confirm", s.confirm_south, d.confirm_south);
        restore("input.dead_zone", s.dead_zone, d.dead_zone);
        restore("input.trigger", s.trigger, d.trigger);
        restore("input.background_gamepad", s.background_gamepad, d.background_gamepad);
        restore("input.right_stick", s.right_stick, d.right_stick);
        restore("input.right_stick_zone", s.right_stick_zone, d.right_stick_zone);
        restore("input.analog_camera", s.analog_camera, d.analog_camera);
        restore("input.dpad_camera", s.dpad_camera, d.dpad_camera);
        restore("input.camera_speed", s.camera_speed, d.camera_speed);
        restore("input.aim_speed", s.aim_speed, d.aim_speed);
        restore("input.invert_camera_x", s.invert_camera_x, d.invert_camera_x);
        restore("input.invert_camera_y", s.invert_camera_y, d.invert_camera_y);
        restore("input.mouse", s.mouse, d.mouse);
        restore("input.mouse_sensitivity", s.mouse_sensitivity, d.mouse_sensitivity);
        restore("input.invert_mouse_x", s.invert_mouse_x, d.invert_mouse_x);
        restore("input.invert_mouse_y", s.invert_mouse_y, d.invert_mouse_y);
        restore("input.touch_controls", s.touch_controls, d.touch_controls);
        restore("input.touch_dpad", s.touch_dpad, d.touch_dpad);
        restore("input.touch_opacity", s.touch_opacity, d.touch_opacity);
        restore("input.touch_size", s.touch_size, d.touch_size);
        restore("input.touch_camera_speed", s.touch_camera_speed, d.touch_camera_speed);
        restore("input.touch_layout", s.touch_layout, d.touch_layout);
        restore("input.touch_haptics", s.touch_haptics, d.touch_haptics);
        if (settings::overridden_by("input.preset") == nullptr) settings::choose_preset(s, d.control_preset);
        preset_notice_.clear();
        settings::save();
    }

    section("Touch screen");
    {
        RowOptions o = options_for("input.touch_controls",
            "Controls drawn over the game once the screen is touched, in the layout below. "
            "They hide again when a gamepad or the keyboard is used.");
        if (toggle_row("On-screen controls", s.touch_controls, o)) {
            s.touch_controls = !s.touch_controls;
            settings::save();
        }
        const auto off = [&](RowOptions options) {
            if (!s.touch_controls && !options.disabled) {
                options.disabled = true;
                options.note = "On-screen controls are off";
            }
            return options;
        };
        const bool action = s.touch_layout == settings::TouchLayout::Action;
        if (choice_row("Layout", action ? "Action" : "PSP buttons",
                off(options_for("input.touch_layout",
                    "PSP buttons: a stick where the left thumb lands, the face buttons on the "
                    "right, L and R at the top corners. Action: large buttons named for what "
                    "they do (attack, evade, guard, the combined attack), a fixed stick, the "
                    "item pouch, and swipes for the item bar; every element can be moved, "
                    "resized and rebound.")))) {
            s.touch_layout = action ? settings::TouchLayout::Psp : settings::TouchLayout::Action;
            settings::save();
        }
        const auto action_only = [&](RowOptions options) {
            options = off(std::move(options));
            if (s.touch_layout != settings::TouchLayout::Action && !options.disabled) {
                options.disabled = true;
                options.note = "For the Action layout";
            }
            return options;
        };
        if (button_row("Edit the action layout…",
                action_only({false, {},
                    "Move, resize, rebind and hide the Action layout's elements, over "
                    "the game. Kept on this device."})))
            open_touch_editor();
        if (toggle_row("Haptic feedback", s.touch_haptics,
                action_only(options_for("input.touch_haptics",
                    "A short vibration when an Action button is pressed or a swipe is "
                    "taken, where the device can.")))) {
            s.touch_haptics = !s.touch_haptics;
            settings::save();
        }
        RowOptions dpad = off(options_for("input.touch_dpad",
            "A D-pad at the left edge, for the game's menus, the "
            "item box and the camera's D-pad controls. Off gives "
            "its place to the stick."));
        if (action && !dpad.disabled) {
            dpad.disabled = true;
            dpad.note = "For the PSP buttons layout";
        }
        if (toggle_row("D-pad", s.touch_dpad, dpad)) {
            s.touch_dpad = !s.touch_dpad;
            settings::save();
        }
        int opacity = static_cast<int>(std::lround(s.touch_opacity * 100.0f));
        if (slider_row("Controls opacity", opacity, 10, 100, 5, "%d%%",
                off(options_for("input.touch_opacity", "How strongly the on-screen controls are drawn.")))) {
            s.touch_opacity = static_cast<float>(opacity) / 100.0f;
            settings::save();
        }
        int size = static_cast<int>(std::lround(s.touch_size * 100.0f));
        if (slider_row("Controls size", size, 60, 160, 5, "%d%%",
                off(options_for("input.touch_size", "The size of the on-screen controls.")))) {
            s.touch_size = static_cast<float>(size) / 100.0f;
            settings::save();
        }
        int speed = static_cast<int>(std::lround(s.touch_camera_speed));
        if (slider_row("Touch camera speed", speed, 30, 720, 10, "%d deg",
                off(options_for("input.touch_camera_speed",
                    "Degrees the camera turns for a drag across the height of the screen.")))) {
            s.touch_camera_speed = static_cast<float>(speed);
            settings::save();
        }
    }
}

void Menu::game() {
    settings::Settings &s = settings::current();
    section("Hunting");
    if (toggle_row("Hold to gather", s.gather_hold,
            options_for("game.gather_hold",
                "On: hold ○ on a gather point or a monster you can carve, or □ on a mining point or a bug net, and it "
                "gathers or carves again each time you can. □ at the Pokke Farm's mining outcrops and bug "
                "thickets works the same way. Moving or pressing another button does not stop it. "
                "Letting go stops the next one. Pressing the same button again and holding it continues. "
                "Off: each button gathers once per press. L+○ or L+□ before a hold starts still uses the item bar."))) {
        s.gather_hold = !s.gather_hold;
        settings::save();
    }
    if (toggle_row("Gather with ○", s.gather_circle,
            options_for("game.gather_circle",
                "On: ○ also mines and catches bugs when a pickaxe or a bug net is in your pouch. The gather uses "
                "the tool and your item bar stays on the item you chose. Without the tool "
                "○ does nothing there, so it never uses another item. A monster you can carve on the point "
                "keeps ○ a carve. At the Pokke Farm ○ mines and catches bugs too. "
                "Off: mining and bug nets need the tool selected and □, as in the original game."))) {
        s.gather_circle = !s.gather_circle;
        settings::save();
    }
    if (toggle_row("Empty item slot", s.empty_item_slot,
            options_for("game.empty_item_slot",
                "On: the item bar has an empty slot, Nothing Selected, before the first item, and each quest "
                "starts on it. With it selected, □ with the weapon sheathed uses nothing. A pouch with all 24 "
                "slots filled has no room for it. Off: the game's own bar, which starts on the first item and "
                "wraps from the last item to the first."))) {
        s.empty_item_slot = !s.empty_item_slot;
        settings::save();
    }
    if (toggle_row("True Attack Value", s.true_attack,
            options_for("game.true_attack",
                "On: the equipment screens show a weapon's attack, element and status as the game counts "
                "them, as FUComplete's True Raw/Ele/Status does: attack without the class factor (a great "
                "sword's 672 shows as 140) and element and status without the trailing zero (fire 250 shows "
                "as 25). Damage is the same either way. Off (default): the game's own numbers."))) {
        s.true_attack = !s.true_attack;
        settings::save();
    }
    section("Lock-on");
    {
        const auto slot = static_cast<std::size_t>(input::Action::LockOn);
        std::string bind = input::format(s.controls.keys[slot]);
        const std::string pad = input::format(s.controls.pad[slot]);
        if (!pad.empty()) bind += (bind.empty() ? "" : " / ") + pad;
        const std::string help = "A tap of Lock on" +
            (bind.empty() ? std::string(" (unbound: set it in Controls)") : " (" + bind + ")") +
            " selects the nearest large monster. Tap again for the next one in the area; after the last one, a tap "
            "lets go. Lock-on style says whether the camera stays on it. Off, the bind does nothing and the hunt "
            "icon is hidden.";
        if (toggle_row("Lock-on", s.lock_on, options_for("input.lock_on", help.c_str()))) {
            s.lock_on = !s.lock_on;
            settings::save();
        }
        const char *const styles[] = {"Follow", "Focus"};
        const int style = s.lock_on_style == settings::LockOnStyle::Focus ? 1 : 0;
        const auto focus = static_cast<std::size_t>(input::Action::FocusCamera);
        std::string focus_bind = input::format(s.controls.keys[focus]);
        const std::string focus_pad = input::format(s.controls.pad[focus]);
        if (!focus_pad.empty()) focus_bind += (focus_bind.empty() ? "" : " / ") + focus_pad;
        const std::string focus_button = focus_bind.empty() ? std::string("L") : focus_bind;
        const std::string style_help = "Follow keeps the camera on the selected monster. " +
            (focus_bind.empty() ? std::string("Focus/Follow Camera, once bound,") : focus_button) +
            " turns that tracking off and on again; the selection stays. "
            "Turning it by hand, the D-pad, or L lets go, and L "
            "recenters behind the hunter. Focus keeps the selection, and a tap of " +
            focus_button +
            " centers the camera on that monster, even when it is already on screen, and L's own camera move stays "
            "off for that tap. A second tap within a third of a second recenters behind the hunter; the selection stays. A "
            "later tap centers again. With nothing selected, L recenters behind the hunter. Focus/Follow Camera in Controls "
            "chooses that tap; with nothing bound, it is L.";
        RowOptions o = options_for("input.lock_on_style", std::move(style_help));
        if (!s.lock_on && !o.disabled) {
            o.disabled = true;
            o.note = "Lock-on is off";
        }
        if (const int delta = choice_row("Lock-on style", styles[style], o)) {
            // Either way, or confirm, moves to the other style, as the other choice rows wrap.
            const int next = cycle(style, delta, 2);
            s.lock_on_style = next == 0 ? settings::LockOnStyle::Follow : settings::LockOnStyle::Focus;
            settings::save();
        }
        o = options_for("input.lock_on_marker",
            "A red ring, a cross through it, and four ticks, over the monster the camera is locked onto. "
            "Follow keeps it up while the camera is tracking. The right stick or Focus/Follow Camera turns it off "
            "with that tracking; the selection stays, and the same button turns both back on. In Focus, selecting "
            "leaves it off. The focus button shows it for a third of a second.");
        if (!s.lock_on && !o.disabled) {
            o.disabled = true;
            o.note = "Lock-on is off";
        }
        if (toggle_row("Lock-on marker", s.lock_on_marker, o)) {
            s.lock_on_marker = !s.lock_on_marker;
            settings::save();
        }
        int portrait = static_cast<int>(s.hunt_portrait_size);
        o = options_for("video.hunt_portrait",
            "The monster icon at the bottom left, as a percent of its usual size. "
            "A loading screen shows none. Off with Lock-on.");
        if (!s.lock_on && !o.disabled) {
            o.disabled = true;
            o.note = "Lock-on is off";
        }
        if (slider_row("Monster Icon", portrait, static_cast<int>(settings::kMinHuntPortraitSize),
                static_cast<int>(settings::kMaxHuntPortraitSize), 10, "%d%%", o)) {
            s.hunt_portrait_size = static_cast<std::uint32_t>(portrait);
            settings::save();
        }
    }
    section("Overlays");
    const bool damage_numbers_on = s.damage_numbers != settings::DamageNumbers::Off;
    {
        const char *const whose[] = {"Off", "Mine", "Everyone"};
        const int current = static_cast<int>(s.damage_numbers);
        if (const int delta = choice_row("Damage numbers", whose[current],
                options_for("game.damage_numbers",
                    "A number shows how much health a hit takes off a monster, drawn where it landed. A weak point "
                    "(hitzone 45 or more, after sharpness for a blade) is yellow and any other part white. A "
                    "critical hit has a glowing pink sparkle above it and a negative critical a faded blue-gray one. A "
                    "killing blow shows "
                    "the whole hit. Poison ticks are light purple and ride the monster. Mine: only your hunter's "
                    "hits, and poison. Everyone: every hit, also those whose attacker is not known, such as a hit "
                    "another player's console applied. Off: nothing is added."))) {
            s.damage_numbers = static_cast<settings::DamageNumbers>(cycle(current, delta, 3));
            settings::save();
        }
    }
    {
        RowOptions o = options_for("game.damage_breakdown",
            "Total: the hit as one number. Split: the raw part in the hitzone's colour, with the element "
            "part beside it, smaller, in the element's colour after its glyph: a curled flame, a drop, a "
            "bolt, ice crystals or dragon spikes. Total + "
            "element: the whole hit, with the element part beside it. A hit without an element is one number "
            "in every mode.");
        o.disabled = o.disabled || !damage_numbers_on;
        const char *const modes[] = {"Total", "Split", "Total + element"};
        const int current = static_cast<int>(s.damage_breakdown);
        if (const int delta = choice_row("Damage breakdown", modes[current], o)) {
            s.damage_breakdown = static_cast<settings::DamageBreakdown>(cycle(current, delta, 3));
            settings::save();
        }
    }
    {
        RowOptions o = options_for("game.damage_number_size",
            "How large damage numbers are drawn, in percent of the interface text. They still shrink when "
            "the camera is close to the monster.");
        o.disabled = o.disabled || !damage_numbers_on;
        int size = static_cast<int>(s.damage_number_size);
        if (slider_row("Damage number size", size, static_cast<int>(settings::kMinDamageNumberSize),
                static_cast<int>(settings::kMaxDamageNumberSize), 10, "%d%%", o)) {
            s.damage_number_size = static_cast<std::uint32_t>(size);
            settings::save();
        }
    }
    {
        RowOptions o = options_for("game.damage_number_thickness",
            "How bold damage numbers are: 1 is the font's own strokes with a thin dark outline, and 2 "
            "thickens both the digits and the outline.");
        o.disabled = o.disabled || !damage_numbers_on;
        int thickness = static_cast<int>(s.damage_number_thickness);
        if (slider_row("Damage number thickness", thickness, 1, static_cast<int>(settings::kMaxDamageNumberThickness),
                1, "%d", o)) {
            s.damage_number_thickness = static_cast<std::uint32_t>(thickness);
            settings::save();
        }
    }
    if (toggle_row("Modern sharpness gauge", s.sharpness_gauge,
            options_for("game.sharpness_gauge",
                "On: the colour the game paints on the blade shortens as that colour is spent, from "
                "the tip back to the handle. It stays solid and glows, and orange and yellow get a "
                "colour like the others. The bar under a long sword ends at the sharpness that is left. "
                "A weapon that loses less per hit, a guard and Handicraft are all included. "
                "Off: the game's own drawing."))) {
        s.sharpness_gauge = !s.sharpness_gauge;
        settings::save();
    }
    if (toggle_row("Gather icon", s.gather_icon,
            options_for("game.gather_icon",
                "On: a mark floats above a gather point while you are close enough to gather it, with the "
                "picture of what it gives, and above each monster or tail you can carve, with its icon; ○ "
                "shows on the one ○ carves. "
                "Off: no mark, and the gather list is not read."))) {
        s.gather_icon = !s.gather_icon;
        settings::save();
    }
    {
        RowOptions o = options_for("game.gather_icon_size",
            "How large the gather mark is, in percent of its usual size. The frame, its picture, the button and "
            "the count grow together.");
        o.disabled = o.disabled || !s.gather_icon;
        int size = static_cast<int>(s.gather_icon_size);
        if (slider_row("Gather icon size", size, static_cast<int>(settings::kMinGatherIconSize),
                static_cast<int>(settings::kMaxGatherIconSize), 10, "%d%%", o)) {
            s.gather_icon_size = static_cast<std::uint32_t>(size);
            settings::save();
        }
    }
    section("Speed");
    if (choice_row("Game speed", s.unthrottled ? "Unlimited" : "Normal",
            options_for("video.unthrottled",
                "Normal holds the game to real time. Unlimited lets it run as "
                "fast as frames can be drawn, which also speeds up the game. "
                "The window keeps up at about 30 pictures a second, so the "
                "display's refresh does not hold it, and the sound is muted."))) {
        s.unthrottled = !s.unthrottled;
        settings::save();
    }
    {
        RowOptions o = options_for("video.fast_loading",
            "Lets the game run ahead of real time while it loads and is silent, so loads "
            "take as long as the computer needs. Never during play, sound, movies or "
            "ad hoc play.");
        if (s.unthrottled && !o.disabled) {
            o.disabled = true;
            o.note = "Game speed is Unlimited";
        }
        if (toggle_row("Fast loading", s.fast_loading, o)) {
            s.fast_loading = !s.fast_loading;
            settings::save();
        }
    }
    {
        // Single player only: during ad hoc play the bind does nothing.
        const bool online = adhoc_networking_on() || adhoc_session_active();
        const auto guarded = [&](RowOptions o) {
            if (o.disabled) return o;
            if (s.unthrottled) {
                o.disabled = true;
                o.note = "Game speed is Unlimited";
            } else if (online) {
                o.disabled = true;
                o.note = "Single player only";
            }
            return o;
        };
        static const char *const kModes[] = {"Hold", "Toggle", "Off"};
        const int current = static_cast<int>(s.fast_forward);
        const std::string key = input::format(s.controls.keys[static_cast<std::size_t>(input::Action::FastForward)]);
        // Two lines at most, so the note of a disabled row still fits under it.
        const std::string help = "The game runs faster while its key (" +
            (key.empty() ? std::string("none: set it in Controls") : key) +
            ") is held, or from one press to the next, muted. Single player only.";
        if (const int delta =
                choice_row("Fast-forward", kModes[current], guarded(options_for("video.fast_forward", help)))) {
            s.fast_forward = static_cast<fast_forward::Mode>(cycle(current, delta, 3));
            settings::save();
        }
        RowOptions o = guarded(options_for("video.fast_forward_speed",
            "How many times faster than real time the game runs while it "
            "fast-forwards, if the computer keeps up."));
        if (s.fast_forward == fast_forward::Mode::Off && !o.disabled) {
            o.disabled = true;
            o.note = "Fast-forward is off";
        }
        int speed = static_cast<int>(s.fast_forward_speed);
        if (slider_row("Fast-forward speed", speed, static_cast<int>(fast_forward::kMinSpeed),
                static_cast<int>(fast_forward::kMaxSpeed), 1, "%dx", o)) {
            s.fast_forward_speed = static_cast<std::uint32_t>(speed);
            settings::save();
        }
    }
    section("Pausing");
    if (toggle_row("Pause the game when the menu opens", s.menu_pause,
            options_for("ui.menu_pause",
                "On: the game stops while this menu is open. Off: it keeps running "
                "and playing sound behind the menu, which takes all input. Applies "
                "the next time the menu opens."))) {
        s.menu_pause = !s.menu_pause;
        settings::save();
    }
    if (toggle_row("Pause during multiplayer", s.menu_pause_multiplayer,
            options_for("ui.menu_pause_multiplayer",
                "In ad hoc play the game keeps running behind the menu unless this is on: a paused "
                "game stops answering the other players and can drop a quest. Applies the next time "
                "the menu opens."))) {
        s.menu_pause_multiplayer = !s.menu_pause_multiplayer;
        settings::save();
    }

    section("Hunter name");
    {
        const bool keyboard = s.name_entry == settings::NameEntry::Keyboard;
        if (choice_row("When the game asks for a name", keyboard ? "On-screen keyboard" : "Use the name below",
                options_for("input.name_entry",
                    "On-screen keyboard: type the name with the gamepad or the keyboard while the "
                    "game waits. Otherwise the name below is given at once."))) {
            s.name_entry = keyboard ? settings::NameEntry::Fixed : settings::NameEntry::Keyboard;
            settings::save();
        }
    }
    if (text_row("name", "Hunter name", s.name, kHunterNameLength, false, hunter_name_character,
            options_for("input.name",
                "Given when the game asks for a name and the on-screen keyboard is "
                "off, and to other players when the network nickname is empty. "
                "Letters, digits, spaces and simple punctuation.")))
        settings::save();

    ImGui::Dummy({0.0f, font_gap()});
    if (button_row("Restore game defaults", {false, {}, "Every setting on this page back to how Pokee ships."})) {
        const settings::Settings &d = settings::defaults();
        const auto restore = [&](const char *key, auto &value, const auto &fallback) {
            if (settings::overridden_by(key) == nullptr) value = fallback;
        };
        restore("game.gather_hold", s.gather_hold, d.gather_hold);
        restore("game.gather_circle", s.gather_circle, d.gather_circle);
        restore("game.empty_item_slot", s.empty_item_slot, d.empty_item_slot);
        restore("input.lock_on", s.lock_on, d.lock_on);
        restore("input.lock_on_style", s.lock_on_style, d.lock_on_style);
        restore("input.lock_on_marker", s.lock_on_marker, d.lock_on_marker);
        restore("video.hunt_portrait", s.hunt_portrait_size, d.hunt_portrait_size);
        restore("game.damage_numbers", s.damage_numbers, d.damage_numbers);
        restore("game.damage_breakdown", s.damage_breakdown, d.damage_breakdown);
        restore("game.damage_number_size", s.damage_number_size, d.damage_number_size);
        restore("game.damage_number_thickness", s.damage_number_thickness, d.damage_number_thickness);
        restore("game.sharpness_gauge", s.sharpness_gauge, d.sharpness_gauge);
        restore("game.gather_icon", s.gather_icon, d.gather_icon);
        restore("game.gather_icon_size", s.gather_icon_size, d.gather_icon_size);
        restore("video.unthrottled", s.unthrottled, d.unthrottled);
        restore("video.fast_loading", s.fast_loading, d.fast_loading);
        restore("video.fast_forward", s.fast_forward, d.fast_forward);
        restore("video.fast_forward_speed", s.fast_forward_speed, d.fast_forward_speed);
        restore("ui.menu_pause", s.menu_pause, d.menu_pause);
        restore("ui.menu_pause_multiplayer", s.menu_pause_multiplayer, d.menu_pause_multiplayer);
        restore("input.name_entry", s.name_entry, d.name_entry);
        restore("input.name", s.name, d.name);
        settings::save();
    }
}

std::string format_duration(std::uint64_t ms) {
    const std::uint64_t seconds = ms / 1000u;
    if (seconds < 60u) return std::to_string(seconds) + " s";
    if (seconds < 3600u) return std::to_string(seconds / 60u) + " min " + std::to_string(seconds % 60u) + " s";
    return std::to_string(seconds / 3600u) + " h " + std::to_string(seconds / 60u % 60u) + " min";
}

std::string format_bytes(std::uint64_t bytes) {
    char text[32];
    if (bytes < 10'000u)
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 10'000'000u)
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// One line of what the client is doing, for the menu and the overlay.
std::string connection_text(const adhoc::Diagnostics &d) {
    switch (d.state) {
    case adhoc::ServerState::Off:
        return d.server.empty() || !settings::current().adhoc ? "Off line" : "Off line (the game is not on line)";
    case adhoc::ServerState::Connecting:
        if (d.failed_attempts == 0u) return "Connecting…";
        return "Reconnecting, attempt " + std::to_string(d.failed_attempts + 1u) +
            (d.last_error.empty() ? "" : " (" + d.last_error + ")");
    case adhoc::ServerState::Online:
        break;
    }
    std::string text = "On line";
    if (d.online_ms) text += " for " + format_duration(*d.online_ms);
    if (d.rtt_ms) {
        char rtt[32];
        std::snprintf(rtt, sizeof(rtt), ", %.0f ms round trip", *d.rtt_ms);
        text += rtt;
    }
    return text;
}

std::string group_text(const adhoc::Diagnostics &d) {
    if (d.group) {
        std::string text = *d.group + ", " + std::to_string(d.peers.size() + 1u) + " players";
        if (d.rejoin_ms) text += ", rejoining for " + format_duration(*d.rejoin_ms);
        return text;
    }
    if (d.joining) return "Joining " + *d.joining + "…";
    return "None";
}

std::string traffic_text(const adhoc::Traffic &t) {
    return "in " + std::to_string(t.packets_in) + " (" + format_bytes(t.bytes_in) + "), out " +
        std::to_string(t.packets_out) + " (" + format_bytes(t.bytes_out) + ")";
}

// The on-screen network overlay (menu: Network, or MHFU_ADHOC_OVERLAY).
bool &network_overlay() {
    static bool shown = [] {
        const char *text = std::getenv("MHFU_ADHOC_OVERLAY");
        return text != nullptr && *text != '\0' && std::string(text) != "0";
    }();
    return shown;
}

std::string &saved_log_path() {
    static std::string path;
    return path;
}

std::string players_text(std::size_t count) {
    return std::to_string(count) + (count == 1u ? " player" : " players");
}

// Hosting and joining a session.
void play_together() {
    settings::Settings &s = settings::current();
    static std::string copied;
    static std::string typed_address;
    section("Play together");
    if (adhoc_hosting()) {
        const adhoc::ServerStatus status = adhoc_host_status();
        const std::string suffix =
            status.adhocctl_port == adhoc::kAdhocctlPort ? std::string() : ":" + std::to_string(status.adhocctl_port);
        info_row("Hosting",
            players_text(status.players.size()) +
                " connected. Everyone now enters the Online Gathering Hall; with more than four, set "
                "the game's Online Gathering Hall option to MANUAL and choose the same hall.");
        const std::vector<adhoc::LocalAddress> addresses = adhoc::local_addresses();
        if (addresses.empty()) info_row("Your addresses", "No network is connected");
        for (const adhoc::LocalAddress &address : addresses) {
            const std::string text = address.address + suffix;
            const std::string label =
                text + "   " + address.network + " (" + address.interface + ")###address " + address.address;
            if (button_row(label.c_str(),
                    {false, {},
                        "Copies this address. Players on the same network find you under Join; over a VPN "
                        "without broadcast, such as Tailscale, they type your VPN address there."})) {
                SDL_SetClipboardText(text.c_str());
                copied = text;
            }
        }
        if (!copied.empty()) info_row("Copied", copied);
        ImGui::PushID("hosted players");
        for (std::size_t i = 0; i < status.players.size(); ++i) {
            const adhoc::ServerPlayer &player = status.players[i];
            ImGui::PushID(static_cast<int>(i));
            // The game names a hall's group "MHP2Q0" (or "MHP2A0") and two
            // more characters (0x08899874); which hall each one is has not
            // been traced, so the name is shown as it is.
            info_row(player.nickname.c_str(),
                player.address + "   " + (player.group ? *player.group : std::string("not in a hall")));
            ImGui::PopID();
        }
        ImGui::PopID();
        // One ID for Host and Stop, so the focus stays on the row.
        if (button_row("Stop hosting###hosting",
                {false, {}, "Stops the server. Everyone in the session is disconnected, as when a connection drops."},
                colors::kDanger)) {
            adhoc_host_stop();
            copied.clear();
        }
    } else {
        if (button_row("Host a session###hosting",
                {false, {},
                    "Runs a server in this game for the others to join: no other program, no port forwarding "
                    "on a local network or a VPN. Then everyone enters the Online Gathering Hall."}))
            adhoc_host_start();
        if (const std::string error = adhoc_host_error(); !error.empty()) info_row("Cannot host", error);
    }

    section("Join a session");
    adhoc::Discovery &discovery = adhoc::Discovery::get();
    discovery.start_listening();
    discovery.query(s.adhoc_recent);
    const std::vector<adhoc::FoundHost> hosts = discovery.hosts();
    const bool hosting = adhoc_hosting();
    const auto joined = [&](const std::string &address) { return !hosting && s.adhoc && s.adhoc_server == address; };
    for (const adhoc::FoundHost &host : hosts) {
        const std::string address = host.join_address();
        const std::string label = (joined(address) ? "Joined " : "Join ") + host.info.name + "   " + address + ", " +
            players_text(host.info.players) + "###found " + std::to_string(host.info.session);
        if (button_row(label.c_str(),
                {false, {},
                    "A session hosted on this network. Joining it takes you out of any "
                    "other; then enter the Online Gathering Hall."}))
            adhoc_join(address);
    }
    if (hosts.empty()) info_row("On this network", "Looking for hosted sessions…");
    if (text_row("join_address", "Address", typed_address, 100u, true, address_character,
            {false, {},
                "The host's address, for a VPN without broadcast such as Tailscale: the host's screen lists it. "
                "Confirming it joins."}) &&
        !typed_address.empty()) {
        adhoc_join(typed_address);
        typed_address.clear();
    }
    for (const std::string &address : s.adhoc_recent) {
        // A recent session that is announcing is listed above already.
        if (std::any_of(hosts.begin(), hosts.end(),
                [&](const adhoc::FoundHost &host) { return host.join_address() == address; }))
            continue;
        const std::string label = (joined(address) ? "Joined " : "Join ") + address + "   recent###recent " + address;
        if (button_row(label.c_str(), {false, {}, "A session you joined before."})) adhoc_join(address);
    }
}

void Menu::network() {
    settings::Settings &s = settings::current();
    adhoc::Client &client = adhoc::Client::get();
    const adhoc::Diagnostics d = client.diagnostics();
    play_together();
    section("Ad hoc play");
    if (toggle_row("Ad hoc play", s.adhoc,
            options_for("network.adhoc",
                "Multiplayer with other players. Off, the game says the wireless "
                "switch is off. Turning it off in the Online Gathering Hall leaves it."))) {
        s.adhoc = !s.adhoc;
        settings::save();
        adhoc_apply_settings();
    }
    if (text_row("server", "Server", s.adhoc_server, 100u, true, host_character,
            options_for("network.server",
                "The session or PSP ad hoc server the game goes on line with: host "
                "name or address, host:port if it is not on 27312. Joining fills "
                "it in; for a public server, type its name. Applies the next time "
                "the game goes on line."))) {
        settings::save();
        adhoc_apply_settings();
    }
    if (text_row("nickname", "Nickname", s.adhoc_nickname, 32u, true, printable_ascii,
            options_for("network.nickname",
                "The name other players and the server see. Empty: the hunter name. "
                "Applies the next time the game goes on line."))) {
        settings::save();
        adhoc_apply_settings();
    }

    section("Status");
    info_row("Connection", connection_text(d));
    if (adhoc_hosting()) {
        const adhoc::ServerStatus st = adhoc_host_status();
        info_row("Server",
            "TCP " + std::to_string(st.adhocctl_port) + " and " + std::to_string(st.relay_port) + ", up " +
                format_duration(st.uptime_ms) + ", " + players_text(st.players.size()) + ", " +
                std::to_string(st.groups) + " halls, " + std::to_string(st.relay_sessions) + " relay connections, " +
                std::to_string(st.streams) + " streams");
        info_row("Relayed",
            std::to_string(st.relayed_packets) + " packets (" + format_bytes(st.relayed_bytes) + "), " +
                std::to_string(st.dropped) + " datagrams dropped");
    }
    {
        const adhoc::DiscoveryStatus ds = adhoc::Discovery::get().status();
        std::string text = ds.listening ? "Listening on UDP " + std::to_string(adhoc::kDiscoveryPort) + ", " +
                std::to_string(ds.hosts) + " hosts heard"
                                        : (ds.listen_error.empty() ? "Not listening" : ds.listen_error);
        if (ds.announcing)
            text += "; announcing " + ds.announce_note + ", " + std::to_string(ds.announcements_sent) + " sent, " +
                std::to_string(ds.queries_answered) + " queries answered";
        info_row("Discovery", text);
        for (const adhoc::FoundHost &host : adhoc::Discovery::get().hosts())
            info_row(("Host " + host.info.name).c_str(),
                host.join_address() + ", " + players_text(host.info.players) + ", " + host.info.product + ", heard " +
                    format_duration(host.heard_ms) + " ago");
    }
    if (!d.server_address.empty()) info_row("Server address", d.server_address);
    info_row("You",
        (s.adhoc_mac.empty() ? std::string("address made up on first use") : s.adhoc_mac) +
            (d.nickname.empty() ? "" : "   " + d.nickname));
    info_row("Group", group_text(d));
    for (const adhoc::PeerSummary &peer : d.peers)
        info_row(peer.nickname.empty() ? "Player" : peer.nickname.c_str(),
            adhoc::format_mac(peer.mac) + "   " +
                (peer.last_heard_ms ? "heard " + format_duration(*peer.last_heard_ms) + " ago" : "not heard yet"));
    for (const adhoc::SocketSummary &socket : d.sockets) {
        const std::string label = socket.kind + " " + std::to_string(socket.port);
        info_row(label.c_str(),
            socket.state +
                (socket.peer ? "   " + adhoc::format_mac(*socket.peer) + " port " + std::to_string(socket.peer_port)
                             : std::string{}));
    }
    if (d.relay_links_wanted != 0u)
        info_row(
            "Relay links", std::to_string(d.relay_links_up) + " of " + std::to_string(d.relay_links_wanted) + " up");
    info_row("Per second", traffic_text(d.per_second));
    info_row("Since start", traffic_text(d.total));
    info_row("Problems",
        std::to_string(d.dropped) + " datagrams dropped, " + std::to_string(d.timeouts) + " calls timed out, " +
            std::to_string(d.reconnects) + " reconnections");

    section("Troubleshooting");
    if (toggle_row("Network overlay", network_overlay(),
            {false, {}, "A small panel over the game with the connection, the group and the traffic."}))
        network_overlay() = !network_overlay();
    if (toggle_row("Log every call and packet", adhoc::Client::tracing(),
            {false, {},
                "The same as MHFU_TRACE_ADHOC=1: every ad hoc call and packet header goes to the "
                "console and to the network log. Busy; for finding a problem."}))
        adhoc::Client::set_tracing(!adhoc::Client::tracing());
    if (button_row("Save network log",
            {false, {},
                "Writes the recent network log and this page's state to a file in the data folder's "
                "logs folder, to send with a problem report."})) {
        std::filesystem::path directory;
        try {
            directory = install::user_data_directory() / "logs";
        } catch (const std::exception &) {
            directory = "logs";
        }
        const std::filesystem::path path = client.save_log(directory);
        saved_log_path() =
            path.empty() ? "Could not write to " + install::path_to_utf8(directory) : install::path_to_utf8(path);
        std::cout << "[adhoc] network log: " << saved_log_path() << std::endl;
    }
    if (!saved_log_path().empty()) info_row("Saved", saved_log_path());
    if (button_row("Reconnect now",
            {d.state == adhoc::ServerState::Off, {},
                "Drop the server connection and connect again at once. The group is joined "
                "again; a quest in progress may end, as when a connection drops."}))
        client.reconnect_now();
    if (button_row("Disconnect",
            {!d.group && !d.joining, {},
                "Leave the group as if the other players were lost. The game shows its own "
                "disconnection message."},
            colors::kDanger))
        client.disconnect_now();

    ImGui::Dummy({0.0f, font_gap()});
    if (button_row("Restore network defaults", {false, {}, "Ad hoc play off and no server. Your address stays."})) {
        const settings::Settings &defaults = settings::defaults();
        const auto restore = [&](const char *key, auto &value, const auto &fallback) {
            if (settings::overridden_by(key) == nullptr) value = fallback;
        };
        restore("network.adhoc", s.adhoc, defaults.adhoc);
        restore("network.server", s.adhoc_server, defaults.adhoc_server);
        restore("network.nickname", s.adhoc_nickname, defaults.adhoc_nickname);
        settings::save();
        adhoc_apply_settings();
    }
}

void Menu::mods() {
    if (layered_armor_screen(back_)) return;
    if (event_quests_screen(back_)) return;
    if (!mods_screen_open()) {
        layered_armor_row();
        settings::Settings &s = settings::current();
        section("Gameplay");
        event_quests_row();
        if (toggle_row("Give Any Rarity Item", s.give_any_rarity,
                options_for("game.give_any_rarity",
                    "On: in the Guild Hall, Start > Item > Give hands an item of any rarity to another hunter. "
                    "Off (default): the game's own limit, Rare 1 to 3 only."))) {
            s.give_any_rarity = !s.give_any_rarity;
            settings::save();
        }
        if (toggle_row("Change Equipment in the Guild Hall", s.hall_equipment,
                options_for("game.hall_equipment",
                    "On (default): the item box in the Guild Hall, online or not, offers Change Equipment, as "
                    "the box at home does, and sends the new equipment to the hall when the equipment screen "
                    "closes, so the other hunters see it, in a full hall too. Refused while readied for a "
                    "quest. Off: greyed out, as in the game."))) {
            s.hall_equipment = !s.hall_equipment;
            settings::save();
        }
        if (toggle_row("Item Sets", s.item_sets,
                options_for("game.item_sets",
                    "On: the item box menu has Item Sets, to register the pouch as a set and load it again "
                    "from the box, in the game's own windows. Off: the game's own six entries."))) {
            s.item_sets = !s.item_sets;
            settings::save();
        }
        if (toggle_row("Eat in the Guild Hall", s.hall_meal,
                options_for("game.hall_meal",
                    "On (default): seated at a table in the Guild Hall, △ orders from your home kitchen's "
                    "menu, with its chefs, prices and skills, as Order Food in the house does. Off: △ does "
                    "nothing there, as in the game."))) {
            s.hall_meal = !s.hall_meal;
            settings::save();
        }
        if (toggle_row("Show Key Quests", s.key_quests,
                options_for("game.key_quests",
                    "On: the quests that count towards the next Urgent Quest are named [Key] in the quest "
                    "lists and wherever else the game shows their name. Off (default): the game's own names."))) {
            s.key_quests = !s.key_quests;
            settings::save();
        }
        if (toggle_row("Throwing Knives", s.throwing_knives,
                options_for("game.throwing_knives",
                    "On (default): throwing knives are items you keep, not supply items. The Throwing Knife is "
                    "sold in the shops for 750z, and Whetstone + Machalite Ore or Dragonite Ore combines into "
                    "one; the coated knives are combined from it. Off: supply items, as in the game."))) {
            s.throwing_knives = !s.throwing_knives;
            settings::save();
        }
    }
    mods_page(back_);
    if (take_mods_restart_request()) {
        install::request_restart_on_exit();
        quit_ = true;
        close_ = true;
    }
}

void Menu::system() {
    if (save_screen(back_)) {
        if (take_restart_request()) {
            install::request_restart_on_exit();
            quit_ = true;
            close_ = true;
        }
        return;
    }
    std::string data_dir;
    try {
        data_dir = install::path_to_utf8(install::user_data_directory());
    } catch (const std::exception &e) {
        data_dir = e.what();
    }
    section("Game");
    if (button_row("Resume", {false, {}, "Back to the game."})) close_ = true;
    if (button_row("Take a screenshot",
            {false, {},
                "Saves the game's picture behind the menu, at the size it is drawn at, as a PNG in "
                "the screenshots folder. In play: " +
                    binds_of(input::Action::Screenshot) + "."})) {
        const std::string where = take_screenshot();
        screenshot_path_ = where.empty() ? "Not saved: no game picture yet" : where;
    }
    if (!screenshot_path_.empty()) info_row("Screenshot", screenshot_path_);
#if !defined(MHFU_ANDROID_APP)
    if (button_row("Open the screenshots folder", {false, {}, "Show the screenshots in the file manager."})) {
        try {
            std::error_code ec;
            const std::filesystem::path folder = screenshot::folder();
            std::filesystem::create_directories(folder, ec);
            if (!SDL_OpenURL(folder_url(folder).c_str()))
                std::cout << "[menu] cannot open " << path_to_utf8(folder) << ": " << SDL_GetError() << "\n";
        } catch (const std::exception &e) {
            std::cout << "[menu] cannot open the screenshots folder: " << e.what() << "\n";
        }
    }
#endif
#if defined(MHFU_ANDROID_APP)
    // An Android app's data folder is out of the file manager's reach; its
    // log goes where the player picks instead, to send with a report.
    (void)data_dir;
    if (button_row("Save the log…",
            {false, {},
                "Copies Pokee's logs (this run's, the previous run's, the last "
                "error's and the logs folder) to a folder you pick, to send with a "
                "problem report."})) {
        const auto saved = android::save_logs(install::user_data_directory(),
            "Pokee log " + savedata::timestamp_for_path(std::chrono::system_clock::now()));
        if (saved) saved_log_path() = saved->error.empty() ? saved->where : "Not saved: " + saved->error;
    }
    if (!saved_log_path().empty()) info_row("Log", saved_log_path());
#else
    if (button_row("Open the data folder", {false, {}, "Show Pokee's data folder in the file manager."})) {
        if (!SDL_OpenURL(folder_url(path_from_utf8(data_dir)).c_str()))
            std::cout << "[menu] cannot open " << data_dir << ": " << SDL_GetError() << "\n";
    }
#endif
    if (button_row("Set up game data again…",
            {false, {}, "Choose the disc image again, for example after moving it. The game closes first."}))
        confirm_ = Confirm::Setup;
    if (button_row("Quit game", {false, {}, "Close Pokee. Progress since your last save is lost."}, colors::kDanger))
        confirm_ = Confirm::Quit;

    section("Saves");
    save_rows();

    section("About");
    info_row("Pokee", std::string(kPokeeVersion));
    info_row("Game", std::string(install::kGameTitle) + " (" + install::kDiscIdDisplay + ")");
    {
        std::string where = data_dir;
        try {
            if (install::data_directory().source == install::DataSource::Portable) where += "  (portable)";
        } catch (const std::exception &) {
        }
        info_row("Data folder", where);
    }
    if (!savedata::memory_stick().empty())
        info_row("Saves folder", install::path_to_utf8(savedata::memory_stick() / "PSP" / "SAVEDATA"));
    info_row("Graphics", "Vulkan on " + renderer().device_name());
    info_row("Interface", std::string("Dear ImGui ") + IMGUI_VERSION);
}

// The quit and setup confirmations. False when the menu should close.
bool Menu::confirm_dialog() {
    if (confirm_ != Confirm::None && !ImGui::IsPopupOpen("##confirm")) ImGui::OpenPopup("##confirm");
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(io.DisplaySize.x * 0.9f, font * 26.0f), 0.0f}, ImGuiCond_Always);
    bool keep_open = true;
    confirm_opened_ = false;
    if (ImGui::BeginPopupModal(
            "##confirm", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings)) {
        if (confirm_ == Confirm::None) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return true;
        }
        confirm_opened_ = true;
        if (confirm_ == Confirm::DeletePreset) {
            settings::Settings &s = settings::current();
            heading("Delete the preset " + s.control_preset.user + "?");
            paragraph("Its bindings are lost, and the Default preset is chosen.", colors::kTextDim);
            ImGui::Dummy({0.0f, font * 0.6f});
            const float gap = font * 0.6f;
            const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
            // Both choices look alike: only the focus ring shows which one is selected.
            if (big_button("Delete", width)) {
                std::erase_if(
                    s.user_presets, [&](const input::UserPreset &p) { return p.name == s.control_preset.user; });
                settings::choose_preset(s, {input::Preset::Default, {}});
                preset_notice_.clear();
                settings::save();
                confirm_ = Confirm::None;
            }
            ImGui::SameLine(0.0f, gap);
            if (big_button("Cancel", width)) confirm_ = Confirm::None;
            ImGui::SetItemDefaultFocus();
            ImGui::Dummy({0.0f, font * 0.2f});
            ImGui::Dummy({0.0f, 0.0f});
            hints({{Control::Confirm, "Select"}, {Control::Back, "Cancel"}});
            if (confirm_ == Confirm::None) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return true;
        }
        const bool quit = confirm_ == Confirm::Quit;
        heading(quit ? "Quit the game?" : "Set up game data again?");
        paragraph(quit ? "Progress since your last save is lost."
                       : "Pokee closes the game and opens the setup, where you choose the disc image again. "
                         "Progress since your last save is lost.",
            colors::kTextDim);
        ImGui::Dummy({0.0f, font * 0.6f});
        const float gap = font * 0.6f;
        const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        // Both choices look alike: only the focus ring shows which one is selected.
        if (big_button(quit ? "Quit" : "Close and set up", width)) {
            if (!quit) install::request_setup_on_exit();
            quit_ = true;
            keep_open = false;
        }
        ImGui::SameLine(0.0f, gap);
        if (big_button("Cancel", width)) confirm_ = Confirm::None;
        // Cancel is the safe default.
        ImGui::SetItemDefaultFocus();
        ImGui::Dummy({0.0f, font * 0.2f});
        ImGui::Dummy({0.0f, 0.0f});
        hints({{Control::Confirm, "Select"}, {Control::Back, "Cancel"}});
        if (confirm_ == Confirm::None || !keep_open) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return keep_open;
}

// Seconds the menu hint has left, or a negative number once it is gone.
double hint_seconds_left() {
    static const Clock::time_point first_frame = Clock::now();
    return kHintSeconds - std::chrono::duration<double>(Clock::now() - first_frame).count();
}

// The hint shown over the game until the menu has been opened once.
void draw_hint(double seconds_left) {
    Layer &layer = Layer::get();
    const ImGuiIO &io = ImGui::GetIO();
    const float font = layer.font_size();
    const float alpha = static_cast<float>(std::clamp(seconds_left, 0.0, 1.0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.8f, font * 0.5f});
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y - font}, ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::Begin("##hint", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    hints({{Control::Menu, "Settings and pause"}});
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// Set once the menu has been opened with a GPU problem on screen: the
// player has seen it, and the menu is where the log is saved.
bool &gpu_problem_seen() {
    static bool seen = false;
    return seen;
}

// What the renderer found wrong with the GPU (VulkanRenderer::gpu_problem),
// at the top of the screen until the menu is opened: without it a player
// would see a black screen and nothing else.
void draw_gpu_problem(const std::string &problem) {
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    const float width = std::min(io.DisplaySize.x - font * 2.0f, font * 34.0f);
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, font * 0.5f}, ImGuiCond_Always, {0.5f, 0.0f});
    ImGui::SetNextWindowSize({width, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.8f, font * 0.5f});
    ImGui::Begin("##gpu_problem", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Graphics problem");
    ImGui::TextUnformatted(problem.c_str());
    ImGui::TextUnformatted("Please open the menu, choose System > Save the log..., and send the log to "
                           "github.com/LocalBBQ/Pokee/issues. Video > GPU compatibility may help. This note "
                           "closes when the menu opens.");
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleVar();
}

// The network overlay: a few lines in the top-right corner.
void draw_network_overlay() {
    const adhoc::Diagnostics d = adhoc::Client::get().diagnostics();
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    ImGui::SetNextWindowPos({io.DisplaySize.x - font * 0.5f, font * 0.5f}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.5f, font * 0.3f});
    ImGui::Begin("##network", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(0.75f);
    ImGui::TextUnformatted(connection_text(d).c_str());
    if (adhoc_hosting())
        ImGui::TextUnformatted(("Hosting: " + players_text(adhoc_host_status().players.size())).c_str());
    ImGui::TextUnformatted(("Group: " + group_text(d)).c_str());
    for (const adhoc::PeerSummary &peer : d.peers)
        ImGui::TextUnformatted(("  " + peer.nickname +
            (peer.last_heard_ms ? "  " + format_duration(*peer.last_heard_ms) : std::string("  -")))
                .c_str());
    std::size_t streams = 0;
    for (const adhoc::SocketSummary &socket : d.sockets)
        if (socket.kind != "PDP" && socket.state == "established") ++streams;
    ImGui::TextUnformatted(("Links " + std::to_string(d.relay_links_up) + "/" + std::to_string(d.relay_links_wanted) +
        ", streams " + std::to_string(streams))
            .c_str());
    ImGui::TextUnformatted(("/s " + traffic_text(d.per_second)).c_str());
    if (d.dropped != 0u || d.timeouts != 0u)
        ImGui::TextUnformatted(
            ("Dropped " + std::to_string(d.dropped) + ", timeouts " + std::to_string(d.timeouts)).c_str());
    ImGui::End();
    ImGui::PopStyleVar();
}

// While the game fast-forwards: two arrowheads and the speed, small, in the
// top-right corner. Drawn by the interface, so it is never part of the game's
// own frames (MHFU_SCREENSHOT_DIR).
void draw_fast_forward() {
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    ImGui::SetNextWindowPos({io.DisplaySize.x - font * 0.5f, font * 0.5f}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.4f, font * 0.2f});
    ImGui::Begin("##fast_forward", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(0.75f);
    const float height = ImGui::GetTextLineHeight();
    const float size = height * 0.6f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const float top = at.y + (height - size) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const float left = at.x + static_cast<float>(i) * size * 0.8f;
        draw->AddTriangleFilled({left, top}, {left + size * 0.8f, top + size * 0.5f}, {left, top + size}, ink);
    }
    ImGui::Dummy({size * 1.6f + height * 0.25f, height});
    ImGui::SameLine(0.0f, 0.0f);
    char text[16];
    std::snprintf(text, sizeof(text), "%.0fx", fast_forward::speed());
    ImGui::TextUnformatted(text);
    ImGui::End();
    ImGui::PopStyleVar();
}

// A note over the game (show_note): the text and when it goes.
struct Note {
    std::string text;
    Clock::time_point until{};
};
Note &note() {
    static Note value;
    return value;
}
constexpr std::chrono::milliseconds kNoteTime{3000};

// The note, small, at the bottom of the window, fading out in its last half
// second.
void draw_note(const Note &n) {
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    const float left = std::chrono::duration<float>(n.until - Clock::now()).count();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y - font * 0.6f}, ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.7f * std::clamp(left * 2.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.5f, font * 0.25f});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(left * 2.0f, 0.0f, 1.0f));
    ImGui::Begin("##note", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(0.7f);
    ImGui::PushTextWrapPos(io.DisplaySize.x * 0.9f);
    ImGui::TextUnformatted(n.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// For a moment after the HUD is hidden or shown (gpu/game_hud.hpp): a short
// line at the top, fading out. Left out of window captures.
void draw_hud_note(double seconds_left) {
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    const float alpha = static_cast<float>(std::clamp(seconds_left / 0.4, 0.0, 1.0));
    const float top = font * 0.5f;
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, top}, ImGuiCond_Always, {0.5f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.5f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {font * 0.5f, font * 0.25f});
    ImGui::Begin("##hud_note", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(0.75f);
    const std::string key =
        input::format(settings::current().controls.keys[static_cast<std::size_t>(input::Action::HideHud)]);
    std::string text = gpu::hud::note_text();
    if (!key.empty()) text += " (" + key + ")";
    ImGui::TextUnformatted(text.c_str());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// The menu while it is open over the running game, and a quit chosen in it.
std::optional<Menu> &menu_over_game_state() {
    static std::optional<Menu> menu;
    return menu;
}
bool &quit_requested() {
    static bool requested = false;
    return requested;
}
Clock::time_point &menu_opened_at() {
    static Clock::time_point opened;
    return opened;
}

void note_menu_opened(bool paused) {
    if (Layer::get().attached() && !Layer::get().renderer().gpu_problem().empty()) gpu_problem_seen() = true;
    settings::Settings &s = settings::current();
    if (!s.menu_hint_seen) {
        s.menu_hint_seen = true;
        settings::save();
    }
    std::cout << (paused ? "[menu] opened; the game is paused" : "[menu] opened; the game keeps running") << std::endl;
    menu_opened_at() = Clock::now();
}

void note_menu_closed(bool quit) {
    const double seconds = std::chrono::duration<double>(Clock::now() - menu_opened_at()).count();
    char text[64];
    std::snprintf(text, sizeof(text), "%.1f", seconds);
    std::cout << "[menu] closed after " << text << " s" << (quit ? "; quitting" : "") << std::endl;
}

} // namespace

bool attach(gpu::VulkanRenderer &renderer) {
    return Layer::get().attach(renderer);
}

void show_note(const std::string &text) {
    note() = {text, Clock::now() + kNoteTime};
}

namespace {
std::optional<std::array<float, 2>> &lock_on_marker_at() {
    static std::optional<std::array<float, 2>> at;
    return at;
}

std::vector<camera::HuntPortrait> &hunt_portraits_at() {
    static std::vector<camera::HuntPortrait> portraits;
    return portraits;
}

// A sheet decoded from the game (the monster icons, the item atlas) as an
// interface texture.
struct SheetTexture {
    ImTextureData *texture{};
    std::uint64_t generation{};
};

SheetTexture &hunt_icon_texture() {
    static SheetTexture value;
    return value;
}

SheetTexture &item_atlas_texture() {
    static SheetTexture value;
    return value;
}

// Uploads a decoded sheet once per change. The first create is picked up by
// the frame; later edits ask for an upload of the same texture.
void upload_sheet(SheetTexture &sheet, bool ready, const std::uint32_t *pixels, std::uint32_t width,
    std::uint32_t height, std::uint64_t generation) {
    if (!ready || pixels == nullptr || width == 0u || height == 0u) return;
    if (sheet.generation == generation && sheet.texture != nullptr) return;
    if (sheet.texture == nullptr) {
        sheet.texture = new ImTextureData();
        sheet.texture->Create(ImTextureFormat_RGBA32, static_cast<int>(width), static_cast<int>(height));
        ImGui::RegisterUserTexture(sheet.texture);
    }
    std::memcpy(sheet.texture->GetPixels(), pixels, static_cast<std::size_t>(width) * height * sizeof(std::uint32_t));
    if (sheet.texture->Status == ImTextureStatus_OK || sheet.texture->Status == ImTextureStatus_WantUpdates)
        ImTextureDataQueueUpload(sheet.texture, 0, 0, static_cast<int>(width), static_cast<int>(height));
    sheet.generation = generation;
}

void upload_hunt_icons(const camera::HuntIconSheet &sheet) {
    upload_sheet(hunt_icon_texture(), sheet.ready, sheet.pixels, sheet.width, sheet.height, sheet.generation);
}

// The monster's icon, bottom left of the game's picture, with no panel behind
// it. The icon stays up when the monster is in another area; it is darkened
// and labelled, because lock-on cannot select it. Thin red corner brackets
// mark the monster lock-on has selected. A loading screen shows nothing: no
// stand-in while the icon is not the one for this area.
void draw_hunt_portraits(const std::vector<camera::HuntPortrait> &portraits) {
    if (fast_loading::active()) return;
    const ImGuiIO &io = ImGui::GetIO();
    const std::array<float, 4> picture = Layer::get().renderer().game_picture();
    const float pixel = picture[3] * io.DisplaySize.y / 272.0f;
    const camera::HuntIconSheet sheet = camera::hunt_icon_sheet();
    upload_hunt_icons(sheet);
    const SheetTexture &icons = hunt_icon_texture();
    const bool icons_ready = sheet.ready && icons.texture != nullptr && icons.generation == sheet.generation;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    ImFont *font = ImGui::GetFont();
    const auto screen = [&](float psp_x, float psp_y) {
        return ImVec2{(picture[0] + psp_x / 480.0f * picture[2]) * io.DisplaySize.x,
            (picture[1] + psp_y / 272.0f * picture[3]) * io.DisplaySize.y};
    };
    for (const camera::HuntPortrait &portrait : portraits) {
        const camera::HuntIconSquare square = camera::hunt_icon_square(portrait);
        const float side = square.side;
        const float icon_x = square.x;
        const float icon_y = square.y;
        const std::optional<camera::HuntIconCell> cell = camera::hunt_icon_cell(portrait.id);
        if (!icons_ready || !cell || cell->u + camera::kHuntIconTexels > sheet.width ||
            cell->v + camera::kHuntIconTexels > sheet.height)
            continue;
        const float u0 = static_cast<float>(cell->u) / static_cast<float>(sheet.width);
        const float v0 = static_cast<float>(cell->v) / static_cast<float>(sheet.height);
        const float u1 = static_cast<float>(cell->u + camera::kHuntIconTexels) / static_cast<float>(sheet.width);
        const float v1 = static_cast<float>(cell->v + camera::kHuntIconTexels) / static_cast<float>(sheet.height);
        const ImU32 tint =
            portrait.other_area || portrait.dead ? IM_COL32(80, 80, 80, 255) : IM_COL32(255, 255, 255, 255);
        draw->AddImage(icons.texture->GetTexRef(), screen(icon_x, icon_y), screen(icon_x + side, icon_y + side),
            {u0, v0}, {u1, v1}, tint);
        const float portrait_scale = side / camera::kHuntCardHeight;
        if (portrait.other_area && !portrait.dead) {
            const char *label = "Diff. Area";
            const ImVec2 icon_min = screen(icon_x, icon_y);
            const ImVec2 icon_max = screen(icon_x + side, icon_y + side);
            const float span = std::max(1.0f, icon_max.x - icon_min.x);
            // Inset from the icon's edges. Monster Icon changes the icon, and
            // the type shrinks with it, then again so it stays inside that inset.
            const float box = std::max(1.0f, span * 0.72f);
            float label_size = 6.0f * pixel * portrait_scale;
            ImVec2 label_extent = font->CalcTextSizeA(label_size, FLT_MAX, 0.0f, label);
            if (label_extent.x > box) label_size *= box / label_extent.x;
            label_extent = font->CalcTextSizeA(label_size, FLT_MAX, 0.0f, label);
            const float pad_y = (icon_max.y - icon_min.y) * 0.08f;
            draw->AddText(font, label_size,
                {icon_min.x + (span - label_extent.x) * 0.5f, icon_max.y - pad_y - label_extent.y},
                IM_COL32(220, 220, 220, 235), label);
        }
        if (!portrait.locked) continue;
        const float kArm = 6.0f * portrait_scale;
        const float kOutset = 1.5f * portrait_scale;
        const float x0 = icon_x - kOutset;
        const float y0 = icon_y - kOutset;
        const float x1 = icon_x + side + kOutset;
        const float y1 = icon_y + side + kOutset;
        const ImU32 bracket = IM_COL32(220, 32, 28, 235);
        const float thick = std::max(1.0f, pixel * 0.5f);
        const float shade = std::max(thick, pixel * 0.75f);
        const ImVec2 corners[4][3] = {
            {screen(x0, y0), screen(x0 + kArm, y0), screen(x0, y0 + kArm)},
            {screen(x1, y0), screen(x1 - kArm, y0), screen(x1, y0 + kArm)},
            {screen(x0, y1), screen(x0 + kArm, y1), screen(x0, y1 - kArm)},
            {screen(x1, y1), screen(x1 - kArm, y1), screen(x1, y1 - kArm)},
        };
        for (const ImVec2(&corner)[3] : corners) {
            draw->AddLine(corner[0], corner[1], IM_COL32(0, 0, 0, 170), shade);
            draw->AddLine(corner[0], corner[2], IM_COL32(0, 0, 0, 170), shade);
            draw->AddLine(corner[0], corner[1], bracket, thick);
            draw->AddLine(corner[0], corner[2], bracket, thick);
        }
    }
}

std::vector<game::GatherMark> &gather_marks_at() {
    static std::vector<game::GatherMark> marks;
    return marks;
}

// Lock-on's marker (camera/lock_on.hpp): a red ring, a cross through it, and
// four ticks outside it, in the game's pixels, over the locked monster.
// Drawn by the interface and never into the game's frames.
void draw_lock_on_marker(const std::array<float, 2> &at) {
    const ImGuiIO &io = ImGui::GetIO();
    const std::array<float, 4> picture = Layer::get().renderer().game_picture();
    const ImVec2 centre{
        (picture[0] + at[0] * picture[2]) * io.DisplaySize.x, (picture[1] + at[1] * picture[3]) * io.DisplaySize.y};
    const float pixel = picture[3] * io.DisplaySize.y / 272.0f;
    const float radius = 3.5f * pixel;
    const float arm = radius * 1.25f;
    const float tick_near = 4.5f * pixel;
    const float tick_far = 7.0f * pixel;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const ImU32 shadow = IM_COL32(0, 0, 0, 170);
    const ImU32 ink = IM_COL32(220, 32, 28, 235);
    for (const auto &[colour, width] : {std::pair{shadow, 1.5f * pixel}, std::pair{ink, 1.0f * pixel}}) {
        draw->AddCircle(centre, radius, colour, 24, width);
        draw->AddLine({centre.x - arm, centre.y}, {centre.x + arm, centre.y}, colour, width);
        draw->AddLine({centre.x, centre.y - arm}, {centre.x, centre.y + arm}, colour, width);
        for (int i = 0; i < 4; ++i) {
            const float dx = i == 0 ? 1.0f : i == 1 ? -1.0f : 0.0f;
            const float dy = i == 2 ? 1.0f : i == 3 ? -1.0f : 0.0f;
            draw->AddLine({centre.x + dx * tick_near, centre.y + dy * tick_near},
                {centre.x + dx * tick_far, centre.y + dy * tick_far}, colour, width);
        }
    }
}

// The mark's picture in a square of `side` around `centre`: the item's icon
// from the game's atlas in the item's tint, or the carved monster's icon.
// False when there is none to draw, and the ? stands in.
// One icon from the game's item atlas, in its tint. False while the atlas is
// not decoded.
bool draw_atlas_icon(ImDrawList *draw, const game::ItemArt &icon, ImVec2 min, ImVec2 max, int opacity) {
    const game::ItemAtlas atlas = game::item_atlas();
    SheetTexture &sheet = item_atlas_texture();
    upload_sheet(sheet, atlas.ready, atlas.pixels, atlas.width, atlas.height, atlas.generation);
    if (!atlas.ready || sheet.texture == nullptr || sheet.generation != atlas.generation) return false;
    const float w = static_cast<float>(atlas.width);
    const float h = static_cast<float>(atlas.height);
    draw->AddImage(sheet.texture->GetTexRef(), min, max, {icon.u / w, icon.v / h},
        {(icon.u + icon.width) / w, (icon.v + icon.height) / h}, IM_COL32(icon.red, icon.green, icon.blue, opacity));
    return true;
}

bool draw_gather_art(ImDrawList *draw, const game::GatherMark &mark, ImVec2 centre, float side, float alpha) {
    const ImVec2 min{centre.x - side * 0.5f, centre.y - side * 0.5f};
    const ImVec2 max{centre.x + side * 0.5f, centre.y + side * 0.5f};
    const int opacity = static_cast<int>(alpha * 255.0f);
    // Nothing else to show: the game's red ?.
    const auto unknown = [&] { return mark.unknown && draw_atlas_icon(draw, *mark.unknown, min, max, opacity); };
    if (mark.art.kind == game::GatherArt::Kind::Item && mark.icon) {
        if (draw_atlas_icon(draw, *mark.icon, min, max, opacity)) return true;
        return unknown();
    }
    if (mark.art.kind == game::GatherArt::Kind::Monster) {
        const camera::HuntIconSheet sheet = camera::hunt_icon_sheet();
        upload_hunt_icons(sheet);
        const SheetTexture &icons = hunt_icon_texture();
        const std::optional<camera::HuntIconCell> cell = camera::hunt_icon_cell(mark.art.monster);
        if (!sheet.ready || icons.texture == nullptr || icons.generation != sheet.generation || !cell ||
            cell->u + camera::kHuntIconTexels > sheet.width || cell->v + camera::kHuntIconTexels > sheet.height)
            return unknown();
        const float w = static_cast<float>(sheet.width);
        const float h = static_cast<float>(sheet.height);
        draw->AddImage(icons.texture->GetTexRef(), min, max, {cell->u / w, cell->v / h},
            {(cell->u + camera::kHuntIconTexels) / w, (cell->v + camera::kHuntIconTexels) / h},
            IM_COL32(255, 255, 255, opacity));
        return true;
    }
    return unknown();
}

// One per size: the pack's image of the sheet is larger than the game's own,
// and upload_sheet keeps the size a texture was made with, so turning the
// pack off copied the game's sheet into the pack's texture, row by row in the
// wrong places.
SheetTexture &button_glyph_texture(std::uint32_t width, std::uint32_t height) {
    static std::map<std::pair<std::uint32_t, std::uint32_t>, SheetTexture> sheets;
    return sheets[{width, height}];
}

// The quest bar's sheet (game/button_glyphs.hpp) as uploaded, or null until
// it has been drawn and decoded: the game's texture, or the texture pack's
// image of it, so a pack's button icons and frame are the ones shown.
SheetTexture *quest_bar_texture() {
    const game::ButtonGlyphSheet sheet = game::button_glyph_sheet();
    SheetTexture &texture = button_glyph_texture(sheet.width, sheet.height);
    upload_sheet(texture, sheet.ready, sheet.pixels, sheet.width, sheet.height, sheet.generation);
    if (!sheet.ready || texture.texture == nullptr || texture.generation != sheet.generation) return nullptr;
    return &texture;
}

// A sprite's cell pulled in by half a texel of the sheet as uploaded. The
// interface samples bilinearly, so at the cell's own edges the quad's outer
// pixels took half of the neighbouring sprites' texels: a faint square
// around the hexagon.
game::GlyphCell inside_texels(game::GlyphCell cell, const SheetTexture &sheet) {
    const float u = 0.5f / static_cast<float>(sheet.texture->Width);
    const float v = 0.5f / static_cast<float>(sheet.texture->Height);
    return {cell.u0 + u, cell.v0 + v, cell.u1 - u, cell.v1 - v};
}

// The button that does what the mark shows (GatherMark::button), at the
// frame's lower left: the game's own glyph from the quest bar's sheet, as
// the game's prompts draw it. Nothing until the sheet has been drawn.
void draw_gather_button(ImDrawList *draw, std::uint32_t button, ImVec2 frame, float side, float alpha) {
    const std::optional<game::ButtonGlyph> glyph = game::button_glyph_for(button);
    SheetTexture *const texture = quest_bar_texture();
    if (!glyph || texture == nullptr) return;
    // The glyph's 16 texels, at the frame's 40-texel scale and a little larger.
    const float half = side * 0.25f;
    const ImVec2 centre{frame.x - side * 0.36f, frame.y + side * 0.36f};
    const game::GlyphCell cell = inside_texels(game::button_glyph_cell(*glyph), *texture);
    draw->AddImage(texture->texture->GetTexRef(), {centre.x - half, centre.y - half},
        {centre.x + half, centre.y + half}, {cell.u0, cell.v0}, {cell.u1, cell.v1},
        IM_COL32(255, 255, 255, static_cast<int>(255.0f * alpha)));
}

// A count in the game's own letters: each character as the game drew it into
// the glyph atlas it draws its text from (fonts::glyph_alone), as the item
// bar's counts are, rebuilt when the count or the font changes. Null while
// the game has not drawn one of the characters yet.
struct CountTexture {
    SheetTexture sheet;
    std::string text;
    std::uint64_t font{};
    std::uint32_t width{};
    std::uint32_t height{};
};

const CountTexture *count_texture(const std::string &text) {
    constexpr int kScale = 4;
    constexpr int kSide = fonts::kCell * kScale;
    static CountTexture kept;
    static std::uint64_t made{};
    if (kept.sheet.texture != nullptr && kept.text == text && kept.font == fonts::generation()) return &kept;
    std::vector<std::vector<std::uint8_t>> cells;
    for (const char letter : text) {
        std::vector<std::uint8_t> cell = fonts::glyph_alone(static_cast<unsigned char>(letter), kScale);
        if (cell.size() != static_cast<std::size_t>(kSide) * kSide) return nullptr;
        cells.push_back(std::move(cell));
    }
    if (cells.empty()) return nullptr;
    const auto width = static_cast<std::uint32_t>(kSide * static_cast<int>(cells.size()));
    const auto height = static_cast<std::uint32_t>(kSide);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height, 0u);
    for (std::size_t n = 0; n < cells.size(); ++n)
        for (int y = 0; y < kSide; ++y)
            for (int x = 0; x < kSide; ++x) {
                // 4-bit ink, white as the game's text.
                const std::uint32_t ink = cells[n][static_cast<std::size_t>(y * kSide + x)] * 17u;
                pixels[static_cast<std::size_t>(y) * width + n * kSide + static_cast<std::size_t>(x)] =
                    (ink << 24u) | 0x00FFFFFFu;
            }
    // A new size needs a new texture: upload_sheet keeps the size it made.
    if (kept.width != width || kept.height != height) kept.sheet = {};
    upload_sheet(kept.sheet, true, pixels.data(), width, height, ++made);
    kept.text = text;
    kept.font = fonts::generation();
    kept.width = width;
    kept.height = height;
    return &kept;
}

// The gather mark (game/gather_icon.hpp), made of the game's own pieces:
// the hexagon the item bar draws behind the selected item, from the quest
// bar's sheet, with the picture of what it gathers over it as the item bar
// draws the item, or the game's red ? when there is none; the button's glyph
// from the same sheet; and the tool count in the game's letters. A piece the
// game has not drawn yet is left out, and the interface draws nothing of
// its own. It grows a little as it fades in. Its vertices follow frame
// interpolation (gpu/ui_motion.hpp) from where the last game frame drew it.
// Drawn by the interface and never into the game's frames.
void draw_gather_mark(const game::GatherMark &mark) {
    const ImGuiIO &io = ImGui::GetIO();
    const std::array<float, 4> picture = Layer::get().renderer().game_picture();
    const auto screen = [&](const std::array<float, 2> &at) {
        return ImVec2{
            (picture[0] + at[0] * picture[2]) * io.DisplaySize.x, (picture[1] + at[1] * picture[3]) * io.DisplaySize.y};
    };
    const ImVec2 centre = screen(mark.at);
    const float alpha = std::clamp(mark.alpha, 0.0f, 1.0f);
    // Ease out: most of the growth comes early in the fade.
    const float grow = 0.8f + 0.2f * (1.0f - (1.0f - alpha) * (1.0f - alpha));
    const float pixel = picture[3] * io.DisplaySize.y / 272.0f * grow;
    const settings::Settings &player = settings::current();
    const game::GatherShape shape = game::gather_mark_shape(player.gather_icon_size);
    // The frame's side, its 40 texels.
    const float side = 2.0f * (shape.radius + shape.ring) * pixel;
    const float texel = side / static_cast<float>(game::kItemBarFrame.width);
    const int opacity = static_cast<int>(255.0f * alpha);
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const int first_vertex = draw->VtxBuffer.Size;
    if (SheetTexture *const sheet = quest_bar_texture(); sheet != nullptr && game::item_bar_frame_ready()) {
        const game::GlyphCell cell = inside_texels(game::quest_bar_cell(game::kItemBarFrame), *sheet);
        const float half = side * 0.5f;
        draw->AddImage(sheet->texture->GetTexRef(), {centre.x - half, centre.y - half},
            {centre.x + half, centre.y + half}, {cell.u0, cell.v0}, {cell.u1, cell.v1},
            IM_COL32(255, 255, 255, opacity));
    }
    // The item bar draws the item's 32-texel icon 4 texels in from the
    // frame's left and 2 from its top.
    (void)draw_gather_art(draw, mark, {centre.x, centre.y - 2.0f * texel}, 32.0f * texel, alpha);
    if (mark.button != 0u) draw_gather_button(draw, mark.button, centre, side, alpha);
    // How many of the tool the pouch holds, as x3, at the frame's upper right,
    // in the game's own letters: 7 by 14 of its pixels each, as the item bar's.
    if (mark.carried > 0u) {
        const std::string count = "x" + std::to_string(mark.carried);
        if (const CountTexture *digits = count_texture(count)) {
            const float advance = 7.0f * texel;
            const float tall = 14.0f * texel;
            const float right = centre.x + side * 0.5f;
            const float top = centre.y - side * 0.5f;
            const ImVec2 min{right - advance * static_cast<float>(count.size()), top};
            draw->AddImage(digits->sheet.texture->GetTexRef(), min, {right, top + tall}, {0.0f, 0.0f}, {1.0f, 1.0f},
                IM_COL32(255, 255, 255, opacity));
        }
    }
    if (mark.older) {
        const ImVec2 older = screen(*mark.older);
        gpu::ui_motion::add(draw, first_vertex, draw->VtxBuffer.Size, {older.x - centre.x, older.y - centre.y});
    }
}

// Every mark of this game frame. Two things at one place (a mining point and
// the body over it, or a node by the water) are set side by side, a frame and
// the spacing apart in the game's pixels, so neither covers the other.
void draw_gather_marks(std::vector<game::GatherMark> marks) {
    const ImGuiIO &io = ImGui::GetIO();
    const std::array<float, 4> picture = Layer::get().renderer().game_picture();
    const float width = picture[2] * io.DisplaySize.x;
    const float height = picture[3] * io.DisplaySize.y;
    if (width > 0.0f && height > 0.0f) {
        const game::GatherShape shape = game::gather_mark_shape(settings::current().gather_icon_size);
        const float apart = (2.0f * (shape.radius + shape.ring) + game::kGatherSpacing) * height / 272.0f;
        game::spread_gather_marks(marks, {apart / width, apart / height});
    }
    for (const game::GatherMark &mark : marks) draw_gather_mark(mark);
}
} // namespace

void set_lock_on_marker(std::optional<std::array<float, 2>> at) {
    lock_on_marker_at() = at;
}

void set_hunt_portraits(std::vector<camera::HuntPortrait> portraits) {
    hunt_portraits_at() = std::move(portraits);
}

void set_gather_marks(std::vector<game::GatherMark> marks) {
    gather_marks_at() = std::move(marks);
}

namespace {
// A hit's number (game/damage_numbers.hpp), drawn by the interface over the
// game's picture and never into the game's own frames. Its vertices follow
// frame interpolation (gpu/ui_motion.hpp) from where the last game frame
// drew it.
void draw_damage_numbers() {
    const ImGuiIO &io = ImGui::GetIO();
    const std::array<float, 4> picture = Layer::get().renderer().game_picture();
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const settings::Settings &s = settings::current();
    const float base_size = Layer::get().font_size() * 0.85f;
    const float full_size = base_size * static_cast<float>(s.damage_number_size) / 100.0f;
    ImFont *font = ImGui::GetFont();
    const game::Breakdown breakdown = s.damage_breakdown == settings::DamageBreakdown::Split ? game::Breakdown::Split
        : s.damage_breakdown == settings::DamageBreakdown::TotalAndElement ? game::Breakdown::TotalAndElement
                                                                           : game::Breakdown::Total;
    for (const game::Number &number : game::damage_numbers()) {
        const game::Anchor anchor = game::number_anchor(number);
        // Smaller when the camera is close, so it stays on the hit's point.
        const float size = full_size * anchor.scale;
        const game::NumberText parts = game::number_text(number, breakdown);
        const std::string &text = parts.main;
        const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        // A pixel of stroke for every interface-sized number's worth of height.
        const game::Stroke stroke = game::number_stroke(s.damage_number_thickness, size, base_size);
        // The tuned gap fits the default size. A bigger or thicker number is
        // lifted by its own height, so the hits stay readable.
        const float picture_px = picture[3] * io.DisplaySize.y;
        const float tuned_px = game::kStack * anchor.scale * picture_px;
        const bool clear = s.damage_number_size != 100u || s.damage_number_thickness != 1u;
        const float gap_px = game::stack_gap_px(tuned_px, static_cast<float>(s.damage_number_size) / 100.0f, extent.y,
            stroke.outline + stroke.weight, clear);
        const float lift = static_cast<float>(number.lane) * (gap_px - tuned_px);
        const ImVec2 at{(picture[0] + anchor.x * picture[2]) * io.DisplaySize.x - extent.x * 0.5f,
            (picture[1] + anchor.y * picture[3]) * io.DisplaySize.y - extent.y * 0.5f - lift};
        const int alpha = static_cast<int>(anchor.alpha * 230.0f);
        if (alpha <= 0) continue;
        const ImU32 outline = IM_COL32(0, 0, 0, alpha);
        const auto text_at = [&](const ImVec2 &origin, float text_size, const game::Ink &ink, const std::string &t) {
            const game::Stroke line = game::number_stroke(s.damage_number_thickness, text_size, base_size);
            for (const game::Offset &o : game::stroke_offsets(line.outline + line.weight, 3))
                draw->AddText(font, text_size, {origin.x + o.x, origin.y + o.y}, outline, t.c_str());
            for (const game::Offset &o : game::stroke_offsets(line.weight, 2))
                draw->AddText(font, text_size, {origin.x + o.x, origin.y + o.y},
                    IM_COL32(ink.red, ink.green, ink.blue, alpha), t.c_str());
        };
        // An element's glyph, in a box as tall as the digits of text `text_size`
        // tall whose line starts at `line_y`, from `x`; outlined like the text.
        // Returns its width.
        const auto glyph_at = [&](float x, float line_y, float text_size, game::Element element, const game::Ink &ink) {
            const game::Glyph glyph = game::element_glyph(element);
            if (glyph.fills.empty()) return 0.0f;
            const float line = font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, "0").y;
            const float box = line * 0.62f;
            const ImVec2 origin{x, line_y + line * 0.55f - box * 0.5f};
            const float edge =
                std::max(1.0f, game::number_stroke(s.damage_number_thickness, text_size, base_size).outline);
            std::vector<ImVec2> points;
            const auto place = [&](const std::vector<game::Offset> &shape, const game::Offset &o) {
                points.clear();
                for (const game::Offset &p : shape)
                    points.push_back({origin.x + p.x * box + o.x, origin.y + p.y * box + o.y});
            };
            const auto fill = [&](const std::vector<game::Offset> &shape, const game::Offset &o, ImU32 colour) {
                place(shape, o);
                draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), colour);
            };
            for (const game::Offset &o : game::stroke_offsets(edge, 2))
                for (const std::vector<game::Offset> &shape : glyph.fills) fill(shape, o, outline);
            for (const std::vector<game::Offset> &shape : glyph.fills)
                fill(shape, {}, IM_COL32(ink.red, ink.green, ink.blue, alpha));
            // The light faces, a little under halfway to white.
            const auto lighter = [](std::uint8_t c) { return c + (255 - c) * 9 / 20; };
            for (const std::vector<game::Offset> &shape : glyph.highlights)
                fill(shape, {}, IM_COL32(lighter(ink.red), lighter(ink.green), lighter(ink.blue), alpha));
            for (const std::vector<game::Offset> &groove : glyph.grooves) {
                place(groove, {});
                draw->AddPolyline(points.data(), static_cast<int>(points.size()), outline, ImDrawFlags_None,
                    std::max(1.0f, glyph.groove_width * box));
            }
            return box;
        };
        const int first_vertex = draw->VtxBuffer.Size;
        const float gap = std::max(1.0f, size * 0.12f) + stroke.outline;
        text_at(at, size, parts.main_ink, text);
        // A hit of element alone: its glyph before the number.
        if (parts.main_glyph != game::Element::None) {
            const float box = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "0").y * 0.62f;
            glyph_at(at.x - gap - box, at.y, size, parts.main_glyph, parts.main_ink);
        }
        // The element share beside it, smaller, on the same baseline, after
        // its glyph.
        if (!parts.side.empty()) {
            const float side_size = size * 0.7f;
            const ImVec2 side_extent = font->CalcTextSizeA(side_size, FLT_MAX, 0.0f, parts.side.c_str());
            float x = at.x + extent.x + gap;
            const float side_y = at.y + extent.y - side_extent.y;
            if (const float box = glyph_at(x, side_y, side_size, parts.side_glyph, parts.side_ink); box > 0.0f)
                x += box + std::max(1.0f, box * 0.15f) + stroke.outline;
            text_at({x, side_y}, side_size, parts.side_ink, parts.side);
        }
        // A critical hit's mark: a glowing sparkle above the number, pink, or
        // for a negative critical a faded blue-gray.
        if (const std::optional<game::Ink> mark = game::affinity_dot(number.grade.affinity)) {
            const bool negative = number.grade.affinity == game::Affinity::Negative;
            // Across, about the number's width; up and down, a third of the
            // digits' height; just above their tops.
            const float half_across = std::max(extent.x * 0.52f, size * 0.42f);
            const float half_up = size * 0.17f;
            const ImVec2 centre{at.x + extent.x * 0.5f, at.y + extent.y * 0.16f - half_up};
            const std::vector<game::Offset> shape = game::sparkle_shape();
            std::vector<ImVec2> points(shape.size());
            const auto sparkle = [&](float across, float up, ImU32 colour) {
                for (std::size_t i = 0; i < shape.size(); ++i)
                    points[i] = {centre.x + shape[i].x * across, centre.y + shape[i].y * up};
                draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), colour);
            };
            const float faded = negative ? 0.6f : 1.0f;
            const auto tone = [&](float share) {
                return IM_COL32(
                    mark->red, mark->green, mark->blue, static_cast<int>(static_cast<float>(alpha) * share * faded));
            };
            // The core is the colour taken most of the way to white: near white
            // for a critical, a pale blue for a negative one.
            const float whiten = negative ? 0.55f : 0.85f;
            const auto core = [&](std::uint8_t c) {
                return static_cast<int>(static_cast<float>(c) + static_cast<float>(255 - c) * whiten);
            };
            // A soft glow, the sparkle, and its core.
            const float glow = std::max(1.0f, half_up * 0.5f);
            sparkle(half_across + glow * 3.0f, half_up + glow * 2.0f, tone(0.25f));
            sparkle(half_across + glow * 1.5f, half_up + glow, tone(0.45f));
            sparkle(half_across, half_up, tone(1.0f));
            sparkle(half_across * 0.72f, half_up * 0.62f,
                IM_COL32(core(mark->red), core(mark->green), core(mark->blue),
                    static_cast<int>(static_cast<float>(alpha) * faded)));
        }
        if (!game::smooth_enabled()) continue;
        if (const std::optional<game::Anchor> older = game::older_anchor(number))
            gpu::ui_motion::add(draw, first_vertex, draw->VtxBuffer.Size,
                {(older->x - anchor.x) * picture[2] * io.DisplaySize.x,
                    (older->y - anchor.y) * picture[3] * io.DisplaySize.y});
    }
}

} // namespace

std::string take_screenshot() {
    Layer &layer = Layer::get();
    if (!layer.attached()) return {};
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    // The render target the game flipped to last. When the HUD is hidden
    // (#184) its draws never reach that target, so the picture has none.
    if (!layer.renderer().read_frame(pixels, width, height)) {
        std::cout << "[screenshot] no game picture to save yet" << std::endl;
        show_note("No game picture to save yet");
        return {};
    }
    std::filesystem::path path;
    try {
        path = screenshot::free_path(screenshot::folder(), std::chrono::system_clock::now());
    } catch (const std::exception &e) {
        std::cout << "[screenshot] not saved: " << e.what() << std::endl;
        show_note(std::string("Screenshot not saved: ") + e.what());
        return {};
    }
    const std::string where = install::path_to_utf8(path);
    std::cout << "[screenshot] taking " << width << "x" << height << " -> " << where << std::endl;
    screenshot::save_png_later(path, std::move(pixels), width, height);
    show_note("Screenshot saved to " + where);
    return where;
}

void draw_over_game() {
    Layer &layer = Layer::get();
    if (!layer.attached()) return;
    script::tick();
    std::optional<Menu> &menu = menu_over_game_state();
    // The game's keyboard request: drawn over every game frame until done. A
    // keyboard opened from the menu over the running game is the menu's.
    if (text_input_open() && !menu) {
        layer.begin_frame();
        text_input_frame();
        layer.end_frame();
        return;
    }
    const double hint_left = menu || settings::current().menu_hint_seen ? -1.0 : hint_seconds_left();
    const bool overlay = network_overlay();
    const bool touch = !menu && layer.renderer().touch_controls_visible();
    const std::string gpu_problem = menu || gpu_problem_seen() ? std::string() : layer.renderer().gpu_problem();
    const bool fast = !menu && fast_forward::active();
    const bool noted =
        !note().text.empty() && Clock::now() < note().until && !layer.renderer().window_capture_pending();
    const double hud_note = menu || layer.renderer().window_capture_pending() ? 0.0 : gpu::hud::note_seconds_left();
    const std::optional<std::array<float, 2>> marker = menu ? std::nullopt : lock_on_marker_at();
    const std::vector<camera::HuntPortrait> portraits =
        menu ? std::vector<camera::HuntPortrait>{} : hunt_portraits_at();
    const std::vector<game::GatherMark> gather = menu ? std::vector<game::GatherMark>{} : gather_marks_at();
    const bool numbers = !menu && !layer.renderer().window_capture_pending() && !game::damage_numbers().empty();
    if (hint_left <= 0.0 && !overlay && !menu && !touch && gpu_problem.empty() && !fast && !noted && hud_note <= 0.0 &&
        !marker && portraits.empty() && gather.empty() && !numbers)
        return;
    layer.begin_frame();
    if (numbers) draw_damage_numbers();
    if (!portraits.empty()) draw_hunt_portraits(portraits);
    if (marker) draw_lock_on_marker(*marker);
    if (!gather.empty()) draw_gather_marks(gather);
    // The marks are on the background list, under every window of the
    // interface; the game's HUD is drawn again over them there.
    if (numbers || !portraits.empty() || marker || !gather.empty()) {
        ImDrawList *under = ImGui::GetBackgroundDrawList();
        under->AddCallback(&gpu::VulkanRenderer::draw_hud_cover, nullptr);
        under->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    }
    if (noted) draw_note(note());
    if (touch) {
        const settings::Settings &player = settings::current();
        if (player.touch_layout == settings::TouchLayout::Action)
            draw_action_controls(layer.renderer().action_touch_controls(), player.touch_opacity, SDL_GetTicks());
        else
            draw_touch_controls(layer.renderer().touch_controls(), player.touch_opacity);
    }
    if (hint_left > 0.0) draw_hint(hint_left);
    if (!gpu_problem.empty()) draw_gpu_problem(gpu_problem);
    if (overlay) draw_network_overlay();
    if (fast) draw_fast_forward();
    if (hud_note > 0.0) draw_hud_note(hud_note);
    if (menu && !menu->frame()) {
        const bool quit = menu->quit() || layer.window_closed();
        menu.reset();
        layer.set_interactive(false);
        layer.renderer().set_game_input(true);
        note_menu_closed(quit);
        if (quit) quit_requested() = true;
    }
    layer.end_frame();
}

bool menu_pauses() {
    const settings::Settings &s = settings::current();
    return adhoc_session_active() ? s.menu_pause_multiplayer : s.menu_pause;
}

void open_menu_over_game() {
    std::optional<Menu> &menu = menu_over_game_state();
    if (menu) return;
    Layer &layer = Layer::get();
    note_menu_opened(false);
    layer.renderer().set_game_input(false);
    layer.set_interactive(true);
    menu.emplace(false);
}

bool menu_over_game() {
    return menu_over_game_state().has_value();
}

bool take_quit_request() {
    return std::exchange(quit_requested(), false);
}

bool menu_requested() {
    Layer &layer = Layer::get();
    if (!layer.attached() || text_input_open()) return false;
    // The on-screen menu button, then Esc or L3+R3.
    const bool touched = layer.renderer().take_touch_menu();
    return layer.take_menu_toggle() || touched;
}

bool run_menu() {
    Layer &layer = Layer::get();
    note_menu_opened(true);
    layer.renderer().set_game_input(false);
    layer.set_interactive(true);
    Menu menu(true);
    const bool window_open = layer.run([&] { return menu.frame(); }, true);
    layer.set_interactive(false);
    layer.renderer().set_game_input(true);
    note_menu_closed(menu.quit());
    return window_open && !menu.quit();
}

} // namespace mhfu::ui
