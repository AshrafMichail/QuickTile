#include "windows/event_router.h"

#include "windows/focus_tracker.h"
#include "layout/layout_engine.h"
#include "platform/logger.h"
#include "workspace/virtual_desktop.h"
#include "windows/window_geometry.h"
#include "windows/window_manager.h"
#include "windows/window_classifier.h"
#include "workspace/workspace_manager.h"

#include <algorithm>
#include <deque>
#include <mutex>

namespace quicktile {

namespace {

std::deque<EventRouter::PostedEvent> g_postedEvents;
std::mutex g_postedEventsMutex;
AppState* g_callbackApp = nullptr;

struct EventBurstState {
    bool refreshDesktopContext = false;
    bool retileAll = false;
    bool refreshFocus = false;
    HWND focusWindow = nullptr;
    std::vector<HMONITOR> monitorsToRestoreMaximized;
};

void AddMonitorOnce(std::vector<HMONITOR>& monitors, HMONITOR monitor) {
    if (monitor == nullptr) {
        return;
    }

    if (std::find(monitors.begin(), monitors.end(), monitor) == monitors.end()) {
        monitors.push_back(monitor);
    }
}

void RestoreMaximizedWindowsOnMonitor(HMONITOR monitor) {
    struct EnumData {
        HMONITOR monitor;
    } data{monitor};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto& enumData = *reinterpret_cast<EnumData*>(lParam);
            if (IsZoomed(hwnd) && IsWindowVisible(hwnd) &&
                MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == enumData.monitor) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));
}

HMONITOR MonitorForManagedWindow(AppState& app, HWND hwnd) {
    for (const auto& [key, state] : WorkspaceManager::WorkspaceMonitors(app)) {
        if (WindowManager::ContainsWindow(state.windows, hwnd)) {
            return key;
        }
    }

    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

}  // namespace

void EventRouter::SetCallbackApp(AppState* app) {
    g_callbackApp = app;
}

EventRouter::Action EventRouter::ClassifyEvent(DWORD event, LONG objectId, LONG childId) {
    switch (event) {
    case EVENT_SYSTEM_MOVESIZESTART:
        return Action::MoveSizeStart;
    case EVENT_SYSTEM_MOVESIZEEND:
        return Action::MoveSizeEnd;
    case EVENT_SYSTEM_MINIMIZESTART:
    case EVENT_SYSTEM_MINIMIZEEND:
        return Action::WindowStateChanged;
    case EVENT_SYSTEM_DESKTOPSWITCH:
        return Action::DesktopSwitched;
    case EVENT_SYSTEM_FOREGROUND:
        return Action::ForegroundChanged;
    case EVENT_OBJECT_FOCUS:
        return Action::FocusChanged;
    default:
        break;
    }

    if (objectId != OBJID_WINDOW || childId != CHILDID_SELF) {
        return Action::Ignore;
    }

    switch (event) {
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_DESTROY:
        return Action::WindowHiddenOrDestroyed;
    case EVENT_OBJECT_SHOW:
        return Action::WindowShown;
    case EVENT_OBJECT_LOCATIONCHANGE:
        return Action::LocationChanged;
    default:
        return Action::Ignore;
    }
}

bool EventRouter::ShouldHandleLocationChange(HWND moveSizeWindow, HMONITOR sourceMonitor, HMONITOR destinationMonitor) {
    return moveSizeWindow == nullptr && sourceMonitor != nullptr && sourceMonitor != destinationMonitor;
}

bool EventRouter::ShouldTileSourceMonitorAfterWindowMove(HMONITOR sourceMonitor, HMONITOR destinationMonitor) {
    return sourceMonitor != nullptr && sourceMonitor != destinationMonitor;
}

bool EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(HMONITOR tiledMonitor, HMONITOR sourceMonitor, HMONITOR destinationMonitor) {
    if (tiledMonitor == nullptr || sourceMonitor == destinationMonitor) {
        return false;
    }

    return tiledMonitor == sourceMonitor || tiledMonitor == destinationMonitor;
}

