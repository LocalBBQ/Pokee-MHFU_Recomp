// sceDisplay, sceCtrl, sceGe_user, sceAudio and sceSasCore. These keep guest
// timing and callbacks behaving like hardware (vblank-paced input reads, GE
// list completion callbacks, blocking audio output); the drawing and the
// mixing themselves live under gpu/ and audio/.
#include "hle_common.hpp"
#include "kernel/fast_forward.hpp"
#include "kernel/fast_loading.hpp"
#include "kernel/load_trace.hpp"

#include "overlays.hpp"
#include "game/body_trace.hpp"
#include "adhoc/client.hpp"
#include "game/damage_numbers.hpp"
#include "game/damage_sync.hpp"
#include "game/gather_icon.hpp"
#include "game/guest_layout.hpp"
#include "game/guest_ram.hpp"
#include "game/hit_trace.hpp"
#include "game/button_glyphs.hpp"
#include "game/item_atlas.hpp"
#include "game/empty_item_slot.hpp"
#include "game/give_rarity.hpp"
#include "game/true_attack.hpp"
#include "game/true_element.hpp"
#include "game/hall_equipment.hpp"
#include "game/hall_meal.hpp"
#include "game/item_sets.hpp"
#include "game/item_box.hpp"
#include "game/key_quests.hpp"
#include "game/pouch_page.hpp"
#include "game/layered_armor.hpp"
#include "mods/mhfu_mods.hpp"
#include "game/sharpness_gauge.hpp"
#include "platform/utf8_path.hpp"

#include "audio/audio_sink.hpp"
#include "audio/channel_queue.hpp"
#include "audio/guest_pcm.hpp"
#include "audio/sas_core.hpp"

#include "psprecomp/common.hpp"

#include "camera_probe.hpp"
#include "camera/camera_input.hpp"
#include "camera/game_aspect.hpp"
#include "camera/field_of_view.hpp"
#include "camera/game_camera.hpp"
#include "camera/hunt_icon.hpp"
#include "camera/lock_on.hpp"
#include "input/bindings.hpp"
#include "settings/settings.hpp"
#include "gpu/game_hud.hpp"
#include "gpu/ge_state.hpp"
#include "perf/frame_stats.hpp"
#if defined(MHFU_HAS_RENDERER)
#include "gpu/vulkan_renderer.hpp"
#include "ui/ui.hpp"
#if defined(MHFU_ANDROID_APP)
#include "platform/android_fatal.hpp"
#include "pokee_version.hpp"
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psprecomp {
// Defined in the core library and declared here, as camera_probe.cpp does, so
// that arming a watch does not rebuild every generated unit and overlay.
void set_write_watch(std::uint32_t address, std::uint32_t size);
} // namespace psprecomp

