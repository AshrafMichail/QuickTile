#include "workspace/workspace_manager.h"

#include "windows/event_router.h"
#include "windows/focus_tracker.h"
#include "layout/layout_engine.h"
#include "layout/layout_helpers.h"
#include "workspace/virtual_desktop.h"
#include "windows/window_geometry.h"
#include "windows/window_classifier.h"
#include "windows/window_manager.h"
#include "workspace/workspace_model.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace quicktile {

namespace {

bool IsInvalidDetectedDesktopKey(std::wstring_view desktopKey) {
    return desktopKey.empty() || desktopKey == L"{00000000-0000-0000-0000-000000000000}";
}

float DefaultMainWidthRatio(const Settings& settings) {
    return LayoutEngine::ClampMainWidthRatio(settings.defaultMainWidthRatio);
}

std::vector<HWND> PreserveWorkspaceWindows(HMONITOR monitor, const std::vector<HWND>& previous) {
    std::vector<HWND> preserved;
    preserved.reserve(previous.size());

    for (HWND hwnd : previous) {
        if (hwnd == nullptr || !IsWindow(hwnd)) {
            continue;
        }

        if (!IsWindowVisible(hwnd)) {
            continue;
        }

        if (IsIconic(hwnd)) {
            continue;
        }

        if (IsZoomed(hwnd)) {
            continue;
        }

        if (WindowClassifier::IsCloakedWindow(hwnd)) {
            continue;
        }

        if (!VirtualDesktop::IsWindowOnCurrentDesktop(hwnd)) {
            continue;
        }

        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != monitor) {
            continue;
        }

        preserved.push_back(hwnd);
    }

    return preserved;
}

void SyncMonitorStateWindows(MonitorState& state, std::vector<HWND> orderedWindows, const Settings& settings) {
    if (state.layoutMode == LayoutMode::Floating) {
        state.windows = std::move(orderedWindows);
        return;
    }

    LayoutEngine::SyncMonitorWindows(state, std::move(orderedWindows), DefaultMainWidthRatio(settings));
}

int PlannedWindowDropIndex(HMONITOR monitor, const MonitorState& state, const std::vector<HWND>& orderedWindows, HWND droppedWindow, const Settings& settings) {
    const int innerGap = WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(settings.innerGap, 0, 256));
    const int outerGap = WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(settings.outerGap, 0, 256));

    if (orderedWindows.empty()) {
        return 0;
    }

    RECT droppedRect{};
    if (!GetWindowRect(droppedWindow, &droppedRect)) {
        return static_cast<int>(orderedWindows.size());
    }

    MonitorState plannedState = state;
    if (plannedState.layoutMode == LayoutMode::Floating) {
        return static_cast<int>(orderedWindows.size());
    }

    LayoutEngine::SyncMonitorWindows(plannedState, orderedWindows, DefaultMainWidthRatio(settings));

    WorkspaceModel::MonitorData monitorData;
    monitorData.handle = monitor;
    monitorData.rect = LayoutEngine::WorkAreaForMonitor(monitor);
    monitorData.layoutMode = plannedState.layoutMode;
    monitorData.mainWidthRatio = LayoutEngine::ClampMainWidthRatio(plannedState.mainWidthRatio);
    monitorData.splitWeights = LayoutEngine::ExportMonitorSplitState(plannedState).splitWeights;
    monitorData.tiles.reserve(plannedState.windows.size());
    for (HWND window : plannedState.windows) {
        monitorData.tiles.push_back(WorkspaceModel::TileData{window, {}});
    }

    const WorkspaceModel model = WorkspaceModel::BuildForTesting({std::move(monitorData)});
    const WorkspaceModel::LayoutPlan plan = model.BuildLayoutPlan(monitor, innerGap, outerGap);
    const std::optional<std::size_t> targetIndex = WorkspaceModel::FindDropTargetIndex(plan, LayoutHelpers::RectCenter(droppedRect));
    return targetIndex.has_value() ? static_cast<int>(*targetIndex) : static_cast<int>(orderedWindows.size());
}

bool ResolveDropPoint(HWND droppedWindow, const POINT* explicitDropPoint, POINT& dropPoint) {
    if (explicitDropPoint != nullptr) {
        dropPoint = *explicitDropPoint;
        return true;
    }

    if (GetCursorPos(&dropPoint) != FALSE) {
        return true;
    }

    RECT droppedRect{};
    if (!GetWindowRect(droppedWindow, &droppedRect)) {
        return false;
    }

    dropPoint = LayoutHelpers::RectCenter(droppedRect);
    return true;
}

