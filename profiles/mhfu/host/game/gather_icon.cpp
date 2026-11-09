#include "game/gather_icon.hpp"

#include "game/damage_numbers.hpp"
#include "game/guest_layout.hpp"
#include "game/hooks.hpp"
#include "game/pouch_slots.hpp"
#include "settings/settings.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace mhfu::game {
namespace {

// game_task 0x09A6F490 and the two calls it makes to find the list.
constexpr GatherWord kWords[] = {
    {0x09A6F490u, 0x27BDFFF0u}, // addiu sp,sp,-16: the search
    {0x09A6F4ACu, 0x3C0208A6u}, // lui v0,0x08A6: the gather object
    {0x09A6F4B0u, 0x8C442D9Cu}, // lw a0,11676(v0): the pointer at 0x08A62D9C
    {0x09A6F4B4u, 0x9645029Au}, // lhu a1,666(s2): the hunter's stage
    {0x09A6F4D0u, 0x3C03BF80u}, // lui v1,0xBF80: the list ends where x is -1
    {0x09A6F4E8u, 0x3C0342C8u}, // lui v1,0x42C8: 100 above the node
    {0x09A6F4F0u, 0x3C034348u}, // lui v1,0x4348: 200 below the node
    {0x08869570u, 0x27BDFFF0u}, // addiu sp,sp,-16: the area lookup
    {0x08869580u, 0x8C900060u}, // lw s0,96(a0): the area table
    {0x088695A8u, 0x9065AF10u}, // lbu a1,-20720(v1): the area byte at GameSys+0x6AF10
    {0x088C722Cu, 0x3C0209CEu}, // lui v0,0x09CE: the stage-id table
    {0x088C7234u, 0x24424808u}, // addiu v0,v0,0x4808: the table at 0x09CE4808
    {0x08886E80u, 0x3C03089Du}, // lui v1,0x89d: the base of the hunter list
    {0x08886EF8u, 0x8E130A84u}, // lw s3,2692(s0): the hunter the camera follows
};

constexpr std::uint32_t kGatherObject = 0x08A62D9Cu;
constexpr std::uint32_t kAreas = 96u;
constexpr std::uint32_t kListBias = 80u;
using layout::kGameSysPointer;
constexpr std::uint32_t kAreaByte = 0x6AF10u;
constexpr std::uint32_t kStageTable = 0x09CE4808u;
// The hunter's position, and the stage it stands in.
constexpr std::uint32_t kHunterPosition = layout::kPosition;
constexpr std::uint32_t kHunterStage = layout::kStageId;
constexpr std::uint32_t kNodeStride = 24u;
constexpr std::uint32_t kNodeRadius = 12u;
constexpr std::uint32_t kNodeId = 16u;
constexpr std::uint32_t kNodeKind = 20u;
constexpr std::uint32_t kSentinel = 0xBF800000u; // -1, where the list ends
constexpr std::uint32_t kMaxNodes = 64u;
constexpr std::uint32_t kMaxStages = 256u;
constexpr float kBelow = 200.0f;
constexpr float kAbove = 100.0f;
// The pad bits the game reads.
constexpr std::uint32_t kCircleButton = 0x2000u; // ○, B: plants and carving
constexpr std::uint32_t kSquareButton = 0x8000u; // □, X: mining and a bug net
constexpr std::uint32_t kLeadButton = 0x0100u;   // L: with ○ or □, the item bar
// The pouch (game/empty_item_slot.hpp).
constexpr std::uint32_t kPouch = 0x6AEA6u;
constexpr std::uint32_t kSelectedSlot = 1608u; // hunter halfword
constexpr std::uint32_t kHunterPlayer = 486u;  // hunter byte: the local one has GameSys +40
constexpr std::uint32_t kLocalPlayer = 40u;

struct Noted {
    GatherPicture picture{};
    std::uint32_t vertices{};
};

Noted &noted() {
    static Noted value;
    return value;
}

bool trace_on() {
    static const bool on = std::getenv("MHFU_TRACE_GATHER") != nullptr;
    return on;
}

float load_float(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return std::bit_cast<float>(memory.load32(address));
}

bool in_ram(std::uint32_t address, std::uint32_t bytes, const psprecomp::GuestMemory &memory) {
    return layout::in_ram(address) && memory.contains(address, bytes);
}

bool &code_logged() {
    static bool logged = false;
    return logged;
}

void trace_node(const std::optional<GatherNode> &node) {
    if (!trace_on()) return;
    static std::uint32_t last = 1u; // not an address, so the first result is printed
    const std::uint32_t address = node ? node->address : 0u;
    if (address == last) return;
    last = address;
    if (!node) {
        std::cout << "[gather] none" << std::endl;
        return;
    }
    std::cout << "[gather] node 0x" << std::hex << node->address << std::dec << " id " << node->id << " kind "
              << node->kind << " at " << node->x << "," << node->y << "," << node->z << std::endl;
}

// MHFU_TRACE_GATHER: the item a node's mark shows and its icon, once a node.
void trace_art(const psprecomp::GuestMemory &memory, const GatherNode &node, const GatherArt &art) {
    if (!trace_on()) return;
    static std::uint32_t last = 0u;
    if (node.address == last) return;
    last = node.address;
    if (art.kind != GatherArt::Kind::Item) {
        std::cout << "[gather] node id " << node.id << " shows no item" << std::endl;
        return;
    }
    std::cout << "[gather] node id " << node.id << " shows item " << art.item;
    if (const std::optional<ItemArt> icon = item_art(memory, art.item))
        std::cout << " at " << icon->u << "," << icon->v << " " << icon->width << "x" << icon->height << " tint "
                  << static_cast<int>(icon->red) << "," << static_cast<int>(icon->green) << ","
                  << static_cast<int>(icon->blue);
    else
        std::cout << ", which has no icon";
    std::cout << std::endl;
    // The whole list: weight, item, and the item's icon and tint numbers.
    if (const std::optional<std::vector<GatherDrop>> drops = gather_drops(memory, node.id)) {
        std::cout << "[gather]   list:";
        for (const GatherDrop &drop : *drops) {
            std::cout << ' ' << drop.weight << 'x' << drop.item;
            if (const std::optional<std::array<std::uint8_t, 2>> look = item_icon_and_tint(memory, drop.item))
                std::cout << "(icon " << static_cast<int>((*look)[0]) << " tint " << static_cast<int>((*look)[1])
                          << ')';
        }
        std::cout << std::endl;
    }
}

} // namespace

std::span<const GatherWord> gather_checked_words() {
    return kWords;
}

GatherShape gather_mark_shape(std::uint32_t size_percent) noexcept {
    const std::uint32_t size = std::clamp(size_percent, settings::kMinGatherIconSize, settings::kMaxGatherIconSize);
    const float scale = static_cast<float>(size) / 100.0f;
    GatherShape shape;
    shape.radius = kGatherDisc * scale;
    shape.glyph = kGatherGlyph * scale;
    shape.ring = scale;
    return shape;
}

// The search lives in game_task.ovl. Until that overlay is loaded the words
// are not the search, and that is a quiet miss rather than a code change.
namespace {

// Every word mapped and matching. A mapped word that differs is logged once
// per `logged` flag.
bool words_match(const psprecomp::GuestMemory &memory, std::span<const GatherWord> words, bool &logged) {
    bool ready = true;
    bool differs = false;
    for (const GatherWord &word : words) {
        if (memory.raw_pointer(word.address, 4u) == nullptr) {
            ready = false;
            continue;
        }
        if (memory.load32(word.address) != word.instruction) differs = true;
    }
    if (differs && !logged) {
        logged = true;
        for (const GatherWord &word : words)
            if (memory.raw_pointer(word.address, 4u) != nullptr && memory.load32(word.address) != word.instruction)
                std::cout << "[gather] game code differs at 0x" << std::hex << word.address << ": 0x"
                          << memory.load32(word.address) << ", expected 0x" << word.instruction << std::dec
                          << std::endl;
    }
    return ready && !differs;
}

} // namespace

bool gather_code_matches(const psprecomp::GuestMemory &memory) {
    if (!layout::quest_running(memory)) return false;
    return words_match(memory, kWords, code_logged());
}

