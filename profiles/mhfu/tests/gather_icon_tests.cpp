// Gather marks: the game's own radius, the area list, and the picture.
// No game data. Guest memory is a stand-in.
#include "game/gather_icon.hpp"
#include "gpu/ui_motion.hpp"
#include "settings/settings.hpp"

#include "psprecomp/guest_memory.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace mhfu::settings {
Settings &current() {
    static Settings settings;
    return settings;
}
} // namespace mhfu::settings

namespace {
using namespace mhfu::game;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_float(psprecomp::GuestMemory &memory, std::uint32_t address, float value) {
    memory.store32(address, std::bit_cast<std::uint32_t>(value));
}

void plant_words(psprecomp::GuestMemory &memory) {
    constexpr std::uint32_t kTaskSlot = 0x09A5F300u;
    constexpr char kName[] = "game_task.ovl";
    memory.store32(kTaskSlot, 0x336F574Du);
    memory.store32(kTaskSlot + 8u, kTaskSlot);
    for (std::size_t i = 0; i < sizeof(kName); ++i)
        memory.store8(kTaskSlot + 32u + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(kName[i]));
    for (const GatherWord &word : gather_checked_words()) memory.store32(word.address, word.instruction);
}

// One 24-byte record, then the -1 that ends the list.
void plant_node(psprecomp::GuestMemory &memory, std::uint32_t address, float x, float y, float z, float radius,
    std::uint16_t id, std::uint16_t kind) {
    write_float(memory, address, x);
    write_float(memory, address + 4u, y);
    write_float(memory, address + 8u, z);
    write_float(memory, address + 12u, radius);
    memory.store16(address + 16u, id);
    memory.store16(address + 20u, kind);
    write_float(memory, address + 24u, -1.0f);
}

void test_the_radius() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t list = 0x09000000u;
    plant_node(memory, list, 10.0f, 0.0f, 20.0f, 50.0f, 7u, 0u);
    plant_node(memory, list + 24u, 1000.0f, 0.0f, 1000.0f, 10.0f, 8u, 3u);
    write_float(memory, list + 48u, -1.0f);

    const std::optional<GatherNode> on_it = gather_from_list(memory, list, 10.0f, 0.0f, 20.0f);
    check(on_it && on_it->address == list && on_it->id == 7u && on_it->kind == 0u,
        "standing on a node gathers that node");
    const std::optional<GatherNode> edge = gather_from_list(memory, list, 60.0f, 0.0f, 20.0f);
    check(edge && edge->address == list, "the radius is still inside");
    check(!gather_from_list(memory, list, 60.1f, 0.0f, 20.0f), "just past the radius is not");
    check(
        gather_from_list(memory, list, 10.0f, -200.0f, 20.0f) && !gather_from_list(memory, list, 10.0f, -200.1f, 20.0f),
        "200 below the node is the low edge");
    check(gather_from_list(memory, list, 10.0f, 99.9f, 20.0f) && !gather_from_list(memory, list, 10.0f, 100.0f, 20.0f),
        "100 above the node is already outside");

    write_float(memory, list + 12u, 0.0f);
    check(!gather_from_list(memory, list, 10.1f, 0.0f, 20.0f) && gather_from_list(memory, list, 10.0f, 0.0f, 20.0f),
        "a zero radius matches only the node's own point");
    write_float(memory, list + 12u, 50.0f);

    write_float(memory, list, 0.0f);
    write_float(memory, list + 8u, 0.0f);
    write_float(memory, list + 12u, 1.0f);
    const std::optional<GatherNode> second = gather_from_list(memory, list, 1000.0f, 0.0f, 1000.0f);
    check(second && second->address == list + 24u && second->kind == 3u, "a miss walks on to the next record");

    write_float(memory, list, -1.0f);
    check(!gather_from_list(memory, list, -1.0f, 0.0f, 0.0f), "x of -1 ends the list and is not a node");
    plant_node(memory, list, 0.0f, 0.0f, 0.0f, 5.0f, 1u, 0u);
    memory.store32(list + 4u, 0x7FC00000u);
    check(!gather_from_list(memory, list, 0.0f, 0.0f, 0.0f), "a non-finite height stops the walk");
}

void test_unmapped() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    check(!gather_code_matches(memory) && !gather_node(memory), "without the quest overlay there is no node");
}

void test_the_area_list() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t base = 0x09500000u;
    constexpr std::uint32_t kGatherObject = 0x08A62D9Cu;
    constexpr std::uint32_t kGameSysPointer = 0x089CC558u;
    constexpr std::uint32_t kStageTable = 0x09CE4808u;
    memory.store32(kGameSysPointer, gamesys);
    memory.store8(gamesys + 0x6AF10u, 2u);
    memory.store32(kStageTable + 16u, 2u);
    memory.store32(kStageTable + 20u, ids);
    memory.store16(ids, 4u);
    memory.store16(ids + 2u, 9u);
    memory.store32(kGatherObject, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 24u);
    memory.store32(areas + 4u, base);
    plant_node(memory, base + 24u, 1.0f, 2.0f, 3.0f, 5.0f, 1u, 0u);

    const std::optional<std::uint32_t> list = gather_list_for(memory, 9u);
    check(list && *list == base + 24u, "the stage id picks that area, plus the object's bias");
    check(!gather_list_for(memory, 3u), "a stage the table does not name has no list");
    memory.store32(areas + 4u, 0u);
    check(!gather_list_for(memory, 9u), "an empty area slot has no list");
}

void test_words_and_the_hunter() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 2u, 4u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, 0.0f);
    memory.store16(hunter + 0x29Au, 4u);

    check(!gather_node(memory), "without the game's instructions there is no node");
    plant_words(memory);
    const std::optional<GatherNode> found = gather_node(memory);
    check(found && found->address == nodes && found->kind == 4u, "the followed hunter gathers the node in range");
    memory.store32(gather_checked_words()[0].address, 0u);
    check(!gather_code_matches(memory) && !gather_node(memory), "a different instruction stays out");
}

GatherPicture picture() {
    GatherPicture out;
    out.projection[0] = out.projection[5] = out.projection[10] = 1.0f;
    out.projection[11] = -1.0f;
    out.x_scale = 240.0f;
    out.x_offset = 240.0f;
    out.y_scale = -136.0f;
    out.y_offset = 136.0f;
    return out;
}

// The first of a frame's marks: the only one while one thing is in reach.
std::optional<GatherMark> first_mark(
    const psprecomp::GuestMemory &memory, const GatherPose &pose, GatherMotions *motions = nullptr) {
    const std::vector<GatherMark> marks = gather_marks(memory, pose, motions);
    if (marks.empty()) return std::nullopt;
    return marks.front();
}

void test_on_the_picture() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 1u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 5.0f, 1u, 0u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 1u);

    mhfu::settings::current().gather_icon = false;
    gather_note_draw(picture(), 100u);
    check(!first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}), "off, nothing is drawn");

    mhfu::settings::current().gather_icon = true;
    check(!first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}),
        "a frame with no world draw draws none");
    gather_note_draw(picture(), 10u);
    GatherPicture flat = picture();
    flat.projection[11] = 0.0f;
    gather_note_draw(flat, 1000u);
    const std::optional<GatherMark> at = first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f});
    check(at && std::fabs(at->at[0] - 0.5f) < 0.05f && std::fabs(at->at[1] - 0.5f) < 0.05f,
        "the mark sits above the node, and an orthographic draw is not the world");

    plant_node(memory, nodes, 0.0f, 0.0f, 10.0f, 5.0f, 1u, 0u);
    write_float(memory, hunter + 0x208u, 10.0f);
    gather_note_draw(picture(), 100u);
    check(!first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}),
        "a node behind the camera is not marked");
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1.0e-4f;
}

// The ring is drawn on the disc's edge, so half of it lies inside. The ? stays
// clear when that inner edge is still outside the glyph.
bool question_stays_clear(const GatherShape &shape) {
    return shape.radius - shape.ring * 0.5f >= shape.glyph * 0.5f;
}

void test_size() {
    const GatherShape usual = gather_mark_shape(100u);
    check(near(usual.radius, kGatherDisc) && near(usual.ring, 1.0f) && near(usual.glyph, kGatherGlyph),
        "the usual mark is an 8-pixel disc, a one-pixel ring and the font's own ?");
    const GatherShape half = gather_mark_shape(mhfu::settings::kMinGatherIconSize);
    check(near(half.radius, kGatherDisc * 0.5f) && near(half.ring, 0.5f) && near(half.glyph, kGatherGlyph * 0.5f),
        "half size keeps the usual proportions");
    const GatherShape doubled = gather_mark_shape(200u);
    check(near(doubled.radius, kGatherDisc * 2.0f) && near(doubled.ring, 2.0f) &&
            near(doubled.glyph, kGatherGlyph * 2.0f),
        "double size keeps the usual proportions");
    const GatherShape odd = gather_mark_shape(73u);
    check(near(odd.radius, kGatherDisc * 0.73f) && near(odd.glyph, kGatherGlyph * 0.73f),
        "a size the menu does not step to is still that percent");
    const GatherShape large = gather_mark_shape(mhfu::settings::kMaxGatherIconSize);
    check(question_stays_clear(usual) && question_stays_clear(half) && question_stays_clear(large),
        "the ring leaves the ? clear, small or large");
    const GatherShape huge = gather_mark_shape(1000u);
    check(near(huge.radius, kGatherDisc * 3.0f) && near(huge.ring, 3.0f), "a size past the limit clamps to it");
    const GatherShape tiny = gather_mark_shape(0u);
    check(near(tiny.radius, kGatherDisc * 0.5f) && near(tiny.ring, 0.5f), "a size under the limit clamps up");
}

constexpr std::uint32_t kCircle = 0x2000u;
constexpr std::uint32_t kSquare = 0x8000u;
constexpr std::uint32_t kLead = 0x0100u;
constexpr std::uint32_t kStickUp = 0x00100000u;
constexpr std::uint16_t kPushed = 90u; // the stick's magnitude, pushed

// The game's copy, frame by frame: what is held, what went down since the
// last frame and what came up, with the stick pushed.
class Copy {
public:
    HunterButtons operator()(std::uint32_t held) {
        const HunterButtons words{held, held & ~last_, last_ & ~held, kPushed};
        last_ = held;
        return words;
    }
    void reset() { last_ = 0u; }

private:
    std::uint32_t last_{};
};

// The presses the hunter is shown over `frames` frames, as '1' for the button
// new and '0' for not.
template <typename Frame> std::string presses(unsigned frames, std::uint32_t button, Frame frame) {
    std::string out;
    for (unsigned i = 0; i < frames; ++i) out += (frame().fresh & button) != 0u ? '1' : '0';
    return out;
}

std::string expected_presses(unsigned frames, unsigned start) {
    std::string out;
    for (unsigned i = 0; i < frames; ++i) out += (start + i) % kGatherPressFrames == 0u ? '1' : '0';
    return out;
}

