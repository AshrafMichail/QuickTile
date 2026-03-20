#include "layout/layout_managers.h"

#include "layout/layout_engine.h"
#include "layout/layout_helpers.h"
#include "layout/layout_policy.h"
#include "windows/window_geometry.h"
#include "workspace/workspace_model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quicktile::LayoutManagers {

namespace {

using LayoutHelpers::Axis;
using LayoutHelpers::AxisForDirection;
using LayoutHelpers::ChildRect;
using LayoutHelpers::DirectionalMetrics;
using LayoutHelpers::IsBetterDirectionalCandidate;
using LayoutHelpers::IsNegativeDirection;
using LayoutHelpers::MakeRect;
using LayoutHelpers::MeasureDirectionalMetrics;

constexpr int kResizeThreshold = 8;

enum class SpiralSide : std::uint8_t {
    Left,
    Top,
    Right,
    Bottom,
};

void NormalizeMainWidthRatio(MonitorState& state, float defaultMainWidthRatio) {
    if (state.mainWidthRatio <= 0.0f) {
        state.mainWidthRatio = defaultMainWidthRatio;
        return;
    }

    state.mainWidthRatio = LayoutEngine::ClampMainWidthRatio(state.mainWidthRatio);
}

void NormalizeSpiralSplitRatios(MonitorState& state, float defaultMainWidthRatio) {
    if (state.windows.size() <= 2) {
        state.splitWeights.clear();
        return;
    }

    const std::size_t ratioCount = state.windows.size() - 2;
    std::vector<float> ratios = state.splitWeights;
    ratios.resize(ratioCount, defaultMainWidthRatio);
    for (float& ratio : ratios) {
        if (ratio <= 0.0f) {
            ratio = defaultMainWidthRatio;
        }
        ratio = LayoutEngine::ClampMainWidthRatio(ratio);
    }

    state.splitWeights = std::move(ratios);
}

SpiralSide SpiralSideForTileIndex(std::size_t tileIndex) {
    switch (tileIndex % 4) {
    case 0:
        return SpiralSide::Left;
    case 1:
        return SpiralSide::Top;
    case 2:
        return SpiralSide::Right;
    case 3:
        return SpiralSide::Bottom;
    default:
        return SpiralSide::Left;
    }
}

Axis AxisForSpiralSide(SpiralSide side) {
    switch (side) {
    case SpiralSide::Left:
    case SpiralSide::Right:
        return Axis::Horizontal;
    case SpiralSide::Top:
    case SpiralSide::Bottom:
        return Axis::Vertical;
    }

    return Axis::Horizontal;
}

WindowManager::FocusDirection DirectionForSpiralSide(SpiralSide side) {
    switch (side) {
    case SpiralSide::Left:
        return WindowManager::FocusDirection::Left;
    case SpiralSide::Top:
        return WindowManager::FocusDirection::Up;
    case SpiralSide::Right:
        return WindowManager::FocusDirection::Right;
    case SpiralSide::Bottom:
        return WindowManager::FocusDirection::Down;
    }

    return WindowManager::FocusDirection::Left;
}

WindowManager::FocusDirection OppositeDirection(WindowManager::FocusDirection direction) {
    switch (direction) {
    case WindowManager::FocusDirection::Left:
        return WindowManager::FocusDirection::Right;
    case WindowManager::FocusDirection::Up:
        return WindowManager::FocusDirection::Down;
    case WindowManager::FocusDirection::Right:
        return WindowManager::FocusDirection::Left;
    case WindowManager::FocusDirection::Down:
        return WindowManager::FocusDirection::Up;
    }

    return WindowManager::FocusDirection::Left;
}

SpiralSide NextSpiralSide(SpiralSide side) {
    switch (side) {
    case SpiralSide::Left:
        return SpiralSide::Top;
    case SpiralSide::Top:
        return SpiralSide::Right;
    case SpiralSide::Right:
        return SpiralSide::Bottom;
    case SpiralSide::Bottom:
        return SpiralSide::Left;
    }

    return SpiralSide::Left;
}

RECT SpiralBandRect(const RECT& rect, SpiralSide side, int length) {
    switch (side) {
    case SpiralSide::Left:
        return MakeRect(rect.left, rect.top, rect.left + length, rect.bottom);
    case SpiralSide::Top:
        return MakeRect(rect.left, rect.top, rect.right, rect.top + length);
    case SpiralSide::Right:
        return MakeRect(rect.right - length, rect.top, rect.right, rect.bottom);
    case SpiralSide::Bottom:
        return MakeRect(rect.left, rect.bottom - length, rect.right, rect.bottom);
    }

    return rect;
}

RECT SpiralRemainingRect(const RECT& rect, SpiralSide side, int bandLength, int gap) {
    switch (side) {
    case SpiralSide::Left:
        return MakeRect(rect.left + bandLength + gap, rect.top, rect.right, rect.bottom);
    case SpiralSide::Top:
        return MakeRect(rect.left, rect.top + bandLength + gap, rect.right, rect.bottom);
    case SpiralSide::Right:
        return MakeRect(rect.left, rect.top, rect.right - bandLength - gap, rect.bottom);
    case SpiralSide::Bottom:
        return MakeRect(rect.left, rect.top, rect.right, rect.bottom - bandLength - gap);
    }

    return rect;
}

int AxisLengthForSpiral(const RECT& rect, SpiralSide side) {
    return LayoutHelpers::AxisLength(rect, AxisForSpiralSide(side));
}

int CurrentBandLength(const RECT& rect, SpiralSide side) {
    switch (side) {
    case SpiralSide::Left:
    case SpiralSide::Right:
        return std::max(1, static_cast<int>(rect.right - rect.left));
    case SpiralSide::Top:
    case SpiralSide::Bottom:
        return std::max(1, static_cast<int>(rect.bottom - rect.top));
    }

    return 1;
}

float SpiralSplitRatioForIndex(const MonitorState& state, std::size_t splitIndex, float defaultMainWidthRatio) {
    if (splitIndex == 0) {
        return state.mainWidthRatio > 0.0f ? state.mainWidthRatio : defaultMainWidthRatio;
    }

    const std::size_t ratioIndex = splitIndex - 1;
    if (ratioIndex < state.splitWeights.size() && state.splitWeights[ratioIndex] > 0.0f) {
        return state.splitWeights[ratioIndex];
    }

    return defaultMainWidthRatio;
}

float SpiralSplitRatioForIndex(const MonitorLayoutData& monitor, std::size_t splitIndex) {
    if (splitIndex == 0) {
        return LayoutEngine::ClampMainWidthRatio(monitor.mainWidthRatio);
    }

    const std::size_t ratioIndex = splitIndex - 1;
    if (ratioIndex < monitor.splitWeights.size() && monitor.splitWeights[ratioIndex] > 0.0f) {
        return LayoutEngine::ClampMainWidthRatio(monitor.splitWeights[ratioIndex]);
    }

    return LayoutEngine::ClampMainWidthRatio(monitor.mainWidthRatio);
}

RECT SpiralAvailableRect(const MonitorState& state, HMONITOR monitor, std::size_t resizedIndex, float defaultMainWidthRatio, int gap, int outerGap) {
    RECT rect = LayoutEngine::WorkAreaForMonitor(monitor);
    rect.left += outerGap;
    rect.top += outerGap;
    rect.right -= outerGap;
    rect.bottom -= outerGap;

    for (std::size_t tileIndex = 0; tileIndex < resizedIndex; ++tileIndex) {
        const SpiralSide side = SpiralSideForTileIndex(tileIndex);
        const int availableLength = std::max(2, AxisLengthForSpiral(rect, side) - gap);
        const float ratio = LayoutEngine::ClampMainWidthRatio(SpiralSplitRatioForIndex(state, tileIndex, defaultMainWidthRatio));
        const int bandLength = std::clamp(
            static_cast<int>(std::lround(static_cast<float>(availableLength) * ratio)),
            1,
            availableLength - 1);
        rect = SpiralRemainingRect(rect, side, bandLength, gap);
    }

    return rect;
}

int CurrentSplitLengthForTile(
    const MonitorState& state,
    HMONITOR monitor,
    std::size_t resizedIndex,
    const RECT& currentRect,
    float defaultMainWidthRatio,
    int gap,
    int outerGap) {
    if (resizedIndex + 1 < state.windows.size()) {
        const SpiralSide side = SpiralSideForTileIndex(resizedIndex);
        return CurrentBandLength(currentRect, side);
    }

    if (resizedIndex == 0) {
        return 0;
    }

    const std::size_t splitIndex = resizedIndex - 1;
    const SpiralSide side = SpiralSideForTileIndex(splitIndex);
    const RECT availableRect = SpiralAvailableRect(state, monitor, splitIndex, defaultMainWidthRatio, gap, outerGap);
    const int availableLength = std::max(2, AxisLengthForSpiral(availableRect, side) - gap);
    const int remainderLength = CurrentBandLength(currentRect, side);
    return std::clamp(availableLength - remainderLength, 1, availableLength - 1);
}

int MinimumExtentForTile(const TileData& tile, Axis axis) {
    const SIZE minimumTrackSize = WindowGeometry::MinimumTrackSizeForWindow(tile.window);
    return axis == Axis::Horizontal
        ? std::max(1, static_cast<int>(minimumTrackSize.cx))
        : std::max(1, static_cast<int>(minimumTrackSize.cy));
}

std::vector<int> BuildMinimumExtents(
    const MonitorLayoutData& monitor,
    std::size_t startIndex,
    std::size_t count,
    Axis axis) {
    std::vector<int> minimumExtents;
    minimumExtents.reserve(count);
    for (std::size_t offset = 0; offset < count; ++offset) {
        minimumExtents.push_back(MinimumExtentForTile(monitor.tiles[startIndex + offset], axis));
    }
    return minimumExtents;
}

int MaximumMinimumExtent(
    const MonitorLayoutData& monitor,
    std::size_t startIndex,
    std::size_t count,
    Axis axis) {
    int maximum = 1;
    for (std::size_t offset = 0; offset < count; ++offset) {
        maximum = std::max(maximum, MinimumExtentForTile(monitor.tiles[startIndex + offset], axis));
    }
    return maximum;
}

std::optional<std::size_t> FindBestDirectionalTileIndexInRange(
    const MonitorLayoutData& monitor,
    std::size_t startIndex,
    std::size_t endIndex,
    const RECT& referenceRect,
    WindowManager::FocusDirection direction) {
    std::optional<std::size_t> bestTileIndex;
    DirectionalMetrics bestMetrics{};
    bool hasBestTile = false;

    for (std::size_t tileIndex = startIndex; tileIndex < endIndex; ++tileIndex) {
        const DirectionalMetrics metrics = MeasureDirectionalMetrics(referenceRect, monitor.tiles[tileIndex].rect, direction);
        if (!metrics.matchesDirection) {
            continue;
        }

        if (IsBetterDirectionalCandidate(metrics, hasBestTile, bestMetrics)) {
            bestTileIndex = tileIndex;
            bestMetrics = metrics;
            hasBestTile = true;
        }
    }

    return bestTileIndex;
}

class FloatingLayoutManager final : public LayoutManager {
public:
    LayoutCapabilities Capabilities() const override {
        return LayoutCapabilities{};
    }

