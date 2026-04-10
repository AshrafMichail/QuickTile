#include "windows/window_manager.h"

#include "layout/direction_resize.h"
#include "windows/focus_tracker.h"
#include "layout/layout_engine.h"
#include "windows/window_geometry.h"
#include "workspace/workspace_manager.h"
#include "workspace/workspace_model.h"

#include <algorithm>
#include <cmath>

namespace quicktile {

namespace {

float DefaultMainWidthRatio() {
    return kDefaultMainWidthRatio;
}

LayoutMode DefaultLayoutMode(const Settings& settings) {
    return settings.defaultLayoutMode;
}

float ResizeStepRatio(const Settings& settings) {
    return std::clamp(settings.resizeStepRatio, 0.01f, 0.25f);
}

int InnerGap(HMONITOR monitor, const Settings& settings) {
    return WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(settings.innerGap, 0, 256));
}

int OuterGap(HMONITOR monitor, const Settings& settings) {
    return WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(settings.outerGap, 0, 256));
}

bool ApplyWeightedResizePlan(HMONITOR monitor, MonitorState& state, const WorkspaceModel::ResizePlan& plan, const Settings& settings) {
    const float defaultMainWidthRatio = DefaultMainWidthRatio();
    const float resizeStepRatio = ResizeStepRatio(settings);
    const int innerGap = InnerGap(monitor, settings);
    const int outerGap = OuterGap(monitor, settings);

    if (plan.kind == WorkspaceModel::ResizePlan::Kind::AdjustColumnWindow) {
        std::vector<float> splitWeights = LayoutEngine::ExportMonitorSplitState(state).splitWeights;
        if (splitWeights.size() != state.windows.size()) {
            splitWeights = LayoutEngine::BuildColumnWeights(state.windows, splitWeights, state.windows);
            LayoutEngine::NormalizeWeights(splitWeights);
            LayoutEngine::RestoreMonitorSplitState(state, state.layoutMode, state.mainWidthRatio, splitWeights, defaultMainWidthRatio);
        }

        return LayoutEngine::AdjustMonitorSplitLengths(
            state,
            LayoutEngine::BuildCurrentColumnWidths(state),
            LayoutEngine::BuildMinimumColumnWidths(state),
            LayoutEngine::ColumnWidthBudgetForMonitor(monitor, state.windows.size(), innerGap, outerGap),
            plan.targetIndex,
            plan.growTarget,
            resizeStepRatio,
            defaultMainWidthRatio);
    }

    if (plan.kind == WorkspaceModel::ResizePlan::Kind::AdjustStackWindow) {
        if (state.windows.size() < 2) {
            return false;
        }

        std::vector<float> splitWeights = LayoutEngine::ExportMonitorSplitState(state).splitWeights;
        if (splitWeights.size() != state.windows.size() - 1) {
            splitWeights = LayoutEngine::BuildStackWeights(state.windows, splitWeights, state.windows);
            LayoutEngine::NormalizeWeights(splitWeights);
            LayoutEngine::RestoreMonitorSplitState(state, state.layoutMode, state.mainWidthRatio, splitWeights, defaultMainWidthRatio);
        }

        return LayoutEngine::AdjustMonitorSplitLengths(
            state,
            LayoutEngine::BuildCurrentStackHeights(state),
            LayoutEngine::BuildMinimumStackHeights(state),
            LayoutEngine::StackHeightBudgetForMonitor(monitor, state.windows.size() - 1, innerGap, outerGap),
            plan.targetIndex,
            plan.growTarget,
            resizeStepRatio,
            defaultMainWidthRatio);
    }

    return false;
}

bool ApplySpiralResizePlan(MonitorState& state, const WorkspaceModel::ResizePlan& plan, const Settings&) {
    const float defaultMainWidthRatio = DefaultMainWidthRatio();
    LayoutEngine::EnsureMonitorStateInitialized(state, defaultMainWidthRatio);

    if (plan.targetIndex == 0) {
        const float updatedRatio = LayoutEngine::ClampMainWidthRatio(state.mainWidthRatio + plan.mainDelta);
        if (std::abs(updatedRatio - state.mainWidthRatio) < 0.000001f) {
            return false;
        }

        state.mainWidthRatio = updatedRatio;
        return true;
    }

    if (state.windows.size() <= 2) {
        return false;
    }

    const std::size_t ratioIndex = plan.targetIndex - 1;
    if (ratioIndex >= state.windows.size() - 2) {
        return false;
    }

    if (ratioIndex >= state.splitWeights.size()) {
        state.splitWeights.resize(state.windows.size() - 2, defaultMainWidthRatio);
    }

    const float currentRatio = LayoutEngine::ClampMainWidthRatio(state.splitWeights[ratioIndex]);
    const float updatedRatio = LayoutEngine::ClampMainWidthRatio(currentRatio + plan.mainDelta);
    if (std::abs(updatedRatio - currentRatio) < 0.000001f) {
        return false;
    }

    state.splitWeights[ratioIndex] = updatedRatio;
    return true;
}

}  // namespace

