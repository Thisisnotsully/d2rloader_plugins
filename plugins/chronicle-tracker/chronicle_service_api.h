#pragma once
//
// Chronicle service: what the Chronicle Tracker offers other plugins (the loot
// filter's "chronicle" condition first of all) through D2RLoader's plugin
// communication service.
//
// One question: is this item's Chronicle entry done for the local character?
// The answer follows the same rules as the "Not in Chronicle" label line, so
// a filter rule and the label always agree.
//
// It only reads. It is offered when the client side of this D2R build matches
// (the checks the label line needs), even when the label line is switched off
// in the config, and not at all when the plugin is disabled.
//
// This header is the whole contract; a consumer includes it. Plain C types only.
//
// Getting the table (consumer side):
//
//     D2RL::PluginCommunication::ServiceLease<ChronicleService::ServiceTable> lease;
//     D2RL::PluginCommunication::Acquire(context, communication,
//         ChronicleService::ProviderPluginId, ChronicleService::ServiceName,
//         ChronicleService::ServiceVersion, ChronicleService::ServiceTableRequiredSize, &lease);
//
// Plugins load in no set order, so acquire it the first time it is needed
// (not in D2RLoaderLoadPlugin), and try again later when that fails.
//
// Versions: a newer service only adds. New functions go at the end of
// ServiceTable and ServiceVersion goes up. A breaking change would get a new
// ServiceName.
//
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ChronicleService {

inline constexpr char     ProviderPluginId[] = "celestialrayone.chronicle-tracker";
inline constexpr char     ServiceName[]      = "chronicle";
inline constexpr uint32_t ServiceVersion     = 1;

enum class Status : uint32_t {
    NotTracked = 0,  // not a Chronicle item: not a set item, unique or runeword, or one the game leaves out
    Missing    = 1,  // a Chronicle item whose entry the local character has not completed
    Completed  = 2,  // a Chronicle item whose entry is done
    Unknown    = 3,  // cannot tell now: no local character yet, not an item, or the service is off
};

struct ServiceTable {
    uint32_t structSize;  // the provider's size of this table
    uint32_t reserved;

    // The Chronicle status of `item`, a game item unit (the pointer the game's
    // own functions pass around), for the local character. Wherever the item
    // is: on the ground, in an inventory, at a vendor. Cheap enough to call
    // for every item the loot filter looks at.
    // Game UI thread only (the thread labels, drops and input run on).
    Status(__cdecl* itemStatus)(const void* item) noexcept;
};

inline constexpr uint32_t ServiceTableRequiredSize = static_cast<uint32_t>(offsetof(ServiceTable, itemStatus) + sizeof(void*));

static_assert(std::is_standard_layout_v<ServiceTable> && std::is_trivially_copyable_v<ServiceTable>);

}  // namespace ChronicleService