WindowDropTarget ResolveHoveredWindowDropTarget(
    const std::vector<HWND>& orderedWindows,
    HWND droppedWindow,
    const POINT& dropPoint,
    HMONITOR destinationMonitor) {
    WindowDropTarget dropTarget;
    dropTarget.monitor = destinationMonitor;

    for (std::size_t index = 0; index < orderedWindows.size(); ++index) {
        const HWND candidate = orderedWindows[index];
        if (candidate == nullptr || candidate == droppedWindow || !IsWindow(candidate)) {
            continue;
        }

        RECT candidateRect{};
        if (!GetWindowRect(candidate, &candidateRect)) {
            continue;
        }

        if (!LayoutHelpers::ContainsPoint(candidateRect, dropPoint)) {
            continue;
        }

        dropTarget.targetWindow = candidate;
        dropTarget.previewRect = candidateRect;
        dropTarget.insertIndex = static_cast<int>(index);
        dropTarget.replacesWindow = true;
        return dropTarget;
    }

    return dropTarget;
}

void PruneInvalidWindows(std::vector<HWND>& windows) {
    windows.erase(std::remove_if(windows.begin(), windows.end(), [](HWND hwnd) { return !IsWindow(hwnd); }), windows.end());
}

std::wstring ActiveDesktopKey(const AppState& app) {
    return app.currentDesktopKey.empty() ? L"global" : app.currentDesktopKey;
}

DesktopWorkspaceState& ActiveDesktopWorkspaceState(AppState& app) {
    const std::wstring desktopKey = ActiveDesktopKey(app);
    auto [iterator, _] = app.monitorState.workspaceStatesByDesktop.try_emplace(desktopKey);
    return iterator->second;
}

const DesktopWorkspaceState* FindActiveDesktopWorkspaceState(const AppState& app) {
    const auto iterator = app.monitorState.workspaceStatesByDesktop.find(ActiveDesktopKey(app));
    return iterator != app.monitorState.workspaceStatesByDesktop.end() ? &iterator->second : nullptr;
}

void EnsureCurrentWorkspaceInitialized(AppState& app) {
    for (auto& [_, state] : app.monitorState.activeWorkspaceMonitors) {
        LayoutEngine::EnsureMonitorStateInitialized(state, DefaultMainWidthRatio(app.settings));
    }
}

MonitorStateMap& ActiveWorkspaceMonitors(AppState& app) {
    return app.monitorState.activeWorkspaceMonitors;
}

const MonitorStateMap& ActiveWorkspaceMonitors(const AppState& app) {
    return app.monitorState.activeWorkspaceMonitors;
}

MonitorStateMap& CurrentStoredWorkspace(AppState& app) {
    auto& desktopState = ActiveDesktopWorkspaceState(app);
    const int workspaceIndex = std::clamp(desktopState.currentWorkspaceIndex, 0, kWorkspaceCount - 1);
    return desktopState.workspaces[static_cast<std::size_t>(workspaceIndex)];
}

const MonitorStateMap& CurrentStoredWorkspace(const AppState& app) {
    static const MonitorStateMap kEmptyWorkspace{};
    const DesktopWorkspaceState* desktopState = FindActiveDesktopWorkspaceState(app);
    if (desktopState == nullptr) {
        return kEmptyWorkspace;
    }

    const int workspaceIndex = std::clamp(desktopState->currentWorkspaceIndex, 0, kWorkspaceCount - 1);
    return desktopState->workspaces[static_cast<std::size_t>(workspaceIndex)];
}

void LoadActiveWorkspaceSnapshot(AppState& app) {
    ActiveWorkspaceMonitors(app) = CurrentStoredWorkspace(app);
    app.monitorState.activeWorkspaceLoaded = true;
    EnsureCurrentWorkspaceInitialized(app);
}

void SaveActiveWorkspaceSnapshot(AppState& app) {
    if (!app.monitorState.activeWorkspaceLoaded) {
        return;
    }

    CurrentStoredWorkspace(app) = ActiveWorkspaceMonitors(app);
}

bool WorkspaceContainsWindow(const MonitorStateMap& workspace, HWND hwnd) {
    for (const auto& [_, state] : workspace) {
        if (std::find(state.windows.begin(), state.windows.end(), hwnd) != state.windows.end()) {
            return true;
        }
    }

    return false;
}

