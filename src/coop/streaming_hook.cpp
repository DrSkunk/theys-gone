#include "streaming_hook.hpp"
#include "log.hpp"
#include "streaming.hpp"

#include <windows.h>

#include <cmath>
#include <cstring>

// World-composition streaming on the supported DaysGone.exe (see docs/REPLICATION-RESEARCH.md, "Independent
// roaming"). Bend's UWorldComposition::GetDistanceVisibleLevels takes a single location, so a second player far
// from the host never gets tiles. The selector is run once per focus and the results are merged.
namespace coop::streaming {
namespace {

constexpr uintptr_t kUpdateRva = 0x2c65b30;           // UpdateStreamingState(locations, count, flag, check)
constexpr uintptr_t kSelectorRva = 0x2c60250;         // GetDistanceVisibleLevels(location, visible, hidden)
constexpr uintptr_t kSelectorReturnRva = 0x2c65c60;   // the update's own call; other callers keep one focus
constexpr uintptr_t kReallocRva = 0x19740c0;          // FMemory::Realloc(pointer, bytes, alignment)
constexpr size_t kLastPosition = 0x28, kThreshold = 0x60, kTiles = 0x50, kTileCount = 0x58;
constexpr size_t kTileStride = 0xe0, kTileFlags = 0xa0;
constexpr uint32_t kOneShotHide = 0x100;

constexpr uint8_t kUpdatePrologue[16] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56,
                                         0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0x01};
constexpr uint8_t kSelectorPrologue[15] = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54,
                                           0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08};
constexpr uint8_t kSelectorCall[5] = {0xe8, 0xf0, 0xa5, 0xff, 0xff};

struct RawArray {
    TileRecord* data;
    int32_t count, capacity;
};

using UpdateFn = uint64_t (*)(void*, const FVector*, int32_t, uint8_t, uint8_t);
using SelectorFn = void (*)(void*, const FVector*, RawArray*, RawArray*);
using ReallocFn = void* (*)(void*, size_t, uint32_t);

struct State {
    uintptr_t base = 0;
    UpdateFn update = nullptr;
    SelectorFn selector = nullptr;
    ReallocFn realloc = nullptr;
    bool installed = false;
    // Written by the game thread's tick and read by the world tick on the same thread.
    bool active = false, selectedActive = false, refresh = false;
    FVector focus{}, selectedFocus{};
    float thresholdSquared = 0;
    FocusReport report;
} g;

void* Patch(uint8_t* target, const uint8_t* expected, size_t length, void* hook) {
    if (std::memcmp(target, expected, length) != 0) return nullptr;
    auto tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(tramp, target, length);
    auto back = reinterpret_cast<uint64_t>(target + length);
    std::memcpy(jmp + 6, &back, 8);
    std::memcpy(tramp + length, jmp, sizeof jmp);
    uint8_t patch[16];
    std::memset(patch, 0x90, sizeof patch);
    auto to = reinterpret_cast<uint64_t>(hook);
    std::memcpy(jmp + 6, &to, 8);
    std::memcpy(patch, jmp, sizeof jmp);
    DWORD old;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &old)) return nullptr;
    std::memcpy(target, patch, length);
    VirtualProtect(target, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, length);
    return tramp;
}

void Copy(const RawArray& from, std::vector<TileRecord>& to) {
    to.assign(from.data, from.data + (from.data && from.count > 0 ? from.count : 0));
}

void Store(RawArray& to, const std::vector<TileRecord>& from) {
    int32_t count = static_cast<int32_t>(from.size());
    if (count > to.capacity) {
        auto grown = static_cast<TileRecord*>(g.realloc(to.data, from.size() * sizeof(TileRecord), 0));
        if (!grown) return;
        to.data = grown;
        to.capacity = count;
    }
    if (count) std::memcpy(to.data, from.data(), from.size() * sizeof(TileRecord));
    to.count = count;
}

uint64_t HookedUpdate(void* composition, const FVector* locations, int32_t count, uint8_t flag, uint8_t check) {
    if (composition && g.refresh) {
        // The update skips selection until its own focus moves; forget that position once to re-select.
        auto bytes = static_cast<uint8_t*>(composition);
        float distant = 1e30f;
        std::memcpy(bytes + kLastPosition, &distant, sizeof distant);
        std::memcpy(bytes + kLastPosition + 4, &distant, sizeof distant);
        g.refresh = false;
    }
    return g.update(composition, locations, count, flag, check);
}

