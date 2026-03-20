#pragma once

#include "layout/layout_types.h"

#include <windows.h>

#include <vector>

namespace quicktile {

struct TileData {
    HWND window = nullptr;
    RECT rect{};
};

struct MonitorLayoutData {
    HMONITOR handle = nullptr;
    RECT rect{};
    LayoutMode layoutMode = DefaultLayoutMode();
    float mainWidthRatio = 0.0f;
    std::vector<float> splitWeights;
    std::vector<TileData> tiles;
};

struct ResizePlan {
    enum class Kind {
        None,
        AdjustMainSplit,
        AdjustSpiralSplit,
        AdjustStackWindow,
        AdjustColumnWindow,
    };

    Kind kind = Kind::None;
    HMONITOR monitor = nullptr;
    float mainDelta = 0.0f;
    std::size_t targetIndex = 0;
    bool growTarget = false;
};

struct WindowPlacement {
    HWND window = nullptr;
    RECT rect{};
};

struct LayoutPlan {
    std::vector<WindowPlacement> placements;
};

}  // namespace quicktile