class ScopedWinEventHookPause {
public:
    explicit ScopedWinEventHookPause(AppState& app)
        : app_(app) {
        WindowManager::RemoveEventHooks(app_);
        EventRouter::DrainPostedEvents(app_);
    }

    ~ScopedWinEventHookPause() {
        WindowManager::InstallEventHooks(app_);
    }

    ScopedWinEventHookPause(const ScopedWinEventHookPause&) = delete;
    ScopedWinEventHookPause& operator=(const ScopedWinEventHookPause&) = delete;

private:
    AppState& app_;
};

HWND FirstWorkspaceWindow(const MonitorStateMap& workspace) {
    for (const auto& [_, state] : workspace) {
        for (HWND hwnd : state.windows) {
            if (hwnd != nullptr && IsWindow(hwnd)) {
                return hwnd;
            }
        }
    }

    return nullptr;
}

void ApplyLoadedWorkspaceMonitor(AppState& app, HMONITOR monitor) {
    const auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
    const auto iterator = workspace.find(monitor);
    if (iterator == workspace.end()) {
        return;
    }

    WorkspaceManager::ApplyMonitorLayout(app, monitor, iterator->second);
}

}  // namespace

int WorkspaceManager::CurrentWorkspaceIndex(const AppState& app) {
    const DesktopWorkspaceState* desktopState = FindActiveDesktopWorkspaceState(app);
    return desktopState != nullptr ? std::clamp(desktopState->currentWorkspaceIndex, 0, kWorkspaceCount - 1) : 0;
}

MonitorStateMap& WorkspaceManager::WorkspaceMonitors(AppState& app) {
    if (!app.monitorState.activeWorkspaceLoaded) {
        LoadActiveWorkspaceSnapshot(app);
    }

    return ActiveWorkspaceMonitors(app);
}

const MonitorStateMap& WorkspaceManager::WorkspaceMonitors(const AppState& app) {
    if (app.monitorState.activeWorkspaceLoaded) {
        return ActiveWorkspaceMonitors(app);
    }

    return CurrentStoredWorkspace(app);
}

MonitorState& WorkspaceManager::GetOrCreateMonitorState(AppState& app, HMONITOR monitor) {
    auto& workspace = WorkspaceMonitors(app);
    auto [iterator, _] = workspace.try_emplace(monitor);
    auto& state = iterator->second;

    LayoutEngine::EnsureMonitorStateInitialized(state, DefaultMainWidthRatio(app.settings));
    return state;
}

MonitorState& WorkspaceManager::ReconcileMonitorState(AppState& app, HMONITOR monitor) {
    auto& state = GetOrCreateMonitorState(app, monitor);
    auto discovered = WindowManager::EnumerateManagedWindows(app, monitor);
    auto ordered = PreserveWorkspaceWindows(monitor, state.windows);
    for (HWND hwnd : discovered) {
        if (!WindowManager::ContainsWindow(ordered, hwnd)) {
            ordered.push_back(hwnd);
        }
    }

    SyncMonitorStateWindows(state, std::move(ordered), app.settings);
    return state;
}

WindowDropTarget WorkspaceManager::ResolveWindowDropTarget(AppState& app, HWND hwnd, HMONITOR destinationMonitor, const POINT* dropPoint) {
    WindowDropTarget dropTarget;
    dropTarget.monitor = destinationMonitor;
    if (hwnd == nullptr || destinationMonitor == nullptr || !IsWindow(hwnd)) {
        return dropTarget;
    }

    POINT resolvedDropPoint{};
    if (!ResolveDropPoint(hwnd, dropPoint, resolvedDropPoint)) {
        return dropTarget;
    }

    auto& destinationState = GetOrCreateMonitorState(app, destinationMonitor);
    auto discovered = WindowManager::EnumerateManagedWindows(app, destinationMonitor);
    if (!WindowManager::ContainsWindow(discovered, hwnd)) {
        discovered.push_back(hwnd);
    }

    const auto ordered = WindowManager::BuildOrderedWindows(destinationMonitor, destinationState.windows, discovered);
    return ResolveHoveredWindowDropTarget(ordered, hwnd, resolvedDropPoint, destinationMonitor);
}