std::optional<GatherNode> gather_from_list(const psprecomp::GuestMemory &memory, std::uint32_t list, float hunter_x,
    float hunter_y, float hunter_z) {
    if (!std::isfinite(hunter_x) || !std::isfinite(hunter_y) || !std::isfinite(hunter_z)) return std::nullopt;
    for (std::uint32_t n = 0; n < kMaxNodes; ++n) {
        if (!in_ram(list, kNodeStride, memory)) return std::nullopt;
        const std::uint32_t x_bits = memory.load32(list);
        if (x_bits == kSentinel) return std::nullopt;
        const float x = std::bit_cast<float>(x_bits);
        const float y = load_float(memory, list + 4u);
        const float z = load_float(memory, list + 8u);
        const float radius = load_float(memory, list + kNodeRadius);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius)) return std::nullopt;
        const float dx = hunter_x - x;
        const float dz = hunter_z - z;
        // 0x09A6F4E8: at or above the node minus 200, and below the node plus 100.
        if (hunter_y >= y - kBelow && hunter_y < y + kAbove && dx * dx + dz * dz <= radius * radius)
            return GatherNode{list, x, y, z, memory.load16(list + kNodeId), memory.load16(list + kNodeKind)};
        list += kNodeStride;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> gather_list_for(const psprecomp::GuestMemory &memory, std::uint16_t stage) {
    if (!in_ram(kGameSysPointer, 4u, memory) || !in_ram(kGatherObject, 4u, memory)) return std::nullopt;
    const std::uint32_t gamesys = memory.load32(kGameSysPointer);
    const std::uint32_t object = memory.load32(kGatherObject);
    if (!in_ram(gamesys + kAreaByte, 1u, memory) || !in_ram(object + kAreas, 4u, memory) ||
        !in_ram(object + kListBias, 4u, memory))
        return std::nullopt;
    const std::uint32_t entry = kStageTable + static_cast<std::uint32_t>(memory.load8(gamesys + kAreaByte)) * 8u;
    if (!in_ram(entry, 8u, memory)) return std::nullopt;
    const auto count = static_cast<std::int32_t>(memory.load32(entry));
    const std::uint32_t ids = memory.load32(entry + 4u);
    if (count <= 0 || !in_ram(ids, 2u, memory)) return std::nullopt;
    const std::uint32_t limit = std::min(static_cast<std::uint32_t>(count), kMaxStages);
    std::optional<std::uint32_t> index;
    for (std::uint32_t i = 0; i < limit; ++i) {
        if (!in_ram(ids + i * 2u, 2u, memory)) return std::nullopt;
        if (memory.load16(ids + i * 2u) == stage) {
            index = i;
            break;
        }
    }
    if (!index) return std::nullopt;
    const std::uint32_t areas = memory.load32(object + kAreas);
    if (!in_ram(areas + *index * 4u, 4u, memory)) return std::nullopt;
    const std::uint32_t base = memory.load32(areas + *index * 4u);
    if (base == 0u) return std::nullopt;
    const std::uint32_t list = base + memory.load32(object + kListBias);
    if (!in_ram(list, kNodeStride, memory)) return std::nullopt;
    return list;
}

namespace {

// The hunter the camera follows, readable from the position at +0x200
// through the stage halfword at +0x29A.
std::optional<std::uint32_t> followed_hunter(const psprecomp::GuestMemory &memory) {
    if (!gather_code_matches(memory)) return std::nullopt;
    const std::uint32_t hunter = layout::followed_hunter(memory, kHunterStage + 2u);
    if (hunter == 0u) return std::nullopt;
    return hunter;
}

std::optional<GatherNode> read_node(const psprecomp::GuestMemory &memory) {
    const std::optional<std::uint32_t> hunter = followed_hunter(memory);
    if (!hunter) return std::nullopt;
    const std::uint16_t stage = memory.load16(*hunter + kHunterStage);
    const std::optional<std::uint32_t> nodes = gather_list_for(memory, stage);
    if (!nodes) return std::nullopt;
    return gather_from_list(memory, *nodes, load_float(memory, *hunter + kHunterPosition),
        load_float(memory, *hunter + kHunterPosition + 4u), load_float(memory, *hunter + kHunterPosition + 8u));
}

// 0x0886CEA8. The circle handler asks it only after the gather search misses.
constexpr std::uint32_t kCarveTable = 0x09A4F1A0u;
constexpr std::uint32_t kCarveStride = 32u;
constexpr std::uint32_t kCarveFirst = 4u;
constexpr std::uint32_t kCarveEmpty = 0xFFFFu;
constexpr std::uint32_t kCarveId = 20u;
constexpr std::uint32_t kCarveStage = 24u;
constexpr std::uint32_t kCarveBlocked = 1164u; // bit 8: the handler leaves the body
constexpr std::uint32_t kCarveBlockBit = 0x100u;

bool body_in_reach(const psprecomp::GuestMemory &memory, std::uint32_t record, float hunter_x, float hunter_y,
    float hunter_z) {
    const float x = load_float(memory, record);
    const float y = load_float(memory, record + 4u);
    const float z = load_float(memory, record + 8u);
    const float radius = load_float(memory, record + 12u);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius)) return false;
    const float dx = hunter_x - x;
    const float dz = hunter_z - z;
    return hunter_y >= y - kBelow && hunter_y < y + kAbove && dx * dx + dz * dz <= radius * radius;
}

// MHFU_TRACE_GATHER: the record of a body that comes into reach, past the
// position and radius, to find which of its words names the monster.
void trace_carve(const psprecomp::GuestMemory &memory, std::uint32_t record) {
    if (!trace_on()) return;
    static std::uint32_t last = 0u;
    if (record == last) return;
    last = record;
    std::cout << "[gather] carve 0x" << std::hex << record << " +16:";
    for (std::uint32_t offset = 16u; offset < kCarveStride; offset += 2u)
        std::cout << ' ' << memory.load16(record + offset);
    std::cout << std::dec << std::endl;
}

// game_task 0x09AA6B18, the spot search, and the item handler's bait case
// that asks it.
constexpr GatherWord kSpotWords[] = {
    {0x09AA6B34u, 0x3C0209A5u}, // lui v0,0x09A5
    {0x09AA6B38u, 0x8C42F1A0u}, // lw v0,-3680(v0): the pointer at 0x09A4F1A0
    {0x09AA6B44u, 0x8C440000u}, // lw a0,0(v0): the stage object
    {0x09AA6B4Cu, 0x8C990000u}, // lw t9,0(a0): its vtable
    {0x09AA6B50u, 0x8F390028u}, // lw t9,40(t9): method +40, the spot table
    {0x09AA6B5Cu, 0x8C500010u}, // lw s0,16(v0): the spot list
    {0x09AA6B88u, 0x8043003Eu}, // lb v1,62(v0): the spot count
    {0x09AA6B94u, 0xC6840204u}, // lwc1 f4,516(s4): the hunter's height
    {0x09AA6B98u, 0x3C024248u}, // lui v0,0x4248: 50 above and below the spot
    {0x09AA6C0Cu, 0xC6010010u}, // lwc1 f1,16(s0): the spot's radius
    {0x09AA6C4Cu, 0x96020002u}, // lhu v0,2(s0): the type it returns
    {0x09AA6C5Cu, 0x26100018u}, // addiu s0,s0,24: 24 bytes a spot
    {0x09AA7ABCu, 0x2402008Fu}, // addiu v0,zero,143: the last bait
    {0x09AA7B10u, 0x24020088u}, // addiu v0,zero,136: the first bait
    {0x09AA7FA4u, 0x0E6A9AC6u}, // jal 0x09AA6B18: a bait asks the spot search
    {0x09AA7FB0u, 0x24020017u}, // addiu v0,zero,23: and fishes at type 23
};

constexpr std::uint32_t kSpotMethod = 40u;
constexpr std::uint32_t kSpotList = 16u;
constexpr std::uint32_t kSpotCount = 62u;
constexpr std::uint32_t kSpotStride = 24u;
constexpr std::uint32_t kSpotType = 2u;
constexpr std::uint32_t kSpotRadius = 16u;
constexpr float kSpotReach = 50.0f;

bool &spot_logged() {
    static bool logged = false;
    return logged;
}