void test_holding_circle() {
    constexpr unsigned kCycle = kGatherPressFrames;
    GatherRepeat repeat;
    Copy copy;
    const auto held = [&](std::uint32_t buttons, bool ready, std::uint32_t button = kCircle) {
        HunterButtons words = copy(buttons);
        (void)repeat.apply(words, ready, button);
        return words;
    };
    const auto restart = [&] {
        repeat.reset();
        copy.reset();
    };

    const HunterButtons first = held(kCircle, true);
    check(repeat.underway() && first.held == kCircle && first.fresh == kCircle,
        "a press on a point is the hold's first press");
    check(presses(kCycle * 4u, kCircle, [&] { return held(kCircle, true); }) == expected_presses(kCycle * 4u, 1u),
        "held, ○ is new again every few frames for as long as it is held");
    const HunterButtons owned = held(kCircle | kStickUp, true);
    check(owned.held == kCircle && owned.released == 0u && owned.magnitude == 0u,
        "the hunter is shown ○ alone, with the stick centred");

    // Running in, stopping, the gather and the stance after it are all the
    // game's business. The presses do not change with any of them, so the
    // first frame the hunter is free has a press within one cycle.
    const std::string run = presses(kCycle * 30u, kCircle, [&] { return held(kCircle, true); });
    std::size_t widest = 0;
    for (std::size_t at = 0, gap = 0; at < run.size(); ++at) {
        gap = run[at] == '0' ? gap + 1u : 0u;
        widest = std::max(widest, gap);
    }
    check(widest == kCycle - 1u, "a long gather from a run keeps pressing all the way");

    restart();
    const HunterButtons away = held(kCircle, false);
    check(!repeat.underway() && away.held == kCircle && away.fresh == kCircle && away.magnitude == kPushed,
        "away from a point the words pass as the game wrote them");

    restart();
    (void)held(kCircle, true);
    const HunterButtons others = held(kCircle | kLead | kSquare, true);
    check(others.held == kCircle && others.magnitude == 0u,
        "L and another button during the hold are left off, and the stick is centred");
    // ○ can leave the resolved pad while another button is down. The hold
    // continues, and the hunter is shown ○, not that button.
    const HunterButtons dropped = held(kSquare, true);
    check(repeat.underway() && dropped.held == kCircle && (dropped.fresh & kSquare) == 0u,
        "another button with ○ missing from the pad keeps the hold");
    const HunterButtons let_go = held(0u, true);
    check(!repeat.underway() && let_go.held == 0u && let_go.magnitude == kPushed,
        "letting every button go ends the hold and frees the stick");

    restart();
    const HunterButtons bar = held(kCircle | kLead, true);
    check(!repeat.underway() && bar.held == (kCircle | kLead), "L+○ before a hold starts is left to the item bar");

    // Pressed and held on the frame before the point is in reach.
    restart();
    check(held(kCircle, false).fresh == kCircle && !repeat.underway(), "walking up with ○ held is not yet a gather");
    check(held(kCircle, true).fresh == kCircle && repeat.underway(),
        "walking onto a point with ○ still held from a press presses at once");

    // A menu was open: the copy cleared the words while ○ was pressed, and ○
    // is still held as it closes. That press was the menu's.
    restart();
    HunterButtons menu{0u, 0u, 0u, 0u};
    (void)repeat.apply(menu, true, kCircle);
    HunterButtons closed{kCircle, 0u, 0u, 0u};
    check(!repeat.apply(closed, true, kCircle) && closed.held == kCircle && closed.fresh == 0u,
        "○ held since a menu closed is not a gather");
    HunterButtons still{kCircle, 0u, 0u, 0u};
    check(!repeat.apply(still, true, kCircle), "nor while it stays held");
    HunterButtons up{0u, 0u, kCircle, 0u};
    (void)repeat.apply(up, true, kCircle);
    HunterButtons again{kCircle, kCircle, 0u, 0u};
    check(repeat.apply(again, true, kCircle) && again.fresh == kCircle, "until it is pressed again");

    // Letting go stops the presses at once.
    restart();
    (void)held(kCircle, true);
    (void)held(kCircle, true);
    check(held(0u, true).fresh == 0u, "letting go presses nothing");
    bool quiet = true;
    for (unsigned frame = 0; frame < kCycle * 2u; ++frame) quiet = quiet && held(0u, true).fresh == 0u;
    check(quiet, "after letting go, nothing is pressed");
    check(held(kCircle, true).fresh == kCircle, "pressing again is a press at once");

    // The point going out of reach ends the hold; coming back with ○ still
    // held from that press presses again at once.
    restart();
    (void)held(kCircle, true);
    (void)held(kCircle, true);
    const HunterButtons left = held(kCircle, false);
    check(!repeat.underway() && left.fresh == 0u && left.magnitude == kPushed, "leaving the point hands ○ back");
    check(held(kCircle, true).fresh == kCircle && repeat.underway(), "coming back presses again");

    // Mining and a bug net use □, pressed the same way.
    check(gather_uses_square(3u) && gather_uses_square(4u) && !gather_uses_square(0u),
        "kinds 3 and 4 are □, and plants are ○");
    restart();
    check(presses(kCycle * 4u, kSquare, [&] { return held(kSquare, true, kSquare); }) ==
                expected_presses(kCycle * 4u, 0u) &&
            repeat.button() == kSquare,
        "holding □ on a mining point presses □ again and again");
    restart();
    check(held(kSquare | kLead, true, kSquare).held == (kSquare | kLead) && !repeat.underway(),
        "L+□ before a hold starts is left to the item bar");
    restart();
    (void)held(kSquare, true, kSquare);
    const HunterButtons circle_while_mining = held(kCircle, true, kCircle);
    check(repeat.underway() && circle_while_mining.held == kSquare, "○ alone while mining keeps the □ hold");

    // △+○ beside a point or a body is an attack. The game gets both buttons.
    constexpr std::uint32_t kTriangle = 0x1000u;
    constexpr std::uint32_t kCross = 0x4000u;
    constexpr std::uint32_t kRight = 0x0200u;
    restart();
    const HunterButtons attack = held(kCircle | kTriangle, true);
    check(!repeat.underway() && attack.held == (kCircle | kTriangle) && attack.fresh == (kCircle | kTriangle) &&
            attack.magnitude == kPushed,
        "△+○ on a point is not a gather and reaches the game whole");
    restart();
    (void)held(kCircle | kTriangle, true);
    HunterButtons after_attack = copy(kCircle);
    check(!repeat.apply(after_attack, true, kCircle),
        "○ still held after the attack is not a gather until it is pressed again");
    restart();
    check(
        held(kCircle | kCross, true).held == (kCircle | kCross) && !repeat.underway(), "× with ○ is left to the game");

    // Once the hold runs, the gather comes first: △ and × are ignored.
    restart();
    (void)held(kCircle, true);
    const HunterButtons chord = held(kCircle | kTriangle, true);
    check(repeat.underway() && chord.held == kCircle, "△ pressed during a hold is kept from the hunter");
    check(held(kCircle | kCross, true).held == kCircle && repeat.underway(), "and so is ×");
    // R is the run with the weapon away: held from the start, it neither
    // blocks the hold nor reaches the hunter.
    restart();
    const HunterButtons running = held(kCircle | kRight, true);
    check(repeat.underway() && running.held == kCircle && running.fresh == kCircle,
        "○ pressed while R is held gathers, and R is kept from the hunter");
    check(presses(kCycle * 3u, kCircle, [&] { return held(kCircle | kRight, true); }) ==
            expected_presses(kCycle * 3u, 1u),
        "and keeps gathering while R stays held");
    restart();
    check(held(kSquare | kRight, true, kSquare).held == kSquare && repeat.underway(), "R with □ on a mining point too");
    restart();
    check(held(kCircle | kSquare, true).held == kCircle && repeat.underway(),
        "□ is not a chord: ○ with □ on a point still gathers");
}

void plant_tool_words(psprecomp::GuestMemory &memory) {
    for (const GatherWord &word : gather_tool_words()) memory.store32(word.address, word.instruction);
}

// The words the game's copy writes, after both settings.
class Hunter {
public:
    Hunter(psprecomp::GuestMemory &memory, std::uint32_t hunter) : memory_(memory), hunter_(hunter) {}
    HunterButtons operator()(std::uint32_t held) {
        HunterButtons words = copy_(held);
        gather_input(words, hunter_, memory_);
        return words;
    }
    void reset() { copy_.reset(); }

private:
    psprecomp::GuestMemory &memory_;
    std::uint32_t hunter_;
    Copy copy_;
};

// The hold on the hunter's words, with the searches reading guest memory.
void test_holding_on_a_point() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 2u, 0u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, 0.0f);
    memory.store16(hunter + 0x29Au, 4u);
    plant_words(memory);
    plant_tool_words(memory);
    Hunter input(memory, hunter);

    auto &settings = mhfu::settings::current();
    settings.gather_hold = false;
    settings.gather_circle = false;
    const HunterButtons off = input(kCircle);
    check(off.held == kCircle && off.fresh == kCircle && off.magnitude == kPushed,
        "hold to gather off leaves the words alone");
    (void)input(0u);
    settings.gather_hold = true;

    const HunterButtons start = input(kCircle | kSquare);
    check(start.held == kCircle && start.fresh == kCircle && start.magnitude == 0u,
        "holding ○ on the point starts a gather, and another button is left off");
    check(input(kCircle | kLead).held == kCircle, "L during the hold does not reach the hunter");
    check(presses(kGatherPressFrames, kCircle, [&] { return input(kCircle); }) ==
            expected_presses(kGatherPressFrames, 2u),
        "the presses keep going");
    const HunterButtons free = input(0u);
    check(free.held == 0u && free.magnitude == kPushed, "letting go lets the hunter move");
    check(input(kCircle).fresh == kCircle, "pressing again is a press at once");
    write_float(memory, hunter + 0x200u, 80.0f);
    const HunterButtons off_point = input(kCircle | kSquare);
    check(off_point.held == (kCircle | kSquare) && off_point.magnitude == kPushed,
        "walking off the point leaves the words as they are");
    (void)input(0u);

    // Another hunter's words, or a hunter the camera does not follow, are not
    // this hunter's.
    write_float(memory, hunter + 0x200u, 0.0f);
    HunterButtons other{kCircle, kCircle, 0u, kPushed};
    gather_input(other, 0x09780000u, memory);
    check(other.held == kCircle && other.magnitude == kPushed, "a hunter the camera does not follow is left alone");

    // Mining is kind 3 and uses □. With Gather with ○ off, the same hold
    // presses □ there.
    memory.store16(nodes + 20u, 3u);
    const HunterButtons mining = input(kSquare | kCircle);
    check(mining.held == kSquare && mining.fresh == kSquare && mining.magnitude == 0u,
        "holding □ on a mining point starts a gather, and ○ is left off");
    check(input(kSquare | kLead).held == kSquare, "L during a □ hold does not reach the hunter");
    check(presses(kGatherPressFrames, kSquare, [&] { return input(kSquare); }) ==
            expected_presses(kGatherPressFrames, 2u),
        "the □ presses keep going on a mining point");
    check(input(kCircle).held == kSquare, "○ alone while mining keeps the □ hold");
    check(input(0u).magnitude == kPushed, "letting go of □ frees the stick");
    memory.store16(nodes + 20u, 4u);
    check(input(kSquare).fresh == kSquare && input(kSquare).held == kSquare, "a bug net point also holds □");
    (void)input(0u);
    check(input(kCircle).magnitude == kPushed, "○ on a bug net point is left alone");
    (void)input(0u);
    memory.store16(nodes + 20u, 0u);
    check(input(kSquare).magnitude == kPushed, "□ on a plant point is left alone");
    (void)input(0u);

    // With Gather with ○ (the default) □ is never a gather on a quest: it is
    // the item bar's and the map's. Mining and a bug net are ○ when the pouch
    // has the tool, and the hold repeats ○ there. The hunter is shown ○: the
    // circle handler takes it as □ (test_gathering_with_circle).
    settings.gather_circle = true;
    memory.store16(nodes + 20u, 3u);
    check(input(kSquare).magnitude == kPushed, "□ on a mining point is left to the game");
    (void)input(0u);
    const HunterButtons no_pickaxe = input(kCircle);
    check(no_pickaxe.held == kCircle && no_pickaxe.magnitude == kPushed, "without a pickaxe ○ there is left alone");
    (void)input(0u);
    memory.store16(gamesys + 0x6AEA6u + 8u, 145u); // a pickaxe in slot 2
    memory.store16(gamesys + 0x6AEA6u + 10u, 1u);
    const HunterButtons pickaxe = input(kCircle | kSquare);
    check(pickaxe.held == kCircle && pickaxe.fresh == kCircle && pickaxe.magnitude == 0u,
        "with one, holding ○ starts a hold, and the hunter is shown ○, never □");
    check(!gather_slot_for(0x09AA6FC0u, hunter), "the words alone ready no slot");
    check(presses(kGatherPressFrames, kCircle, [&] { return input(kCircle); }) ==
            expected_presses(kGatherPressFrames, 1u),
        "the presses keep going as ○");
    memory.store32(hunter + kHunterFresh, kCircle); // as the copy writes it
    std::uint32_t button = 1u;
    gather_handler_searched(memory, hunter, button, 7u, 3u);
    check(button == 2u && gather_slot_for(0x09AA6FC0u, hunter) == std::optional<std::uint16_t>{2u},
        "each press the handler is given reaches its □ path with the pickaxe");
    (void)input(0u);
    memory.store16(nodes + 20u, 4u);
    check(input(kCircle).held == kCircle, "a pickaxe is no net: ○ on a bug point is not held");
    (void)input(0u);
    memory.store16(gamesys + 0x6AEA6u + 8u, 0u);
    memory.store16(gamesys + 0x6AEA6u + 10u, 0u);
    memory.store16(nodes + 20u, 0u);
    input.reset();
}