    void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const override {
        NormalizeMainWidthRatio(state, defaultMainWidthRatio);
    }

    std::vector<float> ExportWeights(const MonitorState& state) const override {
        return state.splitWeights;
    }

    std::vector<float> BuildSyncWeights(const std::vector<HWND>&, const std::vector<float>&, const std::vector<HWND>&, bool) const override {
        return {};
    }

    std::optional<std::size_t> FindStructuralNeighbor(const MonitorLayoutData&, std::size_t, WindowManager::FocusDirection) const override {
        return std::nullopt;
    }

    ResizePlan BuildResizePlan(const MonitorLayoutData&, std::size_t, WindowManager::FocusDirection, bool, float) const override {
        return {};
    }

    LayoutPlan BuildLayoutPlan(const MonitorLayoutData&, int, int) const override {
        return {};
    }

    bool UpdateFromResize(MonitorState&, HMONITOR, std::size_t, const RECT&, const RECT&, float, int, int) const override {
        return false;
    }
};

class MonocleLayoutManager final : public LayoutManager {
public:
    LayoutCapabilities Capabilities() const override {
        return LayoutCapabilities{};
    }

    void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const override {
        NormalizeMainWidthRatio(state, defaultMainWidthRatio);
        if (!state.windows.empty()) {
            state.splitWeights.clear();
        }
    }

