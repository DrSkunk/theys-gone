#pragma once

#include "session.hpp"
#include "ue.hpp"

namespace coop {

// Player preferences, changed in the native CO-OP > PLAYER SETTINGS page.
struct MenuSettings {
    int jacket = 0;
    int trousers = 0;
    int capColor = 0;
    bool cap = true;
    bool motorcycles = true; // always on
    bool passengers = true;
    bool friendlyFire = false;
    bool enemies = true;     // always on
};

using session::kPaletteCount;
using session::PaletteName;

// Gameplay shortcuts that work without opening a menu: F9 or View/Back + X joins or leaves the
// partner's bike. Teleport (TeleportKey, F7 by default) is handled in coop.cpp.
class PartnerShortcuts {
public:
    bool PassengerPressed(bool focused);

private:
    bool f9_ = false;
    bool chord_ = false;
};

} // namespace coop