// One 32-byte carve record. +16 empty is 0xFFFF; +20 is the id; +24 the stage.
void plant_body(psprecomp::GuestMemory &memory, std::uint32_t address, float x, float y, float z, float radius,
    std::uint16_t id, std::uint16_t stage) {
    write_float(memory, address, x);
    write_float(memory, address + 4u, y);
    write_float(memory, address + 8u, z);
    write_float(memory, address + 12u, radius);
    memory.store16(address + 16u, 1u);
    memory.store16(address + 20u, id);
    memory.store16(address + 24u, stage);
}

void test_carving_a_monster() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    write_float(memory, nodes, -1.0f); // no gather point
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, 0.0f);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store32(0x09A4F1A0u, table);
    plant_body(memory, table + 4u, 0.0f, 0.0f, 0.0f, 30.0f, 9u, 4u);
    plant_words(memory);

    check(carve_in_reach(memory), "standing on a monster body can carve it");
    write_float(memory, hunter + 0x200u, 30.1f);
    check(!carve_in_reach(memory), "just past the body's radius is not a carve");
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 100.0f);
    check(!carve_in_reach(memory), "100 above the body is already outside");
    write_float(memory, hunter + 0x204u, -200.0f);
    check(carve_in_reach(memory), "200 below the body is still a carve");
    write_float(memory, hunter + 0x204u, 0.0f);
    memory.store16(hunter + 0x29Au, 9u);
    check(!carve_in_reach(memory), "a body in another area is not a carve");
    memory.store16(hunter + 0x29Au, 4u);
    memory.store16(table + 4u + 16u, 0xFFFFu);
    check(!carve_in_reach(memory), "an empty carve slot is not a body");
    memory.store16(table + 4u + 16u, 1u);
    memory.store16(table + 4u + 20u, 0xFFFFu);
    check(!carve_in_reach(memory), "a body whose id is empty is not carved");
    memory.store16(table + 4u + 20u, 9u);
    memory.store32(hunter + 1164u, 0x100u);
    check(!carve_in_reach(memory), "the carve stays off while the hunter's block bit is set");
    memory.store32(hunter + 1164u, 0u);

    // A later slot is reached when the first is another stage.
    plant_body(memory, table + 4u + 32u, 0.0f, 0.0f, 0.0f, 30.0f, 3u, 4u);
    memory.store16(table + 4u + 24u, 1u);
    check(carve_in_reach(memory), "a miss walks on to the next body");
    memory.store16(table + 4u + 24u, 4u);
    memory.store16(table + 4u + 20u, 0xFFFFu);
    check(!carve_in_reach(memory), "the first body in reach is the one the game would carve");

    auto &settings = mhfu::settings::current();
    Hunter input(memory, hunter);
    settings.gather_hold = false;
    check(input(0u).held == 0u, "turning hold to gather off clears a gather already underway");
    settings.gather_hold = true;
    memory.store16(table + 4u + 20u, 9u);
    memory.store16(table + 4u + 24u, 4u);
    const HunterButtons carve = input(kCircle);
    check(carve.fresh == kCircle && carve.magnitude == 0u, "holding ○ on a body starts a carve");
    check(presses(kGatherPressFrames, kCircle, [&] { return input(kCircle); }) ==
            expected_presses(kGatherPressFrames, 1u),
        "holding ○ on a body keeps carving");
    check(input(kSquare).held == kCircle, "□ alone while carving keeps the ○ hold");
    check(input(kCircle | 0x1000u).held == kCircle, "△ while carving is ignored: the carve comes first");
    (void)input(0u);
    const HunterButtons attack = input(kCircle | 0x1000u);
    check(attack.held == (kCircle | 0x1000u) && attack.magnitude == kPushed,
        "△+○ pressed beside a body is an attack: the words pass and the stick moves");
    check(input(kCircle | 0x1000u).held == (kCircle | 0x1000u), "and stays an attack while held");
    (void)input(0u);
    write_float(memory, hunter + 0x200u, 80.0f);
    check(input(kCircle).magnitude == kPushed, "off the body ○ is left as it is");
    (void)input(0u);
}

// The village: lobby_task in the task slot and game_sub beside it, with the
// farm's instructions in place.
void plant_village(psprecomp::GuestMemory &memory) {
    const auto overlay = [&](std::uint32_t slot, const char *name, std::size_t length) {
        memory.store32(slot, 0x336F574Du);
        memory.store32(slot + 8u, slot);
        for (std::size_t i = 0; i <= length; ++i)
            memory.store8(slot + 32u + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(name[i]));
    };
    overlay(0x09A5F300u, "lobby_task.ovl", 14u);
    overlay(0x09C19100u, "game_sub.ovl", 12u);
    for (const GatherWord &word : farm_checked_words()) memory.store32(word.address, word.instruction);
}

