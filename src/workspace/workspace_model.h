#pragma once

#include "layout/layout_model.h"
#include "windows/window_manager.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace quicktile {

class WorkspaceModel {
public:
    struct TileRef {
        std::size_t monitorIndex = InvalidIndex();
        std::size_t tileIndex = InvalidIndex();

        bool IsValid() const {
            return monitorIndex != InvalidIndex() && tileIndex != InvalidIndex();
        }
    };

    using TileData = quicktile::TileData;
    using MonitorData = quicktile::MonitorLayoutData;

    struct MovePlan {
        enum class Kind {
            None,
            SwapWithWindow,
            MoveToMonitor,
        };

        Kind kind = Kind::None;
        HWND targetWindow = nullptr;
        HMONITOR sourceMonitor = nullptr;
        HMONITOR destinationMonitor = nullptr;
        RECT destinationWorkArea{};
    };

    using ResizePlan = quicktile::ResizePlan;
    using WindowPlacement = quicktile::WindowPlacement;
    using LayoutPlan = quicktile::LayoutPlan;

    static WorkspaceModel Build(const AppState& app);
    static WorkspaceModel BuildForTesting(std::vector<MonitorData> monitors);
    static std::vector<int> DistributeLengths(int totalLength, const std::vector<int>& minimumLengths, const std::vector<float>& weights);
    static std::optional<std::size_t> FindDropTargetIndex(const LayoutPlan& plan, const POINT& dropPoint);

    HWND FirstWindow() const;
    HWND FindFocusTarget(HWND currentWindow, WindowManager::FocusDirection direction) const;
    MovePlan BuildMovePlan(HWND currentWindow, WindowManager::FocusDirection direction) const;
    ResizePlan BuildResizePlan(HWND currentWindow, WindowManager::FocusDirection direction, bool grow, float resizeStep) const;
    LayoutPlan BuildLayoutPlan(HMONITOR monitor, int gap, int outerGap) const;

private:
    static std::size_t InvalidIndex();
    static std::vector<HMONITOR> EnumerateMonitors();

    void BuildMonitor(HMONITOR monitor, const MonitorState* state);
    TileRef FindTileRef(HWND window) const;
    const TileData* TileFromRef(const TileRef& tileRef) const;
    TileRef FindStructuralNeighbor(const TileRef& reference, WindowManager::FocusDirection direction) const;
    TileRef FindDirectionalTileRef(const TileRef& reference, WindowManager::FocusDirection direction) const;
    std::size_t FindDirectionalMonitorIndex(const TileRef& reference, WindowManager::FocusDirection direction) const;
    TileRef FindMonitorEntryTile(std::size_t monitorIndex, const RECT& referenceRect, WindowManager::FocusDirection direction) const;

    std::vector<MonitorData> monitors_;
    std::unordered_map<HWND, TileRef> tileRefsByWindow_;
};

}  // namespace quicktile