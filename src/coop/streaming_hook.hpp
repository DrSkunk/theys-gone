#pragma once
#include "ue.hpp"
#include <cstdint>
#include <string>

namespace coop::streaming {

using ue::FVector;

struct FocusReport {
    bool installed = false, active = false;
    FVector focus{};
    uint64_t merges = 0;
    int primaryVisible = 0, secondaryVisible = 0, mergedVisible = 0, added = 0, refined = 0, forced = 0;
    float thresholdMeters = 0;
};

// Hooks the world-composition tile selector on the supported build only; a mismatch leaves the game untouched.
bool Install(uintptr_t imageBase);
bool Installed();
// Game thread. Adds a second streaming focus in the current world's local coordinates; inactive clears it.
void SetSecondFocus(bool active, const FVector& local);
FocusReport Report();

} // namespace coop::streaming
