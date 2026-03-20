#include "workspace/workspace_model.h"

#include "layout/layout_engine.h"
#include "layout/layout_helpers.h"
#include "layout/layout_managers.h"
#include "layout/length_allocator.h"
#include "windows/window_geometry.h"
#include "workspace/workspace_manager.h"

#include <algorithm>
#include <limits>

namespace quicktile {

namespace {

using LayoutHelpers::DirectionalMetrics;
using LayoutHelpers::IsBetterDirectionalCandidate;
using LayoutHelpers::MeasureDirectionalMetrics;

std::vector<float> BuildEqualPreferences(std::size_t count) {
    return std::vector<float>(count, 1.0f);
}

WorkspaceModel::TileRef FindBestDirectionalTileInMonitor(
    const WorkspaceModel::MonitorData& monitor,
    std::size_t monitorIndex,
    std::size_t referenceIndex,
    const RECT& referenceRect,
    WindowManager::FocusDirection direction) {
    WorkspaceModel::TileRef bestTile;
    DirectionalMetrics bestMetrics{};
    bool hasBestTile = false;

    for (std::size_t tileIndex = 0; tileIndex < monitor.tiles.size(); ++tileIndex) {
        if (tileIndex == referenceIndex) {
            continue;
        }

        const DirectionalMetrics metrics = MeasureDirectionalMetrics(referenceRect, monitor.tiles[tileIndex].rect, direction);
        if (!metrics.matchesDirection) {
            continue;
        }

        if (IsBetterDirectionalCandidate(metrics, hasBestTile, bestMetrics)) {
            bestTile = WorkspaceModel::TileRef{monitorIndex, tileIndex};
            bestMetrics = metrics;
            hasBestTile = true;
        }
    }

    return bestTile;
}

}  // namespace

std::size_t WorkspaceModel::InvalidIndex() {
    return std::numeric_limits<std::size_t>::max();
}

WorkspaceModel WorkspaceModel::Build(const AppState& app) {
    WorkspaceModel model;
    model.monitors_.reserve(8);

    const auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
    for (HMONITOR monitor : EnumerateMonitors()) {
        auto iterator = workspace.find(monitor);
        const MonitorState* state = iterator != workspace.end() ? &iterator->second : nullptr;
        model.BuildMonitor(monitor, state);
    }

    return model;
}

WorkspaceModel WorkspaceModel::BuildForTesting(std::vector<MonitorData> monitors) {
    WorkspaceModel model;
    model.monitors_ = std::move(monitors);

    for (std::size_t monitorIndex = 0; monitorIndex < model.monitors_.size(); ++monitorIndex) {
        const MonitorData& monitor = model.monitors_[monitorIndex];
        for (std::size_t tileIndex = 0; tileIndex < monitor.tiles.size(); ++tileIndex) {
            const HWND window = monitor.tiles[tileIndex].window;
            if (window != nullptr) {
                model.tileRefsByWindow_[window] = TileRef{monitorIndex, tileIndex};
            }
        }
    }

    return model;
}

std::vector<int> WorkspaceModel::DistributeLengths(int totalLength, const std::vector<int>& minimumLengths, const std::vector<float>& weights) {
    if (minimumLengths.empty()) {
        return {};
    }

    std::vector<float> preferences = BuildEqualPreferences(minimumLengths.size());
    if (!weights.empty()) {
        preferences.assign(minimumLengths.size(), 0.0f);
        for (std::size_t index = 0; index < minimumLengths.size(); ++index) {
            if (index < weights.size() && weights[index] > 0.0f) {
                preferences[index] = weights[index];
            }
        }
    }

    return LengthAllocator::ResolveLengthsWithMinimums(totalLength, preferences, minimumLengths);
}

std::optional<std::size_t> WorkspaceModel::FindDropTargetIndex(const LayoutPlan& plan, const POINT& dropPoint) {
    if (plan.placements.empty()) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < plan.placements.size(); ++index) {
        if (LayoutHelpers::ContainsPoint(plan.placements[index].rect, dropPoint)) {
            return index;
        }
    }

