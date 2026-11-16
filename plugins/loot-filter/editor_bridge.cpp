#include "editor_bridge.h"

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/plugin_communication.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace CelestialRayOne::LootFilter::EditorBridge {
namespace {

using LootFilterBridge::FilterState;
using LootFilterBridge::HoveredItem;
using LootFilterBridge::Result;

// A hover older than this is over: the select tests run every frame while
// something is pointed at.
constexpr std::chrono::milliseconds MaxHoverAge{ 200 };

// The smallest struct a caller may pass: structSize and the first field.
constexpr std::uint32_t HoveredItemMinimum = static_cast<std::uint32_t>(offsetof(HoveredItem, unitId) + sizeof(HoveredItem::unitId));
constexpr std::uint32_t FilterStateMinimum = static_cast<std::uint32_t>(offsetof(FilterState, loaded) + sizeof(FilterState::loaded));

Callbacks Calls{};

// The last hover. Written and read on the UI thread; the parts are separate
// atomics, and a mixed read is harmless: describeItem looks the unit up by id
// and answers NoItem unless both agree.
std::atomic<void*>                                   HoverUnit{};
std::atomic<std::uint32_t>                           HoverId{};
std::atomic<std::chrono::steady_clock::duration::rep> HoverTime{};

auto IsActive() -> bool {
    return Calls.active != nullptr && Calls.active();
}

// The caller's structSize, or 0 when the pointer is null or the size is too small.
template <typename T>
auto CallerSize(const T* value, std::uint32_t minimum) -> std::uint32_t {
    if (value == nullptr) return 0;
    std::uint32_t size = 0;
    std::memcpy(&size, value, sizeof(size));
    return size >= minimum ? size : 0;
}

// Copies the answer, but no more than the caller's struct holds, and keeps
// the caller's structSize.
template <typename T>
void CopyOut(T* destination, T& answer, std::uint32_t callerSize) {
    answer.structSize = callerSize;
    std::memcpy(destination, &answer, std::min<std::size_t>(callerSize, sizeof(T)));
}

template <std::size_t N>
void Terminate(char (&text)[N]) {
    text[N - 1] = '\0';
}

auto __cdecl GetHoveredItem(HoveredItem* item) noexcept -> Result {
    try {
        if (!IsActive()) return Result::NotActive;
        const std::uint32_t size = CallerSize(item, HoveredItemMinimum);
        if (size == 0) return Result::InvalidArgument;
        const auto time = HoverTime.load(std::memory_order_acquire);
        void* const unit = HoverUnit.load(std::memory_order_relaxed);
        const std::uint32_t id = HoverId.load(std::memory_order_relaxed);
        const auto age = std::chrono::steady_clock::now().time_since_epoch() - std::chrono::steady_clock::duration(time);
        if (unit == nullptr || age < std::chrono::steady_clock::duration::zero() || age > MaxHoverAge) return Result::NoItem;
        HoveredItem answer{};
        const Result result = Calls.describeItem(unit, id, answer);
        if (result != Result::Success) return result;
        Terminate(answer.code);
        Terminate(answer.name);
        CopyOut(item, answer, size);
        return Result::Success;
    } catch (...) {
        return Result::Failed;
    }
}

auto __cdecl GetFilterState(FilterState* state) noexcept -> Result {
    try {
        if (!IsActive()) return Result::NotActive;
        const std::uint32_t size = CallerSize(state, FilterStateMinimum);
        if (size == 0) return Result::InvalidArgument;
        FilterState answer{};
        const Result result = Calls.filterState(answer);
        if (result != Result::Success) return result;
        Terminate(answer.filterId);
        Terminate(answer.path);
        Terminate(answer.title);
        Terminate(answer.failure);
        for (auto& name : answer.levelNames) Terminate(name);
        answer.levelCount = std::min(answer.levelCount, LootFilterBridge::MaxLevels);
        CopyOut(state, answer, size);
        return Result::Success;
    } catch (...) {
        return Result::Failed;
    }
}

auto __cdecl GetRuleTitle(std::int32_t ruleNumber, char* text, std::uint32_t textSize, std::uint32_t* required) noexcept -> Result {
    try {
        if (!IsActive()) return Result::NotActive;
        // A buffer pointer without a size is fine; a size without a buffer is not. And
        // without a buffer, the size is all there is to learn.
        if ((text == nullptr && textSize != 0) || (text == nullptr && required == nullptr)) return Result::InvalidArgument;
        std::string title;
        const Result result = Calls.ruleTitle(ruleNumber, title);
        if (result != Result::Success) return result;
        title.resize(std::strlen(title.c_str()));  // a C string ends at its first zero
        if (title.size() >= std::numeric_limits<std::uint32_t>::max()) return Result::Failed;
        const auto needed = static_cast<std::uint32_t>(title.size() + 1);
        if (required != nullptr) *required = needed;
        if (text == nullptr || textSize < needed) {
            if (text != nullptr && textSize > 0) text[0] = '\0';
            return Result::BufferTooSmall;
        }
        std::memcpy(text, title.c_str(), needed);
        return Result::Success;
    } catch (...) {
        return Result::Failed;
    }
}

auto __cdecl PrintChat(const char* utf8) noexcept -> Result {
    try {
        if (!IsActive()) return Result::NotActive;
        if (utf8 == nullptr) return Result::InvalidArgument;
        if (*utf8 != '\0') Calls.queueChat(std::string(utf8));
        return Result::Success;
    } catch (...) {
        return Result::Failed;
    }
}

constexpr LootFilterBridge::ServiceTable Table{
    .structSize     = static_cast<std::uint32_t>(sizeof(LootFilterBridge::ServiceTable)),
    .reserved       = 0,
    .getHoveredItem = &GetHoveredItem,
    .getFilterState = &GetFilterState,
    .getRuleTitle   = &GetRuleTitle,
    .printChat      = &PrintChat,
};
static_assert(sizeof(LootFilterBridge::ServiceTable) >= LootFilterBridge::ServiceTableRequiredSize);

}  // namespace

void Publish(const D2RL::PluginContext* context, const Callbacks& callbacks) noexcept {
    if (context == nullptr) return;
    if (callbacks.active == nullptr || callbacks.describeItem == nullptr || callbacks.filterState == nullptr
        || callbacks.ruleTitle == nullptr || callbacks.queueChat == nullptr) {
        context->LogWarn("LootFilter: the editor bridge is incomplete and was not offered.");
        return;
    }
    Calls = callbacks;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        context->LogWarn("LootFilter: no plugin communication service; the editor bridge is off.");
        return;
    }
    // The table is static: there is no unpublish, so it stays valid until the plugin unloads.
    const D2RL::PluginCommunication::PublishServiceRequest request{
        .structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
        .flags          = 0,
        .name           = LootFilterBridge::ServiceName,
        .serviceVersion = LootFilterBridge::ServiceVersion,
        .tableSize      = Table.structSize,
        .table          = &Table,
    };
    const D2RL::PluginCommunication::Result result = communication->publishService(context, &request);
    char message[160]{};
    if (result != D2RL::PluginCommunication::Result::Success) {
        std::snprintf(message, sizeof(message), "LootFilter: the editor bridge was refused (result %u); the filter works as before.",
                      static_cast<unsigned>(result));
        context->LogWarn(message);
        return;
    }
    std::snprintf(message, sizeof(message), "LootFilter: editor bridge offered (%s, version %u).", LootFilterBridge::ServiceName,
                  static_cast<unsigned>(LootFilterBridge::ServiceVersion));
    context->LogInfo(message);
}

void NoteHover(void* unit, std::uint32_t unitId) noexcept {
    HoverUnit.store(unit, std::memory_order_relaxed);
    HoverId.store(unitId, std::memory_order_relaxed);
    HoverTime.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_release);
}

auto PlainText(std::string_view text) -> std::string {
    constexpr std::string_view code = "\xC3\xBF" "c";
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t found = text.find(code, pos);
        if (found == std::string_view::npos) {
            out.append(text.substr(pos));
            break;
        }
        out.append(text.substr(pos, found - pos));
        pos = found + code.size();
        // The color character: one byte, or one whole UTF-8 character should a text use another.
        if (pos < text.size()) {
            ++pos;
            while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) ++pos;
        }
    }
    return out;
}

}  // namespace CelestialRayOne::LootFilter::EditorBridge
