#pragma once

#include "app/app_state.h"
#include "layout/layout_model.h"
#include "windows/window_manager.h"

#include <optional>

namespace quicktile::LayoutManagers {

struct LayoutCapabilities {
    bool normalizeSyncWeights = true;
    bool supportsGenericWeightedResize = false;
};

class LayoutManager {
public:
    virtual ~LayoutManager() = default;

    virtual LayoutCapabilities Capabilities() const = 0;
    virtual void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const = 0;
    virtual std::vector<float> ExportWeights(const MonitorState& state) const = 0;
    virtual std::vector<float> BuildSyncWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered,
        bool startup) const = 0;
    virtual std::optional<std::size_t> FindStructuralNeighbor(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction) const = 0;
    virtual ResizePlan BuildResizePlan(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction,
        bool grow,
        float resizeStep) const = 0;
    virtual LayoutPlan BuildLayoutPlan(
        const MonitorLayoutData& monitor,
        int gap,
        int outerGap) const = 0;
    virtual bool UpdateFromResize(
        MonitorState& state,
        HMONITOR monitor,
        std::size_t resizedIndex,
        const RECT& currentRect,
        const RECT& startRect,
        float defaultMainWidthRatio,
        int gap,
        int outerGap) const = 0;
};

const LayoutManager& LayoutManagerFor(LayoutMode mode);

}  // namespace quicktile::LayoutManagers