    std::size_t bestIndex = 0;
    long long bestDistance = LayoutHelpers::SquaredDistance(dropPoint, LayoutHelpers::RectCenter(plan.placements[0].rect));
    for (std::size_t index = 1; index < plan.placements.size(); ++index) {
        const long long distance = LayoutHelpers::SquaredDistance(dropPoint, LayoutHelpers::RectCenter(plan.placements[index].rect));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    return bestIndex;
}

HWND WorkspaceModel::FirstWindow() const {
    for (const MonitorData& monitor : monitors_) {
        for (const TileData& tile : monitor.tiles) {
            if (tile.window != nullptr) {
                return tile.window;
            }
        }
    }

    return nullptr;
}

HWND WorkspaceModel::FindFocusTarget(HWND currentWindow, WindowManager::FocusDirection direction) const {
    const TileRef currentTile = FindTileRef(currentWindow);
    if (!currentTile.IsValid()) {
        return FirstWindow();
    }

    const TileRef targetTile = FindDirectionalTileRef(currentTile, direction);
    const TileData* tile = TileFromRef(targetTile);
    return tile != nullptr ? tile->window : nullptr;
}

WorkspaceModel::MovePlan WorkspaceModel::BuildMovePlan(HWND currentWindow, WindowManager::FocusDirection direction) const {
    MovePlan plan;
    const TileRef currentTile = FindTileRef(currentWindow);
    if (!currentTile.IsValid()) {
        return plan;
    }

    plan.sourceMonitor = monitors_[currentTile.monitorIndex].handle;

    const TileRef targetTile = FindDirectionalTileRef(currentTile, direction);
    if (const TileData* tile = TileFromRef(targetTile); tile != nullptr) {
        plan.kind = MovePlan::Kind::SwapWithWindow;
        plan.targetWindow = tile->window;
        plan.destinationMonitor = monitors_[targetTile.monitorIndex].handle;
        return plan;
    }

    const std::size_t destinationMonitorIndex = FindDirectionalMonitorIndex(currentTile, direction);
    if (destinationMonitorIndex == InvalidIndex()) {
        return plan;
    }

    const MonitorData& destinationMonitor = monitors_[destinationMonitorIndex];
    if (destinationMonitor.handle == nullptr || destinationMonitor.handle == plan.sourceMonitor) {
        return plan;
    }

    plan.kind = MovePlan::Kind::MoveToMonitor;
    plan.destinationMonitor = destinationMonitor.handle;
    plan.destinationWorkArea = destinationMonitor.rect;
    return plan;
}

WorkspaceModel::ResizePlan WorkspaceModel::BuildResizePlan(
    HWND currentWindow,
    WindowManager::FocusDirection direction,
    bool grow,
    float resizeStep) const {
    const TileRef currentTile = FindTileRef(currentWindow);
    if (!currentTile.IsValid() || currentTile.monitorIndex >= monitors_.size()) {
        return {};
    }

    const MonitorData& monitor = monitors_[currentTile.monitorIndex];
    if (currentTile.tileIndex >= monitor.tiles.size()) {
        return {};
    }

    return LayoutManagers::LayoutManagerFor(monitor.layoutMode).BuildResizePlan(
        monitor,
        currentTile.tileIndex,
        direction,
        grow,
        resizeStep);
}

WorkspaceModel::LayoutPlan WorkspaceModel::BuildLayoutPlan(HMONITOR monitor, int gap, int outerGap) const {
    auto iterator = std::find_if(monitors_.begin(), monitors_.end(), [monitor](const MonitorData& candidate) {
        return candidate.handle == monitor;
    });
    if (iterator == monitors_.end()) {
        return {};
    }

    return LayoutManagers::LayoutManagerFor(iterator->layoutMode).BuildLayoutPlan(*iterator, gap, outerGap);
}

std::vector<HMONITOR> WorkspaceModel::EnumerateMonitors() {
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
    return monitors;
}

void WorkspaceModel::BuildMonitor(HMONITOR monitor, const MonitorState* state) {
    const std::size_t monitorIndex = monitors_.size();
    MonitorData monitorData;
    monitorData.handle = monitor;
    monitorData.rect = WindowGeometry::WorkAreaForMonitor(monitor);
    monitorData.layoutMode = state != nullptr ? state->layoutMode : DefaultLayoutMode();
    monitorData.mainWidthRatio = state != nullptr ? state->mainWidthRatio : 0.0f;
    monitorData.splitWeights = state != nullptr ? LayoutEngine::ExportMonitorSplitState(*state).splitWeights : std::vector<float>{};

    if (state == nullptr || monitorData.layoutMode == LayoutMode::Floating) {
        monitors_.push_back(std::move(monitorData));
        return;
    }

    for (HWND hwnd : state->windows) {
        if (hwnd == nullptr || !IsWindow(hwnd)) {
            continue;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) {
            continue;
        }

        const std::size_t tileIndex = monitorData.tiles.size();
        monitorData.tiles.push_back(TileData{hwnd, rect});
        tileRefsByWindow_[hwnd] = TileRef{monitorIndex, tileIndex};
    }

    monitors_.push_back(std::move(monitorData));
}

WorkspaceModel::TileRef WorkspaceModel::FindTileRef(HWND window) const {
    auto iterator = tileRefsByWindow_.find(window);
    return iterator == tileRefsByWindow_.end() ? TileRef{} : iterator->second;
}

const WorkspaceModel::TileData* WorkspaceModel::TileFromRef(const TileRef& tileRef) const {
    if (!tileRef.IsValid() || tileRef.monitorIndex >= monitors_.size()) {
        return nullptr;
    }

    const MonitorData& monitor = monitors_[tileRef.monitorIndex];
    if (tileRef.tileIndex >= monitor.tiles.size()) {
        return nullptr;
    }

    return &monitor.tiles[tileRef.tileIndex];
}

WorkspaceModel::TileRef WorkspaceModel::FindDirectionalTileRef(const TileRef& reference, WindowManager::FocusDirection direction) const {
    if (const TileRef neighbor = FindStructuralNeighbor(reference, direction); neighbor.IsValid()) {
        return neighbor;
    }

    const TileData* referenceTile = TileFromRef(reference);
    if (referenceTile == nullptr) {
        return TileRef{};
    }

    const MonitorData& monitor = monitors_[reference.monitorIndex];
    if (const TileRef geometricNeighbor = FindBestDirectionalTileInMonitor(
            monitor,
            reference.monitorIndex,
            reference.tileIndex,
            referenceTile->rect,
            direction);
        geometricNeighbor.IsValid()) {
        return geometricNeighbor;
    }

    const std::size_t monitorIndex = FindDirectionalMonitorIndex(reference, direction);
    if (monitorIndex == InvalidIndex()) {
        return TileRef{};
    }

    return FindMonitorEntryTile(monitorIndex, referenceTile->rect, direction);
}

WorkspaceModel::TileRef WorkspaceModel::FindStructuralNeighbor(const TileRef& reference, WindowManager::FocusDirection direction) const {
    if (!reference.IsValid() || reference.monitorIndex >= monitors_.size()) {
        return TileRef{};
    }

    const MonitorData& monitor = monitors_[reference.monitorIndex];
    if (reference.tileIndex >= monitor.tiles.size()) {
        return TileRef{};
    }

    const std::optional<std::size_t> neighborIndex = LayoutManagers::LayoutManagerFor(monitor.layoutMode).FindStructuralNeighbor(
        monitor,
        reference.tileIndex,
        direction);
    if (!neighborIndex.has_value()) {
        return TileRef{};
    }

    return TileRef{reference.monitorIndex, *neighborIndex};
}

std::size_t WorkspaceModel::FindDirectionalMonitorIndex(const TileRef& reference, WindowManager::FocusDirection direction) const {
    if (!reference.IsValid() || reference.monitorIndex >= monitors_.size()) {
        return InvalidIndex();
    }

    const RECT referenceRect = monitors_[reference.monitorIndex].rect;
    std::size_t bestIndex = InvalidIndex();
    DirectionalMetrics bestMetrics{};
    bool hasBestMonitor = false;

    for (std::size_t monitorIndex = 0; monitorIndex < monitors_.size(); ++monitorIndex) {
        if (monitorIndex == reference.monitorIndex) {
            continue;
        }

        const MonitorData& candidate = monitors_[monitorIndex];
        const DirectionalMetrics metrics = MeasureDirectionalMetrics(referenceRect, candidate.rect, direction);
        if (!metrics.matchesDirection) {
            continue;
        }

        if (IsBetterDirectionalCandidate(metrics, hasBestMonitor, bestMetrics)) {
            bestIndex = monitorIndex;
            bestMetrics = metrics;
            hasBestMonitor = true;
        }
    }

    return bestIndex;
}

WorkspaceModel::TileRef WorkspaceModel::FindMonitorEntryTile(
    std::size_t monitorIndex,
    const RECT& referenceRect,
    WindowManager::FocusDirection direction) const {
    if (monitorIndex >= monitors_.size()) {
        return TileRef{};
    }

    const MonitorData& monitor = monitors_[monitorIndex];
    TileRef bestTile;
    DirectionalMetrics bestMetrics{};
    bool hasBestTile = false;

    for (std::size_t tileIndex = 0; tileIndex < monitor.tiles.size(); ++tileIndex) {
        const DirectionalMetrics metrics = MeasureDirectionalMetrics(referenceRect, monitor.tiles[tileIndex].rect, direction);
        if (!metrics.matchesDirection) {
            continue;
        }

        if (IsBetterDirectionalCandidate(metrics, hasBestTile, bestMetrics)) {
            bestTile = TileRef{monitorIndex, tileIndex};
            bestMetrics = metrics;
            hasBestTile = true;
        }
    }

    return bestTile;
}

}  // namespace quicktile