void WindowManager::TileMonitor(AppState& app, HMONITOR monitor) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    if (!app.tilingEnabled || app.windowState.moveSize != nullptr) {
        return;
    }

    auto& state = WorkspaceManager::ReconcileMonitorState(app, monitor);

    if (state.windows.empty()) {
        return;
    }

    WorkspaceManager::ApplyMonitorLayout(app, monitor, state);
}

void WindowManager::TileActiveMonitor(AppState& app) {
    TileMonitor(app, ActiveMonitor());
}

void WindowManager::TileAllKnownMonitors(AppState& app) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    std::vector<HMONITOR> monitors;

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto& list = *reinterpret_cast<std::vector<HMONITOR>*>(lParam);
            list.push_back(monitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    for (HMONITOR monitor : monitors) {
        TileMonitor(app, monitor);
    }

    app.topBar.Refresh(app);
}

void WindowManager::RefreshSettingsEffects(AppState& app) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    app.topBar.SetEnabled(app.settings.topBarEnabled);

    if (app.tilingEnabled) {
        TileAllKnownMonitors(app);
    }

    FocusTracker::ReapplyManagedWindowBorderColors(app);
    app.topBar.Refresh(app);
}

MonitorTilingToggleResult WindowManager::ToggleActiveMonitorTiling(AppState& app) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    MonitorTilingToggleResult result;
    const HMONITOR monitor = ActiveMonitor();
    if (monitor == nullptr) {
        return result;
    }

    const bool currentlyEnabled = IsMonitorTilingEnabled(app, monitor);
    const LayoutChangeResult layoutResult = SetActiveMonitorLayout(app, currentlyEnabled ? LayoutMode::Floating : DefaultLayoutMode(app.settings));
    if (!layoutResult.handled) {
        return result;
    }

    result.enabled = layoutResult.layoutMode != LayoutMode::Floating;
    result.handled = true;
    return result;
}

LayoutChangeResult WindowManager::SetActiveMonitorLayout(AppState& app, LayoutMode layoutMode) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    LayoutChangeResult result;
    const HMONITOR monitor = ActiveMonitor();
    if (monitor == nullptr) {
        return result;
    }

    auto& state = WorkspaceManager::GetOrCreateMonitorState(app, monitor);
    const LayoutMode previousLayoutMode = state.layoutMode;

    if (previousLayoutMode == layoutMode) {
        result.handled = true;
        result.layoutMode = layoutMode;
        return result;
    }

    LayoutEngine::SetMonitorLayoutMode(state, layoutMode, DefaultMainWidthRatio());

    RefreshSettingsEffects(app);
    result.handled = true;
    result.layoutMode = layoutMode;
    return result;
}

void WindowManager::FocusInDirection(AppState& app, FocusDirection direction) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    auto windows = CollectTiledWindows(app);

    if (windows.empty()) {
        TileAllKnownMonitors(app);
        windows = CollectTiledWindows(app);
    }

    if (windows.empty()) {
        return;
    }

    const HWND foreground = FocusTracker::ResolveManagedForegroundWindow(app);
    const WorkspaceModel model = WorkspaceModel::Build(app);
    const HWND nextWindow = model.FindFocusTarget(foreground, direction);

    if (nextWindow != nullptr) {
        SetForegroundWindow(nextWindow);
    }
}