// MHFU_TRACE_GATHER: the fishing spot in reach, once each time it changes.
void trace_fishing(const std::optional<FishingSpot> &spot) {
    if (!trace_on()) return;
    static std::uint32_t last = 1u;
    const std::uint32_t address = spot ? spot->address : 0u;
    if (address == last) return;
    last = address;
    if (!spot) {
        std::cout << "[gather] fishing none" << std::endl;
        return;
    }
    std::cout << "[gather] fishing 0x" << std::hex << spot->address << std::dec << " at " << spot->x << ","
              << spot->y << "," << spot->z << std::endl;
}

} // namespace

std::vector<CarveBody> carve_bodies(const psprecomp::GuestMemory &memory) {
    if (!gather_code_matches(memory)) return {};
    const std::optional<std::uint32_t> hunter = followed_hunter(memory);
    if (!hunter || !in_ram(*hunter + kHunterPosition, kHunterStage + 2u - kHunterPosition, memory) ||
        !in_ram(*hunter + kCarveBlocked, 4u, memory))
        return {};
    if ((memory.load32(*hunter + kCarveBlocked) & kCarveBlockBit) != 0u) return {};
    if (!in_ram(kCarveTable, 4u, memory)) return {};
    const std::uint32_t table = memory.load32(kCarveTable);
    if (!layout::in_ram(table) || !in_ram(table + kCarveFirst, kCarveStride, memory)) return {};
    const float hunter_x = load_float(memory, *hunter + kHunterPosition);
    const float hunter_y = load_float(memory, *hunter + kHunterPosition + 4u);
    const float hunter_z = load_float(memory, *hunter + kHunterPosition + 8u);
    if (!std::isfinite(hunter_x) || !std::isfinite(hunter_y) || !std::isfinite(hunter_z)) return {};
    const std::uint16_t stage = memory.load16(*hunter + kHunterStage);
    std::vector<CarveBody> bodies;
    // 0x0886CEA8 returns the first body in reach and 0x0886CF78 its +20, so
    // ○ carves that one or nothing; the walk goes on for the marks alone.
    bool first = true;
    std::uint32_t record = table + kCarveFirst;
    for (std::uint32_t n = 0; n < kCarveSlots; ++n, record += kCarveStride) {
        if (!in_ram(record, kCarveStride, memory)) break;
        if (memory.load16(record + 16u) == kCarveEmpty || memory.load16(record + kCarveStage) != stage ||
            !body_in_reach(memory, record, hunter_x, hunter_y, hunter_z))
            continue;
        if (memory.load16(record + kCarveId) != kCarveEmpty) {
            if (first) trace_carve(memory, record);
            bodies.push_back(CarveBody{record, static_cast<std::uint8_t>(n), load_float(memory, record),
                load_float(memory, record + 4u), load_float(memory, record + 8u), first});
        }
        first = false;
    }
    return bodies;
}

std::optional<CarveBody> carve_body(const psprecomp::GuestMemory &memory) {
    const std::vector<CarveBody> bodies = carve_bodies(memory);
    if (bodies.empty() || !bodies.front().first) return std::nullopt;
    return bodies.front();
}

bool carve_in_reach(const psprecomp::GuestMemory &memory) {
    return carve_body(memory).has_value();
}

std::span<const GatherWord> fishing_checked_words() {
    return kSpotWords;
}

std::optional<std::uint32_t> spot_table(const psprecomp::GuestMemory &memory) {
    if (!in_ram(kCarveTable, 4u, memory)) return std::nullopt;
    const std::uint32_t quest = memory.load32(kCarveTable);
    if (!in_ram(quest, 4u, memory)) return std::nullopt;
    const std::uint32_t stage = memory.load32(quest);
    if (!in_ram(stage, 4u, memory)) return std::nullopt;
    const std::uint32_t vtable = memory.load32(stage);
    if (!in_ram(vtable + kSpotMethod, 4u, memory)) return std::nullopt;
    const std::uint32_t method = memory.load32(vtable + kSpotMethod);
    if (!in_ram(method, 12u, memory)) return std::nullopt;
    // lui v0,hi; jr ra; addiu v0,v0,lo
    const std::uint32_t high = memory.load32(method);
    const std::uint32_t low = memory.load32(method + 8u);
    if ((high & 0xFFFF0000u) != 0x3C020000u || memory.load32(method + 4u) != 0x03E00008u ||
        (low & 0xFFFF0000u) != 0x24420000u)
        return std::nullopt;
    const std::uint32_t table = (high << 16u) + static_cast<std::uint32_t>(static_cast<std::int16_t>(low & 0xFFFFu));
    if (!in_ram(table, kSpotCount + 1u, memory)) return std::nullopt;
    return table;
}

std::optional<GatherSpot> spot_from_table(const psprecomp::GuestMemory &memory, std::uint32_t table, float hunter_x,
    float hunter_y, float hunter_z) {
    if (!in_ram(table, kSpotCount + 1u, memory)) return std::nullopt;
    std::uint32_t spot = memory.load32(table + kSpotList);
    const auto count = static_cast<std::int8_t>(memory.load8(table + kSpotCount));
    if (spot == 0u || count <= 0) return std::nullopt;
    for (std::int8_t n = 0; n < count; ++n, spot += kSpotStride) {
        if (!in_ram(spot, kSpotStride, memory)) return std::nullopt;
        const float x = load_float(memory, spot + 4u);
        const float y = load_float(memory, spot + 8u);
        const float z = load_float(memory, spot + 12u);
        // 0x09AA6BB4: below the spot minus 50, or a height that is not a
        // number, is skipped; so is the spot plus 50 and above.
        if (!(hunter_y >= y - kSpotReach) || !(hunter_y < y + kSpotReach)) continue;
        const float dx = hunter_x - x;
        const float dz = hunter_z - z;
        if (!(std::sqrt(dx * dx + dz * dz) <= load_float(memory, spot + kSpotRadius))) continue;
        return GatherSpot{spot, memory.load16(spot + kSpotType), x, y, z};
    }
    return std::nullopt;
}

namespace {

std::optional<FishingSpot> read_fishing(const psprecomp::GuestMemory &memory) {
    if (!gather_code_matches(memory) || !words_match(memory, kSpotWords, spot_logged())) return std::nullopt;
    const std::optional<std::uint32_t> hunter = followed_hunter(memory);
    if (!hunter || !in_ram(*hunter + kHunterPosition, 12u, memory)) return std::nullopt;
    const std::optional<std::uint32_t> table = spot_table(memory);
    if (!table) return std::nullopt;
    const std::optional<GatherSpot> spot = spot_from_table(memory, *table, load_float(memory, *hunter + kHunterPosition),
        load_float(memory, *hunter + kHunterPosition + 4u), load_float(memory, *hunter + kHunterPosition + 8u));
    // The item handler takes the search's first spot and fishes only at 23.
    if (!spot || spot->type != kFishingSpotType) return std::nullopt;
    return FishingSpot{spot->address, spot->x, spot->y, spot->z};
}

} // namespace

std::optional<FishingSpot> fishing_spot(const psprecomp::GuestMemory &memory) {
    const std::optional<FishingSpot> spot = read_fishing(memory);
    trace_fishing(spot);
    return spot;
}