void WorkspaceManager::ReorderWindowByDrop(AppState& app, HWND hwnd, HMONITOR sourceMonitor, HMONITOR destinationMonitor) {
    if (destinationMonitor != nullptr) {
        const WindowDropTarget dropTarget = ResolveWindowDropTarget(app, hwnd, destinationMonitor);
        auto& destinationState = GetOrCreateMonitorState(app, destinationMonitor);

        auto discovered = WindowManager::EnumerateManagedWindows(app, destinationMonitor);
        if (!WindowManager::ContainsWindow(discovered, hwnd)) {
            discovered.push_back(hwnd);
        }

        auto ordered = WindowManager::BuildOrderedWindows(destinationMonitor, destinationState.windows, discovered);
        ordered.erase(std::remove(ordered.begin(), ordered.end(), hwnd), ordered.end());

        const int insertIndex = dropTarget.replacesWindow
            ? std::clamp(dropTarget.insertIndex, 0, static_cast<int>(ordered.size()))
            : PlannedWindowDropIndex(destinationMonitor, destinationState, ordered, hwnd, app.settings);
        ordered.insert(ordered.begin() + std::clamp(insertIndex, 0, static_cast<int>(ordered.size())), hwnd);
        SyncMonitorStateWindows(destinationState, std::move(ordered), app.settings);
    }

    if (sourceMonitor != nullptr && sourceMonitor != destinationMonitor) {
        auto& workspace = WorkspaceMonitors(app);
        auto sourceIterator = workspace.find(WindowManager::MonitorKey(sourceMonitor));
        if (sourceIterator != workspace.end()) {
            auto& sourceState = sourceIterator->second;
            auto sourceOrdered = sourceState.windows;
            sourceOrdered.erase(std::remove(sourceOrdered.begin(), sourceOrdered.end(), hwnd), sourceOrdered.end());
            SyncMonitorStateWindows(sourceState, std::move(sourceOrdered), app.settings);
        }
    }
}

bool WorkspaceManager::SwapWindows(AppState& app, HWND sourceWindow, HMONITOR sourceMonitor, HWND targetWindow, HMONITOR targetMonitor) {
    auto& workspace = WorkspaceMonitors(app);
    auto sourceIterator = workspace.find(WindowManager::MonitorKey(sourceMonitor));
    auto targetIterator = workspace.find(WindowManager::MonitorKey(targetMonitor));
    if (sourceIterator == workspace.end() || targetIterator == workspace.end()) {
        return false;
    }

    auto& sourceState = sourceIterator->second;
    auto& targetState = targetIterator->second;
    PruneInvalidWindows(sourceState.windows);
    if (sourceMonitor != targetMonitor) {
        PruneInvalidWindows(targetState.windows);
    }

    auto sourceWindowIterator = std::find(sourceState.windows.begin(), sourceState.windows.end(), sourceWindow);
    auto targetWindowIterator = std::find(targetState.windows.begin(), targetState.windows.end(), targetWindow);
    if (sourceWindowIterator == sourceState.windows.end() || targetWindowIterator == targetState.windows.end()) {
        return false;
    }

    std::iter_swap(sourceWindowIterator, targetWindowIterator);
    return true;
}

void WorkspaceManager::RefreshVirtualDesktopContext(AppState& app) {
    std::wstring detectedDesktopKey = VirtualDesktop::DetectCurrentDesktopKey();
    if (IsInvalidDetectedDesktopKey(detectedDesktopKey)) {
        detectedDesktopKey = app.currentDesktopKey.empty() ? L"global" : app.currentDesktopKey;
    }

    SetVirtualDesktopContext(app, std::move(detectedDesktopKey));
}

void WorkspaceManager::SetVirtualDesktopContext(AppState& app, std::wstring desktopKey) {
    if (desktopKey.empty()) {
        desktopKey = L"global";
    }

    const auto ensureDesktopDisplayNumber = [&app](const std::wstring& key) {
        const auto [iterator, inserted] = app.desktopDisplayNumbers.try_emplace(key, app.nextDesktopDisplayNumber);
        if (inserted) {
            ++app.nextDesktopDisplayNumber;
        }
        return iterator->second;
    };

    if (!app.currentDesktopKey.empty() && app.currentDesktopKey != L"global") {
        (void)ensureDesktopDisplayNumber(app.currentDesktopKey);
    }

    if (desktopKey == L"global") {
        app.currentDesktopName = L"Desktop";
    } else {
        app.currentDesktopName = L"Desktop " + std::to_wstring(ensureDesktopDisplayNumber(desktopKey));
    }

    if (desktopKey == app.currentDesktopKey) {
        if (!app.monitorState.activeWorkspaceLoaded) {
            LoadActiveWorkspaceSnapshot(app);
        }
        app.topBar.Refresh(app);
        return;
    }

    SaveActiveWorkspaceSnapshot(app);
    app.currentDesktopKey = std::move(desktopKey);
    LoadActiveWorkspaceSnapshot(app);
    app.topBar.Refresh(app);
}