bool EventRouter::ShouldTileForWindowClassificationChange(HMONITOR monitor, const std::vector<HWND>& previous, const std::vector<HWND>& classified) {
    return WindowManager::BuildOrderedWindows(monitor, previous, classified) != previous;
}

void RefreshFocusBorder(AppState& app, HWND hwnd) {
    const HWND managedWindow = hwnd != nullptr
        ? FocusTracker::ResolveManagedWindow(app, hwnd)
        : FocusTracker::ResolveManagedForegroundWindow(app);
    app.windowState.focused = managedWindow;
    FocusTracker::UpdateFocusedWindowBorder(app, managedWindow);
}

void HandleMoveSizeStart(AppState& app, HWND hwnd) {
    app.windowState.moveSize = hwnd;
    if (hwnd != nullptr) {
        GetWindowRect(hwnd, &app.windowState.moveSizeStartRect);
    }
    app.monitorState.moveSizeOriginMonitor = hwnd != nullptr ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY) : nullptr;
}

void HandleMoveSizeEnd(AppState& app, HWND hwnd, EventBurstState& burstState) {
    const HMONITOR monitor = hwnd != nullptr ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY) : WindowManager::ActiveMonitor();
    const HMONITOR sourceMonitor = app.monitorState.moveSizeOriginMonitor;
    bool handledResize = false;
    if (hwnd != nullptr && sourceMonitor == monitor) {
        auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
        auto monitorIterator = workspace.find(WindowManager::MonitorKey(monitor));
        if (monitorIterator != workspace.end()) {
            handledResize = LayoutEngine::UpdateSplitFromResize(
                monitorIterator->second,
                hwnd,
                monitor,
                app.windowState.moveSizeStartRect,
                app.settings.defaultMainWidthRatio,
                WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(app.settings.innerGap, 0, 256)),
                WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(app.settings.outerGap, 0, 256)));
        }
    }
    app.windowState.moveSize = nullptr;
    app.monitorState.moveSizeOriginMonitor = nullptr;

    if (!handledResize && hwnd != nullptr && WindowManager::IsManagedWindow(app, hwnd)) {
        WindowManager::ReorderWindowByDrop(app, hwnd, sourceMonitor, monitor);
        if (EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(sourceMonitor, sourceMonitor, monitor)) {
            AddMonitorOnce(burstState.monitorsToRestoreMaximized, sourceMonitor);
        }
        if (EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(monitor, sourceMonitor, monitor)) {
            AddMonitorOnce(burstState.monitorsToRestoreMaximized, monitor);
        }
    }

    burstState.retileAll = true;
    burstState.refreshFocus = true;
}