// ○ mines and catches bugs when the pouch has the tool. The hunter's words
// stay ○; the circle handler, asked with ○, takes the gather as its □ and is
// given the tool's slot. The selection never moves.
void test_gathering_with_circle() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    constexpr std::uint32_t pouch = gamesys + 0x6AEA6u;
    constexpr std::uint32_t selected = hunter + 1608u;
    constexpr std::uint16_t kPotion = 1u;
    constexpr std::uint32_t kHandlerCircle = 1u;
    constexpr std::uint32_t kHandlerSquare = 2u;
    // The handler's four reads of the selection return here.
    constexpr std::uint32_t kMiningCheck = 0x09AA6FC0u;
    constexpr std::uint32_t kKeptTool = 0x09AA723Cu;
    constexpr std::uint32_t kNetCheck = 0x09AA72B8u;
    constexpr std::uint32_t kKeptNet = 0x09AA7334u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store8(gamesys + 40u, 2u); // the local hunter is slot 2
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 7u, 3u); // a mining point
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, 0.0f);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store8(hunter + 486u, 2u);
    memory.store32(0x09A4F1A0u, table); // no body yet
    memory.store16(table + 4u + 16u, 0xFFFFu);
    plant_words(memory);
    plant_tool_words(memory);
    const auto put = [&](std::uint16_t slot, std::uint16_t item, std::uint16_t count) {
        memory.store16(pouch + slot * 4u, item);
        memory.store16(pouch + slot * 4u + 2u, count);
    };
    put(0u, kPotion, 5u);
    memory.store16(selected, 0u);
    auto &settings = mhfu::settings::current();
    settings.gather_circle = true;
    settings.gather_hold = false; // one press at a time
    Hunter input(memory, hunter);
    const auto pressed = [&](std::uint32_t held) {
        (void)input(0u);
        return input(held);
    };
    // The handler's search has found the point: what its button is after.
    const auto searched = [&](std::uint32_t button, std::uint16_t kind) {
        gather_handler_searched(memory, hunter, button, 7u, kind);
        return button;
    };

    check(!circle_gather(memory, hunter, kHandlerCircle, 3u), "without a pickaxe ○ on a mining point is the game's");
    check(searched(kHandlerCircle, 3u) == kHandlerCircle && !gather_slot_for(kMiningCheck, hunter),
        "so the handler keeps ○ and goes on to the carve search");
    check(gather_button(memory, GatherGlyph::Mining, 3u, false) == 0u, "and the mark shows no button");
    put(3u, 145u, 0u);
    check(!gather_tool_slot(memory, 3u), "a pickaxe slot with none left is no tool");

    put(3u, 145u, 1u);
    check(gather_tool_slot(memory, 3u) == std::optional<std::uint16_t>{3u}, "the pickaxe in slot 3 is found");
    check(!gather_tool_slot(memory, 4u), "a pickaxe is no bug net");
    check(gather_button(memory, GatherGlyph::Mining, 3u, false) == kCircle, "the mark shows ○");
    const HunterButtons mine = pressed(kCircle);
    check(mine.held == kCircle && mine.fresh == kCircle && !gather_slot_for(kMiningCheck, hunter),
        "the hunter's words stay ○: no □ reaches the □ dispatcher or the item handler");
    check(!circle_gather(memory, hunter, kHandlerCircle, 3u), "a call with no new ○ in the words is the game's");
    memory.store32(hunter + kHunterFresh, kCircle); // as the copy writes it
    check(circle_gather(memory, hunter, kHandlerCircle, 3u) == std::optional<std::uint16_t>{3u},
        "with a pickaxe, the handler asked with ○ takes the pickaxe's slot");
    check(circle_gather(memory, hunter, 0u, 3u) == std::optional<std::uint16_t>{3u},
        "and so does ○ from the hunter state a fishing spot's ○ leaves, which passes 0");
    check(!circle_gather(memory, hunter, kHandlerSquare, 3u), "asked with □ it is the game's own");
    check(!circle_gather(memory, hunter, kHandlerCircle, 0u), "and so is a plant");
    check(!circle_gather(memory, 0x09780000u, kHandlerCircle, 3u), "and another hunter");

    check(searched(kHandlerCircle, 3u) == kHandlerSquare, "the handler's button becomes □ after its search");
    check(gather_slot_for(kMiningCheck, hunter) == std::optional<std::uint16_t>{3u} &&
            gather_slot_for(kKeptTool, hunter) == std::optional<std::uint16_t>{3u} &&
            gather_slot_for(kNetCheck, hunter) == std::optional<std::uint16_t>{3u} &&
            gather_slot_for(kKeptNet, hunter) == std::optional<std::uint16_t>{3u},
        "the handler's four reads are given the pickaxe's slot");
    check(gather_slot_for(0x09AA6E70u, hunter) == std::optional<std::uint16_t>{3u} &&
            gather_slot_for(0x09AA74A4u, hunter) == std::optional<std::uint16_t>{3u},
        "as is any other read from inside the handler");
    check(!gather_slot_for(0x09AA74A8u, hunter) && !gather_slot_for(0x09AA6E6Cu, hunter),
        "a call from just outside it keeps the selection");
    check(!gather_slot_for(0x0883FD28u, hunter), "and so does the item bar's");
    check(!gather_slot_for(kMiningCheck, 0x09780000u), "and another hunter's");
    check(memory.load16(selected) == 0u, "and the selection stays on the potion");
    (void)input(kCircle);
    check(!gather_slot_for(kMiningCheck, hunter), "the hunter's next frame ends it");
    (void)searched(kHandlerCircle, 3u);
    std::uint32_t none = kHandlerCircle;
    gather_handler_searched(memory, hunter, none, 0xFFFFu, 3u);
    check(none == kHandlerCircle && !gather_slot_for(kMiningCheck, hunter),
        "so does a search that finds nothing, which leaves ○ alone");
    check(searched(0x00012301u, 3u) == 0x00012302u, "only the low byte the handler tests becomes □");
    check(searched(0u, 3u) == kHandlerSquare && gather_slot_for(kKeptTool, hunter) == std::optional<std::uint16_t>{3u},
        "a 0 for ○ is taken as □ with the pickaxe too");
    check(searched(3u, 3u) == 3u, "a button the handler is never given is left alone");
    (void)searched(kHandlerCircle, 0u);
    check(!gather_slot_for(kMiningCheck, hunter), "and a later search for a plant ends it");

    // The handler's code differs: the search is never wrapped (test_wrappers),
    // and the hunter's words are still ○.
    memory.store32(gather_tool_words()[0].address, 0u);
    check(pressed(kCircle).held == kCircle, "with the handler's code different, ○ is left alone");
    plant_tool_words(memory);

    put(7u, 146u, 2u);
    memory.store16(selected, 7u);
    check(gather_tool_slot(memory, 3u) == std::optional<std::uint16_t>{7u}, "a pickaxe already selected is used");
    check(searched(kHandlerCircle, 3u) == kHandlerSquare &&
            gather_slot_for(kKeptTool, hunter) == std::optional<std::uint16_t>{7u},
        "and given to the handler");
    check(gather_button(memory, GatherGlyph::Mining, 3u, false) == kCircle, "and the mark still shows ○");
    put(7u, 0u, 0u);
    memory.store16(selected, 1u);

    // A bug point takes a net. The bug path reads the selection twice: the
    // check, then the net it keeps at hunter +1684 for action 82. Both are
    // inside the handler, so both are given the net.
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 7u, 4u);
    check(!circle_gather(memory, hunter, kHandlerCircle, 4u), "a pickaxe does not catch bugs");
    put(9u, 1226u, 1u);
    check(searched(kHandlerCircle, 4u) == kHandlerSquare &&
            gather_slot_for(kNetCheck, hunter) == std::optional<std::uint16_t>{9u} &&
            gather_slot_for(kKeptNet, hunter) == std::optional<std::uint16_t>{9u} && memory.load16(selected) == 1u,
        "a bug net does: the check and the net the catch keeps are the net's slot, and the selection stays");
    check(pressed(kCircle).held == kCircle, "and the hunter's words are still ○");

    // A body ○ would carve keeps ○ a carve.
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 7u, 3u);
    plant_body(memory, table + 4u, 0.0f, 0.0f, 0.0f, 30.0f, 9u, 4u);
    check(searched(kHandlerCircle, 3u) == kHandlerCircle, "○ over a carvable body carves");
    check(gather_button(memory, GatherGlyph::Mining, 3u, true) == 0u,
        "the point's mark shows no button while the pickaxe is not selected");
    memory.store16(selected, 3u);
    check(gather_button(memory, GatherGlyph::Mining, 3u, true) == kSquare, "and □ when it is");
    memory.store16(selected, 1u);
    memory.store16(table + 4u + 16u, 0xFFFFu);

    const std::vector<GatherTarget> targets = gather_targets(memory);
    check(!targets.empty() && targets[0].button == kCircle, "the mining point's target carries ○");
    check(gather_button(memory, GatherGlyph::Plant, 0u, false) == kCircle &&
            gather_button(memory, GatherGlyph::Carve, 0u, false) == kCircle &&
            gather_button(memory, GatherGlyph::Fishing, 0u, false) == kSquare,
        "plants and carves show ○, fishing □");

    // Another console's hunter: its bar is not this pouch's.
    memory.store8(hunter + 486u, 1u);
    check(searched(kHandlerCircle, 3u) == kHandlerCircle, "a hunter that is not the local one is left alone");
    memory.store8(hunter + 486u, 2u);

    check(searched(kHandlerCircle, 3u) == kHandlerSquare, "the pickaxe is used again");
    settings.gather_circle = false;
    check(searched(kHandlerCircle, 3u) == kHandlerCircle && !gather_slot_for(kMiningCheck, hunter),
        "turning the setting off leaves ○ the game's");
    check(gather_button(memory, GatherGlyph::Mining, 3u, false) == 0u, "and the mark shows no button");
    memory.store16(selected, 3u);
    check(gather_button(memory, GatherGlyph::Mining, 3u, false) == kSquare, "until the pickaxe is selected: □");
    settings.gather_circle = true;
    settings.gather_hold = true;
    (void)input(0u);
}

// The game's copy, a stand-in: it writes the pad words it is given into the
// hunter and returns, or stops partway as a call that went back to the
// dispatcher would.
HunterButtons copy_writes{};
bool copy_partway{};
unsigned copy_calls{};
void copy_stand_in(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    ++copy_calls;
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t hunter = ctx.gpr[4];
    memory.store32(hunter + kHunterHeld, copy_writes.held);
    memory.store32(hunter + kHunterFresh, copy_writes.fresh);
    memory.store32(hunter + kHunterReleased, copy_writes.released);
    memory.store16(hunter + kHunterMagnitude, copy_writes.magnitude);
    ctx.pc = copy_partway ? 0x08900000u : ctx.gpr[31];
}

// 0x08856884 as the game has it.
void slot_stand_in(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t slot = static_cast<std::int32_t>(ctx.gpr[5]) < 25 ? ctx.gpr[5] : 0u;
    ctx.gpr[2] = ctx.gpr[4] + slot * 4u + 0x6AEA6u;
    ctx.pc = ctx.gpr[31];
}

// The gather search, a stand-in: it writes the kind where the handler asked
// (a1, sp + 78 in the handler), returns the id, or stops partway.
std::uint16_t search_kind{};
std::uint16_t search_id{};
bool search_partway{};
void search_stand_in(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    runtime.memory().store16(ctx.gpr[5], search_kind);
    ctx.gpr[2] = search_id;
    ctx.pc = search_partway ? 0x08900000u : ctx.gpr[31];
}

