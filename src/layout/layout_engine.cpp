#include "layout/layout_engine.h"

#include "layout/layout_managers.h"

#include "layout/length_allocator.h"
#include "layout/layout_policy.h"
#include "windows/window_geometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace quicktile {

namespace {

bool SetWeightedWindowLength(
    const std::vector<int>& currentLengths,
    std::vector<float>& weights,
    int totalBudget,
    std::size_t targetIndex,
    int desiredLength) {
    if (weights.size() < 2 || currentLengths.size() != weights.size() || targetIndex >= weights.size()) {
        return false;
    }

    totalBudget = std::max(static_cast<int>(weights.size()), totalBudget);

    std::vector<float> otherPreferences;
    otherPreferences.reserve(weights.size() - 1);
    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (index == targetIndex) {
            continue;
        }

        otherPreferences.push_back(static_cast<float>(std::max(1, currentLengths[index])));
    }

    const int targetLength = std::clamp(
        std::max(1, desiredLength),
        1,
        totalBudget - static_cast<int>(weights.size()) + 1);
    const std::vector<int> otherLengths = LengthAllocator::AllocateIgnoringMinimums(totalBudget - targetLength, otherPreferences);

    std::vector<int> lengths;
    lengths.reserve(weights.size());
    std::size_t otherOffset = 0;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (index == targetIndex) {
            lengths.push_back(targetLength);
        } else {
            lengths.push_back(otherLengths[otherOffset++]);
        }
    }

    std::vector<float> updatedWeights(lengths.size(), 0.0f);
    for (std::size_t index = 0; index < lengths.size(); ++index) {
        updatedWeights[index] = static_cast<float>(lengths[index]) / static_cast<float>(totalBudget);
    }

    float totalDifference = 0.0f;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        totalDifference += std::abs(updatedWeights[index] - weights[index]);
    }

    if (totalDifference < 0.000001f) {
        return false;
    }

    weights = std::move(updatedWeights);
    LayoutEngine::NormalizeWeights(weights);
    return true;
}
std::vector<float> ExportWeightsForState(const MonitorState& state) {
    return LayoutManagers::LayoutManagerFor(state.layoutMode).ExportWeights(state);
}

}  // namespace

float LayoutEngine::ClampMainWidthRatio(float ratio) {
    return LayoutPolicy::ClampMainWidthRatio(ratio);
}

LayoutEngine::MonitorSplitStateData LayoutEngine::ExportMonitorSplitState(const MonitorState& state) {
    return MonitorSplitStateData{state.mainWidthRatio, ExportWeightsForState(state)};
}

void LayoutEngine::EnsureMonitorStateInitialized(MonitorState& state, float defaultMainWidthRatio) {
    LayoutManagers::LayoutManagerFor(state.layoutMode).NormalizeState(
        state,
        ClampMainWidthRatio(defaultMainWidthRatio));
}

void LayoutEngine::RetargetDefaultMainWidthRatio(MonitorState& state, float oldDefaultMainWidthRatio, float newDefaultMainWidthRatio) {
    if (state.mainWidthRatio <= 0.0f || std::abs(state.mainWidthRatio - oldDefaultMainWidthRatio) < 0.0001f) {
        state.mainWidthRatio = ClampMainWidthRatio(newDefaultMainWidthRatio);
    }

    EnsureMonitorStateInitialized(state, newDefaultMainWidthRatio);
}

void LayoutEngine::RestoreMonitorSplitState(MonitorState& state, LayoutMode layoutMode, float mainWidthRatio, const std::vector<float>& splitWeights, float defaultMainWidthRatio) {
    state.layoutMode = layoutMode;
    state.mainWidthRatio = mainWidthRatio;
    state.splitWeights = splitWeights;
    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
}

void LayoutEngine::SetMonitorLayoutMode(MonitorState& state, LayoutMode layoutMode, float defaultMainWidthRatio) {
    const std::vector<float> splitWeights = ExportWeightsForState(state);
    RestoreMonitorSplitState(state, layoutMode, state.mainWidthRatio, splitWeights, defaultMainWidthRatio);
}

std::vector<int> LayoutEngine::BuildMinimumStackHeights(const MonitorState& state) {
    std::vector<int> minimumHeights;
    minimumHeights.reserve(state.windows.size() > 1 ? state.windows.size() - 1 : 0);
    for (std::size_t index = 1; index < state.windows.size(); ++index) {
        minimumHeights.push_back(WindowGeometry::MinimumTrackSizeForWindow(state.windows[index]).cy);
    }
    return minimumHeights;
}

