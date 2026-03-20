#pragma once

#include "app/app_state.h"

#include <windows.h>

namespace quicktile {

inline constexpr UINT kTrayIconMessage = WM_APP + 3;

class Systray {
public:
    static bool Initialize(AppState& app, HWND window);
    static void Shutdown(HWND window);
    static bool HandleNotificationMessage(AppState& app, HWND window, LPARAM lParam);
    static bool HandleCommand(AppState& app, UINT command, HWND window);
    static void ReloadSettings(AppState& app);
};

}  // namespace quicktile