// The wrappers over the copy, over 0x08856884 and over the gather search, as
// the game calls them.
void test_wrappers() {
    psprecomp::Runtime runtime;
    psprecomp::GuestMemory &memory = runtime.memory();
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    constexpr std::uint32_t stack = 0x09900000u; // the handler's sp
    constexpr std::uint32_t pouch = gamesys + 0x6AEA6u;
    constexpr std::uint32_t kCopy = 0x088D61CCu;
    constexpr std::uint32_t kSlot = 0x08856884u;
    constexpr std::uint32_t kSearch = 0x09A6F490u;
    constexpr std::uint32_t kCopyReturn = 0x088DB47Cu;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store8(gamesys + 40u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, 0.0f, 30.0f, 7u, 3u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store32(0x09A4F1A0u, table);
    memory.store16(table + 4u + 16u, 0xFFFFu);
    plant_words(memory);
    plant_tool_words(memory);
    memory.store16(pouch, 1u); // a potion in slot 0, selected
    memory.store16(pouch + 2u, 5u);
    memory.store16(pouch + 12u, 145u); // a pickaxe in slot 3
    memory.store16(pouch + 14u, 1u);
    memory.store16(hunter + 1608u, 0u);
    auto &settings = mhfu::settings::current();
    settings.gather_circle = true;
    settings.gather_hold = true;

    runtime.register_function(kCopy, &copy_stand_in, "recomp_unit_copy_test");
    runtime.register_function(kSlot, &slot_stand_in, "recomp_unit_slot_test");
    runtime.register_function(kSearch, &search_stand_in, "recomp_unit_search_test");
    gather_frame(runtime);
    check(runtime.function_at(kCopy) == &copy_stand_in && runtime.function_at(kSlot) == &slot_stand_in &&
            runtime.function_at(kSearch) == &search_stand_in,
        "nothing is wrapped before the code matches");
    for (const GatherWord &word : gather_input_words()) memory.store32(word.address, word.instruction);
    gather_frame(runtime);
    check(runtime.function_at(kCopy) != &copy_stand_in, "the copy is wrapped once its code matches");
    check(runtime.function_at(kSlot) == &slot_stand_in, "the slot waits for its own code");
    for (const GatherWord &word : gather_slot_words()) memory.store32(word.address, word.instruction);
    for (int frame = 0; frame < 3; ++frame) gather_frame(runtime);
    check(runtime.function_at(kSlot) != &slot_stand_in, "and is wrapped once it matches");
    check(runtime.function_at(kSearch) == &search_stand_in, "the search waits for the handler's code");
    for (const GatherWord &word : gather_handler_words()) memory.store32(word.address, word.instruction);
    memory.store32(gather_tool_words()[0].address, 0u);
    gather_frame(runtime);
    check(runtime.function_at(kSearch) == &search_stand_in, "and for the handler's reads of the selection");
    plant_tool_words(memory);
    gather_frame(runtime);
    check(runtime.function_at(kSearch) != &search_stand_in, "and is wrapped once both match");

    // As the game calls them: the copy at 0x088DB474; the search and the slot
    // from the circle handler, with the button in s0 and the hunter in s2.
    const auto copy = [&](HunterButtons pad) {
        copy_writes = pad;
        copy_calls = 0u;
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[4] = hunter;
        ctx.gpr[31] = kCopyReturn;
        runtime.function_at(kCopy)(runtime, ctx);
        return ctx.pc;
    };
    const auto search = [&](std::uint32_t button, std::uint16_t kind, std::uint32_t return_address = 0x09AA6EF0u) {
        search_kind = kind;
        search_id = 7u;
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[4] = hunter;
        ctx.gpr[5] = stack + 78u;
        ctx.gpr[16] = button;
        ctx.gpr[18] = hunter;
        ctx.gpr[29] = stack;
        ctx.gpr[31] = return_address;
        runtime.function_at(kSearch)(runtime, ctx);
        return ctx.gpr[16];
    };
    const auto slot = [&](std::uint32_t return_address) {
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[4] = gamesys;
        ctx.gpr[5] = memory.load16(hunter + 1608u);
        ctx.gpr[18] = hunter;
        ctx.gpr[31] = return_address;
        runtime.function_at(kSlot)(runtime, ctx);
        return ctx.gpr[2];
    };
    check(copy({kCircle, kCircle, 0u, kPushed}) == kCopyReturn && copy_calls == 1u,
        "the copy runs once through its wrapper, frames after it is in place, and returns to its caller");
    check(memory.load32(hunter + kHunterHeld) == kCircle && memory.load32(hunter + kHunterFresh) == kCircle,
        "○ on the mining point stays ○ in the hunter's words");
    check(slot(0x09AA6FC0u) == pouch, "and readies no slot by itself");
    check(search(1u, 3u) == 2u, "the handler's search, asked with ○, leaves its button □");
    check(search(0u, 3u) == 2u, "and so does one asked with 0, the ○ of another hunter state");
    check(slot(0x09AA6FC0u) == pouch + 12u && slot(0x09AA723Cu) == pouch + 12u && slot(0x09AA7334u) == pouch + 12u,
        "and the handler then reads the pickaxe's slot");
    check(slot(0x0883FD28u) == pouch && memory.load16(hunter + 1608u) == 0u,
        "the item bar reads the potion, and the selection never moved");
    (void)copy({kCircle, 0u, 0u, kPushed});
    check(slot(0x09AA6FC0u) == pouch, "the next frame the handler reads the selection again");
    check(search(0u, 3u) == 0u && search(1u, 3u) == 1u && slot(0x09AA6FC0u) == pouch,
        "with no new ○ in the words, a call to the handler is the game's");
    (void)copy({kCircle, kCircle, 0u, kPushed});
    check(search(2u, 3u) == 2u && slot(0x09AA6FC0u) == pouch, "asked with □ the handler is the game's");
    check(search(1u, 0u) == 1u, "a plant leaves ○");
    check(search(1u, 3u, 0x09A70000u) == 1u && slot(0x09AA6FC0u) == pouch, "a search for another caller is left alone");
    search_partway = true;
    check(search(1u, 3u) == 1u && slot(0x09AA6FC0u) == pouch, "and so is one that stops partway");
    search_partway = false;

    // A copy that goes back to the dispatcher partway writes the words after
    // the wrapper has returned: they are left to the game.
    (void)copy({0u, 0u, kCircle, kPushed});
    copy_partway = true;
    check(
        copy({kCircle, kCircle, 0u, kPushed}) == 0x08900000u, "a copy that stops partway goes back to the dispatcher");
    copy_partway = false;
    check(memory.load32(hunter + kHunterHeld) == kCircle && slot(0x09AA6FC0u) == pouch,
        "and its words and the selection are the game's");
    (void)copy({0u, 0u, kCircle, kPushed});
}

void test_farm_spots() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t farm = gamesys + 0x6A238u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t state = 0x09800000u;
    constexpr std::uint32_t point = 0x09900000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 40u, 1u); // the local hunter is slot 1
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    memory.store16(hunter + 486u, 1u);
    memory.store32(0x09B22E70u, state);
    memory.store32(hunter + 1364u, point);
    memory.store16(point + 2u, kFarmMining);
    memory.store16(point + 22u, 1u);
    memory.store8(farm + 2u, 2u);        // two mining spots
    memory.store16(farm + 8u + 8u, 3u);  // the second has three uses
    memory.store8(farm + 3u, 1u);        // one bug thicket
    memory.store16(farm + 48u + 0u, 5u); // with five uses
    auto &settings = mhfu::settings::current();
    Hunter input(memory, hunter);
    settings.gather_hold = false;
    settings.gather_circle = true;

    check(!farm_point(memory), "nothing before the village overlays are loaded");
    plant_village(memory);
    check(farm_code_matches(memory), "the planted farm words match");
    const std::optional<FarmPoint> mining = farm_point(memory);
    check(
        mining && mining->address == point && mining->type == kFarmMining && mining->index == 1u && mining->uses == 3u,
        "the mining outcrop in reach is found with its uses");
    check(input(kCircle).held == kSquare && !gather_slot_for(0x09AA6FC0u, hunter),
        "○ at a farm outcrop is □, which takes no tool");
    (void)input(0u);
    memory.store16(farm + 8u + 8u, 0u);
    check(!farm_point(memory), "a mined-out outcrop is not a gather");
    check(input(kCircle).held == kCircle, "○ at a mined-out outcrop is left alone");
    (void)input(0u);
    memory.store16(farm + 8u + 8u, 3u);
    memory.store16(point + 22u, 2u);
    memory.store16(farm + 8u + 16u, 4u);
    check(farm_point(memory) && farm_point(memory)->uses == 4u, "an index equal to the count is still a spot");
    memory.store16(point + 22u, 3u);
    check(!farm_point(memory), "an index past the count is not");

    memory.store16(point + 2u, kFarmBugs);
    memory.store16(point + 22u, 0u);
    const std::optional<FarmPoint> bugs = farm_point(memory);
    check(bugs && bugs->type == kFarmBugs && bugs->uses == 5u, "a bug thicket reads its uses at +48");
    memory.store16(point + 22u, 1u);
    check(!farm_point(memory), "past the thicket count is not a spot");
    memory.store16(point + 22u, 0u);
    memory.store16(point + 2u, 17u);
    check(!farm_point(memory), "another farm spot is left alone");
    memory.store16(point + 2u, 1u);
    check(!farm_point(memory), "a person is left alone");
    memory.store16(point + 2u, kFarmMining);
    memory.store16(point + 22u, 1u);

    memory.store32(hunter + 1364u, 0u);
    check(!farm_point(memory), "no point in reach is no spot");
    memory.store32(hunter + 1364u, point);
    memory.store32(state, 54u);
    check(!farm_point(memory), "a gather already running is not a new one");
    memory.store32(state, 0u);
    memory.store16(state + 26u, 1u);
    check(!farm_point(memory), "nor while the state object is held");
    memory.store16(state + 26u, 0u);
    memory.store16(hunter + 486u, 0u);
    check(!farm_point(memory), "a hunter that is not the local one is not read");
    memory.store16(hunter + 486u, 1u);
    memory.store32(0x09B0F33Cu, 0u);
    check(!farm_code_matches(memory) && !farm_point(memory), "a differing word turns the farm off");
    plant_village(memory);
    memory.store8(0x09C19100u + 32u, static_cast<std::uint8_t>('d'));
    check(!farm_point(memory), "another sub overlay is not the farm's");
    plant_village(memory);
    check(farm_point(memory).has_value(), "and the farm is back");

    // The hold presses □ at the outcrop, as on a quest's mining point with
    // Gather with ○ off.
    settings.gather_hold = true;
    settings.gather_circle = false;
    const HunterButtons start = input(kSquare | kCircle);
    check(start.held == kSquare && start.fresh == kSquare && start.magnitude == 0u,
        "holding □ at the outcrop starts a gather, and ○ is left off");
    check(presses(kGatherPressFrames, kSquare, [&] { return input(kSquare); }) ==
            expected_presses(kGatherPressFrames, 1u),
        "the □ presses keep going at the outcrop");
    // While the swing runs the state object is not 0: the words pass as the
    // game wrote them and the game ignores □.
    memory.store32(state, 54u);
    const HunterButtons swing = input(kSquare);
    check(swing.held == kSquare && swing.fresh == 0u && swing.magnitude == kPushed,
        "during the swing □ reaches the hunter as held");
    memory.store32(state, 0u);
    check(input(kSquare).fresh == kSquare, "after the swing the next press is at once");
    check(presses(kGatherPressFrames, kSquare, [&] { return input(kSquare); }) ==
            expected_presses(kGatherPressFrames, 1u),
        "and the presses carry on from it");
    check(input(0u).magnitude == kPushed, "letting go of □ frees the stick");
    check(input(kCircle).held == kCircle, "○ at the outcrop is left alone");
    (void)input(0u);
    memory.store16(farm + 8u + 8u, 0u);
    check(input(kSquare).magnitude == kPushed, "□ at a mined-out outcrop is left alone");
    (void)input(0u);
    memory.store16(farm + 8u + 8u, 3u);
    // With Gather with ○ the hold also repeats ○ there, and each ○ is □.
    settings.gather_circle = true;
    const HunterButtons circle = input(kCircle);
    check(circle.held == kSquare && circle.fresh == kSquare && circle.magnitude == 0u,
        "with Gather with ○, holding ○ at the outcrop is a hold the hunter is shown as □");
    (void)input(0u);
    settings.gather_hold = false;
    check(input(kSquare).magnitude == kPushed, "hold to gather off leaves □ alone at the farm");
    settings.gather_hold = true;
    (void)input(0u);
}

// The gather mark also stands over a body or a cut tail ○ would carve.
void test_mark_over_a_carve() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    write_float(memory, nodes, -1.0f); // no gather point
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store32(0x09A4F1A0u, table);
    // Straight ahead of the camera, so its mark is the picture's centre.
    plant_body(memory, table + 4u, 0.0f, 0.0f, -10.0f, 30.0f, 9u, 4u);

    const std::optional<CarveBody> body = carve_body(memory);
    check(body && body->address == table + 4u && body->x == 0.0f && body->y == 0.0f && body->z == -10.0f,
        "the carve search gives the body's record and position");
    mhfu::settings::current().gather_icon = true;
    gather_note_draw(picture(), 100u);
    const GatherPose pose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f};
    const std::optional<GatherMark> at = first_mark(memory, pose);
    check(at && std::fabs(at->at[0] - 0.5f) < 0.05f && std::fabs(at->at[1] - 0.5f) < 0.05f,
        "with no gather point in range, the mark stands over the body that can be carved");

    // A plant in range is gathered by ○, and the circle handler never asks
    // the carve search, so the body under it has no mark.
    plant_node(memory, nodes, 5.0f, 0.0f, -10.0f, 30.0f, 1u, 0u);
    gather_note_draw(picture(), 100u);
    const std::vector<GatherMark> plant = gather_marks(memory, pose);
    check(plant.size() == 1u && plant[0].at[0] > 0.55f && plant[0].glyph == GatherGlyph::Plant,
        "a plant in range takes the only mark, and the body under it gets none");
    check(at && at->glyph == GatherGlyph::Carve, "a carve is shown as one");
    // Mining is □ with a pickaxe; ○ there goes on to the carve search, so the
    // body keeps its own mark beside the point's.
    plant_node(memory, nodes, 5.0f, 0.0f, -10.0f, 30.0f, 1u, 3u);
    gather_note_draw(picture(), 100u);
    const std::vector<GatherMark> mining = gather_marks(memory, pose);
    check(mining.size() == 2u && mining[0].glyph == GatherGlyph::Mining && mining[1].glyph == GatherGlyph::Carve,
        "a body over a mining point is marked beside it");
    check(mining.size() == 2u && mining[0].world[0] == 5.0f && mining[1].world[0] == 0.0f, "each over its own place");
    plant_node(memory, nodes, 5.0f, 0.0f, -10.0f, 30.0f, 1u, 4u);
    gather_note_draw(picture(), 100u);
    check(gather_marks(memory, pose).size() == 2u, "and over a bug net point");
    plant_node(memory, nodes, 5.0f, 0.0f, -10.0f, 30.0f, 1u, 7u);
    gather_note_draw(picture(), 100u);
    check(gather_marks(memory, pose).size() == 2u, "and over any kind but a plant");
    write_float(memory, nodes, -1.0f);

    memory.store16(table + 4u + 20u, 0xFFFFu);
    gather_note_draw(picture(), 100u);
    check(!carve_body(memory) && !first_mark(memory, pose), "a body with nothing left to carve is not marked");
    memory.store16(table + 4u + 20u, 9u);
    memory.store32(hunter + 1164u, 0x100u);
    gather_note_draw(picture(), 100u);
    check(!first_mark(memory, pose), "nor one the hunter cannot carve now");
    memory.store32(hunter + 1164u, 0u);
    write_float(memory, hunter + 0x200u, 40.0f);
    gather_note_draw(picture(), 100u);
    check(!first_mark(memory, pose), "nor one out of reach");
}