namespace {

// lobby_task 0x09AAF290 keeps the point in reach, 0x09AAD350 starts it on □,
// and game_sub counts the uses left on a farm spot.
constexpr GatherWord kFarmWords[] = {
    {0x09AAF3ACu, 0xAE510554u}, // sw s1,1364(s2): the point in reach
    {0x09AAD4CCu, 0x962401E6u}, // lhu a0,486(s1): the local hunter only
    {0x09AAD4F8u, 0x3C0309B2u}, // lui v1,0x09B2: the village state object
    {0x09AAD4FCu, 0x8C702E70u}, // lw s0,11888(v1): the pointer at 0x09B22E70
    {0x09AAD7B4u, 0x8E040000u}, // lw a0,0(s0): its state
    {0x09AAD7CCu, 0x8603001Au}, // lh v1,26(s0): and the halfword at +26
    {0x09AADA40u, 0x8E230484u}, // lw v1,1156(s1): the buttons new this read
    {0x09AADA44u, 0x30638000u}, // andi v1,v1,0x8000: □ starts the point
    {0x09AADA54u, 0x96430002u}, // lhu v1,2(s2): the point's type
    {0x09AADA64u, 0x3C0409B1u}, // lui a0,0x09B1
    {0x09AADA6Cu, 0x2484F300u}, // addiu a0,a0,-3328: the table at 0x09B0F300
    {0x09B0F33Cu, 0x09AAE08Cu}, // type 15: mining
    {0x09B0F340u, 0x09AAE118u}, // type 16: bugs
    {0x09AAE094u, 0x0E70A57Eu}, // jal 0x09C295F8: mining uses left
    {0x09AAE120u, 0x0E70A592u}, // jal 0x09C29648: bug uses left
    {0x09C29604u, 0x3421A238u}, // ori at,at,0xA238: the farm in GameSys
    {0x09C2960Cu, 0x90620002u}, // lbu v0,2(v1): the mining spot count
    {0x09C29624u, 0x94420008u}, // lhu v0,8(v0): its uses left
    {0x09C29654u, 0x3421A238u}, // ori at,at,0xA238
    {0x09C2965Cu, 0x90620003u}, // lbu v0,3(v1): the bug spot count
    {0x09C29674u, 0x94420030u}, // lhu v0,48(v0): its uses left
    {0x08886E80u, 0x3C03089Du}, // lui v1,0x89d: the base of the hunter list
    {0x08886EF8u, 0x8E130A84u}, // lw s3,2692(s0): the hunter the camera follows
};

constexpr std::uint32_t kVillageState = 0x09B22E70u;
constexpr std::uint32_t kVillageBlock = 26u;
constexpr std::uint32_t kPointInReach = 1364u;
constexpr std::uint32_t kPointType = 2u;
constexpr std::uint32_t kPointIndex = 22u;
constexpr std::uint32_t kHunterSlot = 486u;
constexpr std::uint32_t kLocalSlot = 40u;
constexpr std::uint32_t kFarm = 0x6A238u;
constexpr std::uint32_t kFarmStride = 8u;

bool &farm_logged() {
    static bool logged = false;
    return logged;
}

// MHFU_TRACE_GATHER: the farm spot in reach, once each time it changes.
void trace_farm(const std::optional<FarmPoint> &point) {
    if (!trace_on()) return;
    static std::uint32_t last = 1u;
    static std::uint16_t last_uses = 0u;
    const std::uint32_t address = point ? point->address : 0u;
    const std::uint16_t uses = point ? point->uses : 0u;
    if (address == last && uses == last_uses) return;
    last = address;
    last_uses = uses;
    if (!point) {
        std::cout << "[gather] farm none" << std::endl;
        return;
    }
    std::cout << "[gather] farm 0x" << std::hex << point->address << std::dec << " type " << point->type << " index "
              << point->index << " uses " << point->uses << std::endl;
}

std::optional<FarmPoint> read_farm(const psprecomp::GuestMemory &memory) {
    if (!farm_code_matches(memory) || !in_ram(kGameSysPointer, 4u, memory) || !in_ram(kVillageState, 4u, memory))
        return std::nullopt;
    const std::uint32_t hunter = layout::followed_hunter(memory, kPointInReach + 4u);
    const std::uint32_t gamesys = memory.load32(kGameSysPointer);
    const std::uint32_t state = memory.load32(kVillageState);
    if (hunter == 0u || !in_ram(gamesys + kLocalSlot, 1u, memory) ||
        !in_ram(gamesys + kFarm, 4u, memory) || !in_ram(state, kVillageBlock + 2u, memory))
        return std::nullopt;
    if (memory.load16(hunter + kHunterSlot) != memory.load8(gamesys + kLocalSlot)) return std::nullopt;
    // Another state is a menu, a talk or a gather already running: the
    // handler does not start a point then.
    if (memory.load32(state) != 0u || memory.load16(state + kVillageBlock) != 0u) return std::nullopt;
    const std::uint32_t point = memory.load32(hunter + kPointInReach);
    if (!in_ram(point, kPointIndex + 2u, memory)) return std::nullopt;
    const std::uint16_t type = memory.load16(point + kPointType);
    if (type != kFarmMining && type != kFarmBugs) return std::nullopt;
    const std::uint16_t index = memory.load16(point + kPointIndex);
    const std::uint32_t farm = gamesys + kFarm;
    // 0x09C295F8 and 0x09C29648: an index past the count has no uses.
    const std::uint8_t count = memory.load8(farm + (type == kFarmMining ? 2u : 3u));
    if (index > count) return std::nullopt;
    const std::uint32_t uses_at = farm + (type == kFarmMining ? 8u : 48u) + index * kFarmStride;
    if (!in_ram(uses_at, 2u, memory)) return std::nullopt;
    const std::uint16_t uses = memory.load16(uses_at);
    if (uses == 0u) return std::nullopt;
    return FarmPoint{point, type, index, uses};
}

} // namespace

std::span<const GatherWord> farm_checked_words() {
    return kFarmWords;
}

bool farm_code_matches(const psprecomp::GuestMemory &memory) {
    if (!layout::overlay_loaded(memory, layout::kTaskSlot, layout::kVillageOverlay) ||
        !layout::overlay_loaded(memory, layout::kSubSlot, layout::kSubOverlay))
        return false;
    return words_match(memory, kFarmWords, farm_logged());
}

std::optional<FarmPoint> farm_point(const psprecomp::GuestMemory &memory) {
    const std::optional<FarmPoint> point = read_farm(memory);
    trace_farm(point);
    return point;
}

std::optional<GatherNode> gather_node(const psprecomp::GuestMemory &memory) {
    const std::optional<GatherNode> node = read_node(memory);
    trace_node(node);
    return node;
}

void gather_note_draw(const GatherPicture &picture, std::uint32_t vertices) {
    if (vertices == 0u || std::fabs(picture.projection[11]) < 1.0e-6f) return;
    Noted &slot = noted();
    if (vertices <= slot.vertices) return;
    slot.picture = picture;
    slot.vertices = vertices;
}

GatherGlyph gather_glyph(std::uint16_t kind) noexcept {
    if (kind == 3u) return GatherGlyph::Mining;
    if (kind == 4u) return GatherGlyph::BugNet;
    return GatherGlyph::Plant;
}

std::vector<GatherTarget> gather_targets(const psprecomp::GuestMemory &memory) {
    std::vector<GatherTarget> targets;
    const std::optional<GatherNode> node = gather_node(memory);
    // 0x09AA6F1C: a plant is gathered, and the carve search is not asked.
    const std::vector<CarveBody> bodies = !node || node->kind != 0u ? carve_bodies(memory) : std::vector<CarveBody>{};
    const bool carve = !bodies.empty() && bodies.front().first;
    if (node) {
        GatherArt art;
        if (gather_art_code_matches(memory)) art = gather_art_for(memory, node->kind, node->id);
        // The pickaxe or the net ○ would use, in its own tint, in place of
        // the plain one, and how many of it the pouch holds.
        std::uint16_t carried = 0u;
        if (art.kind == GatherArt::Kind::Item && gather_uses_square(node->kind)) {
            if (const std::optional<std::uint16_t> tool = gather_tool_item(memory, node->kind)) {
                art = GatherArt::of_item(*tool);
                carried = gather_tool_count(memory, *tool);
            }
        }
        trace_art(memory, *node, art);
        const GatherGlyph glyph = gather_glyph(node->kind);
        targets.push_back(GatherTarget{node->address, node->x, node->y, node->z, glyph, art,
            gather_button(memory, glyph, node->kind, carve), carried});
    }
    for (const CarveBody &body : bodies)
        targets.push_back(GatherTarget{body.address, body.x, body.y, body.z, GatherGlyph::Carve, {},
            body.first ? kCircleButton : 0u, 0u, body.slot});
    if (const std::optional<FishingSpot> spot = fishing_spot(memory)) {
        GatherArt art;
        if (gather_art_code_matches(memory)) art = GatherArt::of_item(kBaitItem);
        targets.push_back(
            GatherTarget{spot->address, spot->x, spot->y, spot->z, GatherGlyph::Fishing, art, kSquareButton});
    }
    return targets;
}

GatherRole gather_role(GatherGlyph glyph) noexcept {
    if (glyph == GatherGlyph::Carve) return GatherRole::Carve;
    if (glyph == GatherGlyph::Fishing) return GatherRole::Fishing;
    return GatherRole::Node;
}

