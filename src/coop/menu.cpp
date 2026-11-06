#include "menu.hpp"
#include "log.hpp"

#include <windows.h>
#include <xinput.h>

#include <array>
#include <bit>

namespace coop {
namespace {

WORD GamepadButtons() {
    static unsigned previousConnections = ~0u;
    static ULONGLONG nextConnectionPoll = 0;
    ULONGLONG now = GetTickCount64();
    if (!previousConnections && now < nextConnectionPoll) return 0;
    using GetState = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    static auto providers = [] {
        const wchar_t* names[] = {L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll"};
        std::array<GetState, 3> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            auto name = names[i];
            HMODULE module = LoadLibraryW(name);
            if (module) result[i] = std::bit_cast<GetState>(GetProcAddress(module, "XInputGetState"));
        }
        return result;
    }();
    WORD buttons = 0;
    unsigned connected = 0;
    for (size_t provider = 0; provider < providers.size(); ++provider) {
        auto get = providers[provider];
        if (!get) continue;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE state{};
            if (get(i, &state) == ERROR_SUCCESS) {
                connected |= 1u << (provider * XUSER_MAX_COUNT + i);
                buttons |= state.Gamepad.wButtons;
            }
        }
    }
    nextConnectionPoll = now + 250;
    if (connected != previousConnections) {
        previousConnections = connected;
        Log("menu: XInput controller mask=0x%x (providers 1_3, 1_4, 9_1_0; four slots each)", connected);
        if (!connected) Log("menu: no XInput controller is exposed; native Steam Input layouts may require gamepad emulation");
    }
    return buttons;
}
} // namespace

bool PartnerShortcuts::PassengerPressed(bool focused) {
    bool f9 = focused && (GetAsyncKeyState(VK_F9) & 0x8000);
    constexpr WORD chord = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_X;
    bool held = focused && (GamepadButtons() & chord) == chord;
    bool pressed = (f9 && !f9_) || (held && !chord_);
    f9_ = f9;
    chord_ = held;
    return pressed;
}

} // namespace coop