std::vector<int> LayoutEngine::BuildCurrentStackHeights(const MonitorState& state) {
    std::vector<int> currentHeights;
    currentHeights.reserve(state.windows.size() > 1 ? state.windows.size() - 1 : 0);
    for (std::size_t index = 1; index < state.windows.size(); ++index) {
        RECT rect{};
        int height = 1;
        if (GetWindowRect(state.windows[index], &rect)) {
            height = std::max(1, static_cast<int>(rect.bottom - rect.top));
        }
        currentHeights.push_back(height);
    }
    return currentHeights;
}

int LayoutEngine::StackHeightBudgetForMonitor(HMONITOR monitor, std::size_t stackCount, int gap, int outerGap) {
    const RECT workArea = WorkAreaForMonitor(monitor);
    const int contentTop = static_cast<int>(workArea.top) + outerGap;
    const int contentBottom = static_cast<int>(workArea.bottom) - outerGap;
    const int contentHeight = std::max(1, contentBottom - contentTop);
    return std::max(static_cast<int>(stackCount), contentHeight - (gap * std::max(0, static_cast<int>(stackCount) - 1)));
}

std::vector<int> LayoutEngine::BuildMinimumColumnWidths(const MonitorState& state) {
    std::vector<int> minimumWidths;
    minimumWidths.reserve(state.windows.size());
    for (HWND window : state.windows) {
        minimumWidths.push_back(WindowGeometry::MinimumTrackSizeForWindow(window).cx);
    }
    return minimumWidths;
}

std::vector<int> LayoutEngine::BuildCurrentColumnWidths(const MonitorState& state) {
    std::vector<int> currentWidths;
    currentWidths.reserve(state.windows.size());
    for (HWND window : state.windows) {
        RECT rect{};
        int width = 1;
        if (GetWindowRect(window, &rect)) {
            width = std::max(1, static_cast<int>(rect.right - rect.left));
        }
        currentWidths.push_back(width);
    }
    return currentWidths;
}

int LayoutEngine::ColumnWidthBudgetForMonitor(HMONITOR monitor, std::size_t columnCount, int gap, int outerGap) {
    const RECT workArea = WorkAreaForMonitor(monitor);
    const int contentLeft = static_cast<int>(workArea.left) + outerGap;
    const int contentRight = static_cast<int>(workArea.right) - outerGap;
    const int contentWidth = std::max(1, contentRight - contentLeft);
    return std::max(static_cast<int>(columnCount), contentWidth - (gap * std::max(0, static_cast<int>(columnCount) - 1)));
}

std::vector<float> LayoutEngine::BuildStackWeights(
    const std::vector<HWND>& previousWindows,
    const std::vector<float>& previousWeights,
    const std::vector<HWND>& ordered) {
    std::vector<float> weights;
    if (ordered.size() <= 1) {
        return weights;
    }

    weights.reserve(ordered.size() - 1);
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        const HWND hwnd = ordered[index];
        float weight = 1.0f;
        auto iterator = std::find(previousWindows.begin(), previousWindows.end(), hwnd);
        if (iterator != previousWindows.end()) {
            const std::size_t previousIndex = static_cast<std::size_t>(std::distance(previousWindows.begin(), iterator));
            if (previousIndex > 0) {
                const std::size_t previousWeightIndex = previousIndex - 1;
                if (previousWeightIndex < previousWeights.size() && previousWeights[previousWeightIndex] > 0.0f) {
                    weight = previousWeights[previousWeightIndex];
                }
            }
        }

        weights.push_back(weight);
    }

    return weights;
}

std::vector<float> LayoutEngine::BuildColumnWeights(
    const std::vector<HWND>& previousWindows,
    const std::vector<float>& previousWeights,
    const std::vector<HWND>& ordered) {
    std::vector<float> weights;
    if (ordered.empty()) {
        return weights;
    }

    weights.reserve(ordered.size());
    const bool canReusePreviousWeights = previousWeights.size() == previousWindows.size();
    for (HWND hwnd : ordered) {
        float weight = 1.0f;
        if (canReusePreviousWeights) {
            auto iterator = std::find(previousWindows.begin(), previousWindows.end(), hwnd);
            if (iterator != previousWindows.end()) {
                const std::size_t previousIndex = static_cast<std::size_t>(std::distance(previousWindows.begin(), iterator));
                if (previousIndex < previousWeights.size() && previousWeights[previousIndex] > 0.0f) {
                    weight = previousWeights[previousIndex];
                }
            }
        }
        weights.push_back(weight);
    }

    return weights;
}

void LayoutEngine::NormalizeWeights(std::vector<float>& weights) {
    LayoutPolicy::NormalizeWeights(weights);
}

