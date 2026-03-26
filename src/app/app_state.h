#pragma once

#include "platform/file_watcher.h"
#include "layout/layout_types.h"
#include "platform/logger.h"
#include "config/settings.h"
#include "ui/drop_preview_overlay.h"
#include "ui/status_overlay.h"
#include "ui/top_bar.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace quicktile {

constexpr int kWorkspaceCount = 5;

struct MonitorState {
    std::vector<HWND> windows;
    LayoutMode layoutMode = DefaultLayoutMode();
    float mainWidthRatio = 0.0;
    std::vector<float> splitWeights;
};

using MonitorStateMap = std::unordered_map<HMONITOR, MonitorState>;

struct DesktopWorkspaceState {
    int currentWorkspaceIndex = 0;
    std::array<MonitorStateMap, kWorkspaceCount> workspaces;
};

struct Window {
    HWND focused = nullptr;
    HWND moveSize = nullptr;
    RECT moveSizeStartRect{};
};

struct Monitors {
    HMONITOR moveSizeOriginMonitor = nullptr;
    MonitorStateMap activeWorkspaceMonitors;
    bool activeWorkspaceLoaded = false;
    std::unordered_map<std::wstring, DesktopWorkspaceState> workspaceStatesByDesktop;
};

struct AppState {
    explicit AppState(HINSTANCE instanceValue = nullptr)
        : instance(instanceValue), dropPreview(instanceValue, logger), statusOverlay(instanceValue, logger), topBar(instanceValue, logger) {
        desktopDisplayNumbers.emplace(L"global", 0);
    }

    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    Logger logger;
    DropPreviewOverlay dropPreview;
    StatusOverlay statusOverlay;
    TopBar topBar;
    bool tilingEnabled = true;
    std::unique_ptr<FileWatcher> fileWatcher;
    Settings settings;
    Window windowState;
    Monitors monitorState;
    std::wstring currentDesktopKey = L"global";
    std::wstring currentDesktopName = L"Desktop";
    std::unordered_map<std::wstring, int> desktopDisplayNumbers;
    int nextDesktopDisplayNumber = 1;
    std::vector<HWINEVENTHOOK> eventHooks;
};

}  // namespace quicktile