    std::vector<float> ExportWeights(const MonitorState& state) const override {
        return state.windows.empty() ? state.splitWeights : std::vector<float>{};
    }

    std::vector<float> BuildSyncWeights(const std::vector<HWND>&, const std::vector<float>&, const std::vector<HWND>&, bool) const override {
        return {};
    }

    std::optional<std::size_t> FindStructuralNeighbor(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction) const override {
        if (monitor.tiles.size() <= 1 || tileIndex >= monitor.tiles.size()) {
            return std::nullopt;
        }

        if (direction == WindowManager::FocusDirection::Left || direction == WindowManager::FocusDirection::Right) {
            return std::nullopt;
        }

        const bool negativeDirection = IsNegativeDirection(direction);
        const std::size_t neighborIndex = negativeDirection
            ? (tileIndex == 0 ? monitor.tiles.size() - 1 : tileIndex - 1)
            : ((tileIndex + 1) % monitor.tiles.size());
        return neighborIndex;
    }

    ResizePlan BuildResizePlan(const MonitorLayoutData&, std::size_t, WindowManager::FocusDirection, bool, float) const override {
        return {};
    }

    LayoutPlan BuildLayoutPlan(const MonitorLayoutData& monitor, int, int outerGap) const override {
        LayoutPlan plan;
        if (monitor.tiles.empty()) {
            return plan;
        }

        const RECT contentRect = MakeRect(
            static_cast<int>(monitor.rect.left) + outerGap,
            static_cast<int>(monitor.rect.top) + outerGap,
            static_cast<int>(monitor.rect.right) - outerGap,
            static_cast<int>(monitor.rect.bottom) - outerGap);
        for (const TileData& tile : monitor.tiles) {
            plan.placements.push_back(WindowPlacement{tile.window, contentRect});
        }
        return plan;
    }