void WindowManager::MoveFocusedWindowInDirection(AppState& app, FocusDirection direction) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    auto windows = CollectTiledWindows(app);

    if (windows.empty()) {
        TileAllKnownMonitors(app);
        windows = CollectTiledWindows(app);
    }

    if (windows.empty()) {
        return;
    }

    const HWND foreground = FocusTracker::ResolveManagedForegroundWindow(app);
    if (std::find(windows.begin(), windows.end(), foreground) == windows.end()) {
        return;
    }

    const WorkspaceModel model = WorkspaceModel::Build(app);
    const WorkspaceModel::MovePlan plan = model.BuildMovePlan(foreground, direction);
    if (plan.kind == WorkspaceModel::MovePlan::Kind::None) {
        return;
    }

    if (plan.kind == WorkspaceModel::MovePlan::Kind::MoveToMonitor) {
        if (plan.destinationMonitor == nullptr || plan.destinationMonitor == plan.sourceMonitor) {
            return;
        }

        RECT foregroundRect{};
        if (!GetWindowRect(foreground, &foregroundRect)) {
            return;
        }

        const RECT destinationWorkArea = plan.destinationWorkArea;
        const int windowWidth = std::min(
            std::max(1, static_cast<int>(foregroundRect.right - foregroundRect.left)),
            std::max(1, static_cast<int>(destinationWorkArea.right - destinationWorkArea.left)));
        const int windowHeight = std::min(
            std::max(1, static_cast<int>(foregroundRect.bottom - foregroundRect.top)),
            std::max(1, static_cast<int>(destinationWorkArea.bottom - destinationWorkArea.top)));
        const int centeredLeft = static_cast<int>((destinationWorkArea.left + destinationWorkArea.right - windowWidth) / 2);
        const int centeredTop = static_cast<int>((destinationWorkArea.top + destinationWorkArea.bottom - windowHeight) / 2);

        SetWindowPos(
            foreground,
            nullptr,
            centeredLeft,
            centeredTop,
            windowWidth,
            windowHeight,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

        ReorderWindowByDrop(app, foreground, plan.sourceMonitor, plan.destinationMonitor);
        TileMonitor(app, plan.sourceMonitor);
        TileMonitor(app, plan.destinationMonitor);
        SetForegroundWindow(foreground);
        return;
    }

    const HWND targetWindow = plan.targetWindow;
    const HMONITOR sourceMonitor = plan.sourceMonitor;
    const HMONITOR targetMonitor = plan.destinationMonitor;
    if (!WorkspaceManager::SwapWindows(app, foreground, sourceMonitor, targetWindow, targetMonitor)) {
        return;
    }

    RECT foregroundRect{};
    RECT targetRect{};
    const bool haveForegroundRect = GetWindowRect(foreground, &foregroundRect) != FALSE;
    const bool haveTargetRect = GetWindowRect(targetWindow, &targetRect) != FALSE;

    if (sourceMonitor == targetMonitor) {
        TileMonitor(app, sourceMonitor);
    } else {
        if (haveForegroundRect && haveTargetRect) {
            SetWindowPos(
                foreground,
                nullptr,
                targetRect.left,
                targetRect.top,
                std::max(1, static_cast<int>(targetRect.right - targetRect.left)),
                std::max(1, static_cast<int>(targetRect.bottom - targetRect.top)),
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
            SetWindowPos(
                targetWindow,
                nullptr,
                foregroundRect.left,
                foregroundRect.top,
                std::max(1, static_cast<int>(foregroundRect.right - foregroundRect.left)),
                std::max(1, static_cast<int>(foregroundRect.bottom - foregroundRect.top)),
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        }

        TileMonitor(app, sourceMonitor);
        TileMonitor(app, targetMonitor);
    }

    SetForegroundWindow(foreground);
}

void WindowManager::ResizeFocusedWindowInDirection(AppState& app, FocusDirection direction, bool grow) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    const HWND foreground = FocusTracker::ResolveManagedForegroundWindow(app);
    if (!IsManagedWindow(app, foreground)) {
        return;
    }

    const WorkspaceModel model = WorkspaceModel::Build(app);
    const WorkspaceModel::ResizePlan plan = model.BuildResizePlan(foreground, direction, grow, ResizeStepRatio(app.settings));
    if (plan.kind == WorkspaceModel::ResizePlan::Kind::None) {
        return;
    }

    const HMONITOR monitor = plan.monitor;
    auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
    auto monitorIterator = workspace.find(MonitorKey(monitor));
    if (monitorIterator == workspace.end()) {
        return;
    }

    auto& state = monitorIterator->second;
    state.windows.erase(
        std::remove_if(state.windows.begin(), state.windows.end(), [](HWND hwnd) { return !IsWindow(hwnd); }),
        state.windows.end());

    auto windowIterator = std::find(state.windows.begin(), state.windows.end(), foreground);
    if (windowIterator == state.windows.end()) {
        return;
    }

    bool changed = false;
    if (plan.kind == WorkspaceModel::ResizePlan::Kind::AdjustMainSplit) {
        state.mainWidthRatio = LayoutEngine::ClampMainWidthRatio(state.mainWidthRatio + plan.mainDelta);
        changed = true;
    } else if (plan.kind == WorkspaceModel::ResizePlan::Kind::AdjustSpiralSplit) {
        changed = ApplySpiralResizePlan(state, plan, app.settings);
    } else {
        changed = ApplyWeightedResizePlan(monitor, state, plan, app.settings);
    }

    if (!changed) {
        return;
    }

    TileMonitor(app, monitor);
    SetForegroundWindow(foreground);
}

}  // namespace quicktile