void GatherMotion::step(const std::optional<GatherTarget> &target) {
    if (!target) {
        alpha_ = std::max(0.0f, alpha_ - kFadeStep);
        if (alpha_ <= 0.0f) live_ = false;
        return;
    }
    const float dx = target->x - x_;
    const float dy = target->y - y_;
    const float dz = target->z - z_;
    // Nothing on screen, or too far to glide: the mark starts on the target.
    if (!live_ || dx * dx + dy * dy + dz * dz > kJump * kJump) {
        x_ = target->x;
        y_ = target->y;
        z_ = target->z;
        if (!live_) alpha_ = 0.0f;
    } else if (dx * dx + dy * dy + dz * dz <= kArrive * kArrive) {
        x_ = target->x;
        y_ = target->y;
        z_ = target->z;
    } else {
        x_ += dx * kGlide;
        y_ += dy * kGlide;
        z_ += dz * kGlide;
    }
    live_ = true;
    glyph_ = target->glyph;
    art_ = target->art;
    button_ = target->button;
    carried_ = target->carried;
    alpha_ = std::min(1.0f, alpha_ + kFadeStep);
}

std::optional<GatherMotion::Shown> GatherMotion::shown() const {
    if (!live_ || alpha_ <= 0.0f) return std::nullopt;
    return Shown{x_, y_, z_, alpha_, glyph_, art_, button_, carried_};
}

std::vector<GatherMark> gather_marks(
    const psprecomp::GuestMemory &memory, const std::optional<GatherPose> &pose, GatherMotions *motions) {
    Noted &slot = noted();
    const GatherPicture picture = slot.picture;
    const std::uint32_t vertices = std::exchange(slot.vertices, 0u);
    slot.picture = {};
    if (!settings::current().gather_icon || vertices == 0u || !pose) {
        if (motions != nullptr) motions->reset();
        return {};
    }
    const std::vector<GatherTarget> targets = gather_targets(memory);
    const WorldPoint eye{pose->eye_x, pose->eye_y, pose->eye_z};
    const WorldPoint look{pose->look_x, pose->look_y, pose->look_z};
    const std::optional<std::array<float, 16>> view = look_at_view(eye, look);
    const auto project = [&](const std::array<float, 3> &point) -> std::optional<std::array<float, 2>> {
        if (!view || picture.x_scale == 0.0f || picture.y_scale == 0.0f) return std::nullopt;
        WorldCamera camera;
        camera.view = *view;
        camera.projection = picture.projection;
        camera.x_scale = picture.x_scale;
        camera.y_scale = picture.y_scale;
        camera.x_offset = picture.x_offset;
        camera.y_offset = picture.y_offset;
        camera.origin_x = picture.origin_x;
        camera.origin_y = picture.origin_y;
        const std::optional<PicturePoint> at = project_world(camera, point[0], point[1] + kGatherLift, point[2]);
        if (!at) return std::nullopt;
        return std::array<float, 2>{at->x, at->y};
    };
    std::vector<GatherMark> marks;
    if (motions == nullptr) {
        for (const GatherTarget &target : targets) {
            GatherMark mark;
            mark.glyph = target.glyph;
            mark.art = target.art;
            mark.button = target.button;
            mark.carried = target.carried;
            mark.world = {target.x, target.y, target.z};
            const std::optional<std::array<float, 2>> at = project(mark.world);
            if (!at) continue;
            mark.at = *at;
            marks.push_back(mark);
        }
        return marks;
    }
    // The node, each slot of the carve table, then the fishing spot: the
    // order of gather_targets.
    std::array<GatherMotion *, kCarveSlots + 2u> order{};
    order.front() = &motions->node;
    for (std::size_t slot = 0; slot < kCarveSlots; ++slot) order[slot + 1u] = &motions->carves[slot];
    order.back() = &motions->fishing;
    // The motion each target moves.
    const auto motion_of = [&](const GatherTarget &target) -> GatherMotion * {
        switch (gather_role(target.glyph)) {
        case GatherRole::Carve: return &motions->carves[target.slot % kCarveSlots];
        case GatherRole::Fishing: return &motions->fishing;
        default: return &motions->node;
        }
    };
    for (GatherMotion *const each_motion : order) {
        GatherMotion &motion = *each_motion;
        std::optional<GatherTarget> target;
        for (const GatherTarget &each : targets)
            if (motion_of(each) == &motion) target = each;
        motion.step(target);
        const std::optional<GatherMotion::Shown> shown = motion.shown();
        if (!shown) {
            motion.set_last_at(std::nullopt);
            continue;
        }
        GatherMark mark;
        mark.alpha = shown->alpha;
        mark.glyph = shown->glyph;
        mark.art = shown->art;
        mark.button = shown->button;
        mark.carried = shown->carried;
        mark.world = {shown->x, shown->y, shown->z};
        const std::optional<std::array<float, 2>> at = project(mark.world);
        mark.older = motion.last_at();
        motion.set_last_at(at);
        if (!at) continue;
        mark.at = *at;
        marks.push_back(mark);
    }
    return marks;
}

void spread_gather_marks(std::span<GatherMark> marks, std::array<float, 2> apart) {
    if (marks.size() < 2u || !(apart[0] > 0.0f) || !(apart[1] > 0.0f)) return;
    // Closer than this, measured in `apart`, two discs overlap. A row laid out
    // below is exactly 1 apart, and the margin keeps rounding from regrouping it.
    constexpr float kTouch = 1.0f - 1.0e-3f;
    const auto overlap = [&](const GatherMark &a, const GatherMark &b) {
        const float dx = (a.at[0] - b.at[0]) / apart[0];
        const float dy = (a.at[1] - b.at[1]) / apart[1];
        return dx * dx + dy * dy < kTouch * kTouch;
    };
    // A row can reach a mark beside it, so groups that overlap are joined and
    // laid out again until none does. A group stays joined, so its row is not
    // split again, and each pass joins at least two.
    std::vector<std::size_t> group(marks.size());
    for (std::size_t i = 0; i < group.size(); ++i) group[i] = i;
    for (std::size_t pass = 0; pass < marks.size(); ++pass) {
        bool any = false;
        for (std::size_t i = 0; i < marks.size(); ++i)
            for (std::size_t j = i + 1u; j < marks.size(); ++j) {
                if (!overlap(marks[i], marks[j]) || group[i] == group[j]) continue;
                any = true;
                const std::size_t from = group[j];
                for (std::size_t &each : group)
                    if (each == from) each = group[i];
            }
        if (!any) return;
        for (std::size_t first = 0; first < marks.size(); ++first) {
            std::vector<std::size_t> members;
            for (std::size_t i = 0; i < marks.size(); ++i)
                if (group[i] == first) members.push_back(i);
            if (members.size() < 2u) continue;
            float x = 0.0f;
            float y = 0.0f;
            for (const std::size_t i : members) {
                x += marks[i].at[0];
                y += marks[i].at[1];
            }
            const float count = static_cast<float>(members.size());
            x /= count;
            y /= count;
            std::stable_sort(members.begin(), members.end(),
                [&](std::size_t a, std::size_t b) { return marks[a].at[0] < marks[b].at[0]; });
            for (std::size_t n = 0; n < members.size(); ++n) {
                GatherMark &mark = marks[members[n]];
                const std::array<float, 2> to{x + (static_cast<float>(n) - (count - 1.0f) * 0.5f) * apart[0], y};
                if (mark.older)
                    *mark.older = {(*mark.older)[0] + to[0] - mark.at[0], (*mark.older)[1] + to[1] - mark.at[1]};
                mark.at = to;
            }
        }
    }
}