namespace mhfu {
namespace {

constexpr std::uint32_t kEdramBase = 0x04000000u;
constexpr std::uint32_t kEdramSize = 0x00200000u;
constexpr std::uint32_t kAudioSampleRate = 44'100u;

struct DisplayState {
    std::uint32_t mode{};
    std::uint32_t width{480u};
    std::uint32_t height{272u};
    std::uint32_t framebuffer{};
    std::uint32_t buffer_width{};
    std::uint32_t pixel_format{};
};

struct GeCallback {
    std::uint32_t signal_function{};
    std::uint32_t signal_argument{};
    std::uint32_t finish_function{};
    std::uint32_t finish_argument{};
};

struct GeList {
    std::uint32_t pc{};
    std::uint32_t stall{};
    std::int32_t callback{-1};
    bool done{};
};

struct AudioChannel {
    bool reserved{};
    std::uint32_t samples{};
    std::uint32_t format{};
    // Virtual time at which everything handed to this channel has finished
    // playing, and the host ring position its next frames are mixed at.
    audio::ChannelQueue queue{};
    std::uint64_t cursor{};
};

struct MediaState {
    DisplayState display;
    std::uint32_t ctrl_cycle{};
    std::uint32_t ctrl_mode{};
    std::map<std::int32_t, GeCallback> ge_callbacks;
    std::int32_t next_ge_callback{};
    std::map<std::uint32_t, GeList> ge_lists;
    std::uint32_t next_ge_list{1u};
    std::array<AudioChannel, 8> audio{};
    gpu::GeState ge;
#if defined(MHFU_HAS_RENDERER)
    std::unique_ptr<gpu::VulkanRenderer> renderer;
#endif
};

MediaState &media() {
    static MediaState state;
    return state;
}

// Runs a display list: the GE state machine produces draw calls for the
// renderer and raises the guest's signal/finish callbacks in interrupt context.
// A GE block transfer, row by row. A source in a framebuffer the renderer drew
// is written back to guest memory first: the game copies the last frame of a
// hunt this way and textures the quest reward screen's background from it.
void block_transfer(Runtime &rt, const gpu::BlockTransfer &transfer) {
    psprecomp::GuestMemory &memory = rt.memory();
#if defined(MHFU_HAS_RENDERER)
    if (media().renderer && media().renderer->available())
        media().renderer->read_back_framebuffer(transfer.source, memory);
#endif
    const std::size_t row_bytes = static_cast<std::size_t>(transfer.width) * transfer.bytes_per_pixel;
    for (std::uint32_t row = 0; row < transfer.height; ++row) {
        const std::uint32_t from = transfer.source +
            ((transfer.source_y + row) * transfer.source_stride + transfer.source_x) * transfer.bytes_per_pixel;
        const std::uint32_t to = transfer.destination +
            ((transfer.destination_y + row) * transfer.destination_stride + transfer.destination_x) *
                transfer.bytes_per_pixel;
        const std::uint8_t *source = memory.raw_pointer(from, row_bytes);
        std::uint8_t *destination = memory.raw_pointer(to, row_bytes);
        if (source == nullptr || destination == nullptr) {
            log_once("ge-transfer-range", "[ge] block transfer outside guest memory skipped");
            return;
        }
        std::memmove(destination, source, row_bytes);
    }
#if defined(MHFU_HAS_RENDERER)
    // Textures already looked up in this list may have changed.
    if (media().renderer && media().renderer->available()) media().renderer->begin_display_list();
#endif
}

void run_ge_list(Runtime &rt, std::uint32_t id) {
    auto found = media().ge_lists.find(id);
    if (found == media().ge_lists.end()) return;
    const perf::Clock::time_point start = perf::Clock::now();
    GeList &list = found->second;
    const GeCallback *callback = nullptr;
    if (const auto cb = media().ge_callbacks.find(list.callback); cb != media().ge_callbacks.end())
        callback = &cb->second;

    media().ge.set_signal_sink([callback](std::uint32_t signal, std::uint32_t pc) {
        if (callback == nullptr) return;
        const bool finish = (signal & 0x10000u) != 0u;
        const std::uint32_t function = finish ? callback->finish_function : callback->signal_function;
        if (function == 0u) return;
        InterruptCall call{};
        call.function = function;
        call.arguments = {signal & 0xFFFFu, finish ? callback->finish_argument : callback->signal_argument, pc, 0u};
        kernel().queue_interrupt(std::move(call));
    });
#if defined(MHFU_HAS_RENDERER)
    if (media().renderer && media().renderer->available()) {
        gpu::VulkanRenderer &renderer = *media().renderer;
        const psprecomp::GuestMemory &memory = rt.memory();
        renderer.begin_display_list();
        media().ge.set_raw_vertices(renderer.gpu_decode(), renderer.check_gpu_decode());
        media().ge.set_draw_sink([&renderer, &memory](const gpu::DrawCall &call) { renderer.submit(call, memory); });
        media().ge.set_vertex_reuse(
            [&renderer](const gpu::VertexReuseLookup &lookup) { return renderer.reuse_decoded_vertices(lookup); });
    }
#endif
    media().ge.set_transfer_sink([&rt](const gpu::BlockTransfer &transfer) { block_transfer(rt, transfer); });

    bool finished = false;
    try {
        const perf::SplitScope split(perf::Split::Lists);
        list.pc = media().ge.execute(rt.memory(), list.pc, list.stall, finished);
    } catch (const psprecomp::Error &error) {
        // A malformed list must not take the whole run down: drop it and carry on.
        log_once("ge-list-error", std::string("[ge] display list aborted: ") + error.what());
        finished = true;
    }
    list.done = finished;
    media().ge.set_draw_sink(nullptr);
    media().ge.set_transfer_sink(nullptr);
    perf::add_render_time(perf::Clock::now() - start);
}

#if defined(MHFU_HAS_RENDERER)
// The mouse's motion since the previous pump, as degrees for the camera
// layer. Added after the flip, so whoever drives the camera takes it in the
// update this frame leads to.
void feed_mouse(gpu::VulkanRenderer &renderer) {
    const settings::Settings &s = settings::current();
    // A drag on the touch screen: Touch camera speed degrees for the screen's
    // height, slowed while aiming as the mouse is.
    const gpu::MouseMotion drag = renderer.take_touch_motion();
    if (drag.x != 0.0f || drag.y != 0.0f) {
        const float scale = camera::game_camera_degrees_per_second() / std::max(s.camera_speed, 1.0f);
        const float degrees = s.touch_camera_speed * scale;
        camera::add_motion(camera::Source::Touch, drag.x * degrees, drag.y * degrees);
    }
    const gpu::MouseMotion motion = renderer.take_mouse_motion();
    if (motion.x == 0.0f && motion.y == 0.0f) return;
    // While a bow or a bowgun aims, Aim speed's share of Camera speed, as
    // for the stick.
    const float scale = camera::game_camera_degrees_per_second() / std::max(s.camera_speed, 1.0f);
    const input::MouseTurn turn =
        input::mouse_turn(motion.x, motion.y, s.mouse_sensitivity, s.invert_mouse_x, s.invert_mouse_y, scale);
    camera::add_motion(camera::Source::Mouse, turn.yaw, turn.pitch);
    static const bool trace = std::getenv("MHFU_TRACE_PAD") != nullptr;
    if (trace)
        std::cout << "[pad] mouse " << motion.x << "," << motion.y << " -> " << turn.yaw << "," << turn.pitch
                  << " degrees" << std::endl;
}
#endif

#if defined(MHFU_HAS_RENDERER)
// The quest camera's eye for damage numbers and the gather mark, asking for
// the camera hook when either of those, or MHFU_TRACE_HITS, is on.
std::optional<game::EyePose> damage_numbers_pose(Runtime &rt) {
    camera::game_camera_want_pose(settings::current().damage_numbers != settings::DamageNumbers::Off ||
        settings::current().gather_icon || game::hit_trace::enabled());
    const std::optional<camera::CameraPose> pose = camera::game_camera_pose(rt.memory());
    if (!pose) return std::nullopt;
    return game::EyePose{
        {pose->eye[0], pose->eye[1], pose->eye[2]}, {pose->look_at[0], pose->look_at[1], pose->look_at[2]}};
}

// Damage numbers in ad hoc play (game/damage_sync.hpp): while this console
// plays a quest in an ad hoc group, the hits it applies go to the other
// players and theirs come in, on a socket of the port's own. Before
// damage_numbers_frame, so a hit received now explains this frame's drop.
void share_damage_numbers(Runtime &rt) {
    constexpr std::size_t kSyncBuffer = 16u * 1024u;
    constexpr int kMaxDatagrams = 64; // a frame's worth from three players, and more
    static int socket = 0;
    // Only a large monster is the same on every console.
    static const bool large_set = [] {
        game::set_large_monsters([](const psprecomp::GuestMemory &memory) {
            std::vector<std::uint32_t> large;
            for (const camera::HuntMonster &monster : camera::hunt_monsters(memory)) large.push_back(monster.address);
            return large;
        });
        return true;
    }();
    (void)large_set;
    const bool active = game::sync_enabled() && settings::current().adhoc && game::layout::quest_running(rt.memory()) &&
        adhoc::Client::get().in_group();
    if (active && socket == 0) {
        socket = adhoc::Client::get().pdp_open(game::kSyncPort, kSyncBuffer);
        if (game::hit_trace::enabled())
            std::cout << "[dmg] sharing hits on ad hoc port " << game::kSyncPort
                      << (socket != 0 ? "" : ": the port is taken") << std::endl;
    } else if (!active && socket != 0) {
        adhoc::Client::get().pdp_close(socket);
        socket = 0;
        if (game::hit_trace::enabled()) std::cout << "[dmg] sharing hits stops" << std::endl;
    }
    game::set_sharing(socket != 0);
    if (socket == 0) return;
    adhoc::Client &client = adhoc::Client::get();
    for (const std::string &datagram : game::encode_hits(game::take_shared_hits()))
        if (!client.pdp_send(socket, adhoc::kBroadcastMac, game::kSyncPort, datagram.data(), datagram.size())) {
            socket = 0; // the client closed every socket; open it again next frame
            return;
        }
    for (int i = 0; i < kMaxDatagrams; ++i) {
        const std::optional<adhoc::Datagram> datagram = client.pdp_receive(socket);
        if (!datagram) break;
        if (const std::optional<std::vector<game::SharedHit>> hits = game::decode_hits(datagram->data))
            game::receive_shared_hits(rt.memory(), *hits);
    }
}
#endif

// The emulated time of the vblank the frame being flipped started from. The
// game starts a frame every other vblank, when its vblank handler has counted
// two since the last one; the flip comes when the frame's code has run, and
// on a slower machine that is often after the vblank between, so the latest
// vblank is not the frame's own. Frames are kept on a grid of two vblanks
// from the one before: the latest start on that grid not after the latest
// vblank. A flip that comes before a whole step, or two steps late, starts
// the grid again at its latest vblank.
std::uint64_t frame_start_us(std::uint64_t latest_vblank_us) {
    static std::uint64_t previous = 0u;
    static bool known = false;
    constexpr std::uint64_t kStep = 2u * kVBlankPeriodUs;
    std::uint64_t start = latest_vblank_us;
    if (known && latest_vblank_us >= previous + kStep && latest_vblank_us < previous + 3u * kStep)
        start = previous + kStep * ((latest_vblank_us - previous) / kStep);
    previous = start;
    known = true;
    return start;
}

void call_guest_function(
    AllegrexContext &ctx, std::uint32_t function, std::uint32_t argument, game::hall_equipment::ThenFunction then) {
    kernel().call_guest(ctx, function, {argument, 0u, 0u, 0u}, [then](AllegrexContext &c, std::uint32_t) { then(c); });
}

void call_guest_with_result(AllegrexContext &ctx, std::uint32_t function, const std::array<std::uint32_t, 4> &arguments,
    game::hall_meal::ResultFunction then) {
    kernel().call_guest(
        ctx, function, arguments, [then](AllegrexContext &c, std::uint32_t result) { then(c, result); });
}

// From the top of user memory, as thread stacks are, and never given back.
std::uint32_t allocate_guest_block(std::uint32_t size) {
    const std::int32_t uid = kernel().allocate_block("mhfu_hall_equipment", 1u, size, 0u);
    if (uid < 0) return 0u;
    const MemoryBlock *block = kernel().find_block(uid);
    return block != nullptr ? block->address : 0u;
}

// The key quests' tagged names, from the top of user memory as well.
std::uint32_t allocate_key_quest_block(std::uint32_t size) {
    const std::int32_t uid = kernel().allocate_block("mhfu_key_quests", 1u, size, 0u);
    if (uid < 0) return 0u;
    const MemoryBlock *block = kernel().find_block(uid);
    return block != nullptr ? block->address : 0u;
}

void present_frame(Runtime &rt) {
    load_trace::note_flip();
    // Overlays are swapped between frames; re-check before drawing the next one.
    revalidate_overlays(rt);
    // The menu, drawn below, reads the game's memory between frames.
    game::attach(rt);
    // Layered armor puts its wrappers in place here, between two frames.
    game::layered::frame(rt);
    game::item_box::frame(rt);
    game::empty_item_slot::frame(rt);
    game::gather_frame(rt);
    game::give_rarity::frame(rt);
    game::true_attack::frame(rt);
    game::true_element::frame(rt);
    game::hall_equipment::frame(rt, &call_guest_function, &allocate_guest_block);
    game::item_sets::frame(rt);
    game::pouch_page::frame(rt);
    game::hall_meal::frame(rt, &call_guest_with_result);
    game::key_quests::frame(rt.memory(), &allocate_key_quest_block, &mods::entry_bytes);
    game::sharpness::gauge_frame(settings::current().sharpness_gauge, &rt.memory());
#if defined(MHFU_HAS_RENDERER)
    if (!media().renderer || !media().renderer->available()) {
        perf::end_frame(kernel().now_us());
        return;
    }
    gpu::VulkanRenderer &renderer = *media().renderer;
    // The guest passes a VRAM offset when the high byte is zero.
    const std::uint32_t address = (media().display.framebuffer & 0xFF000000u) == 0u
        ? (media().display.framebuffer | 0x04000000u)
        : media().display.framebuffer;
    const perf::Clock::time_point present_start = perf::Clock::now();
    // The GE only draws into VRAM, so a framebuffer in main memory was
    // written by the CPU (the movie player's sceJpegCsc) and has to be shown
    // from memory.
    constexpr std::uint32_t kPixelFormat8888 = 3u;
    const DisplayState &display = media().display;
    if ((address & 0x1F000000u) != kEdramBase && display.pixel_format == kPixelFormat8888) {
        const std::size_t bytes = static_cast<std::size_t>(display.buffer_width) * display.height * 4u;
        renderer.upload_frame(
            address, rt.memory().raw_pointer(address, bytes), display.width, display.height, display.buffer_width);
    }
    const std::optional<game::EyePose> pose = damage_numbers_pose(rt);
    game::hit_trace::set_clock([] { return kernel().now_us() / 1000u; });
    share_damage_numbers(rt);
    const settings::DamageNumbers numbers = settings::current().damage_numbers;
    game::damage_numbers_frame(numbers != settings::DamageNumbers::Off, &rt.memory(), pose,
        numbers == settings::DamageNumbers::Mine ? game::Whose::Mine : game::Whose::Everyone);
    if (game::body_trace::enabled()) {
        static game::body_trace::Tracker bodies{game::body_trace::watch_offset()};
        bodies.frame(rt.memory(), std::cout);
        if (const std::optional<std::uint32_t> watch = bodies.take_watch()) psprecomp::set_write_watch(*watch, 4u);
    }
    // The draws of this frame have already named the camera they used, so the
    // marker matches the picture about to be shown, aim camera included.
    ui::set_lock_on_marker(settings::current().lock_on_marker ? camera::lock_on_marker(rt.memory()) : std::nullopt);
    if (!settings::current().lock_on) {
        ui::set_hunt_portraits({});
    } else {
        camera::refresh_hunt_icons(rt.memory());
        const camera::LockOnStatus locked = camera::lock_on_status();
        // Read every frame, so the order of the icons still starts again with
        // a new quest. They are shown only with the quest HUD: the clock is
        // gone once the screens after a quest begin. While the player hides
        // the HUD its clock is not drawn, so the icon stays as before.
        // MHFU_HUNT_ICON_WITH_HUD=0 shows them whenever a quest runs.
        static const bool with_hud = [] {
            const char *value = std::getenv("MHFU_HUNT_ICON_WITH_HUD");
            return value == nullptr || std::string_view(value) != "0";
        }();
        std::vector<camera::HuntMonster> monsters = camera::hunt_monsters(rt.memory());
        if (with_hud && !gpu::hud::active() && !renderer.quest_hud_up()) monsters.clear();
        ui::set_hunt_portraits(camera::place_hunt_portraits(monsters, locked.target, locked.locked));
    }
    std::optional<game::GatherPose> gather_pose;
    if (pose)
        gather_pose =
            game::GatherPose{pose->eye.x, pose->eye.y, pose->eye.z, pose->look_at.x, pose->look_at.y, pose->look_at.z};
    // The mark fades, glides between targets, and follows frame interpolation
    // (game::GatherMotion, gpu/ui_motion.hpp). MHFU_GATHER_ICON_SMOOTH=0
    // draws it where each game frame puts it.
    static const bool gather_smooth = [] {
        const char *value = std::getenv("MHFU_GATHER_ICON_SMOOTH");
        return value == nullptr || std::string_view(value) != "0";
    }();
    static game::GatherMotions gather_motions;
    std::vector<game::GatherMark> gather =
        game::gather_marks(rt.memory(), gather_pose, gather_smooth ? &gather_motions : nullptr);
    // A mark shows the item a point gives, or the monster being carved
    // (game/gather_art.hpp); the sheets they come from are decoded here.
    for (game::GatherMark &mark : gather) {
        if (mark.glyph == game::GatherGlyph::Carve) {
            const camera::Vec3 body{mark.world[0], mark.world[1], mark.world[2]};
            if (const std::optional<std::uint8_t> species = camera::carved_monster(rt.memory(), body)) {
                mark.art = game::GatherArt::of_monster(*species);
                camera::refresh_hunt_icons(rt.memory());
                // MHFU_TRACE_GATHER: whether the icon sheet could be read, once a change.
                static const bool trace = std::getenv("MHFU_TRACE_GATHER") != nullptr;
                static int told = -1;
                const int ready = camera::hunt_icon_sheet().ready ? 1 : 0;
                if (trace && ready != told) {
                    told = ready;
                    std::cout << "[gather] carving species " << static_cast<unsigned>(*species) << ", icon sheet "
                              << (ready ? "ready" : "not decoded") << std::endl;
                }
            }
        } else if (mark.art.kind == game::GatherArt::Kind::Item) {
            mark.icon = game::item_art(rt.memory(), mark.art.item);
        }
        // Anything without a picture shows the game's red ?.
        mark.unknown = game::icon_art(rt.memory(), game::kUnknownIcon, game::kUnknownTint);
    }
    if (!gather.empty()) {
        game::refresh_item_atlas(rt.memory());
        game::refresh_button_glyphs(rt.memory());
    }
    ui::set_gather_marks(std::move(gather));
    ui::draw_over_game();
    renderer.write_back_frame(rt.memory());
    // A load running fast, fast-forward, or Game speed Unlimited flips more
    // often than the display refreshes. Without this, Vsync's wait for the
    // next image holds the game to the display rate.
    renderer.set_fast_forward(fast_loading::active() || fast_forward::active() || settings::current().unthrottled);
    // The real time the frame stands for, which frame interpolation spaces
    // its presents by: that of the vblank the game's frame started from.
    const bool presented = renderer.present(address, kernel().real_time_of(frame_start_us(kernel().last_vblank_us())));
    // The frame's camera has been measured by now, so the hunt for the guest
    // variables behind it can compare RAM against it.
    probe::camera_frame(rt, media().ge.view_matrix_source());
    // The game's flip is the camera's frame: the camera update runs once
    // between two flips, however many presents interpolation adds. The stick is
    // already shaped and inverted by the input layer; its rate becomes degrees
    // over the real time since the previous flip.
    {
        static perf::Clock::time_point previous_flip = present_start;
        const float seconds = std::chrono::duration<float>(present_start - previous_flip).count();
        previous_flip = present_start;
        camera::set_rate(camera::Source::Stick, (static_cast<int>(renderer.pad().right_x) - 0x80) / 127.0f,
            (static_cast<int>(renderer.pad().right_y) - 0x80) / 127.0f);
        // A tap of Lock on (camera/lock_on.hpp) waits for the next update
        // of the follow camera, which game_camera_frame makes sure to drive.
        if (renderer.take_lock_on_press()) camera::lock_on_tap();
        camera::set_focus_camera_held(renderer.focus_camera_held());
        camera::game_camera_frame(rt);
        // The view's shape follows the picture's: the game builds its next
        // projection with the aspect ratio of the target it will draw into.
        camera::game_aspect_frame(rt, renderer.game_aspect());
        // The setting's field of view, for the camera's next update.
        camera::field_of_view_frame(rt, settings::current().field_of_view);
        camera::advance(seconds, camera::game_camera_degrees_per_second());
    }
    perf::add_render_time(perf::Clock::now() - present_start);
    // A frame ends when its image has been handed to the swapchain, or with
    // frame interpolation when the presents after it are scheduled.
    perf::end_frame(kernel().now_us(), presented);
    gpu::hud::frame(rt, renderer.frames_presented());
    if (renderer.take_hide_hud_toggle()) gpu::hud::toggle();

    // Optional frame capture, independent of the window.
    static const std::filesystem::path screenshot_dir = environment_path("MHFU_SCREENSHOT_DIR");
    static const std::uint64_t screenshot_every = [] {
        const char *text = std::getenv("MHFU_SCREENSHOT_EVERY");
        return text != nullptr ? std::strtoull(text, nullptr, 10) : 60ull;
    }();
    if (!screenshot_dir.empty() && screenshot_every != 0u && renderer.frames_presented() % screenshot_every == 0u) {
        const std::filesystem::path path =
            screenshot_dir / ("frame_" + std::to_string(renderer.frames_presented()) + ".bmp");
        if (renderer.capture_frame(path))
            std::cout << "[render] frame " << renderer.frames_presented() << " (" << renderer.draws_submitted()
                      << " draws) -> " << path_to_utf8(path) << "\n";
    }
    bool window_open = renderer.pump_events();
    // The screenshot bind (#187): the frame just presented, as drawn.
    // With MHFU_TRACE_CAPTURE, the frames after it are traced too.
    if (renderer.take_screenshot_request()) {
        (void)ui::take_screenshot();
        gpu::hud::screenshot_taken();
    }
    // The fast-forward bind as the events just pumped left it; the kernel's
    // pacing follows it from the next wait on.
    fast_forward::note_bind(renderer.pad().fast_forward);
    feed_mouse(renderer);
    camera::game_camera_anticipate_aim(rt);
    if (!window_open) {
        rt.stop("window closed");
    } else if (ui::take_quit_request()) {
        rt.stop("quit from the menu");
    } else if (!ui::menu_over_game() && ui::backup_reminder_due()) {
        // Before the game gets going: the reminder to back up the saves, with
        // the game paused as for the menu.
        renderer.pause_interpolation();
        audio::AudioSink::instance().set_paused(true);
        const bool keep_playing = ui::run_backup_reminder();
        audio::AudioSink::instance().set_paused(false);
        kernel().resync_real_time();
        perf::restart_measurement();
        if (!keep_playing) rt.stop("window closed");
    } else if (!ui::menu_over_game() && ui::menu_requested()) {
        if (ui::menu_pauses()) {
            // The menu pauses the game: guest code and emulated time stand
            // still while it runs in here, and the device stops playing.
            renderer.pause_interpolation();
            audio::AudioSink::instance().set_paused(true);
            const bool keep_playing = ui::run_menu();
            audio::AudioSink::instance().set_paused(false);
            // Resume at normal speed rather than racing to make up the pause,
            // and keep the pause out of the frame statistics.
            kernel().resync_real_time();
            perf::restart_measurement();
            if (!keep_playing) rt.stop("quit from the menu");
        } else {
            // The game keeps running, sound and pacing included; the menu is
            // drawn over each frame and takes all input until it closes.
            ui::open_menu_over_game();
        }
    }
#else
    (void)rt;
    perf::end_frame(kernel().now_us());
#endif
}

void register_display_ctrl(HleRegistrar &hle) {
    hle.add("sceDisplay", "sceDisplaySetMode", [](Runtime &, AllegrexContext &ctx) {
        media().display.mode = arg(ctx, 0);
        media().display.width = arg(ctx, 1);
        media().display.height = arg(ctx, 2);
        kernel().finish(ctx, 0u);
    });
    // The guest flipping the framebuffer is the end of a frame.
    hle.add("sceDisplay", "sceDisplaySetFrameBuf", [](Runtime &rt, AllegrexContext &ctx) {
        media().display.framebuffer = arg(ctx, 0);
        media().display.buffer_width = arg(ctx, 1);
        media().display.pixel_format = arg(ctx, 2);
        present_frame(rt);
        kernel().finish(ctx, 0u);
    });

    hle.add("sceCtrl", "sceCtrlSetSamplingCycle", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t previous = media().ctrl_cycle;
        media().ctrl_cycle = arg(ctx, 0);
        kernel().finish(ctx, previous);
    });
    hle.add("sceCtrl", "sceCtrlSetSamplingMode", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t previous = media().ctrl_mode;
        media().ctrl_mode = arg(ctx, 0);
        // The mode is recorded but not acted on: the game subtracts 128 from Lx
        // unconditionally, so reporting the neutral 0x80 is right in both modes.
        if (std::getenv("MHFU_TRACE_PAD") != nullptr && media().ctrl_mode != previous)
            std::cout << "[pad] sceCtrlSetSamplingMode " << media().ctrl_mode << "\n";
        kernel().finish(ctx, previous);
    });
    // Reading the controller buffer blocks until the next sample (vblank).
    hle.add("sceCtrl", "sceCtrlReadBufferPositive", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t address = arg(ctx, 0);
        const std::uint32_t count = std::clamp<std::uint32_t>(arg(ctx, 1), 1u, 64u);
        std::uint32_t buttons = 0u;
        std::uint8_t analog_x = 0x80u;
        std::uint8_t analog_y = 0x80u;
        std::uint8_t right_x = 0x80u;
        std::uint8_t right_y = 0x80u;
        // Cleared unless this sample is an aim push. The aim function reads
        // it; the movement stick below is unchanged.
        camera::game_camera_present_aim(0, 0);
#if defined(MHFU_HAS_RENDERER)
        if (media().renderer && media().renderer->available()) {
            // The pad as it is now, not as it was at the last flip (#8).
            // MHFU_PAD_AT_FLIP keeps the state of the flip, as before.
            static const bool at_flip = std::getenv("MHFU_PAD_AT_FLIP") != nullptr;
            if (!at_flip) media().renderer->sample_pad();
            const gpu::PadState pad = media().renderer->pad();
            buttons = pad.buttons;
            fast_loading::note_buttons(buttons != 0u);
            analog_x = pad.analog_x;
            analog_y = pad.analog_y;
            right_x = pad.right_x;
            right_y = pad.right_y;
            // Keep the game's digital commands neutral while the analog
            // camera consumes these axes: otherwise the game's one-shot
            // vertical command fires from the same push and glides the camera
            // against what the port is doing. The physical D-pad stays available.
            if (camera::game_camera_driving()) {
                right_x = 0x80u;
                right_y = 0x80u;
            } else if (camera::game_camera_aim_boost()) {
                // The direction, at full length, is shown to the aim function
                // so it steps; the wrapper then sizes that step from how far
                // the stick is pushed. Bytes 8 and 9 stay the movement stick,
                // and with the stick idle the mouse's direction stands in.
                // The mouse has no length of its own: its step is sized later
                // from its degrees.
                int dx = static_cast<int>(right_x) - 0x80;
                int dy = static_cast<int>(right_y) - 0x80;
                const bool stick = dx != 0 || dy != 0;
                // A bow aims only from the D-pad, in code no wrapper reaches:
                // the stick is pressed there in the sample (game_camera.hpp).
                if (stick) buttons |= camera::game_camera_aim_dpad(dx, dy);
                if (!stick) {
                    if (const auto mouse = camera::game_camera_mouse_aim()) {
                        dx = static_cast<int>(std::lround(mouse->x * 127.0f));
                        dy = static_cast<int>(std::lround(mouse->y * 127.0f));
                    }
                }
                const float length = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (length > 0.0f) {
                    const int sx = static_cast<int>(std::lround(dx * 127.0f / length));
                    const int sy = static_cast<int>(std::lround(dy * 127.0f / length));
                    if (stick)
                        camera::game_camera_present_aim(
                            sx, sy, static_cast<float>(dx) / 127.0f, static_cast<float>(dy) / 127.0f);
                    else
                        camera::game_camera_present_aim(sx, sy);
                }
                // The aim wrapper shows this push to the aim function. The
                // second stick stays centred: its up and down are the game's
                // own camera commands, and a bowgun aims from the D-pad bits
                // written for that call, not from this stick.
                right_x = 0x80u;
                right_y = 0x80u;
            } else if (right_x == 0x80u && right_y == 0x80u &&
                settings::current().right_stick == settings::RightStick::Camera) {
                // The game's own camera: the mouse switches its turn on while
                // it moves sideways. Not in the D-pad mode, where the same
                // bits move cursors in the game's menus.
                if (const int turn = camera::game_camera_mouse_stock_turn()) right_x = turn > 0 ? 0xFFu : 0x01u;
            }
        }
#endif
        // The hold repeats ○ on every point ○ gathers, mining and bug points
        // included; then ○ on a mining or bug point with the tool in the
        // pouch is the □ the game mines with, the tool selected.
        buttons = game::gather_hold(buttons, rt.memory());
        buttons = game::gather_circle(buttons, rt.memory());
        // A stick push during the hold walks the hunter off the point, or
        // out of the carve, and the game drops the animation.
        if (game::gather_hold_centres_stick()) {
            analog_x = 0x80u;
            analog_y = 0x80u;
        }
        {
            static const bool trace_reads = std::getenv("MHFU_TRACE_PAD_READS") != nullptr;
            if (trace_reads)
                std::cout << "[pad-read] " << kernel().now_us() << " count " << count << " buttons 0x" << std::hex
                          << buttons << std::dec << std::endl;
        }
        auto &memory = rt.memory();
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = address + i * 16u;
            memory.store32(entry, static_cast<std::uint32_t>(kernel().now_us()));
            memory.store32(entry + 4u, buttons);
            memory.store8(entry + 8u, analog_x);
            memory.store8(entry + 9u, analog_y);
            // Bytes 10 and 11 are the HD release's second stick, not padding.
            // Leaving them zero reads as a full diagonal deflection and turns
            // the camera every frame; 0x80 is the centre the guest tests for.
            memory.store8(entry + 10u, right_x);
            memory.store8(entry + 11u, right_y);
            for (std::uint32_t j = 12u; j < 16u; ++j) memory.store8(entry + j, 0u);
        }
        WaitState wait{};
        wait.type = WaitType::VBlank;
        kernel().block(ctx, wait, count);
    });
}