    bool UpdateFromResize(MonitorState&, HMONITOR, std::size_t, const RECT&, const RECT&, float, int, int) const override {
        return false;
    }
};

class SpiralLayoutManager final : public LayoutManager {
public:
    LayoutCapabilities Capabilities() const override {
        LayoutCapabilities capabilities;
        capabilities.normalizeSyncWeights = false;
        return capabilities;
    }

    void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const override {
        NormalizeMainWidthRatio(state, defaultMainWidthRatio);
        NormalizeSpiralSplitRatios(state, defaultMainWidthRatio);
    }

    std::vector<float> ExportWeights(const MonitorState& state) const override {
        return state.splitWeights;
    }

    std::vector<float> BuildSyncWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered,
        bool) const override {
        std::vector<float> ratios;
        if (ordered.size() <= 2) {
            return ratios;
        }

        ratios.reserve(ordered.size() - 2);
        for (std::size_t orderedIndex = 1; orderedIndex + 1 < ordered.size(); ++orderedIndex) {
            float ratio = 0.0f;
            auto iterator = std::find(previousWindows.begin(), previousWindows.end(), ordered[orderedIndex]);
            if (iterator != previousWindows.end()) {
                const std::size_t previousIndex = static_cast<std::size_t>(std::distance(previousWindows.begin(), iterator));
                if (previousIndex > 0 && previousIndex + 1 < previousWindows.size()) {
                    const std::size_t previousRatioIndex = previousIndex - 1;
                    if (previousRatioIndex < previousWeights.size()) {
                        ratio = previousWeights[previousRatioIndex];
                    }
                }
            }
            ratios.push_back(ratio);
        }

        return ratios;
    }

    std::optional<std::size_t> FindStructuralNeighbor(const MonitorLayoutData&, std::size_t, WindowManager::FocusDirection) const override {
        return std::nullopt;
    }

    ResizePlan BuildResizePlan(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction,
        bool grow,
        float resizeStep) const override {
        ResizePlan plan;
        if (monitor.tiles.size() <= 1 || tileIndex >= monitor.tiles.size()) {
            return plan;
        }

        const bool isLastTile = tileIndex + 1 >= monitor.tiles.size();
        const std::size_t splitTileIndex = isLastTile ? tileIndex - 1 : tileIndex;
        const SpiralSide side = SpiralSideForTileIndex(splitTileIndex);
        const WindowManager::FocusDirection sharedEdge = isLastTile
            ? DirectionForSpiralSide(side)
            : OppositeDirection(DirectionForSpiralSide(side));
        if (direction != sharedEdge) {
            return plan;
        }

        plan.kind = ResizePlan::Kind::AdjustSpiralSplit;
        plan.monitor = monitor.handle;
        plan.targetIndex = splitTileIndex;
        const float delta = grow ? resizeStep : -resizeStep;
        plan.mainDelta = isLastTile ? -delta : delta;
        return plan;
    }