bool GatherRepeat::apply(HunterButtons &buttons, bool ready, std::uint32_t button) {
    const std::uint32_t pad = buttons.held & kPadButtons;
    // L with ○ or □ before a hold starts scrolls the item bar. Once the hold
    // has started, L is another button and must not open that bar.
    if ((pad & kLeadButton) != 0u && !holding_ && !latched_) return false;
    // △+○ and ×+○ are attacks, not a gather, even beside a point or a body,
    // so with one of them down a hold does not start and the words go
    // through. Once a hold runs, the gather comes first: they are ignored.
    if ((pad & kGatherChord) != 0u && !holding_) {
        holding_ = false;
        latched_ = false;
        pressed_ = false;
        return false;
    }
    // A hold already in progress keeps its own button. A new hold takes the
    // caller's, so mining can press □ while carving presses ○.
    if (holding_) {
        button = button_;
    } else if (button != button_) {
        button_ = button;
        pressed_ = false;
    }
    const bool down = (pad & button) != 0u;
    if ((buttons.fresh & button) != 0u) pressed_ = true;
    // A second button can drop the gather button from the resolved pad for a
    // few frames, and the hold goes on through them. Missing for longer, or
    // with nothing held, it has been let go.
    if (down) {
        latched_ = true;
        missing_ = 0u;
    } else if ((pad & ~button) == 0u || ++missing_ > kGatherDropFrames) {
        latched_ = false;
        pressed_ = false;
        missing_ = 0u;
    }
    if (!ready || !latched_ || !pressed_) {
        holding_ = false;
        return false;
    }
    // A press the hunter has just seen is the hold's first. One seen before
    // the point came into reach, or before the hunter was free, is pressed
    // again at once.
    if (!holding_) {
        holding_ = true;
        step_ = 0u;
    }
    const bool press = step_ == 0u || (buttons.fresh & button) != 0u;
    if (step_ == 0u && trace_on()) std::cout << "[gather] hold" << std::endl;
    step_ = (step_ + 1u) % kGatherPressFrames;
    buttons.held = button;
    buttons.fresh = press ? button : 0u;
    buttons.released = 0u;
    buttons.magnitude = 0u;
    return true;
}

namespace {

// The pouch's first slot, or nothing when GameSys does not reach past it (the
// empty stop, slot 24, is the word just after the 24 slots). With the fourth
// pouch page its 32 slots follow it in the host's block (game/pouch_slots.hpp).
std::optional<std::uint32_t> pouch_first(const psprecomp::GuestMemory &memory) {
    if (!in_ram(kGameSysPointer, 4u, memory)) return std::nullopt;
    const std::uint32_t sys = memory.load32(kGameSysPointer);
    if (!in_ram(sys, kPouch + (pouch_slots::kGameSlots + 1u) * 4u, memory)) return std::nullopt;
    const std::uint32_t first = pouch_slots::first(sys);
    if (!in_ram(first, pouch_slots::count() * 4u, memory)) return std::nullopt;
    return first;
}

// The slots of the pouch: 24, or 32 with the fourth page.
std::uint16_t slot_count() {
    return static_cast<std::uint16_t>(pouch_slots::count());
}

// The hunter the camera follows when it is this console's, whose selection
// the item bar shows.
std::optional<std::uint32_t> local_hunter(const psprecomp::GuestMemory &memory) {
    const std::optional<std::uint32_t> hunter = followed_hunter(memory);
    if (!hunter || !in_ram(*hunter + kSelectedSlot, 2u, memory)) return std::nullopt;
    const std::uint32_t sys = memory.load32(kGameSysPointer);
    if (memory.load8(*hunter + kHunterPlayer) != memory.load8(sys + kLocalPlayer)) return std::nullopt;
    return hunter;
}

bool is_tool(std::uint16_t kind, std::uint16_t item) {
    const std::span<const std::uint16_t> tools = kind == 3u ? std::span<const std::uint16_t>(kPickaxes)
        : kind == 4u                                        ? std::span<const std::uint16_t>(kBugNets)
                                                            : std::span<const std::uint16_t>{};
    return std::find(tools.begin(), tools.end(), item) != tools.end();
}

bool holds_tool(const psprecomp::GuestMemory &memory, std::uint32_t pouch, std::uint16_t slot, std::uint16_t kind) {
    return slot < slot_count() && is_tool(kind, memory.load16(pouch + slot * 4u)) &&
        memory.load16(pouch + slot * 4u + 2u) > 0u;
}

} // namespace

std::uint16_t gather_tool_count(const psprecomp::GuestMemory &memory, std::uint16_t item) {
    const std::optional<std::uint32_t> pouch = pouch_first(memory);
    if (!pouch || item == 0u) return 0u;
    std::uint32_t total = 0u;
    for (std::uint16_t slot = 0; slot < slot_count(); ++slot)
        if (memory.load16(*pouch + slot * 4u) == item) total += memory.load16(*pouch + slot * 4u + 2u);
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(total, 0xFFFFu));
}

std::optional<std::uint16_t> gather_tool_item(const psprecomp::GuestMemory &memory, std::uint16_t kind) {
    const std::optional<std::uint16_t> slot = gather_tool_slot(memory, kind);
    const std::optional<std::uint32_t> pouch = pouch_first(memory);
    if (!slot || !pouch) return std::nullopt;
    return memory.load16(*pouch + *slot * 4u);
}

std::optional<std::uint16_t> gather_tool_slot(const psprecomp::GuestMemory &memory, std::uint16_t kind) {
    if (kind != 3u && kind != 4u) return std::nullopt;
    const std::optional<std::uint32_t> hunter = local_hunter(memory);
    const std::optional<std::uint32_t> pouch = pouch_first(memory);
    if (!hunter || !pouch) return std::nullopt;
    const std::uint16_t selected = memory.load16(*hunter + kSelectedSlot);
    if (holds_tool(memory, *pouch, selected, kind)) return selected;
    for (std::uint16_t slot = 0; slot < slot_count(); ++slot)
        if (holds_tool(memory, *pouch, slot, kind)) return slot;
    return std::nullopt;
}

bool gather_tool_selected(const psprecomp::GuestMemory &memory, std::uint16_t kind) {
    const std::optional<std::uint32_t> hunter = local_hunter(memory);
    const std::optional<std::uint32_t> pouch = pouch_first(memory);
    return hunter && pouch && holds_tool(memory, *pouch, memory.load16(*hunter + kSelectedSlot), kind);
}

std::uint32_t gather_button(
    const psprecomp::GuestMemory &memory, GatherGlyph glyph, std::uint16_t kind, bool carve) {
    switch (glyph) {
    case GatherGlyph::Plant:
    case GatherGlyph::Carve:
        return kCircleButton;
    case GatherGlyph::Fishing:
        return kSquareButton;
    case GatherGlyph::Mining:
    case GatherGlyph::BugNet:
        if (!carve && settings::current().gather_circle && gather_tool_slot(memory, kind)) return kCircleButton;
        return gather_tool_selected(memory, kind) ? kSquareButton : 0u;
    }
    return 0u;
}