void register_ge(HleRegistrar &hle) {
    hle.add(
        "sceGe_user", "sceGeEdramGetAddr", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, kEdramBase); });
    hle.add(
        "sceGe_user", "sceGeEdramGetSize", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, kEdramSize); });
    hle.add("sceGe_user", "sceGeEdramSetAddrTranslation",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    hle.add("sceGe_user", "sceGeSetCallback", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t data = arg(ctx, 0);
        auto &memory = rt.memory();
        const std::int32_t id = media().next_ge_callback++;
        media().ge_callbacks[id] = GeCallback{
            memory.load32(data), memory.load32(data + 4u), memory.load32(data + 8u), memory.load32(data + 12u)};
        kernel().finish(ctx, static_cast<std::uint32_t>(id));
    });
    hle.add("sceGe_user", "sceGeUnsetCallback", [](Runtime &, AllegrexContext &ctx) {
        media().ge_callbacks.erase(static_cast<std::int32_t>(arg(ctx, 0)));
        kernel().finish(ctx, 0u);
    });
    const auto enqueue = [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t id = media().next_ge_list++;
        GeList list{};
        list.pc = arg(ctx, 0) & 0x0FFFFFFFu;
        list.stall = arg(ctx, 1) & 0x0FFFFFFFu;
        list.callback = static_cast<std::int32_t>(arg(ctx, 2));
        media().ge_lists[id] = std::move(list);
        perf::count_display_list();
        run_ge_list(rt, id);
        kernel().finish(ctx, id);
    };
    hle.add("sceGe_user", "sceGeListEnQueue", enqueue);
    hle.add("sceGe_user", "sceGeListEnQueueHead", enqueue);
    hle.add("sceGe_user", "sceGeListUpdateStallAddr", [](Runtime &rt, AllegrexContext &ctx) {
        auto found = media().ge_lists.find(arg(ctx, 0));
        if (found == media().ge_lists.end()) {
            kernel().finish(ctx, error::kIllegalArgument);
            return;
        }
        found->second.stall = arg(ctx, 1) & 0x0FFFFFFFu;
        run_ge_list(rt, arg(ctx, 0));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceGe_user", "sceGeListSync", [](Runtime &, AllegrexContext &ctx) {
        auto found = media().ge_lists.find(arg(ctx, 0));
        // 0 = completed, 2 = still stalled (drawing).
        const std::uint32_t state = found == media().ge_lists.end() || found->second.done ? 0u : 2u;
        kernel().finish(ctx, state);
    });
    hle.add("sceGe_user", "sceGeDrawSync", [](Runtime &, AllegrexContext &ctx) {
        std::erase_if(media().ge_lists, [](const auto &item) { return item.second.done; });
        kernel().finish(ctx, 0u);
    });
    hle.add("sceGe_user", "sceGeBreak", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    hle.add("sceGe_user", "sceGeContinue", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
}

// MHFU_TRACE_AUDIO_CALLS: a line for each channel reserved, released or
// changed, and for each output that waits longer than its own buffer lasts:
// one draining what the channel had queued rather than pacing a stream.
bool trace_audio_calls() {
    static const bool enabled = std::getenv("MHFU_TRACE_AUDIO_CALLS") != nullptr;
    return enabled;
}

void log_audio_call(std::uint32_t channel, const std::string &what) {
    const Thread *thread = kernel().current_thread();
    std::cout << "[audio-call] ch=" << channel << ' ' << what << " thread=" << (thread ? thread->name : "?")
              << " at=" << static_cast<double>(kernel().now_us()) / 1000.0 << "ms\n";
}

// sceAudioOutput*Blocking returns once the previously submitted buffer has
// drained, which is what paces the guest's audio thread: the channel's queue
// is tracked in virtual time and the thread waits on the kernel clock, never
// on the host. The samples themselves go straight to the sink.
void audio_output(Runtime &rt, AllegrexContext &ctx) {
    const std::uint32_t channel = arg(ctx, 0);
    if (channel >= media().audio.size()) {
        kernel().finish(ctx, 0x80260002u);
        return;
    }
    AudioChannel &state = media().audio[channel];
    const std::uint32_t left = arg(ctx, 1);
    const std::uint32_t right = arg(ctx, 2);
    const std::uint32_t buffer = arg(ctx, 3);
    const std::uint32_t frames = state.samples;
    // Format 0x10 is mono: one sample per frame instead of a stereo pair.
    const bool mono = (state.format & 0x10u) != 0u;

    if (buffer != 0u && frames != 0u) {
        static std::vector<std::int16_t> staging;
        if (audio::stage_guest_pcm(rt.memory(), buffer, frames, mono, staging)) {
            // How loud the buffer is after the channel's volume, worked out
            // as the sink mixes it (0x8000 is full volume): 0 means the sink
            // would add nothing but zeros.
            const std::int32_t gains[2] = {static_cast<std::int32_t>(std::min<std::uint32_t>(left, 0x8000u)),
                static_cast<std::int32_t>(std::min<std::uint32_t>(right, 0x8000u))};
            int peak = 0;
            for (std::size_t i = 0; i < staging.size(); ++i)
                peak = std::max(peak, std::abs((static_cast<std::int32_t>(staging[i]) * gains[i & 1u]) >> 15));
            load_trace::note_audio_peak(peak);
            // While a load runs faster than real time its silence is dropped:
            // played, it would pile up faster than the device plays it. So is
            // everything while the player fast-forwards, or Game speed is
            // Unlimited: the sound is muted rather than sped up. The channel's
            // cursor stays where it was and catches up with the device when
            // sound comes back.
            const bool silent_load = fast_loading::note_audio(peak);
            if (!silent_load && !fast_forward::active() && !settings::current().unthrottled)
                audio::AudioSink::instance().mix(state.cursor, staging.data(), frames, left, right);
        } else {
            log_once("audio-buffer", "[audio] output buffer is not a single mapped range; dropping it");
        }
    }

    // MHFU_OLD_AUDIO_DRAIN: a short buffer that drains the channel waits for
    // the longer ones queued before it too, as before (audio/channel_queue.hpp).
    static const bool wait_for_drain = std::getenv("MHFU_OLD_AUDIO_DRAIN") != nullptr;
    const std::uint64_t now = kernel().now_us();
    const std::uint64_t queued = state.queue.queued_until_us > now ? state.queue.queued_until_us - now : 0u;
    const std::uint64_t buffer_us = static_cast<std::uint64_t>(frames) * 1'000'000u / kAudioSampleRate;
    const std::uint64_t wait_us = audio::queue_output(state.queue, now, frames, kAudioSampleRate, wait_for_drain);
    if (trace_audio_calls() && queued > buffer_us)
        log_audio_call(channel,
            "output frames=" + std::to_string(frames) + (buffer == 0u ? " (no buffer)" : "") + " behind " +
                std::to_string(static_cast<double>(queued) / 1000.0) + "ms waits " +
                std::to_string(static_cast<double>(wait_us) / 1000.0) + "ms");
    if (wait_us != 0u)
        kernel().delay_current(ctx, wait_us, frames);
    else
        kernel().finish(ctx, frames);
}

// __sceSasCore renders one grain into a guest buffer; the guest then hands that
// buffer to a sceAudio channel itself, so nothing here reaches the sink.
void sas_render(Runtime &rt, std::uint32_t core, std::uint32_t output, bool mix, std::uint32_t left_volume,
    std::uint32_t right_volume) {
    audio::SasCore &sas = audio::sas_core(core);
    const std::size_t frames = sas.grain();
    static std::vector<std::int16_t> staging;
    staging.resize(frames * 2u);
    sas.render(rt.memory(), staging.data(), frames);

    std::uint8_t *destination = rt.memory().raw_pointer(output, frames * 4u);
    if (destination == nullptr) {
        log_once("sas-output", "[sas] output buffer is not a single mapped range; dropping the grain");
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t side = 0; side < 2u; ++side) {
            const std::size_t index = frame * 2u + side;
            std::int32_t value = staging[index];
            if (mix) {
                const std::uint32_t gain = side == 0u ? left_volume : right_volume;
                value = (value * static_cast<std::int32_t>(std::min(gain, 0x1000u))) >> 12;
                value += static_cast<std::int16_t>(destination[index * 2u] | (destination[index * 2u + 1u] << 8));
                value = std::clamp(value, -32768, 32767);
            }
            destination[index * 2u] = static_cast<std::uint8_t>(value);
            destination[index * 2u + 1u] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) >> 8u);
        }
    }
}