    LayoutPlan BuildLayoutPlan(const MonitorLayoutData& monitor, int gap, int outerGap) const override {
        LayoutPlan plan;
        if (monitor.tiles.empty()) {
            return plan;
        }

        RECT remainingRect = MakeRect(
            static_cast<int>(monitor.rect.left) + outerGap,
            static_cast<int>(monitor.rect.top) + outerGap,
            static_cast<int>(monitor.rect.right) - outerGap,
            static_cast<int>(monitor.rect.bottom) - outerGap);
        SpiralSide side = SpiralSide::Left;
        for (std::size_t tileIndex = 0; tileIndex < monitor.tiles.size(); ++tileIndex) {
            const std::size_t remainingCount = monitor.tiles.size() - tileIndex;
            if (remainingCount == 1) {
                plan.placements.push_back(WindowPlacement{monitor.tiles[tileIndex].window, remainingRect});
                return plan;
            }

            const Axis axis = AxisForSpiralSide(side);
            const int availableLength = std::max(2, LayoutHelpers::AxisLength(remainingRect, axis) - gap);
            const std::vector<int> splitMinimums = {
                MinimumExtentForTile(monitor.tiles[tileIndex], axis),
                MaximumMinimumExtent(monitor, tileIndex + 1, remainingCount - 1, axis),
            };
            const float splitRatio = SpiralSplitRatioForIndex(monitor, tileIndex);
            const std::vector<int> splitLengths = WorkspaceModel::DistributeLengths(
                availableLength,
                splitMinimums,
                LayoutPolicy::NormalizedWeights(2, std::vector<float>{splitRatio, 1.0f - splitRatio}));

            plan.placements.push_back(WindowPlacement{monitor.tiles[tileIndex].window, SpiralBandRect(remainingRect, side, splitLengths[0])});
            remainingRect = SpiralRemainingRect(remainingRect, side, splitLengths[0], gap);
            side = NextSpiralSide(side);
        }

        return plan;
    }

    bool UpdateFromResize(
        MonitorState& state,
        HMONITOR monitor,
        std::size_t resizedIndex,
        const RECT& currentRect,
        const RECT& startRect,
        float defaultMainWidthRatio,
        int gap,
        int outerGap) const override {
        if (state.windows.size() <= 1) {
            return false;
        }

        const std::size_t splitIndex = resizedIndex + 1 < state.windows.size() ? resizedIndex : resizedIndex - 1;
        const SpiralSide side = SpiralSideForTileIndex(splitIndex);
        const int currentLength = CurrentSplitLengthForTile(state, monitor, resizedIndex, currentRect, defaultMainWidthRatio, gap, outerGap);
        const int startLength = CurrentSplitLengthForTile(state, monitor, resizedIndex, startRect, defaultMainWidthRatio, gap, outerGap);
        if (std::abs(currentLength - startLength) < kResizeThreshold) {
            return false;
        }

        const RECT availableRect = SpiralAvailableRect(state, monitor, splitIndex, defaultMainWidthRatio, gap, outerGap);
        const int availableLength = std::max(2, AxisLengthForSpiral(availableRect, side) - gap);
        const float updatedRatio = LayoutEngine::ClampMainWidthRatio(static_cast<float>(currentLength) / static_cast<float>(availableLength));
        const float currentRatio = LayoutEngine::ClampMainWidthRatio(SpiralSplitRatioForIndex(state, splitIndex, defaultMainWidthRatio));
        if (std::abs(updatedRatio - currentRatio) < 0.000001f) {
            return false;
        }

        if (splitIndex == 0) {
            state.mainWidthRatio = updatedRatio;
        } else {
            const std::size_t ratioIndex = splitIndex - 1;
            if (ratioIndex >= state.splitWeights.size()) {
                state.splitWeights.resize(ratioIndex + 1, defaultMainWidthRatio);
            }
            state.splitWeights[ratioIndex] = updatedRatio;
        }

        NormalizeState(state, defaultMainWidthRatio);
        return true;
    }
};

class VerticalColumnsLayoutManager final : public LayoutManager {
public:
    LayoutCapabilities Capabilities() const override {
        LayoutCapabilities capabilities;
        capabilities.supportsGenericWeightedResize = true;
        return capabilities;
    }

    void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const override {
        NormalizeMainWidthRatio(state, defaultMainWidthRatio);
        if (!state.windows.empty()) {
            state.splitWeights = LayoutPolicy::NormalizedWeights(state.windows.size(), state.splitWeights);
        }
    }

    std::vector<float> ExportWeights(const MonitorState& state) const override {
        if (state.windows.empty()) {
            return state.splitWeights;
        }

        return LayoutPolicy::NormalizedWeights(state.windows.size(), state.splitWeights);
    }