// Two monsters that fell together, or a monster and the tail cut beside it:
// each body has its own mark, and only the one ○ would carve shows ○.
void test_bodies_lying_together() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    write_float(memory, nodes, -1.0f); // no gather point
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store32(0x09A4F1A0u, table);
    for (std::uint32_t n = 0; n < 20u; ++n) memory.store16(table + 4u + n * 32u + 16u, 0xFFFFu);
    const auto slot = [&](std::uint32_t n) { return table + 4u + n * 32u; };
    plant_body(memory, slot(2u), 0.0f, 0.0f, -10.0f, 30.0f, 9u, 4u);
    plant_body(memory, slot(5u), 6.0f, 0.0f, -12.0f, 30.0f, 7u, 4u);
    plant_body(memory, slot(7u), 6.0f, 0.0f, -12.0f, 30.0f, 7u, 3u);   // another stage
    plant_body(memory, slot(8u), 200.0f, 0.0f, -12.0f, 30.0f, 7u, 4u); // out of reach

    const std::vector<CarveBody> bodies = carve_bodies(memory);
    check(bodies.size() == 2u && bodies[0].slot == 2u && bodies[0].first && bodies[1].slot == 5u && !bodies[1].first &&
            bodies[1].address == slot(5u) && bodies[1].x == 6.0f && bodies[1].z == -12.0f,
        "both bodies in reach are listed in the table's order, and the first is the one ○ carves");
    check(carve_body(memory) && carve_body(memory)->slot == 2u, "○ carves the first");

    mhfu::settings::current().gather_icon = true;
    const GatherPose pose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f};
    gather_note_draw(picture(), 100u);
    std::vector<GatherMark> marks = gather_marks(memory, pose);
    check(marks.size() == 2u && marks[0].glyph == GatherGlyph::Carve && marks[1].glyph == GatherGlyph::Carve &&
            marks[0].world[0] == 0.0f && marks[1].world[0] == 6.0f,
        "each body has a mark over it");
    check(
        marks.size() == 2u && marks[0].button == 0x2000u && marks[1].button == 0u, "and only the one ○ carves shows ○");

    // The first carved out but still lying there: the search stops at it, so
    // ○ carves nothing, and the other keeps its mark without ○.
    memory.store16(slot(2u) + 20u, 0xFFFFu);
    check(!carve_body(memory) && !carve_in_reach(memory), "with the first carved out, ○ carves nothing here");
    gather_note_draw(picture(), 100u);
    marks = gather_marks(memory, pose);
    check(marks.size() == 1u && marks[0].world[0] == 6.0f && marks[0].button == 0u,
        "the other body is still marked, without ○");
    // Once it is gone from the table, ○ carves the next.
    memory.store16(slot(2u) + 16u, 0xFFFFu);
    check(carve_body(memory) && carve_body(memory)->slot == 5u, "once the first is gone, ○ carves the next");
    gather_note_draw(picture(), 100u);
    marks = gather_marks(memory, pose);
    check(marks.size() == 1u && marks[0].button == 0x2000u, "and its mark shows ○");

    // Each body's mark has its own motion: the one carved out fades where it
    // lay, and the other stays.
    plant_body(memory, slot(2u), 0.0f, 0.0f, -10.0f, 30.0f, 9u, 4u);
    GatherMotions motions;
    for (int i = 0; i < 4; ++i) {
        gather_note_draw(picture(), 100u);
        (void)gather_marks(memory, pose, &motions);
    }
    check(motions.carves[2].shown() && motions.carves[5].shown() && !motions.carves[0].shown(),
        "each body's slot moves its own mark");
    memory.store16(slot(2u) + 20u, 0xFFFFu);
    gather_note_draw(picture(), 100u);
    marks = gather_marks(memory, pose, &motions);
    check(marks.size() == 2u && marks[0].world[0] == 0.0f && near(marks[0].alpha, 1.0f - GatherMotion::kFadeStep) &&
            marks[1].world[0] == 6.0f && near(marks[1].alpha, 1.0f) && marks[1].button == 0u,
        "the body carved out fades where it lay, and the other stays");

    // Over a plant the carve search is not asked, so neither body is marked.
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 30.0f, 1u, 0u);
    gather_note_draw(picture(), 100u);
    marks = gather_marks(memory, pose);
    check(marks.size() == 1u && marks[0].glyph == GatherGlyph::Plant, "a plant over both takes the only mark");
}

// One 24-byte spot: +2 the type, +4 x, +8 y, +12 z, +16 the radius.
void plant_spot(psprecomp::GuestMemory &memory, std::uint32_t address, std::uint16_t type, float x, float y, float z,
    float radius) {
    memory.store16(address + 2u, type);
    write_float(memory, address + 4u, x);
    write_float(memory, address + 8u, y);
    write_float(memory, address + 12u, z);
    write_float(memory, address + 16u, radius);
}

// The quest object behind 0x09A4F1A0, its stage object, and the stage's
// method +40 returning the spot table: lui v0,0x0985; jr ra; addiu v0,v0,-0x7F00.
constexpr std::uint32_t kSpotTable = 0x09848100u;
constexpr std::uint32_t kSpots = 0x09860000u;

void plant_stage(psprecomp::GuestMemory &memory) {
    constexpr std::uint32_t quest = 0x09800000u;
    constexpr std::uint32_t stage = 0x09810000u;
    constexpr std::uint32_t vtable = 0x09820000u;
    constexpr std::uint32_t method = 0x09830000u;
    memory.store32(0x09A4F1A0u, quest);
    memory.store32(quest, stage);
    memory.store32(stage, vtable);
    memory.store32(vtable + 40u, method);
    memory.store32(method, 0x3C020985u);
    memory.store32(method + 4u, 0x03E00008u);
    memory.store32(method + 8u, 0x24428100u);
    memory.store32(kSpotTable + 16u, kSpots);
    memory.store8(kSpotTable + 62u, 2u);
}

void plant_fishing_words(psprecomp::GuestMemory &memory) {
    for (const GatherWord &word : fishing_checked_words()) memory.store32(word.address, word.instruction);
}

void test_fishing_spots() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    write_float(memory, nodes, -1.0f); // no gather point
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 100.0f);
    write_float(memory, hunter + 0x204u, -40.0f);
    write_float(memory, hunter + 0x208u, 0.0f);
    memory.store16(hunter + 0x29Au, 4u);
    plant_stage(memory);
    plant_spot(memory, kSpots, 24u, 5000.0f, 0.0f, 5000.0f, 200.0f); // a supply box far off
    plant_spot(memory, kSpots + 24u, kFishingSpotType, 0.0f, -50.0f, 0.0f, 100.0f);

    const std::optional<std::uint32_t> table = spot_table(memory);
    check(table && *table == kSpotTable, "the stage's getter gives the spot table, its low half signed");
    check(!fishing_spot(memory), "without the game's instructions there is no fishing spot");
    plant_words(memory);
    check(!fishing_spot(memory), "nor with only the gather search's");
    plant_fishing_words(memory);
    const std::optional<FishingSpot> spot = fishing_spot(memory);
    check(spot && spot->address == kSpots + 24u && spot->x == 0.0f && spot->y == -50.0f && spot->z == 0.0f,
        "the hunter at the water's edge is at the fishing spot");

    write_float(memory, hunter + 0x200u, 100.1f);
    check(!fishing_spot(memory), "just past the radius is not");
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, -100.0f);
    check(fishing_spot(memory).has_value(), "50 below the spot is still in reach");
    write_float(memory, hunter + 0x204u, -100.1f);
    check(!fishing_spot(memory), "and lower is not");
    write_float(memory, hunter + 0x204u, -0.1f);
    check(fishing_spot(memory).has_value(), "just under 50 above it is in reach");
    write_float(memory, hunter + 0x204u, 0.0f);
    check(!fishing_spot(memory), "50 above it is already outside");
    write_float(memory, hunter + 0x204u, -40.0f);

    // The search gives the first spot in reach, and the bait fishes only at 23.
    plant_spot(memory, kSpots, 24u, 0.0f, -50.0f, 0.0f, 200.0f);
    const std::optional<GatherSpot> first = spot_from_table(memory, kSpotTable, 0.0f, -40.0f, 0.0f);
    check(first && first->address == kSpots && first->type == 24u, "the first spot in reach is returned");
    check(!fishing_spot(memory), "so a camp spot before the water is no fishing");
    plant_spot(memory, kSpots, 24u, 5000.0f, 0.0f, 5000.0f, 200.0f);
    memory.store8(kSpotTable + 62u, 1u);
    check(!fishing_spot(memory), "a spot past the count is not read");
    memory.store8(kSpotTable + 62u, 0x80u);
    check(!fishing_spot(memory), "a negative count is no spots");
    memory.store8(kSpotTable + 62u, 2u);
    memory.store32(kSpotTable + 16u, 0u);
    check(!fishing_spot(memory), "an area without a list has no spots");
    memory.store32(kSpotTable + 16u, kSpots);
    memory.store32(kSpots + 24u + 8u, 0x7FC00000u);
    check(!fishing_spot(memory), "a height that is not a number is skipped");
    write_float(memory, kSpots + 24u + 8u, -50.0f);
    check(fishing_spot(memory).has_value(), "and the spot is back");

    memory.store32(0x09830000u + 4u, 0u);
    check(!spot_table(memory) && !fishing_spot(memory), "a method that is not the getter is not followed");
    memory.store32(0x09830000u + 4u, 0x03E00008u);
    memory.store32(0x09800000u, 0u);
    check(!spot_table(memory), "no stage object is no table");
    memory.store32(0x09800000u, 0x09810000u);
    memory.store32(fishing_checked_words()[4].address, 0u);
    check(!fishing_spot(memory), "a differing spot instruction turns fishing off");
    check(gather_code_matches(memory), "and leaves gathering on");
    plant_fishing_words(memory);

    // The mark: the bait's icon, below a gather point and a carve.
    for (const GatherArtWord &word : gather_art_words()) memory.store32(word.address, word.instruction);
    mhfu::settings::current().gather_icon = true;
    // Straight ahead of the camera, so the mark is the picture's centre.
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, -10.0f);
    plant_spot(memory, kSpots + 24u, kFishingSpotType, 0.0f, 0.0f, -10.0f, 100.0f);
    const GatherPose pose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f};
    gather_note_draw(picture(), 100u);
    const std::optional<GatherMark> mark = first_mark(memory, pose);
    check(mark && mark->glyph == GatherGlyph::Fishing && mark->art == GatherArt::of_item(kBaitItem),
        "a fishing spot is marked with the bait");
    check(mark && std::fabs(mark->at[0] - 0.5f) < 0.05f && std::fabs(mark->at[1] - 0.5f) < 0.05f &&
            mark->world[2] == -10.0f,
        "over the spot");
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 30.0f, 1u, 0u);
    gather_note_draw(picture(), 100u);
    // The bait is the item action and its search does not read the gather
    // list, so both marks are shown, the point's first.
    const std::vector<GatherMark> both = gather_marks(memory, pose);
    check(both.size() == 2u && both[0].glyph == GatherGlyph::Plant && both[1].glyph == GatherGlyph::Fishing &&
            both[1].art == GatherArt::of_item(kBaitItem),
        "a gather point at the water is marked beside the fishing spot");
    write_float(memory, nodes, -1.0f);
}