RECT LayoutEngine::WorkAreaForMonitor(HMONITOR monitor) {
    return WindowGeometry::WorkAreaForMonitor(monitor);
}

void LayoutEngine::SyncMonitorWindows(MonitorState& state, std::vector<HWND> ordered, float defaultMainWidthRatio) {
    const std::vector<HWND> previousWindows = state.windows;
    const std::vector<float> previousWeights = ExportWeightsForState(state);
    const LayoutManagers::LayoutManager& layoutManager = LayoutManagers::LayoutManagerFor(state.layoutMode);
    const LayoutManagers::LayoutCapabilities capabilities = layoutManager.Capabilities();

    std::vector<float> weights;
    if (previousWindows.empty()) {
        weights = layoutManager.BuildSyncWeights(previousWindows, previousWeights, ordered, true);
    }

    if (weights.empty()) {
        weights = layoutManager.BuildSyncWeights(previousWindows, previousWeights, ordered, false);
    }

    if (capabilities.normalizeSyncWeights) {
        NormalizeWeights(weights);
    }
    state.windows = std::move(ordered);
    state.splitWeights = std::move(weights);
    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
}

bool LayoutEngine::AdjustWeightedWindowLengths(
    const std::vector<int>& currentLengths,
    std::vector<float>& weights,
    const std::vector<int>& minimumLengths,
    int totalBudget,
    std::size_t targetIndex,
    bool grow,
    float deltaRatio) {
    if (deltaRatio <= 0.0f || targetIndex >= weights.size() || minimumLengths.size() != weights.size() || currentLengths.size() != weights.size()) {
        return false;
    }

    const int deltaPixels = std::max(1, static_cast<int>(std::lround(deltaRatio * static_cast<float>(totalBudget))));
    const int desiredLength = currentLengths[targetIndex] + (grow ? deltaPixels : -deltaPixels);
    return SetWeightedWindowLength(currentLengths, weights, totalBudget, targetIndex, desiredLength);
}

bool LayoutEngine::UpdateSplitFromResize(MonitorState& state, HWND hwnd, HMONITOR monitor, const RECT& moveSizeStartRect, float defaultMainWidthRatio, int gap, int outerGap) {
    auto windowIterator = std::find(state.windows.begin(), state.windows.end(), hwnd);
    if (windowIterator == state.windows.end() || state.windows.size() < 2) {
        return false;
    }

    RECT currentRect{};
    if (!GetWindowRect(hwnd, &currentRect)) {
        return false;
    }

    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
    const std::size_t resizedIndex = static_cast<std::size_t>(std::distance(state.windows.begin(), windowIterator));
    return LayoutManagers::LayoutManagerFor(state.layoutMode).UpdateFromResize(
        state,
        monitor,
        resizedIndex,
        currentRect,
        moveSizeStartRect,
        defaultMainWidthRatio,
        gap,
        outerGap);
}

bool LayoutEngine::SetMonitorSplitLength(
    MonitorState& state,
    const std::vector<int>& currentLengths,
    const std::vector<int>& minimumLengths,
    int totalBudget,
    std::size_t targetIndex,
    int desiredLength,
    float defaultMainWidthRatio) {
    (void)minimumLengths;

    const LayoutManagers::LayoutManager& layoutManager = LayoutManagers::LayoutManagerFor(state.layoutMode);
    if (!layoutManager.Capabilities().supportsGenericWeightedResize) {
        return false;
    }

    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
    std::vector<float> splitWeights = ExportWeightsForState(state);
    if (!SetWeightedWindowLength(currentLengths, splitWeights, totalBudget, targetIndex, desiredLength)) {
        return false;
    }

    state.splitWeights = std::move(splitWeights);
    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
    return true;
}

bool LayoutEngine::AdjustMonitorSplitLengths(
    MonitorState& state,
    const std::vector<int>& currentLengths,
    const std::vector<int>& minimumLengths,
    int totalBudget,
    std::size_t targetIndex,
    bool grow,
    float deltaRatio,
    float defaultMainWidthRatio) {
    const LayoutManagers::LayoutManager& layoutManager = LayoutManagers::LayoutManagerFor(state.layoutMode);
    if (!layoutManager.Capabilities().supportsGenericWeightedResize) {
        return false;
    }

    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
    std::vector<float> splitWeights = ExportWeightsForState(state);
    if (!AdjustWeightedWindowLengths(currentLengths, splitWeights, minimumLengths, totalBudget, targetIndex, grow, deltaRatio)) {
        return false;
    }

    state.splitWeights = std::move(splitWeights);
    EnsureMonitorStateInitialized(state, defaultMainWidthRatio);
    return true;
}

}  // namespace quicktile