namespace {

// The copy (game/gather_icon.hpp, HunterButtons) and its one call.
constexpr std::uint32_t kInputCopy = 0x088D61CCu;
constexpr GatherWord kInputWords[] = {
    {0x088D61CCu, 0x27BDFFF0u}, // addiu sp,sp,-16: the copy
    {0x088D61DCu, 0x8C452D88u}, // lw a1,11656(v0): the pad's buttons held
    {0x088D61ECu, 0xAC850480u}, // sw a1,1152(a0): to the hunter
    {0x088D61F0u, 0x8C452D8Cu}, // lw a1,11660(v0): the ones new
    {0x088D61FCu, 0xAC850484u}, // sw a1,1156(a0)
    {0x088D6200u, 0x8C632D90u}, // lw v1,11664(v1): the ones let go
    {0x088D620Cu, 0xAC830488u}, // sw v1,1160(a0)
    {0x088D621Cu, 0x94E72D9Au}, // lhu a3,11674(a3): the stick's magnitude
    {0x088D6224u, 0xA487049Eu}, // sh a3,1182(a0)
    {0x088DB474u, 0x0E235873u}, // jal 0x088D61CC: for the local hunter
};

// 0x08856884: a pouch slot's address, a0 + 0x6AEA6 + 4 * a1, slot 0 past 24.
constexpr std::uint32_t kPouchSlot = 0x08856884u;
constexpr GatherWord kSlotWords[] = {
    {0x08856884u, 0x28A20019u}, // slti v0,a1,25
    {0x08856898u, 0x3C010006u}, // lui at,0x6
    {0x088568A0u, 0x3421AEA6u}, // ori at,at,0xAEA6: the pouch
};

// The circle handler, game_task 0x09AA6E70: a0 the hunter, kept in s2, and
// a1 the button, kept in s0. Its callers pass 2 for □ (0x09AA959C and
// 0x09AA5C04) and 1 or 0 for ○: 1 from the ○ dispatcher (0x09AA9630), 0 from
// the ○ of another hunter state (0x09AA5484), the one a ○ at a fishing spot
// leaves the hunter in. The handler only ever asks whether it is 2. It asks
// the gather search once, past its own checks of the hunter, and switches on
// the kind the search wrote on its stack. Mining and a bug net go on only for
// 2; otherwise the handler goes to the carve search. After it has started
// one, 2 returns before that search.
constexpr std::uint32_t kGatherSearch = 0x09A6F490u;
constexpr std::uint32_t kHandlerSearchReturn = 0x09AA6EF0u;
constexpr std::uint32_t kHandlerStart = 0x09AA6E70u;
constexpr std::uint32_t kHandlerEnd = 0x09AA74A8u;
constexpr std::uint32_t kHandlerKind = 78u; // on the handler's stack
constexpr std::uint32_t kHandlerCircle = 1u; // 0 is ○ as well
constexpr std::uint32_t kHandlerSquare = 2u;
constexpr GatherWord kHandlerWords[] = {
    {0x09A6F490u, 0x27BDFFF0u}, // addiu sp,sp,-16: the gather search
    {0x09AA6E8Cu, 0x00A08021u}, // move s0,a1: the button
    {0x09AA6EE4u, 0x27A5004Eu}, // addiu a1,sp,78: where the search writes the kind
    {0x09AA6EE8u, 0x0E69BD24u}, // jal 0x09A6F490
    {0x09AA6F00u, 0x87A4004Eu}, // lh a0,78(sp): the kind the handler switches on
    {0x09AA6FA4u, 0x24030002u}, // addiu v1,zero,2: mining goes on only for □
    {0x09AA6FA8u, 0x14830100u}, // bne a0,v1: else to the carve search
    {0x09AA729Cu, 0x24030002u}, // addiu v1,zero,2: and so does a bug net
    {0x09AA72A0u, 0x14830042u}, // bne a0,v1
    {0x09AA73ACu, 0x321000FFu}, // andi s0,s0,0xff
    {0x09AA73B4u, 0x12030035u}, // beq s0,v1: □ returns before the carve search
    {0x09AA749Cu, 0x03E00008u}, // jr ra: the end of the handler
    {0x09AA95A0u, 0x24050002u}, // addiu a1,zero,2: the □ dispatcher's call
    {0x09AA9634u, 0x24050001u}, // addiu a1,zero,1: the ○ dispatcher's call
};

// The handler's reads of the selection.
constexpr GatherWord kToolWords[] = {
    {0x09AA6FB8u, 0x0E215A21u}, // jal 0x08856884: mining's tool check
    {0x09AA6FBCu, 0x96450648u}, // lhu a1,1608(s2): of the selection
    {0x09AA7230u, 0x96450648u}, // lhu a1,1608(s2)
    {0x09AA7234u, 0x0E215A21u}, // jal 0x08856884: the pickaxe it keeps
    {0x09AA7248u, 0xA6420694u}, // sh v0,1684(s2): at hunter +1684
    {0x09AA72B0u, 0x0E215A21u}, // jal 0x08856884: the net's tool check
    {0x09AA72B4u, 0x96450648u}, // lhu a1,1608(s2)
    {0x09AA732Cu, 0x0E215A21u}, // jal 0x08856884: the net it keeps
    {0x09AA7330u, 0x96450648u}, // lhu a1,1608(s2)
    {0x09AA7340u, 0xA6420694u}, // sh v0,1684(s2): at hunter +1684
};

// The handler call that takes ○ as □ for a tool, from its gather search until
// the handler searches again or the hunter's next frame.
struct HandlerTool {
    bool active{};
    std::uint32_t hunter{};
    std::uint16_t slot{};
};

HandlerTool &handler_tool() {
    static HandlerTool tool;
    return tool;
}

// Hold to gather on one frame: what is in reach decides whether ○ or □
// gathers, and the hold presses it.
void hold(GatherRepeat &repeat, HunterButtons &buttons, const psprecomp::GuestMemory &memory) {
    const std::uint32_t pad = buttons.held & kPadButtons;
    const bool lead = (pad & kLeadButton) != 0u;
    bool circle_ready = false;
    bool square_ready = false;
    if ((!lead && (pad & (kCircleButton | kSquareButton)) != 0u) || repeat.underway()) {
        const bool circle_gathers = settings::current().gather_circle;
        if (const std::optional<GatherNode> node = gather_node(memory)) {
            // With Gather with ○, mining and a bug net are ○ when the pouch
            // has the tool (gather_input turns each press into □), and □ is
            // never a gather on a quest: it is left to the item bar and the
            // map. Off, they are □ as in the game.
            if (!gather_uses_square(node->kind)) circle_ready = true;
            else if (!circle_gathers) square_ready = true;
            else if (gather_tool_slot(memory, node->kind)) circle_ready = true;
        }
        if (carve_in_reach(memory)) circle_ready = true;
        // The farm's mining outcrops and bug thickets, in the village: □, and
        // ○ as well with Gather with ○.
        if (farm_point(memory)) {
            square_ready = true;
            if (circle_gathers) circle_ready = true;
        }
    }
    // Even away from a point the held button is still tracked, so walking onto
    // one with a press still held starts the hold there.
    std::uint32_t button = repeat.button();
    bool ready = false;
    if (repeat.underway()) {
        ready = button == kSquareButton ? square_ready : circle_ready;
    } else if (!lead && (pad & kCircleButton) != 0u && circle_ready) {
        button = kCircleButton;
        ready = true;
    } else if (!lead && (pad & kSquareButton) != 0u && square_ready) {
        button = kSquareButton;
        ready = true;
    } else if (!lead && (pad & kSquareButton) != 0u) {
        button = kSquareButton;
    } else if (!lead && (pad & kCircleButton) != 0u) {
        button = kCircleButton;
    }
    (void)repeat.apply(buttons, ready, button);
}

// At a Pokke Farm outcrop or thicket, which takes no tool, each new ○ is □
// in the hunter's words until ○ is let go: the farm's handler (lobby_task
// 0x09AAD350) tests □ inline and has no call to give ○ to.
void farm_circle(bool &as_square, HunterButtons &buttons, const psprecomp::GuestMemory &memory) {
    const std::uint32_t pad = buttons.held & kPadButtons;
    if ((pad & kCircleButton) == 0u && (buttons.fresh & kCircleButton) == 0u) as_square = false;
    if ((buttons.fresh & kCircleButton) != 0u)
        as_square = (pad & (kLeadButton | kGatherChord)) == 0u && farm_point(memory).has_value();
    if (!as_square) return;
    for (std::uint32_t *word : {&buttons.held, &buttons.fresh, &buttons.released})
        if ((*word & kCircleButton) != 0u) *word = (*word & ~kCircleButton) | kSquareButton;
}

psprecomp::Runtime::RecompiledFunction input_next{};
psprecomp::Runtime::RecompiledFunction slot_next{};
psprecomp::Runtime::RecompiledFunction search_next{};

// The copy, then both settings on the words it wrote.
void input_copy(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t hunter = ctx.gpr[4];
    const std::uint32_t return_pc = ctx.gpr[31];
    ctx.pc = kInputCopy;
    input_next(runtime, ctx);
    // Back to the dispatcher partway: the rest of the copy writes the words
    // later, so this frame is left to the game.
    if (ctx.pc != return_pc) return;
    psprecomp::GuestMemory &memory = runtime.memory();
    if (!in_ram(hunter + kHunterHeld, kHunterMagnitude + 2u - kHunterHeld, memory)) return;
    HunterButtons buttons{memory.load32(hunter + kHunterHeld), memory.load32(hunter + kHunterFresh),
        memory.load32(hunter + kHunterReleased), memory.load16(hunter + kHunterMagnitude)};
    const HunterButtons game = buttons;
    gather_input(buttons, hunter, memory);
    if (buttons.held != game.held) memory.store32(hunter + kHunterHeld, buttons.held);
    if (buttons.fresh != game.fresh) memory.store32(hunter + kHunterFresh, buttons.fresh);
    if (buttons.released != game.released) memory.store32(hunter + kHunterReleased, buttons.released);
    if (buttons.magnitude != game.magnitude) memory.store16(hunter + kHunterMagnitude, buttons.magnitude);
}

// The gather search. Asked by the circle handler, ○ may then be the handler's □.
void gather_search(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t return_pc = ctx.gpr[31];
    const bool from_handler = return_pc == kHandlerSearchReturn;
    if (from_handler) handler_tool() = {};
    ctx.pc = kGatherSearch;
    search_next(runtime, ctx);
    if (!from_handler || ctx.pc != return_pc) return;
    psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t kind_at = ctx.gpr[29] + kHandlerKind;
    if (!in_ram(kind_at, 2u, memory)) return;
    std::uint32_t button = ctx.gpr[16];
    gather_handler_searched(memory, ctx.gpr[18], button, static_cast<std::uint16_t>(ctx.gpr[2]),
        memory.load16(kind_at));
    ctx.gpr[16] = button;
}

// The circle handler's reads of the selection, while it takes ○ as □.
void pouch_slot(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if (const std::optional<std::uint16_t> slot = gather_slot_for(ctx.gpr[31], ctx.gpr[18])) {
        if (trace_on())
            std::cout << "[gather] slot " << *slot << " in place of " << (ctx.gpr[5] & 0xFFFFu) << " for 0x"
                      << std::hex << ctx.gpr[31] << std::dec << std::endl;
        ctx.gpr[5] = *slot;
    } else if (trace_on() && ctx.gpr[31] >= kHandlerStart && ctx.gpr[31] < kHandlerEnd) {
        std::cout << "[gather] the handler reads slot " << (ctx.gpr[5] & 0xFFFFu) << " for 0x" << std::hex
                  << ctx.gpr[31] << std::dec << std::endl;
    }
    ctx.pc = kPouchSlot;
    slot_next(runtime, ctx);
}

} // namespace

