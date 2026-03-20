#include "windows/focus_tracker.h"

#include "layout/layout_engine.h"
#include "windows/window_manager.h"

#include <array>

#include <dwmapi.h>

namespace quicktile {

namespace {

constexpr UINT_PTR kBorderRefreshTimerId = 1;
constexpr UINT kBorderRefreshDelayMs = 200;

}  // namespace

void FocusTracker::SetWindowBorderColor(const AppState& app, HWND hwnd, bool foregroundWindow) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    auto &color = foregroundWindow ? app.settings.focusedBorderColor : kDwmDefaultColor;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));
}

HWND FocusTracker::ResolveManagedWindow(AppState& app, HWND hwnd) {
    const std::array<HWND, 4> candidates = {
        hwnd,
        hwnd != nullptr ? GetAncestor(hwnd, GA_ROOT) : nullptr,
        hwnd != nullptr ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr,
        GetForegroundWindow(),
    };

    for (HWND candidate : candidates) {
        if (candidate != nullptr && IsWindow(candidate) && WindowManager::IsManagedWindow(app, candidate)) {
            return candidate;
        }
    }

    return nullptr;
}

HWND FocusTracker::ResolveManagedForegroundWindow(AppState& app) {
    return ResolveManagedWindow(app, GetForegroundWindow());
}

void FocusTracker::UpdateFocusedWindowBorder(AppState& app, HWND focusedWindow) {
    if (app.windowState.focused != nullptr && app.windowState.focused != focusedWindow) {
        SetWindowBorderColor(app, app.windowState.focused, false);
    }

    app.windowState.focused = nullptr;

    if (!WindowManager::IsManagedWindow(app, focusedWindow)) {
        return;
    }

    SetWindowBorderColor(app, focusedWindow, true);
    app.windowState.focused = focusedWindow;
    ScheduleBorderRefresh(app);
}

void FocusTracker::ReapplyManagedWindowBorderColors(AppState& app) {
    const HWND foregroundWindow = ResolveManagedForegroundWindow(app);
    auto windows = WindowManager::CollectTiledWindows(app);

    if (app.windowState.focused != nullptr &&
        (!IsWindow(app.windowState.focused) || !WindowManager::ContainsWindow(windows, app.windowState.focused) || app.windowState.focused != foregroundWindow)) {
        SetWindowBorderColor(app, app.windowState.focused, false);
    }

    app.windowState.focused = nullptr;
    for (HWND hwnd : windows) {
        const bool isFocusedWindow = hwnd == foregroundWindow;
        SetWindowBorderColor(app, hwnd, isFocusedWindow);
        if (isFocusedWindow) {
            app.windowState.focused = hwnd;
        }
    }
}

void FocusTracker::ScheduleBorderRefresh(AppState& app) {
    if (app.window == nullptr) {
        return;
    }

    SetTimer(app.window, kBorderRefreshTimerId, kBorderRefreshDelayMs, nullptr);
}

bool FocusTracker::HandleTimer(AppState& app, WPARAM timerId) {
    if (timerId != kBorderRefreshTimerId) {
        return false;
    }

    if (app.window != nullptr) {
        KillTimer(app.window, kBorderRefreshTimerId);
    }

    ReapplyManagedWindowBorderColors(app);
    return true;
}

}  // namespace quicktile