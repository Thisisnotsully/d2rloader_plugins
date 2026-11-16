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

namespace D2RL {
struct PluginContext;
}

namespace CelestialRayOne::LootFilter::EditorBridge {

// What the bridge asks plugin.cpp. All set, or Publish publishes nothing.
// They may throw; the bridge turns that into Result::Failed.
struct Callbacks {
    // False until the plugin is active and again once it unloads: every call answers NotActive then.
    bool (*active)();
    // Fills `item` (zeroed, full size) from the unit the select tests last
    // accepted. First checks it is still the item `unitId`, on the ground;
    // NoItem when not. UI thread.
    LootFilterBridge::Result (*describeItem)(void* unit, std::uint32_t unitId, LootFilterBridge::HoveredItem& item);
    // Fills `state` (zeroed, full size). Any thread.
    LootFilterBridge::Result (*filterState)(LootFilterBridge::FilterState& state);
    // The comment of rule `ruleNumber`: NoFilter, NotFound or Success. Any thread.
    LootFilterBridge::Result (*ruleTitle)(std::int32_t ruleNumber, std::string& title);
    // Queues a line as one of the plugin's own. Any thread.
    void (*queueChat)(std::string line);
};

// Offers the table through the plugin communication service. Without that
// service, or when it refuses, the log says so and nothing else changes.
// Call once, at load, after the plugin became active.
void Publish(const D2RL::PluginContext* context, const Callbacks& callbacks) noexcept;

// Records `unit` as pointed at now. The select test hooks call it for every
// item they accept, so it only stores; it reads nothing. UI thread.
void NoteHover(void* unit, std::uint32_t unitId) noexcept;

// `text` without the game's color codes (ÿc and the character after it).
auto PlainText(std::string_view text) -> std::string;

}  // namespace CelestialRayOne::LootFilter::EditorBridge