void test_glyphs() {
    check(gather_glyph(0u) == GatherGlyph::Plant, "kind 0 is a plant");
    check(gather_glyph(3u) == GatherGlyph::Mining, "kind 3 is mining");
    check(gather_glyph(4u) == GatherGlyph::BugNet, "kind 4 is a bug net");
    check(gather_glyph(7u) == GatherGlyph::Plant, "a kind ○ gathers is shown as a plant");
}

GatherTarget target_at(std::uint32_t address, float x) {
    return GatherTarget{address, x, 0.0f, 0.0f, GatherGlyph::Plant};
}

void test_motion() {
    GatherMotion motion;
    check(!motion.shown(), "nothing is shown before a target");
    motion.step(target_at(1u, 0.0f));
    std::optional<GatherMotion::Shown> shown = motion.shown();
    check(shown && near(shown->alpha, GatherMotion::kFadeStep) && shown->x == 0.0f,
        "a new target starts on its place and fades in");
    for (int i = 0; i < 10; ++i) motion.step(target_at(1u, 0.0f));
    check(motion.shown() && near(motion.shown()->alpha, 1.0f), "and stays at full opacity");

    // A target close by is glided to, half the way each frame, then reached.
    motion.step(GatherTarget{2u, 100.0f, 0.0f, 0.0f, GatherGlyph::Mining});
    shown = motion.shown();
    check(shown && near(shown->x, 50.0f) && shown->glyph == GatherGlyph::Mining,
        "a near target is glided to and its kind shown at once");
    for (int i = 0; i < 12; ++i) motion.step(target_at(2u, 100.0f));
    check(motion.shown() && motion.shown()->x == 100.0f, "the glide ends on the target");

    // A far one is jumped to.
    motion.step(target_at(3u, 100.0f + GatherMotion::kJump + 1.0f));
    check(motion.shown() && motion.shown()->x == 100.0f + GatherMotion::kJump + 1.0f, "a far target is jumped to");

    // Losing the target fades the mark out where it was.
    const float place = motion.shown()->x;
    motion.step(std::nullopt);
    shown = motion.shown();
    check(shown && near(shown->alpha, 1.0f - GatherMotion::kFadeStep) && shown->x == place,
        "with no target the mark fades out in place");
    // A target found again while fading glides from there and fades back in.
    motion.step(target_at(4u, place + 20.0f));
    shown = motion.shown();
    check(shown && near(shown->alpha, 1.0f) && near(shown->x, place + 10.0f),
        "a target found while fading is glided to from the fading mark");
    for (int i = 0; i < 4; ++i) motion.step(std::nullopt);
    check(!motion.shown(), "four frames without a target and the mark is gone");
    motion.step(target_at(5u, -300.0f));
    shown = motion.shown();
    check(shown && shown->x == -300.0f && near(shown->alpha, GatherMotion::kFadeStep),
        "once gone, the next target starts on its own place");
}

void test_motion_on_the_picture() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 1u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 5.0f, 1u, 4u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x200u, 0.0f);
    write_float(memory, hunter + 0x204u, 0.0f);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 1u);
    mhfu::settings::current().gather_icon = true;

    GatherMotions motions;
    const GatherPose pose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f};
    gather_note_draw(picture(), 100u);
    const std::optional<GatherMark> first = first_mark(memory, pose, &motions);
    check(first && !first->older && near(first->alpha, GatherMotion::kFadeStep) && first->glyph == GatherGlyph::BugNet,
        "the first frame fades the mark in, with no older place to blend from");
    // The camera turns a little: the next frame blends from the first one's place.
    const GatherPose turned{0.0f, 100.0f, 0.0f, 1.0f, 100.0f, -10.0f};
    gather_note_draw(picture(), 100u);
    const std::optional<GatherMark> second = first_mark(memory, turned, &motions);
    check(first && second && second->older && near((*second->older)[0], first->at[0]) &&
            near((*second->older)[1], first->at[1]) && second->at[0] != first->at[0],
        "the next frame keeps where the last one drew the mark");

    // The hunter walks off: the mark fades out where it was.
    write_float(memory, hunter + 0x200u, 40.0f);
    gather_note_draw(picture(), 100u);
    const std::optional<GatherMark> leaving = first_mark(memory, turned, &motions);
    check(leaving && second && near(leaving->alpha, second->alpha - GatherMotion::kFadeStep) &&
            near(leaving->at[0], second->at[0]),
        "out of reach, the mark fades in place");

    // A frame with no world draw (a menu, a load) ends the mark.
    check(
        !first_mark(memory, turned, &motions) && !motions.node.shown(), "a frame without the world resets the motion");
    gather_note_draw(picture(), 100u);
    check(!first_mark(memory, turned, &motions), "and nothing is left to fade");
}

// A body that falls on a mining point: each has its own motion, so the
// carve's mark fades in while the point's stays.
void test_motions_by_role() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t object = 0x09200000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t nodes = 0x09500000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    constexpr std::uint32_t table = 0x09800000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 4u);
    memory.store32(0x08A62D9Cu, object);
    memory.store32(object + 96u, areas);
    memory.store32(object + 80u, 0u);
    memory.store32(areas, nodes);
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 30.0f, 1u, 3u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 4u);
    memory.store32(0x09A4F1A0u, table);
    for (std::uint32_t n = 0; n < 20u; ++n) memory.store16(table + 4u + n * 32u + 16u, 0xFFFFu);
    mhfu::settings::current().gather_icon = true;

    GatherMotions motions;
    const GatherPose pose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f};
    for (int i = 0; i < 4; ++i) {
        gather_note_draw(picture(), 100u);
        (void)gather_marks(memory, pose, &motions);
    }
    plant_body(memory, table + 4u, 0.0f, 0.0f, -10.0f, 30.0f, 9u, 4u);
    gather_note_draw(picture(), 100u);
    const std::vector<GatherMark> marks = gather_marks(memory, pose, &motions);
    check(marks.size() == 2u && marks[0].glyph == GatherGlyph::Mining && near(marks[0].alpha, 1.0f) &&
            marks[1].glyph == GatherGlyph::Carve && near(marks[1].alpha, GatherMotion::kFadeStep),
        "a carve that comes into reach fades in beside the point, which stays");
    check(marks.size() == 2u && marks[0].older && !marks[1].older, "only the mark already shown blends from before");
    check(motions.carves[0].shown().has_value() && !motions.fishing.shown(), "each role keeps its own motion");
    check(gather_role(GatherGlyph::BugNet) == GatherRole::Node && gather_role(GatherGlyph::Plant) == GatherRole::Node &&
            gather_role(GatherGlyph::Fishing) == GatherRole::Fishing,
        "every node kind is the node's role");

    // Without motions, every target is a mark at full opacity.
    gather_note_draw(picture(), 100u);
    const std::vector<GatherMark> plain = gather_marks(memory, pose);
    check(plain.size() == 2u && plain[0].alpha == 1.0f && plain[1].alpha == 1.0f && !plain[1].older,
        "unsmoothed, both are drawn at once");
}

GatherMark mark_at(float x, float y) {
    GatherMark mark;
    mark.at = {x, y};
    return mark;
}

void test_spreading_marks() {
    const std::array<float, 2> apart{0.1f, 0.2f};
    std::vector<GatherMark> one{mark_at(0.5f, 0.5f)};
    spread_gather_marks(one, apart);
    check(one[0].at[0] == 0.5f && one[0].at[1] == 0.5f, "a lone mark stays where it is");

    std::vector<GatherMark> apart_already{mark_at(0.3f, 0.5f), mark_at(0.5f, 0.5f), mark_at(0.5f, 0.75f)};
    spread_gather_marks(apart_already, apart);
    check(apart_already[0].at[0] == 0.3f && apart_already[1].at[0] == 0.5f && apart_already[1].at[1] == 0.5f &&
            apart_already[2].at[1] == 0.75f,
        "marks that do not overlap stay where they are");

    // Two at one place: side by side around it, in the order they came, and
    // the older place goes with its mark.
    std::vector<GatherMark> same{mark_at(0.5f, 0.5f), mark_at(0.5f, 0.5f)};
    same[1].older = std::array<float, 2>{0.48f, 0.5f};
    spread_gather_marks(same, apart);
    check(near(same[0].at[0], 0.45f) && near(same[1].at[0], 0.55f) && near(same[0].at[1], 0.5f) &&
            near(same[1].at[1], 0.5f),
        "two marks at one place are set side by side around it");
    check(!same[0].older && same[1].older && near((*same[1].older)[0], 0.53f) && near((*same[1].older)[1], 0.5f),
        "the older place moves with its mark");

    // Close but not on top: the row keeps their order left to right, at the middle.
    std::vector<GatherMark> close{mark_at(0.52f, 0.5f), mark_at(0.5f, 0.54f)};
    spread_gather_marks(close, apart);
    check(near(close[1].at[0], 0.46f) && near(close[0].at[0], 0.56f) && near(close[0].at[1], 0.52f) &&
            near(close[1].at[1], 0.52f),
        "overlapping marks keep their left-to-right order around their middle");

    // A row that reaches a third mark takes it in, and no pair overlaps after.
    std::vector<GatherMark> chain{mark_at(0.5f, 0.5f), mark_at(0.5f, 0.5f), mark_at(0.62f, 0.5f)};
    spread_gather_marks(chain, apart);
    check(near(chain[0].at[0], 0.44f) && near(chain[1].at[0], 0.54f) && near(chain[2].at[0], 0.64f),
        "a row that reaches the next mark is laid out again with it");

    std::vector<GatherMark> untouched{mark_at(0.5f, 0.5f), mark_at(0.5f, 0.5f)};
    spread_gather_marks(untouched, {0.0f, 0.2f});
    check(untouched[0].at[0] == 0.5f && untouched[1].at[0] == 0.5f, "no picture to measure leaves the marks alone");
}

void test_ui_shift() {
    mhfu::gpu::ui_motion::Shift shift{{10.0f, -4.0f}, {}};
    std::array<float, 2> move = shift.to(0.0f);
    check(near(move[0], 10.0f) && near(move[1], -4.0f), "weight 0 moves the piece to the older frame's place");
    move = shift.to(0.5f);
    check(near(move[0], -5.0f) && near(move[1], 2.0f), "halfway moves it back by half");
    move = shift.to(0.5f);
    check(move[0] == 0.0f && move[1] == 0.0f, "the same weight again does not move it");
    move = shift.to(2.0f);
    check(near(move[0], -5.0f) && near(move[1], 2.0f) && shift.applied[0] == 0.0f && shift.applied[1] == 0.0f,
        "weight 1 (or past it) puts it back where it was built");
    move = shift.to(-1.0f);
    check(near(move[0], 10.0f), "a weight below 0 is the older frame");
}

// A point's list (game/gather_art.hpp): base is the gather object's +80, and
// the word at 0x08A5C584 is where the point offsets start past it.
constexpr std::uint32_t kArtObject = 0x09200000u;
constexpr std::uint32_t kArtBase = 0x09800000u;
constexpr std::uint32_t kArtOffsets = 0x100u;

void plant_list(psprecomp::GuestMemory &memory, std::uint16_t id, std::uint32_t entry,
    std::initializer_list<std::array<std::uint16_t, 2>> pairs) {
    memory.store32(0x08A62D9Cu, kArtObject);
    memory.store32(kArtObject + 80u, kArtBase);
    memory.store32(0x08A5C584u, kArtOffsets);
    memory.store32(kArtBase + kArtOffsets + static_cast<std::uint32_t>(id) * 4u, entry);
    std::uint32_t at = kArtBase + entry;
    for (const std::array<std::uint16_t, 2> &pair : pairs) {
        memory.store16(at, pair[0]);
        memory.store16(at + 2u, pair[1]);
        at += 4u;
    }
    memory.store16(at, 0xFFFFu);
}

