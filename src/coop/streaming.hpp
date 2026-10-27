#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace coop::streaming {

// UWorldComposition's FDistanceVisibleLevel on the supported build: tile index, its ULevelStreaming and the
// selected LOD. LOD -1 is the full-detail level and higher indices are coarser.
struct TileRecord {
    int32_t tile = 0;
    int32_t pad0 = 0;
    void* level = nullptr;
    int32_t lod = -1;
    int32_t pad1 = 0;
};
static_assert(sizeof(TileRecord) == 24, "FDistanceVisibleLevel layout");

struct Selection {
    std::vector<TileRecord> visible, hidden;
};

struct MergeStats {
    size_t added = 0;   // visible only because of the second focus
    size_t refined = 0; // visible for both, at a finer LOD for the second focus
    size_t forced = 0;  // one-shot variant tiles kept hidden
};

// Combines two single-focus selections. A tile is kept visible if either focus needs it, at the finer LOD.
// Tiles whose one-shot hide flag was set before selection stay hidden if either pass hid them, because the
// selector clears that flag and the other pass would no longer reproduce the hide.
inline Selection MergeSelections(const Selection& primary, const Selection& secondary,
                                 const std::vector<int32_t>& flagged, MergeStats* stats = nullptr) {
    MergeStats local;
    std::unordered_set<int32_t> forced;
    if (!flagged.empty()) {
        std::unordered_set<int32_t> flaggedSet(flagged.begin(), flagged.end());
        for (const auto* hidden : {&primary.hidden, &secondary.hidden})
            for (const auto& r : *hidden)
                if (flaggedSet.count(r.tile)) forced.insert(r.tile);
    }
    Selection out;
    std::unordered_map<int32_t, size_t> visibleAt;
    for (const auto& r : primary.visible) {
        if (forced.count(r.tile) || visibleAt.count(r.tile)) continue;
        visibleAt[r.tile] = out.visible.size();
        out.visible.push_back(r);
    }
    std::unordered_set<int32_t> primaryVisible;
    for (const auto& r : primary.visible) primaryVisible.insert(r.tile);
    for (const auto& r : secondary.visible) {
        if (forced.count(r.tile)) continue;
        auto it = visibleAt.find(r.tile);
        if (it == visibleAt.end()) {
            visibleAt[r.tile] = out.visible.size();
            out.visible.push_back(r);
            if (!primaryVisible.count(r.tile)) ++local.added;
        } else if (r.lod < out.visible[it->second].lod) {
            out.visible[it->second].lod = r.lod;
            ++local.refined;
        }
    }
    std::unordered_set<int32_t> hiddenSeen;
    for (const auto* hidden : {&primary.hidden, &secondary.hidden})
        for (const auto& r : *hidden) {
            if (visibleAt.count(r.tile) || !hiddenSeen.insert(r.tile).second) continue;
            out.hidden.push_back(r);
        }
    local.forced = forced.size();
    if (stats) *stats = local;
    return out;
}

// The engine only re-selects tiles after the primary focus moves far enough; a moving second focus needs
// its own threshold to request a refresh.
inline bool FocusNeedsRefresh(bool active, bool wasActive, float dx, float dy, float thresholdSquared) {
    if (active != wasActive) return true;
    return active && dx * dx + dy * dy >= thresholdSquared;
}

// Free-roam separation with hysteresis, so players hovering at the boundary do not flip region ownership.
constexpr float kSeparateDistance = 25000.f, kRejoinDistance = 15000.f; // cm, horizontal

inline bool NextSeparated(bool separated, float horizontalDistance) {
    return horizontalDistance > (separated ? kRejoinDistance : kSeparateDistance);
}

// Debug offsets: "<east m> <north m>", each within 20 km.
inline bool ParseOffsetMeters(const std::string& text, float& east, float& north) {
    char tail = 0;
    if (std::sscanf(text.c_str(), "%f %f %c", &east, &north, &tail) != 2) return false;
    return std::isfinite(east) && std::isfinite(north) && std::fabs(east) <= 20000 && std::fabs(north) <= 20000;
}

} // namespace coop::streaming
