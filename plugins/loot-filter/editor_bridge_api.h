#pragma once
//
// Loot filter editor bridge: the service the loot filter plugin offers other
// plugins (the in-game filter editor first of all) through D2RLoader's plugin
// communication service.
//
// It only reads. Nothing in it changes how the filter behaves, and when the
// loot filter is not active every call answers NotActive.
//
// This header is the whole contract. Both sides include it; the provider is
// editor_bridge.cpp, a consumer copies this file. Plain C types only, so the
// two DLLs never share C++ library objects.
//
// Getting the table (consumer side):
//
//     D2RL::PluginCommunication::ServiceLease<LootFilterBridge::ServiceTable> lease;
//     D2RL::PluginCommunication::Acquire(context, communication,
//         LootFilterBridge::ProviderPluginId, LootFilterBridge::ServiceName,
//         LootFilterBridge::ServiceVersion, LootFilterBridge::ServiceTableRequiredSize, &lease);
//
// Plugins load in no set order, so acquire it the first time it is needed
// (not in D2RLoaderLoadPlugin), and try again later when that fails.
//
// Versions: a newer bridge only adds. New functions go at the end of
// ServiceTable, new fields at the end of the structs, and ServiceVersion goes
// up. Every struct starts with structSize: the caller sets it to the size it
// knows, the bridge fills only that much and never writes past it. A breaking
// change would get a new ServiceName.
//
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace LootFilterBridge {

inline constexpr char     ProviderPluginId[] = "celestialrayone.loot-filter";
inline constexpr char     ServiceName[]      = "editor-bridge";
inline constexpr uint32_t ServiceVersion     = 1;

enum class Result : uint32_t {
    Success         = 0,
    NotActive       = 1,  // the loot filter is not active (disabled, unsupported build, unloading)
    NoItem          = 2,  // no ground item under the cursor or the controller's target
    NoFilter        = 3,  // no filter is loaded
    NotFound        = 4,  // no such rule
    BufferTooSmall  = 5,  // *required holds the bytes needed, trailing zero included
    InvalidArgument = 6,  // null pointer or a structSize too small
    Failed          = 7,  // anything else; the loot filter's log may say more
};

inline constexpr uint32_t MaxMatchedRules = 64;
inline constexpr uint32_t MaxLevels       = 16;

// The ground item the player points at: under the mouse cursor, or the item a
// controller has targeted, as the game itself decided a moment ago. Items the
// filter hides cannot be pointed at, unless Show Hidden Items is on (or the
// game's own item filter toggle is off; gold can always be pointed at).
// Texts are UTF-8 without color codes and always end with a zero.
struct HoveredItem {
    uint32_t structSize;          // set by the caller
    uint32_t unitId;              // the game's id of the item unit
    char     code[8];             // item code, e.g. "hax"
    char     name[256];           // the item's name in the game's language
    int32_t  quality;             // 1 inferior .. 9 tempered, as the filter's "quality"
    int32_t  baseTier;            // 0 normal, 1 exceptional, 2 elite, -1 none
    int32_t  itemLevel;
    int32_t  sockets;
    int32_t  maxSockets;
    int32_t  uniqueOrSetId;       // uniqueitems.txt / setitems.txt row, -1 none
    uint8_t  ethereal;            // 0 or 1, here and below
    uint8_t  identified;
    uint8_t  runeword;
    uint8_t  gold;
    // What the active filter does with it now.
    uint8_t  hidden;              // a rule hides it (it can only be pointed at with Show Hidden Items on)
    uint8_t  nameChanged;
    uint8_t  reserved[2];
    int32_t  hiddenBy;            // the hiding rule's number; -1 hideUnmatchedItems; 0 not hidden
    uint32_t matchedCount;        // every rule that matched, possibly more than the list holds
    int32_t  matchedRules[MaxMatchedRules];  // rule numbers (1-based file order), in order
    uint64_t filterRevision;      // FilterState::revision this answer belongs to
};

// The loaded filter. Texts are UTF-8 and end with a zero; level names are in
// the game's language.
struct FilterState {
    uint32_t structSize;          // set by the caller
    uint8_t  loaded;              // 0: no filter or it failed to load (failure says why); the rest is empty
    uint8_t  shipped;             // 1: the file ships with the mod (copy it before editing)
    uint8_t  showHidden;          // Show Hidden Items is on
    uint8_t  hoverAvailable;      // 0: the mouse and controller select hooks are not both installed; hovers may not be seen
    char     filterId[260];       // the launcher's id: "shipped:x.json", "user:x.json", "repo:r/x.json"
    char     path[1024];          // the filter file
    char     title[256];
    char     failure[256];        // why it did not load
    int32_t  level;               // the active level's id, 0 when the filter has none
    uint32_t levelCount;          // up to MaxLevels
    int32_t  levelIds[MaxLevels];
    char     levelNames[MaxLevels][64];
    uint32_t ruleCount;           // rules in the file
    uint32_t errors;              // problems found when it loaded
    uint32_t warnings;
    uint64_t textHash;            // changes whenever the file's text changes
    uint64_t revision;            // changes whenever any answer could change (reload, level, Show Hidden Items)
};

// Every function is noexcept: errors come back as a Result.
// Threads: getHoveredItem runs on the game's UI thread only (an input action
// callback is there). The others may run on any thread.
struct ServiceTable {
    uint32_t structSize;  // the provider's size of this table
    uint32_t reserved;

    // The item the player points at now. NoItem when there is none.
    Result(__cdecl* getHoveredItem)(HoveredItem* item) noexcept;

    // The loaded filter.
    Result(__cdecl* getFilterState)(FilterState* state) noexcept;

    // The title (the rule's "comment") of rule `ruleNumber` of the loaded
    // filter, in UTF-8. Empty when the rule has no comment. Pass a null or
    // small buffer to learn the size: BufferTooSmall and *required.
    Result(__cdecl* getRuleTitle)(int32_t ruleNumber, char* text, uint32_t textSize, uint32_t* required) noexcept;

    // Prints a line in the chat window, like the loot filter's own lines
    // (ÿc color codes work). Queued: it shows on the next UI tick.
    Result(__cdecl* printChat)(const char* utf8) noexcept;
};

inline constexpr uint32_t ServiceTableRequiredSize = static_cast<uint32_t>(offsetof(ServiceTable, printChat) + sizeof(void*));

static_assert(std::is_standard_layout_v<HoveredItem> && std::is_trivially_copyable_v<HoveredItem>);
static_assert(std::is_standard_layout_v<FilterState> && std::is_trivially_copyable_v<FilterState>);
static_assert(std::is_standard_layout_v<ServiceTable> && std::is_trivially_copyable_v<ServiceTable>);

}  // namespace LootFilterBridge
