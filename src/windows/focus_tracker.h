#pragma once

#include "app/app_state.h"

namespace quicktile {

inline constexpr COLORREF kDwmDefaultColor = 0xFFFFFFFF;

class FocusTracker {
public:
    static void SetWindowBorderColor(const AppState &app, HWND hwnd, bool foregroundWindow);
    static HWND ResolveManagedWindow(AppState& app, HWND hwnd);
    static HWND ResolveManagedForegroundWindow(AppState& app);
    static void UpdateFocusedWindowBorder(AppState& app, HWND focusedWindow);
    static void ReapplyManagedWindowBorderColors(AppState& app);
    static void ScheduleBorderRefresh(AppState& app);
    static bool HandleTimer(AppState& app, WPARAM timerId);
};

}  // namespace quicktile