#pragma once

#include "app/app_state.h"

namespace quicktile {

struct WindowDropTarget {
    HMONITOR monitor = nullptr;
    HWND targetWindow = nullptr;
    RECT previewRect{};
    int insertIndex = 0;
    bool replacesWindow = false;
};

class WorkspaceManager {
public:
    static int CurrentWorkspaceIndex(const AppState& app);
    static MonitorStateMap& WorkspaceMonitors(AppState& app);
    static const MonitorStateMap& WorkspaceMonitors(const AppState& app);
    static MonitorState& GetOrCreateMonitorState(AppState& app, HMONITOR monitor);
    static MonitorState& ReconcileMonitorState(AppState& app, HMONITOR monitor);
    static WindowDropTarget ResolveWindowDropTarget(AppState& app, HWND hwnd, HMONITOR destinationMonitor, const POINT* dropPoint = nullptr);
    static void ReorderWindowByDrop(AppState& app, HWND hwnd, HMONITOR sourceMonitor, HMONITOR destinationMonitor);
    static bool SwapWindows(AppState& app, HWND sourceWindow, HMONITOR sourceMonitor, HWND targetWindow, HMONITOR targetMonitor);
    static void SetVirtualDesktopContext(AppState& app, std::wstring desktopKey);
    static void RefreshVirtualDesktopContext(AppState& app);
    static bool SwitchWorkspace(AppState& app, int workspaceNumber);
    static void ApplyMonitorLayout(AppState& app, HMONITOR monitor, const MonitorState& state);
};

}  // namespace quicktile