void ApplyPostedEvent(AppState& app, const EventRouter::PostedEvent& postedEvent, EventBurstState& burstState) {
    switch (EventRouter::ClassifyEvent(postedEvent.event, postedEvent.objectId, postedEvent.childId)) {
    case EventRouter::Action::MoveSizeStart:
        HandleMoveSizeStart(app, postedEvent.hwnd);
        return;
    case EventRouter::Action::MoveSizeEnd:
        HandleMoveSizeEnd(app, postedEvent.hwnd, burstState);
        return;
    case EventRouter::Action::WindowStateChanged:
        burstState.retileAll = true;
        burstState.refreshFocus = true;
        return;
    case EventRouter::Action::DesktopSwitched:
        burstState.refreshDesktopContext = true;
        burstState.retileAll = true;
        burstState.refreshFocus = true;
        return;
    case EventRouter::Action::ForegroundChanged: {
        burstState.refreshFocus = true;
        burstState.focusWindow = postedEvent.hwnd;
        const HWND managedWindow = FocusTracker::ResolveManagedWindow(app, postedEvent.hwnd);
        if (managedWindow != nullptr) {
            AddMonitorOnce(
                burstState.monitorsToRestoreMaximized,
                MonitorFromWindow(managedWindow, MONITOR_DEFAULTTONEAREST));
            burstState.retileAll = true;
        }
        return;
    }
    case EventRouter::Action::FocusChanged:
        burstState.refreshFocus = true;
        burstState.focusWindow = postedEvent.hwnd;
        return;
    case EventRouter::Action::WindowHiddenOrDestroyed:
        if (postedEvent.hwnd == app.windowState.focused) {
            app.windowState.focused = nullptr;
        }
        burstState.retileAll = true;
        burstState.refreshFocus = true;
        return;
    case EventRouter::Action::WindowShown:
        if (postedEvent.hwnd != nullptr && WindowManager::IsManagedWindow(app, postedEvent.hwnd)) {
            AddMonitorOnce(
                burstState.monitorsToRestoreMaximized,
                MonitorFromWindow(postedEvent.hwnd, MONITOR_DEFAULTTONEAREST));
            burstState.retileAll = true;
            burstState.refreshFocus = true;
        }
        return;
    case EventRouter::Action::LocationChanged:
        if (postedEvent.hwnd == nullptr || !WindowManager::IsManagedWindow(app, postedEvent.hwnd)) {
            return;
        }

        {
            const HMONITOR sourceMonitor = MonitorForManagedWindow(app, postedEvent.hwnd);
            const HMONITOR destinationMonitor = MonitorFromWindow(postedEvent.hwnd, MONITOR_DEFAULTTONEAREST);
            if (!EventRouter::ShouldHandleLocationChange(app.windowState.moveSize, sourceMonitor, destinationMonitor)) {
                return;
            }

            AddMonitorOnce(burstState.monitorsToRestoreMaximized, sourceMonitor);
            AddMonitorOnce(burstState.monitorsToRestoreMaximized, destinationMonitor);
            WindowManager::ReorderWindowByDrop(app, postedEvent.hwnd, sourceMonitor, destinationMonitor);
            burstState.retileAll = true;
            burstState.refreshFocus = true;
        }
        return;
    case EventRouter::Action::Ignore:
        return;
    }
}

void ApplyEventBurst(AppState& app, const EventBurstState& burstState) {
    if (burstState.refreshDesktopContext) {
        WorkspaceManager::RefreshVirtualDesktopContext(app);
    }

    for (HMONITOR monitor : burstState.monitorsToRestoreMaximized) {
        RestoreMaximizedWindowsOnMonitor(monitor);
    }

    if (burstState.retileAll) {
        WindowManager::TileAllKnownMonitors(app);
    }

    if (burstState.refreshFocus) {
        RefreshFocusBorder(app, burstState.focusWindow);
    }
}

void EventRouter::DrainPostedEvents(AppState& app) {
    std::deque<EventRouter::PostedEvent> pendingEvents;
    {
        std::lock_guard<std::mutex> lock(g_postedEventsMutex);
        pendingEvents.swap(g_postedEvents);
    }

    EventBurstState burstState;
    for (const PostedEvent& postedEvent : pendingEvents) {
        ApplyPostedEvent(app, postedEvent, burstState);
    }

    ApplyEventBurst(app, burstState);
}

void CALLBACK EventRouter::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG objectId, LONG childId, DWORD, DWORD) {
    if (g_callbackApp == nullptr || g_callbackApp->window == nullptr) {
        return;
    }

    if (ClassifyEvent(event, objectId, childId) == Action::Ignore) {
        return;
    }

    bool posted = false;
    {
        std::lock_guard<std::mutex> lock(g_postedEventsMutex);
        g_postedEvents.push_back(PostedEvent{event, hwnd, objectId, childId});
        posted = PostMessageW(g_callbackApp->window, kQueuedWinEventMessage, 0, 0) != FALSE;
        if (!posted) {
            g_postedEvents.pop_back();
        }
    }

    if (!posted) {
        g_callbackApp->logger.ErrorLastWin32(L"Failed to queue WinEvent to UI thread", GetLastError());
    }
}

}  // namespace quicktile