    std::vector<float> BuildSyncWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered,
        bool startup) const override {
        if (startup) {
            return ordered.empty() || previousWeights.empty() ? std::vector<float>{} : LayoutPolicy::NormalizedWeights(ordered.size(), previousWeights);
        }

        return LayoutEngine::BuildColumnWeights(previousWindows, previousWeights, ordered);
    }

    std::optional<std::size_t> FindStructuralNeighbor(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction) const override {
        if (tileIndex >= monitor.tiles.size() || AxisForDirection(direction) != Axis::Horizontal) {
            return std::nullopt;
        }

        if (direction == WindowManager::FocusDirection::Left) {
            return tileIndex > 0 ? std::optional<std::size_t>{tileIndex - 1} : std::nullopt;
        }

        return tileIndex + 1 < monitor.tiles.size() ? std::optional<std::size_t>{tileIndex + 1} : std::nullopt;
    }

    ResizePlan BuildResizePlan(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction,
        bool grow,
        float) const override {
        ResizePlan plan;
        if (tileIndex >= monitor.tiles.size() || AxisForDirection(direction) != Axis::Horizontal) {
            return plan;
        }

        if ((direction == WindowManager::FocusDirection::Left && tileIndex == 0) ||
            (direction == WindowManager::FocusDirection::Right && tileIndex + 1 >= monitor.tiles.size())) {
            return plan;
        }

        plan.kind = ResizePlan::Kind::AdjustColumnWindow;
        plan.monitor = monitor.handle;
        plan.targetIndex = tileIndex;
        plan.growTarget = grow;
        return plan;
    }

    LayoutPlan BuildLayoutPlan(const MonitorLayoutData& monitor, int gap, int outerGap) const override {
        LayoutPlan plan;
        if (monitor.tiles.empty()) {
            return plan;
        }

        const RECT contentRect = MakeRect(
            static_cast<int>(monitor.rect.left) + outerGap,
            static_cast<int>(monitor.rect.top) + outerGap,
            static_cast<int>(monitor.rect.right) - outerGap,
            static_cast<int>(monitor.rect.bottom) - outerGap);
        const int totalGap = gap * std::max(0, static_cast<int>(monitor.tiles.size()) - 1);
        const int budget = std::max(static_cast<int>(monitor.tiles.size()), LayoutHelpers::AxisLength(contentRect, Axis::Horizontal) - totalGap);
        const std::vector<int> minimumWidths = BuildMinimumExtents(monitor, 0, monitor.tiles.size(), Axis::Horizontal);
        const std::vector<int> columnWidths = WorkspaceModel::DistributeLengths(
            budget,
            minimumWidths,
            LayoutPolicy::NormalizedWeights(monitor.tiles.size(), monitor.splitWeights));

        int currentOffset = 0;
        for (std::size_t tileIndex = 0; tileIndex < monitor.tiles.size(); ++tileIndex) {
            plan.placements.push_back(WindowPlacement{monitor.tiles[tileIndex].window, ChildRect(contentRect, Axis::Horizontal, currentOffset, columnWidths[tileIndex])});
            currentOffset += columnWidths[tileIndex] + gap;
        }

        return plan;
    }

    bool UpdateFromResize(
        MonitorState& state,
        HMONITOR monitor,
        std::size_t resizedIndex,
        const RECT& currentRect,
        const RECT& startRect,
        float defaultMainWidthRatio,
        int gap,
        int outerGap) const override {
        const int currentWidth = static_cast<int>(currentRect.right - currentRect.left);
        const int startWidth = static_cast<int>(startRect.right - startRect.left);
        if (std::abs(currentWidth - startWidth) < kResizeThreshold) {
            return false;
        }

        return LayoutEngine::SetMonitorSplitLength(
            state,
            LayoutEngine::BuildCurrentColumnWidths(state),
            LayoutEngine::BuildMinimumColumnWidths(state),
            LayoutEngine::ColumnWidthBudgetForMonitor(monitor, state.windows.size(), gap, outerGap),
            resizedIndex,
            currentWidth,
            defaultMainWidthRatio);
    }
};

class MainStackLayoutManager final : public LayoutManager {
public:
    LayoutCapabilities Capabilities() const override {
        LayoutCapabilities capabilities;
        capabilities.supportsGenericWeightedResize = true;
        return capabilities;
    }

    void NormalizeState(MonitorState& state, float defaultMainWidthRatio) const override {
        NormalizeMainWidthRatio(state, defaultMainWidthRatio);
        if (state.windows.size() > 1) {
            state.splitWeights = LayoutPolicy::NormalizedWeights(state.windows.size() - 1, state.splitWeights);
        } else if (state.windows.size() == 1) {
            state.splitWeights.clear();
        }
    }

    std::vector<float> ExportWeights(const MonitorState& state) const override {
        if (state.windows.empty()) {
            return state.splitWeights;
        }
        if (state.windows.size() == 1) {
            return {};
        }

        return LayoutPolicy::NormalizedWeights(state.windows.size() - 1, state.splitWeights);
    }