// An item with an icon: its record, the icon's sprite, the rectangle and the tint.
void plant_item(psprecomp::GuestMemory &memory, std::uint16_t item, std::uint8_t icon, std::uint8_t tint,
    std::uint16_t sprite, std::array<std::uint16_t, 4> rect, std::array<std::uint8_t, 3> colour) {
    const std::uint32_t record = 0x0899D2BCu + static_cast<std::uint32_t>(item) * 24u;
    memory.store8(record + 1u, icon);
    memory.store8(record + 2u, tint);
    memory.store16(0x0893ABECu + static_cast<std::uint32_t>(icon) * 2u, sprite);
    const std::uint32_t at = 0x089392C8u + static_cast<std::uint32_t>(sprite) * 12u;
    for (std::uint32_t i = 0; i < 4u; ++i) memory.store16(at + i * 2u, rect[i]);
    const std::uint32_t rgb = 0x0893AB7Cu + static_cast<std::uint32_t>(tint) * 4u;
    for (std::uint32_t i = 0; i < 3u; ++i) memory.store8(rgb + i, colour[i]);
}

void test_gather_items() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    check(!gather_item(memory, 3u), "no gather object, no item");
    plant_list(memory, 3u, 0x400u, {{10u, 7u}, {60u, 5u}, {60u, 9u}, {20u, 11u}});
    const std::optional<std::uint16_t> item = gather_item(memory, 3u);
    check(item && *item == 5u, "the heaviest item is shown, the first of equal weights");
    plant_list(memory, 4u, 0x800u, {});
    check(!gather_item(memory, 4u), "an empty list has no item");
    // A list that never ends is not read past 64 pairs.
    memory.store32(kArtBase + kArtOffsets + 5u * 4u, 0xC00u);
    for (std::uint32_t i = 0; i < 70u; ++i) memory.store32(kArtBase + 0xC00u + i * 4u, 0x00010001u);
    check(!gather_item(memory, 5u), "a list with no end is no item");

    check(gather_art_for(memory, 3u, 3u) == GatherArt::of_item(kPickaxeItem), "a mining point shows the pickaxe");
    check(gather_art_for(memory, 4u, 3u) == GatherArt::of_item(kBugNetItem), "a bug point shows the bug net");
    check(gather_art_for(memory, 0u, 3u) == GatherArt::of_item(5u), "a ○ point shows what it gives");
    check(gather_art_for(memory, 0u, 4u).kind == GatherArt::Kind::None, "and nothing when it gives nothing");

    // A bone pile as traced: mostly a husk (icon 28), then two bones (icon 10).
    const auto icon_of = [&](std::uint16_t item, std::uint8_t icon, std::uint8_t tint) {
        const std::uint32_t record = 0x0899D2BCu + static_cast<std::uint32_t>(item) * 24u;
        memory.store8(record + 1u, icon);
        memory.store8(record + 2u, tint);
    };
    icon_of(94u, kHuskIcon, 0u);
    icon_of(292u, kBoneIcon, 0u);
    icon_of(290u, kBoneIcon, 4u);
    plant_list(memory, 27u, 0x1400u, {{60u, 94u}, {30u, 292u}, {10u, 290u}});
    check(gather_art_for(memory, 0u, 27u) == GatherArt::of_item(kBoneItem), "a bone pile shows the bone, not the husk");
    plant_list(memory, 28u, 0x1800u, {{50u, 94u}});
    check(gather_art_for(memory, 0u, 28u) == GatherArt::of_item(kBoneItem), "so does a pile of husks alone");
    plant_list(memory, 29u, 0x1C00u, {{40u, 290u}, {10u, 7u}});
    check(gather_art_for(memory, 0u, 29u) == GatherArt::of_item(kBoneItem), "and one whose bone is not plain");
    // An herb point as traced, with the grey husk as filler: still the herb.
    icon_of(93u, kHuskIcon, 10u);
    icon_of(166u, 15u, 2u);
    plant_list(memory, 14u, 0x2000u, {{55u, 166u}, {30u, 93u}, {15u, 137u}});
    check(gather_art_for(memory, 0u, 14u) == GatherArt::of_item(166u), "a husk as filler leaves an herb point an herb");
}

void test_item_art() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_item(memory, 145u, 16u, 0u, 90u, {24u, 48u, 24u, 24u}, {0xEFu, 0xEFu, 0xE9u});
    const std::optional<ItemArt> pick = item_art(memory, 145u);
    check(pick && pick->u == 24u && pick->v == 48u && pick->width == 24u && pick->height == 24u && pick->red == 0xEFu &&
            pick->blue == 0xE9u,
        "an item's icon is its sprite's rectangle in its tint");
    plant_item(memory, 7u, 255u, 0u, 0u, {}, {});
    check(!item_art(memory, 7u), "icon 255 is none");
    plant_item(memory, 8u, 20u, 2u, 91u, {240u, 0u, 24u, 24u}, {1u, 2u, 3u});
    check(!item_art(memory, 8u), "a rectangle past the atlas is none");
    plant_item(memory, 9u, 21u, 11u, 92u, {0u, 0u, 24u, 24u}, {1u, 2u, 3u});
    check(!item_art(memory, 9u), "a tint past the palette is none");
    check(!item_art(memory, 1500u), "nor an id past the table");

    // The red ? goes through the same icon and tint tables.
    plant_item(memory, 1168u, kUnknownIcon, kUnknownTint, 107u, {48u, 96u, 24u, 24u}, {0xFFu, 0x5Du, 0x5Du});
    const std::optional<ItemArt> unknown = icon_art(memory, kUnknownIcon, kUnknownTint);
    check(unknown && unknown->u == 48u && unknown->v == 96u && unknown->red == 0xFFu && unknown->green == 0x5Du,
        "the game's ? is icon 33 in its red");
    check(!icon_art(memory, 255u, 0u), "icon 255 has no picture");

    check(!gather_art_code_matches(memory), "the lookups need their code");
    for (const GatherArtWord &word : gather_art_words()) memory.store32(word.address, word.instruction);
    check(gather_art_code_matches(memory), "with every word, they run");
    memory.store32(gather_art_words()[2].address, 0u);
    check(!gather_art_code_matches(memory), "a word that differs stops them");
}

void test_mark_shows_art() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    plant_words(memory);
    for (const GatherArtWord &word : gather_art_words()) memory.store32(word.address, word.instruction);
    constexpr std::uint32_t gamesys = 0x09100000u;
    constexpr std::uint32_t ids = 0x09300000u;
    constexpr std::uint32_t areas = 0x09400000u;
    constexpr std::uint32_t hunter_list = 0x09600000u;
    constexpr std::uint32_t hunter = 0x09700000u;
    memory.store32(0x089CC558u, gamesys);
    memory.store8(gamesys + 0x6AF10u, 0u);
    memory.store32(0x09CE4808u, 1u);
    memory.store32(0x09CE480Cu, ids);
    memory.store16(ids, 1u);
    plant_list(memory, 6u, 0x400u, {{80u, 41u}});
    // The area list sits past the base, which +80 also adds to the area pointer.
    memory.store32(kArtObject + 96u, areas);
    memory.store32(areas, 0x1000u);
    const std::uint32_t nodes = kArtBase + 0x1000u;
    plant_node(memory, nodes, 0.0f, 0.0f, -10.0f, 5.0f, 6u, 0u);
    memory.store32(0x089CBD04u, hunter_list);
    memory.store32(hunter_list + 2692u, hunter);
    write_float(memory, hunter + 0x208u, -10.0f);
    memory.store16(hunter + 0x29Au, 1u);
    mhfu::settings::current().gather_icon = true;

    gather_note_draw(picture(), 100u);
    GatherMotions motions;
    const std::optional<GatherMark> mark =
        first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}, &motions);
    check(mark && mark->art == GatherArt::of_item(41u), "the mark carries the item the point gives");
    check(mark && mark->world[0] == 0.0f && mark->world[2] == -10.0f, "and the point it stands over");

    // Mining shows the pickaxe whatever the list says.
    memory.store16(nodes + 20u, 3u);
    gather_note_draw(picture(), 100u);
    const std::optional<GatherMark> mining =
        first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}, &motions);
    check(mining && mining->art == GatherArt::of_item(kPickaxeItem), "a mining point's mark is the pickaxe");

    // With a pickaxe in the pouch, the mark is the one ○ would use, and it
    // follows the pouch from frame to frame.
    const std::uint32_t pouch = gamesys + 0x6AEA6u;
    const auto put = [&](std::uint16_t slot, std::uint16_t item, std::uint16_t count) {
        memory.store16(pouch + slot * 4u, item);
        memory.store16(pouch + slot * 4u + 2u, count);
    };
    const auto shown = [&] {
        gather_note_draw(picture(), 100u);
        const std::optional<GatherMark> now =
            first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}, &motions);
        return now ? now->art : GatherArt{};
    };
    memory.store16(hunter + 1608u, 0u);
    put(2u, 146u, 1u);
    check(shown() == GatherArt::of_item(146u), "the mark is the pickaxe in the pouch");
    put(0u, 1225u, 2u);
    check(shown() == GatherArt::of_item(1225u), "the selected pickaxe before another one");
    put(0u, 0u, 0u);
    check(shown() == GatherArt::of_item(146u), "one used up gives way to the next");
    put(2u, 146u, 0u);
    check(shown() == GatherArt::of_item(kPickaxeItem), "with none left the plain pickaxe again");
    memory.store16(nodes + 20u, 4u);
    put(5u, 1226u, 1u);
    check(shown() == GatherArt::of_item(1226u), "a bug point shows the net the pouch has");

    // And how many of it: every slot holding that tool, and no other.
    const auto carried = [&] {
        gather_note_draw(picture(), 100u);
        const std::optional<GatherMark> now =
            first_mark(memory, GatherPose{0.0f, 100.0f, 0.0f, 0.0f, 100.0f, -10.0f}, &motions);
        return now ? now->carried : std::uint16_t{0xFFFFu};
    };
    check(carried() == 1u, "one net carried");
    put(7u, 1226u, 2u);
    put(8u, 149u, 5u);
    check(carried() == 3u, "the net's stacks add up, another kind of net does not");
    check(gather_tool_count(memory, 149u) == 5u && gather_tool_count(memory, 0u) == 0u, "the count is per item");
    put(5u, 0u, 0u);
    put(7u, 0u, 0u);
    put(8u, 0u, 0u);
    check(carried() == 0u, "no tool, no count: the plain net");
}

} // namespace

int main() {
    test_gather_items();
    test_item_art();
    test_mark_shows_art();
    test_glyphs();
    test_motion();
    test_motion_on_the_picture();
    test_motions_by_role();
    test_spreading_marks();
    test_ui_shift();
    test_the_radius();
    test_the_area_list();
    test_unmapped();
    test_words_and_the_hunter();
    test_on_the_picture();
    test_size();
    test_holding_circle();
    test_holding_on_a_point();
    test_carving_a_monster();
    test_gathering_with_circle();
    test_wrappers();
    test_farm_spots();
    test_mark_over_a_carve();
    test_bodies_lying_together();
    test_fishing_spots();
    std::cout << (failures ? "FAIL" : "PASS") << ": gather icon (" << failures << " failures)\n";
    return failures ? 1 : 0;
}