void register_audio(HleRegistrar &hle) {
    audio::AudioSink::instance().initialize();

    hle.add("sceAudio", "sceAudioChReserve", [](Runtime &, AllegrexContext &ctx) {
        auto channel = static_cast<std::int32_t>(arg(ctx, 0));
        auto &channels = media().audio;
        if (channel < 0) {
            channel = -1;
            for (std::size_t i = 0; i < channels.size(); ++i) {
                if (!channels[i].reserved) {
                    channel = static_cast<std::int32_t>(i);
                    break;
                }
            }
        }
        if (channel < 0 || channel >= static_cast<std::int32_t>(channels.size()) ||
            channels[static_cast<std::size_t>(channel)].reserved) {
            kernel().finish(ctx, 0x80260002u);
            return;
        }
        channels[static_cast<std::size_t>(channel)] = AudioChannel{true, arg(ctx, 1), arg(ctx, 2), {}, 0u};
        if (trace_audio_calls())
            log_audio_call(static_cast<std::uint32_t>(channel),
                "reserve frames=" + std::to_string(arg(ctx, 1)) + " format=" + std::to_string(arg(ctx, 2)));
        kernel().finish(ctx, static_cast<std::uint32_t>(channel));
    });
    hle.add("sceAudio", "sceAudioChRelease", [](Runtime &, AllegrexContext &ctx) {
        if (arg(ctx, 0) < media().audio.size()) media().audio[arg(ctx, 0)].reserved = false;
        if (trace_audio_calls()) log_audio_call(arg(ctx, 0), "release");
        kernel().finish(ctx, 0u);
    });
    hle.add("sceAudio", "sceAudioSetChannelDataLen", [](Runtime &, AllegrexContext &ctx) {
        if (arg(ctx, 0) < media().audio.size()) media().audio[arg(ctx, 0)].samples = arg(ctx, 1);
        if (trace_audio_calls()) log_audio_call(arg(ctx, 0), "length frames=" + std::to_string(arg(ctx, 1)));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceAudio", "sceAudioChangeChannelConfig", [](Runtime &, AllegrexContext &ctx) {
        if (arg(ctx, 0) < media().audio.size()) media().audio[arg(ctx, 0)].format = arg(ctx, 1);
        if (trace_audio_calls()) log_audio_call(arg(ctx, 0), "format " + std::to_string(arg(ctx, 1)));
        kernel().finish(ctx, 0u);
    });
    hle.add(
        "sceAudio", "sceAudioChangeChannelVolume", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    // How much of what the channel was given is still to play, in samples.
    hle.add("sceAudio", "sceAudioGetChannelRestLength", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t channel = arg(ctx, 0);
        if (channel >= media().audio.size()) {
            kernel().finish(ctx, 0x80260002u);
            return;
        }
        const std::uint64_t now = kernel().now_us();
        const AudioChannel &state = media().audio[channel];
        const std::uint64_t queued_until = state.queue.queued_until_us;
        const std::uint64_t remaining = queued_until > now ? queued_until - now : 0u;
        kernel().finish(ctx, static_cast<std::uint32_t>(remaining * kAudioSampleRate / 1'000'000u));
    });
    hle.add("sceAudio", "sceAudioOutputPannedBlocking", audio_output);
    hle.try_add("sceAudio", "sceAudioOutputPanned", audio_output);

    hle.add("sceSasCore", "__sceSasInit", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).init(arg(ctx, 1), arg(ctx, 2), arg(ctx, 3));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetVoice", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).set_voice(arg(ctx, 1), arg(ctx, 2), arg(ctx, 3), arg(ctx, 4) != 0u);
        kernel().finish(ctx, 0u);
    });
    hle.try_add("sceSasCore", "__sceSasSetVoicePCM", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0))
            .set_voice_pcm(arg(ctx, 1), arg(ctx, 2), arg(ctx, 3), static_cast<std::int32_t>(arg(ctx, 4)));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetPitch", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).set_pitch(arg(ctx, 1), arg(ctx, 2));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetVolume", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0))
            .set_volume(arg(ctx, 1), static_cast<std::int32_t>(arg(ctx, 2)), static_cast<std::int32_t>(arg(ctx, 3)));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetSimpleADSR", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).set_simple_adsr(arg(ctx, 1), arg(ctx, 2), arg(ctx, 3));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetKeyOn", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).key_on(arg(ctx, 1));
        kernel().finish(ctx, 0u);
    });
    hle.add("sceSasCore", "__sceSasSetKeyOff", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).key_off(arg(ctx, 1));
        kernel().finish(ctx, 0u);
    });
    hle.try_add("sceSasCore", "__sceSasSetPause", [](Runtime &, AllegrexContext &ctx) {
        audio::sas_core(arg(ctx, 0)).set_pause(arg(ctx, 1), arg(ctx, 2) != 0u);
        kernel().finish(ctx, 0u);
    });
    hle.try_add("sceSasCore", "__sceSasGetEnvelopeHeight", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish(ctx, static_cast<std::uint32_t>(audio::sas_core(arg(ctx, 0)).envelope_height(arg(ctx, 1))));
    });
    // Reverb is not modelled, so the sends are accepted and dropped.
    for (const char *name : {"__sceSasRevType", "__sceSasRevParam", "__sceSasRevEVOL", "__sceSasRevVON"})
        hle.add("sceSasCore", name, [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    hle.add("sceSasCore", "__sceSasGetOutputmode",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, audio::sas_core(arg(ctx, 0)).output_mode()); });
    hle.add("sceSasCore", "__sceSasGetEndFlag",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, audio::sas_core(arg(ctx, 0)).end_flag()); });
    hle.add("sceSasCore", "__sceSasCore", [](Runtime &rt, AllegrexContext &ctx) {
        sas_render(rt, arg(ctx, 0), arg(ctx, 1), false, 0u, 0u);
        kernel().finish(ctx, 0u);
    });
    hle.try_add("sceSasCore", "__sceSasCoreWithMix", [](Runtime &rt, AllegrexContext &ctx) {
        sas_render(rt, arg(ctx, 0), arg(ctx, 1), true, arg(ctx, 2), arg(ctx, 3));
        kernel().finish(ctx, 0u);
    });
}

} // namespace

