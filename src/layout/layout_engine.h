#pragma once

#include "app/app_state.h"

namespace quicktile {

class LayoutEngine {
public:
    struct MonitorSplitStateData {
        float mainWidthRatio = 0.0;
        std::vector<float> splitWeights;
    };

    static float ClampMainWidthRatio(float ratio);
    static void EnsureMonitorStateInitialized(MonitorState& state, float defaultMainWidthRatio);
    static void RetargetDefaultMainWidthRatio(MonitorState& state, float oldDefaultMainWidthRatio, float newDefaultMainWidthRatio);
    static MonitorSplitStateData ExportMonitorSplitState(const MonitorState& state);
    static void RestoreMonitorSplitState(MonitorState& state, LayoutMode layoutMode, float mainWidthRatio, const std::vector<float>& splitWeights, float defaultMainWidthRatio);
    static void SetMonitorLayoutMode(MonitorState& state, LayoutMode layoutMode, float defaultMainWidthRatio);
    static std::vector<int> BuildMinimumStackHeights(const MonitorState& state);
    static std::vector<int> BuildCurrentStackHeights(const MonitorState& state);
    static int StackHeightBudgetForMonitor(HMONITOR monitor, std::size_t stackCount, int gap, int outerGap);
    static std::vector<int> BuildMinimumColumnWidths(const MonitorState& state);
    static std::vector<int> BuildCurrentColumnWidths(const MonitorState& state);
    static int ColumnWidthBudgetForMonitor(HMONITOR monitor, std::size_t columnCount, int gap, int outerGap);
    static std::vector<float> BuildStackWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered);
    static std::vector<float> BuildColumnWeights(
        const std::vector<HWND>& previousWindows,
        const std::vector<float>& previousWeights,
        const std::vector<HWND>& ordered);
    static void NormalizeWeights(std::vector<float>& weights);
    static RECT WorkAreaForMonitor(HMONITOR monitor);
    static void SyncMonitorWindows(MonitorState& state, std::vector<HWND> ordered, float defaultMainWidthRatio);
    static bool AdjustWeightedWindowLengths(const std::vector<int>& currentLengths, std::vector<float>& weights, const std::vector<int>& minimumLengths, int totalBudget, std::size_t targetIndex, bool grow, float deltaRatio);
    static bool SetMonitorSplitLength(MonitorState& state, const std::vector<int>& currentLengths, const std::vector<int>& minimumLengths, int totalBudget, std::size_t targetIndex, int desiredLength, float defaultMainWidthRatio);
    static bool AdjustMonitorSplitLengths(MonitorState& state, const std::vector<int>& currentLengths, const std::vector<int>& minimumLengths, int totalBudget, std::size_t targetIndex, bool grow, float deltaRatio, float defaultMainWidthRatio);
    static bool UpdateSplitFromResize(MonitorState& state, HWND hwnd, HMONITOR monitor, const RECT& moveSizeStartRect, float defaultMainWidthRatio, int gap, int outerGap);
};

}  // namespace quicktile