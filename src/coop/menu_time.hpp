#pragma once
#include <cstdint>
#include <optional>

namespace coop {

enum class WheelTimeAction { none, suppress, restore };

inline std::optional<bool> MenuPauseTarget(bool connected, bool localMenu, bool peerMenu, bool managed) {
    if (connected && localMenu) return peerMenu;
    if (managed) return localMenu;
    return std::nullopt;
}

class WheelTimePolicy {
public:
    WheelTimeAction Evaluate(bool connected, bool open, int32_t request) {
        if (!open || request == -1) {
            Clear();
            return WheelTimeAction::none;
        }
        if (connected)
            return suppressed_ && request == request_ ? WheelTimeAction::none : WheelTimeAction::suppress;
        if (suppressed_ && request == request_) return WheelTimeAction::restore;
        Clear();
        return WheelTimeAction::none;
    }
    void Suppressed(int32_t request) { suppressed_ = true; request_ = request; }
    void Clear() { suppressed_ = false; request_ = -1; }

private:
    bool suppressed_ = false;
    int32_t request_ = -1;
};

} // namespace coop