void initialize_renderer() {
#if defined(MHFU_HAS_RENDERER)
    (void)ensure_renderer();
#else
    std::cout << "Renderer: not built\n";
#endif
}

void register_media(HleRegistrar &hle) {
    initialize_renderer();
    register_display_ctrl(hle);
    register_ge(hle);
    register_audio(hle);
}

#if defined(MHFU_HAS_RENDERER)
gpu::VulkanRenderer *active_renderer() {
    return media().renderer && media().renderer->available() ? media().renderer.get() : nullptr;
}

gpu::VulkanRenderer *ensure_renderer() {
    static bool tried = false;
    if (tried) return active_renderer();
    tried = true;
    if (std::getenv("MHFU_NO_RENDER") != nullptr) {
        std::cout << "Renderer: disabled by MHFU_NO_RENDER\n";
        return nullptr;
    }
    auto renderer = std::make_unique<gpu::VulkanRenderer>();
    std::string error;
    gpu::RendererConfig config;
    // Tells windows apart when several instances run side by side, e.g. two
    // players testing ad hoc play on one machine.
    if (const std::optional<std::string> title = environment_utf8("MHFU_WINDOW_TITLE"); title && !title->empty())
        config.title = *title;
    if (!renderer->initialize(config, error)) {
#if defined(MHFU_ANDROID_APP)
        // A phone would run on with sound and a black screen, which tells
        // the player nothing: say what failed, on which GPU, driver and
        // phone, so a screenshot of the dialog is a report, and offer the
        // log; then close.
        std::cerr << "Renderer: unavailable (" << error << ")\n";
        const std::string device = renderer->device_summary();
        const std::string text = "Pokee cannot draw on this device's GPU:\n\n" + error + "\n\n" +
            (device.empty() ? std::string("No GPU chosen yet") : device) + "\n" + android::system_summary() + "\n" +
            std::string(kPokeeVersion) +
            "\n\nPlease report it at github.com/LocalBBQ/Pokee/issues with a screenshot and "
            "the log.";
        android::fatal_error("Pokee: graphics error", text, 4);
#else
        std::cerr << "Renderer: unavailable (" << error << "); running headless\n";
        return nullptr;
#endif
    }
    media().renderer = std::move(renderer);
    ui::attach(*media().renderer);
    // Frame interpolation presents between flips: while the kernel waits for
    // real time, and while the game's code runs.
    kernel().set_idle_hook([](std::chrono::steady_clock::time_point wake) {
        if (gpu::VulkanRenderer *active = active_renderer()) active->present_until(wake);
    });
    kernel().set_poll_hook([] {
        if (gpu::VulkanRenderer *active = active_renderer()) active->present_due();
    });
    return media().renderer.get();
}
#endif

} // namespace mhfu
