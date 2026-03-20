#pragma once

#include "app/app_state.h"

namespace quicktile {

inline constexpr UINT kQueuedWinEventMessage = WM_APP + 4;

class EventRouter {
public:
    struct PostedEvent {
        DWORD event = 0;
        HWND hwnd = nullptr;
        LONG objectId = 0;
        LONG childId = 0;
    };

    enum class Action {
        Ignore,
        MoveSizeStart,
        MoveSizeEnd,
        WindowStateChanged,
        DesktopSwitched,
        ForegroundChanged,
        FocusChanged,
        WindowHiddenOrDestroyed,
        WindowShown,
        LocationChanged,
    };

    static Action ClassifyEvent(DWORD event, LONG objectId, LONG childId);
    static bool ShouldHandleLocationChange(HWND moveSizeWindow, HMONITOR sourceMonitor, HMONITOR destinationMonitor);
    static bool ShouldTileSourceMonitorAfterWindowMove(HMONITOR sourceMonitor, HMONITOR destinationMonitor);
    static bool ShouldRestoreMaximizedWindowsBeforeMonitorRetile(HMONITOR tiledMonitor, HMONITOR sourceMonitor, HMONITOR destinationMonitor);
    static bool ShouldTileForWindowClassificationChange(HMONITOR monitor, const std::vector<HWND>& previous, const std::vector<HWND>& classified);
    static void SetCallbackApp(AppState* app);
    static void DrainPostedEvents(AppState& app);
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG objectId, LONG childId, DWORD, DWORD);
};

}  // namespace quicktile