std::span<const GatherWord> gather_input_words() {
    return kInputWords;
}

std::span<const GatherWord> gather_slot_words() {
    return kSlotWords;
}

std::span<const GatherWord> gather_tool_words() {
    return kToolWords;
}

std::span<const GatherWord> gather_handler_words() {
    return kHandlerWords;
}

namespace {

// The local hunter's frames since ○ was last new in its words.
std::uint32_t &circle_age() {
    static std::uint32_t age = kGatherCircleRecent;
    return age;
}

// ○ is down in the hunter's words, or was new in them within the last
// kGatherCircleRecent frames: the game keeps a ○ pressed during an action and
// asks the handler only once the action ends, when it is no longer new (the
// hold's next press while mining). A call the game makes with ○ long let go
// is never a gather.
bool circle_pressed(const psprecomp::GuestMemory &memory, std::uint32_t hunter) {
    if (circle_age() < kGatherCircleRecent) return true;
    if (!in_ram(hunter + kHunterHeld, kHunterFresh + 4u - kHunterHeld, memory)) return false;
    return ((memory.load32(hunter + kHunterHeld) | memory.load32(hunter + kHunterFresh)) & kCircleButton) != 0u;
}

} // namespace

std::optional<std::uint16_t> circle_gather(
    const psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint32_t button, std::uint16_t kind) {
    if (!settings::current().gather_circle || (button & 0xFFu) > kHandlerCircle || !gather_uses_square(kind))
        return std::nullopt;
    const std::optional<std::uint32_t> local = local_hunter(memory);
    if (!local || *local != hunter || !circle_pressed(memory, hunter) || carve_in_reach(memory))
        return std::nullopt;
    return gather_tool_slot(memory, kind);
}

namespace {

// Why the handler's ○ stays ○, for the trace.
const char *circle_left(const psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint32_t button,
    std::uint16_t id, std::uint16_t kind) {
    if (id == 0xFFFFu) return "no point found";
    if (!settings::current().gather_circle) return "the setting is off";
    if ((button & 0xFFu) > kHandlerCircle) return "the button is □";
    if (!gather_uses_square(kind)) return "the point takes ○ already";
    const std::optional<std::uint32_t> local = local_hunter(memory);
    if (!local) return "no local hunter";
    if (*local != hunter) return "another hunter";
    if (!circle_pressed(memory, hunter)) return "○ is not down or just pressed";
    if (carve_in_reach(memory)) return "a body to carve";
    return "no tool in the pouch";
}

} // namespace

void gather_handler_searched(const psprecomp::GuestMemory &memory, std::uint32_t hunter, std::uint32_t &button,
    std::uint16_t id, std::uint16_t kind) {
    handler_tool() = {};
    const std::optional<std::uint16_t> slot =
        id == 0xFFFFu ? std::nullopt : circle_gather(memory, hunter, button, kind);
    if (trace_on())
        std::cout << "[gather] the circle handler searched: button " << (button & 0xFFu) << ", point 0x" << std::hex
                  << id << std::dec << ", kind " << kind << ", hunter 0x" << std::hex << hunter << std::dec
                  << (slot ? "" : ", left: ") << (slot ? "" : circle_left(memory, hunter, button, id, kind))
                  << std::endl;
    if (!slot) return;
    button = (button & ~0xFFu) | kHandlerSquare;
    handler_tool() = HandlerTool{true, hunter, *slot};
    circle_age() = kGatherCircleRecent; // that press is taken
    if (trace_on()) std::cout << "[gather] the circle handler takes ○ as □ with the tool in slot " << *slot << std::endl;
}

void gather_input(HunterButtons &buttons, std::uint32_t hunter, psprecomp::GuestMemory &memory) {
    static GatherRepeat repeat;
    static bool as_square = false;
    handler_tool() = {};
    // The searches read the hunter the camera follows.
    if (hunter == 0u || hunter != layout::followed_hunter(memory, kSelectedSlot + 2u)) {
        repeat.reset();
        as_square = false;
        return;
    }
    if (settings::current().gather_hold) hold(repeat, buttons, memory);
    else repeat.reset();
    if (settings::current().gather_circle) farm_circle(as_square, buttons, memory);
    else as_square = false;
    std::uint32_t &age = circle_age();
    if ((buttons.fresh & kCircleButton) != 0u) age = 0u;
    else if (age < kGatherCircleRecent) ++age;
}

std::optional<std::uint16_t> gather_slot_for(std::uint32_t return_address, std::uint32_t hunter) {
    const HandlerTool &tool = handler_tool();
    if (!tool.active || tool.hunter != hunter) return std::nullopt;
    if (return_address < kHandlerStart || return_address >= kHandlerEnd) return std::nullopt;
    return tool.slot;
}

void gather_frame(psprecomp::Runtime &runtime) {
    const settings::Settings &s = settings::current();
    if (!s.gather_hold && !s.gather_circle) return;
    const auto install = [&](std::uint32_t address, std::span<const GatherWord> words, bool &logged,
                             psprecomp::Runtime::RecompiledFunction wrapper,
                             psprecomp::Runtime::RecompiledFunction &next, const char *name) {
        if (runtime.function_at(address) == nullptr || !words_match(runtime.memory(), words, logged)) return false;
        const bool was_in_place = runtime.function_at(address) == wrapper;
        // None of them switches a thread: chained, their callers run them inline.
        if (!hooks::wrap(runtime, address, wrapper, next, name, true)) return false;
        if (trace_on() && !was_in_place)
            std::cout << "[gather] " << name << " in place at 0x" << std::hex << address << std::dec << std::endl;
        return true;
    };
    static bool input_logged = false;
    if (!install(kInputCopy, kInputWords, input_logged, &input_copy, input_next, "mhfu_gather_input")) return;
    if (!s.gather_circle) return;
    // The slot first and the search only after it, so the handler never takes
    // ○ as □ without the tool's slot. game_task is not always loaded: the
    // search is put in place again each time it is.
    static bool slot_logged = false;
    if (!install(kPouchSlot, kSlotWords, slot_logged, &pouch_slot, slot_next, "mhfu_gather_slot")) return;
    static bool tool_logged = false;
    if (!words_match(runtime.memory(), kToolWords, tool_logged)) return;
    static bool handler_logged = false;
    (void)install(kGatherSearch, kHandlerWords, handler_logged, &gather_search, search_next, "mhfu_gather_search");
}

} // namespace mhfu::game