bool WorkspaceManager::SwitchWorkspace(AppState& app, int workspaceNumber) {
    if (workspaceNumber < 1 || workspaceNumber > kWorkspaceCount) {
        return false;
    }

    ScopedWinEventHookPause pausedHooks(app);
    RefreshVirtualDesktopContext(app);

    const int targetWorkspaceIndex = workspaceNumber - 1;
    if (targetWorkspaceIndex == CurrentWorkspaceIndex(app)) {
        return true;
    }

    const MonitorStateMap sourceWorkspace = WorkspaceMonitors(app);
    SaveActiveWorkspaceSnapshot(app);
    ActiveDesktopWorkspaceState(app).currentWorkspaceIndex = targetWorkspaceIndex;
    LoadActiveWorkspaceSnapshot(app);
    const MonitorStateMap targetWorkspace = WorkspaceMonitors(app);

    for (const auto& [_, state] : sourceWorkspace) {
        for (HWND hwnd : state.windows) {
            if (hwnd == nullptr || !IsWindow(hwnd) || WorkspaceContainsWindow(targetWorkspace, hwnd)) {
                continue;
            }

            if (!IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
        }
    }

    if (app.windowState.focused != nullptr && !WorkspaceContainsWindow(targetWorkspace, app.windowState.focused)) {
        app.windowState.focused = nullptr;
        FocusTracker::UpdateFocusedWindowBorder(app, nullptr);
    }

    for (const auto& [monitor, _] : targetWorkspace) {
        ApplyLoadedWorkspaceMonitor(app, monitor);
    }

    if (const HWND targetWindow = FirstWorkspaceWindow(targetWorkspace); targetWindow != nullptr) {
        SetForegroundWindow(targetWindow);
    }

    app.topBar.Refresh(app);
    return true;
}

void WorkspaceManager::ApplyMonitorLayout(AppState& app, HMONITOR monitor, const MonitorState& state) {
    if (state.windows.empty()) {
        return;
    }

    const HWND foregroundWindow = FocusTracker::ResolveManagedForegroundWindow(app);
    if (state.layoutMode == LayoutMode::Floating) {
        for (HWND hwnd : state.windows) {
            if (hwnd == nullptr || !IsWindow(hwnd)) {
                continue;
            }

            FocusTracker::SetWindowBorderColor(app, hwnd, hwnd == foregroundWindow);
            if (hwnd == foregroundWindow) {
                app.windowState.focused = hwnd;
            }

            ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOWNOACTIVATE);
        }

        return;
    }

    WorkspaceModel::MonitorData monitorData;
    monitorData.handle = monitor;
    monitorData.rect = WindowGeometry::WorkAreaForMonitor(monitor);
    monitorData.layoutMode = state.layoutMode;
    monitorData.mainWidthRatio = LayoutEngine::ClampMainWidthRatio(state.mainWidthRatio);
    monitorData.splitWeights = LayoutEngine::ExportMonitorSplitState(state).splitWeights;
    monitorData.tiles.reserve(state.windows.size());

    for (HWND hwnd : state.windows) {
        if (!IsWindow(hwnd)) {
            continue;
        }

        monitorData.tiles.push_back(WorkspaceModel::TileData{hwnd, monitorData.rect});
    }

    if (monitorData.tiles.empty()) {
        return;
    }

    const WorkspaceModel model = WorkspaceModel::BuildForTesting({std::move(monitorData)});
    const WorkspaceModel::LayoutPlan plan = model.BuildLayoutPlan(
        monitor,
        WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(app.settings.innerGap, 0, 256)),
        WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(app.settings.outerGap, 0, 256)));
    for (const auto& placement : plan.placements) {
        const HWND hwnd = placement.window;
        if (!IsWindow(hwnd)) {
            continue;
        }

        FocusTracker::SetWindowBorderColor(app, hwnd, hwnd == foregroundWindow);
        if (hwnd == foregroundWindow) {
            app.windowState.focused = hwnd;
        }

        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOWNOACTIVATE);
        const int targetWidth = std::max(1, static_cast<int>(placement.rect.right - placement.rect.left));
        const int targetHeight = std::max(1, static_cast<int>(placement.rect.bottom - placement.rect.top));
        SetWindowPos(
            hwnd,
            nullptr,
            placement.rect.left,
            placement.rect.top,
            targetWidth,
            targetHeight,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }
}

}  // namespace quicktile