    std::vector<float> BuildSyncWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered,
        bool startup) const override {
        if (startup) {
            return ordered.size() <= 1 || previousWeights.empty() ? std::vector<float>{} : LayoutPolicy::NormalizedWeights(ordered.size() - 1, previousWeights);
        }

        return LayoutEngine::BuildStackWeights(previousWindows, previousWeights, ordered);
    }

    std::optional<std::size_t> FindStructuralNeighbor(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction) const override {
        if (monitor.tiles.size() <= 1 || tileIndex >= monitor.tiles.size()) {
            return std::nullopt;
        }

        if (AxisForDirection(direction) == Axis::Horizontal) {
            if (direction == WindowManager::FocusDirection::Left) {
                return tileIndex > 0 ? std::optional<std::size_t>{0} : std::nullopt;
            }

            if (tileIndex == 0) {
                return FindBestDirectionalTileIndexInRange(monitor, 1, monitor.tiles.size(), monitor.tiles[0].rect, direction);
            }

            return std::nullopt;
        }

        if (tileIndex == 0 || monitor.tiles.size() <= 2) {
            return std::nullopt;
        }

        if (direction == WindowManager::FocusDirection::Up) {
            return tileIndex > 1 ? std::optional<std::size_t>{tileIndex - 1} : std::nullopt;
        }

        return tileIndex + 1 < monitor.tiles.size() ? std::optional<std::size_t>{tileIndex + 1} : std::nullopt;
    }

    ResizePlan BuildResizePlan(
        const MonitorLayoutData& monitor,
        std::size_t tileIndex,
        WindowManager::FocusDirection direction,
        bool grow,
        float resizeStep) const override {
        ResizePlan plan;
        if (monitor.tiles.size() <= 1 || tileIndex >= monitor.tiles.size()) {
            return plan;
        }

        if (AxisForDirection(direction) == Axis::Horizontal) {
            if ((tileIndex == 0 && direction != WindowManager::FocusDirection::Right) ||
                (tileIndex != 0 && direction != WindowManager::FocusDirection::Left)) {
                return plan;
            }

            plan.kind = ResizePlan::Kind::AdjustMainSplit;
            plan.monitor = monitor.handle;
            const float delta = grow ? resizeStep : -resizeStep;
            plan.mainDelta = tileIndex == 0 ? delta : -delta;
            return plan;
        }

        if (monitor.tiles.size() <= 2 || tileIndex == 0) {
            return plan;
        }

        const std::size_t stackIndex = tileIndex - 1;
        const std::size_t stackCount = monitor.tiles.size() - 1;
        if ((direction == WindowManager::FocusDirection::Up && stackIndex == 0) ||
            (direction == WindowManager::FocusDirection::Down && stackIndex + 1 >= stackCount)) {
            return plan;
        }

        plan.kind = ResizePlan::Kind::AdjustStackWindow;
        plan.monitor = monitor.handle;
        plan.targetIndex = stackIndex;
        plan.growTarget = grow;
        return plan;
    }

    LayoutPlan BuildLayoutPlan(const MonitorLayoutData& monitor, int gap, int outerGap) const override {
        LayoutPlan plan;
        if (monitor.tiles.empty()) {
            return plan;
        }

        const RECT contentRect = MakeRect(
            static_cast<int>(monitor.rect.left) + outerGap,
            static_cast<int>(monitor.rect.top) + outerGap,
            static_cast<int>(monitor.rect.right) - outerGap,
            static_cast<int>(monitor.rect.bottom) - outerGap);
        if (monitor.tiles.size() == 1) {
            plan.placements.push_back(WindowPlacement{monitor.tiles[0].window, contentRect});
            return plan;
        }

        const int horizontalBudget = std::max(2, LayoutHelpers::AxisLength(contentRect, Axis::Horizontal) - gap);
        const std::vector<int> splitMinimums = {
            MinimumExtentForTile(monitor.tiles[0], Axis::Horizontal),
            MaximumMinimumExtent(monitor, 1, monitor.tiles.size() - 1, Axis::Horizontal),
        };
        const std::vector<int> splitWidths = WorkspaceModel::DistributeLengths(
            horizontalBudget,
            splitMinimums,
            LayoutPolicy::NormalizedWeights(2, std::vector<float>{monitor.mainWidthRatio, 1.0f - monitor.mainWidthRatio}));

        const RECT mainRect = ChildRect(contentRect, Axis::Horizontal, 0, splitWidths[0]);
        plan.placements.push_back(WindowPlacement{monitor.tiles[0].window, mainRect});

        const RECT stackRect = ChildRect(contentRect, Axis::Horizontal, splitWidths[0] + gap, splitWidths[1]);
        if (monitor.tiles.size() == 2) {
            plan.placements.push_back(WindowPlacement{monitor.tiles[1].window, stackRect});
            return plan;
        }

        const std::size_t stackCount = monitor.tiles.size() - 1;
        const int totalGap = gap * std::max(0, static_cast<int>(stackCount) - 1);
        const int verticalBudget = std::max(static_cast<int>(stackCount), LayoutHelpers::AxisLength(stackRect, Axis::Vertical) - totalGap);
        const std::vector<int> minimumHeights = BuildMinimumExtents(monitor, 1, stackCount, Axis::Vertical);
        const std::vector<int> stackHeights = WorkspaceModel::DistributeLengths(
            verticalBudget,
            minimumHeights,
            LayoutPolicy::NormalizedWeights(stackCount, monitor.splitWeights));

        int currentOffset = 0;
        for (std::size_t stackIndex = 0; stackIndex < stackCount; ++stackIndex) {
            plan.placements.push_back(WindowPlacement{monitor.tiles[stackIndex + 1].window, ChildRect(stackRect, Axis::Vertical, currentOffset, stackHeights[stackIndex])});
            currentOffset += stackHeights[stackIndex] + gap;
        }

        return plan;
    }

    bool UpdateFromResize(
        MonitorState& state,
        HMONITOR monitor,
        std::size_t resizedIndex,
        const RECT& currentRect,
        const RECT& startRect,
        float defaultMainWidthRatio,
        int gap,
        int outerGap) const override {
        const int currentWidth = static_cast<int>(currentRect.right - currentRect.left);
        const int startWidth = static_cast<int>(startRect.right - startRect.left);
        if (std::abs(currentWidth - startWidth) >= kResizeThreshold) {
            const RECT workArea = LayoutEngine::WorkAreaForMonitor(monitor);
            const int currentBoundary = resizedIndex == 0 ? static_cast<int>(currentRect.right) : static_cast<int>(currentRect.left);
            const int startBoundary = resizedIndex == 0 ? static_cast<int>(startRect.right) : static_cast<int>(startRect.left);
            if (std::abs(currentBoundary - startBoundary) >= kResizeThreshold) {
                const int totalWidth = std::max(1, static_cast<int>(workArea.right - workArea.left));
                const int relativeBoundary = std::clamp(currentBoundary - static_cast<int>(workArea.left), 0, totalWidth);
                const float updatedMainWidthRatio = LayoutEngine::ClampMainWidthRatio(static_cast<float>(relativeBoundary) / static_cast<float>(totalWidth));
                if (std::abs(updatedMainWidthRatio - state.mainWidthRatio) >= 0.000001f) {
                    state.mainWidthRatio = updatedMainWidthRatio;
                    return true;
                }
            }
        }

        if (resizedIndex == 0 || state.windows.size() <= 2) {
            return false;
        }

        const int currentHeight = static_cast<int>(currentRect.bottom - currentRect.top);
        const int startHeight = static_cast<int>(startRect.bottom - startRect.top);
        if (std::abs(currentHeight - startHeight) < kResizeThreshold) {
            return false;
        }

        return LayoutEngine::SetMonitorSplitLength(
            state,
            LayoutEngine::BuildCurrentStackHeights(state),
            LayoutEngine::BuildMinimumStackHeights(state),
            LayoutEngine::StackHeightBudgetForMonitor(monitor, state.windows.size() - 1, gap, outerGap),
            resizedIndex - 1,
            currentHeight,
            defaultMainWidthRatio);
    }
};

}  // namespace

const LayoutManager& LayoutManagerFor(LayoutMode mode) {
    static const FloatingLayoutManager floatingLayoutManager;
    static const MonocleLayoutManager monocleLayoutManager;
    static const SpiralLayoutManager spiralLayoutManager;
    static const VerticalColumnsLayoutManager verticalColumnsLayoutManager;
    static const MainStackLayoutManager mainStackLayoutManager;

    switch (mode) {
    case LayoutMode::Floating:
        return floatingLayoutManager;
    case LayoutMode::Monocle:
        return monocleLayoutManager;
    case LayoutMode::Spiral:
        return spiralLayoutManager;
    case LayoutMode::VerticalColumns:
        return verticalColumnsLayoutManager;
    case LayoutMode::MainStack:
        return mainStackLayoutManager;
    }

    return mainStackLayoutManager;
}

}  // namespace quicktile::LayoutManagers