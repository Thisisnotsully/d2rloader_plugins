#pragma once
//
// The provider side of the editor bridge (editor_bridge_api.h): the service
// table, the size checks on what callers pass, and the hovered item record.
//
// It knows nothing of the plugin's internals. plugin.cpp hands it a few
// functions at publish time and does the reading itself, so either side can
// change without the other.
//
#include "editor_bridge_api.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace CelestialRayOne::LootFilter::EditorBridge {

// What the bridge asks plugin.cpp. All set, or Publish publishes nothing.
// They may throw; the bridge turns that into Result::Failed.
struct Callbacks {
    // False until the plugin is active and again once it unloads: every call answers NotActive then.
    bool (*active)();
    // Fills `item` (zeroed, full size) from `unit`, the item the select tests
    // or a tooltip last showed. First checks it is still the item `unitId`;
    // NoItem when not. An item off the ground is judged as if dropped where the
    // player is. UI thread.
    LootFilterBridge::Result (*describeItem)(void* unit, std::uint32_t unitId, LootFilterBridge::HoveredItem& item);
    // Fills `state` (zeroed, full size). Any thread.
    LootFilterBridge::Result (*filterState)(LootFilterBridge::FilterState& state);
    // The comment of rule `ruleNumber`: NoFilter, NotFound or Success. Any thread.
    LootFilterBridge::Result (*ruleTitle)(std::int32_t ruleNumber, std::string& title);
    // Queues a line as one of the plugin's own. Any thread.
    void (*queueChat)(std::string line);
    // Every stat of the item `unitId` if it is still an item (on the ground or
    // carried), else NoItem; an unidentified item gives Success and no stats. UI thread.
    LootFilterBridge::Result (*itemStats)(std::uint32_t unitId, std::vector<LootFilterBridge::ItemStat>& stats);
};

// Offers the table through the plugin communication service. Without that
// service, or when it refuses, the log says so and nothing else changes.
// Call once, at load, after the plugin became active.
void Publish(const D2RL::PluginContext* context, const Callbacks& callbacks) noexcept;

// Records `unit` as pointed at now. The select test hooks call it for every
// item they accept, so it only stores; it reads nothing. UI thread.
void NoteHover(void* unit, std::uint32_t unitId) noexcept;

// Records `unit` as the item whose tooltip the game is drawing now. The item
// name hook calls it when a tooltip asks for a name, every frame the tooltip
// shows. When several tooltips are drawn in one frame (the game's comparison
// with what is equipped), the first one, the hovered item, is kept. UI thread.
void NoteTooltip(void* unit, std::uint32_t unitId) noexcept;

// `text` without color codes: ÿc, or the game's own marker when given, and
// the character after it.
auto PlainText(std::string_view text, std::string_view gameMarker = {}) -> std::string;

}  // namespace CelestialRayOne::LootFilter::EditorBridge