void HookedSelector(void* composition, const FVector* location, RawArray* visible, RawArray* hidden) {
    auto from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (!g.active || from != g.base + kSelectorReturnRva || !composition || !location || !visible || !hidden) {
        g.selector(composition, location, visible, hidden);
        if (from == g.base + kSelectorReturnRva) g.selectedActive = false;
        return;
    }
    auto bytes = static_cast<uint8_t*>(composition);
    float threshold = 0;
    std::memcpy(&threshold, bytes + kThreshold, sizeof threshold);
    g.thresholdSquared = threshold;
    std::vector<int32_t> flagged;
    auto tiles = *reinterpret_cast<uint8_t**>(bytes + kTiles);
    int32_t tileCount = *reinterpret_cast<int32_t*>(bytes + kTileCount);
    for (int32_t i = 0; tiles && i < tileCount; ++i) {
        uint32_t flags = 0;
        std::memcpy(&flags, tiles + static_cast<size_t>(i) * kTileStride + kTileFlags, sizeof flags);
        if (flags & kOneShotHide) flagged.push_back(i);
    }
    Selection primary, secondary;
    g.selector(composition, location, visible, hidden);
    Copy(*visible, primary.visible);
    Copy(*hidden, primary.hidden);
    FVector focus = g.focus;
    g.selector(composition, &focus, visible, hidden);
    Copy(*visible, secondary.visible);
    Copy(*hidden, secondary.hidden);
    MergeStats stats;
    auto merged = MergeSelections(primary, secondary, flagged, &stats);
    Store(*visible, merged.visible);
    Store(*hidden, merged.hidden);
    g.selectedActive = true;
    g.selectedFocus = focus;
    auto& r = g.report;
    int added = static_cast<int>(stats.added), forced = static_cast<int>(stats.forced);
    bool changed = added != r.added || forced != r.forced || !r.merges;
    ++r.merges;
    r.primaryVisible = static_cast<int>(primary.visible.size());
    r.secondaryVisible = static_cast<int>(secondary.visible.size());
    r.mergedVisible = static_cast<int>(merged.visible.size());
    r.added = added;
    r.refined = static_cast<int>(stats.refined);
    r.forced = forced;
    if (changed)
        Log("streaming: second focus (%.0f, %.0f) keeps %d extra tiles visible (host %d, partner %d, "
            "finer LOD %d, one-shot hidden %d)", focus.x, focus.y, added, r.primaryVisible, r.secondaryVisible,
            r.refined, forced);
}

} // namespace

bool Install(uintptr_t imageBase) {
    if (g.installed) return true;
    g.base = imageBase;
    auto update = reinterpret_cast<uint8_t*>(imageBase + kUpdateRva);
    auto selector = reinterpret_cast<uint8_t*>(imageBase + kSelectorRva);
    auto call = reinterpret_cast<uint8_t*>(imageBase + kSelectorReturnRva - sizeof kSelectorCall);
    if (std::memcmp(call, kSelectorCall, sizeof kSelectorCall) != 0 ||
        std::memcmp(update, kUpdatePrologue, sizeof kUpdatePrologue) != 0 ||
        std::memcmp(selector, kSelectorPrologue, sizeof kSelectorPrologue) != 0) {
        Log("streaming: world-composition code differs from the supported build; partner streaming disabled");
        return false;
    }
    g.realloc = reinterpret_cast<ReallocFn>(imageBase + kReallocRva);
    g.selector = reinterpret_cast<SelectorFn>(Patch(selector, kSelectorPrologue, sizeof kSelectorPrologue,
                                                    reinterpret_cast<void*>(&HookedSelector)));
    if (!g.selector) {
        Log("streaming: selector hook failed; partner streaming disabled");
        return false;
    }
    g.update = reinterpret_cast<UpdateFn>(Patch(update, kUpdatePrologue, sizeof kUpdatePrologue,
                                                reinterpret_cast<void*>(&HookedUpdate)));
    if (!g.update) {
        // The selector hook alone is still correct; the partner's tiles refresh whenever the host moves.
        Log("streaming: update hook failed; partner tiles refresh only when the host moves");
    }
    g.installed = g.report.installed = true;
    Log("streaming: world-composition hooks installed");
    return true;
}

bool Installed() { return g.installed; }

void SetSecondFocus(bool active, const FVector& local) {
    if (!g.installed) return;
    g.active = active;
    if (active) g.focus = local;
    float dx = local.x - g.selectedFocus.x, dy = local.y - g.selectedFocus.y;
    float threshold = g.thresholdSquared > 1e4f ? g.thresholdSquared : 1e6f;
    if (FocusNeedsRefresh(active, g.selectedActive, dx, dy, threshold)) g.refresh = true;
    if (active != g.report.active) Log("streaming: second focus %s", active ? "on" : "off");
    g.report.active = active;
    g.report.focus = g.focus;
}

FocusReport Report() {
    auto r = g.report;
    r.thresholdMeters = g.thresholdSquared > 0 ? std::sqrt(g.thresholdSquared) / 100.f : 0;
    return r;
}

} // namespace coop::streaming
