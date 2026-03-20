#include "windows/window_geometry.h"

#include <algorithm>
#include <cmath>

namespace quicktile::WindowGeometry {

RECT MonitorRectForMonitor(HMONITOR monitor) {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return RECT{};
    }

    return monitorInfo.rcMonitor;
}

SIZE MinimumTrackSizeForWindow(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return SIZE{1, 1};
    }

    MINMAXINFO minMaxInfo{};
    minMaxInfo.ptMinTrackSize.x = GetSystemMetrics(SM_CXMINTRACK);
    minMaxInfo.ptMinTrackSize.y = GetSystemMetrics(SM_CYMINTRACK);
    DWORD_PTR unusedResult = 0;
    SendMessageTimeoutW(
        window,
        WM_GETMINMAXINFO,
        0,
        reinterpret_cast<LPARAM>(&minMaxInfo),
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        50,
        &unusedResult);

    return SIZE{
        std::max(1, static_cast<int>(minMaxInfo.ptMinTrackSize.x)),
        std::max(1, static_cast<int>(minMaxInfo.ptMinTrackSize.y))};
}

RECT RawWorkAreaForMonitor(HMONITOR monitor) {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    return monitorInfo.rcWork;
}

RECT WorkAreaForMonitor(HMONITOR monitor) {
    return RawWorkAreaForMonitor(monitor);
}

float MonitorScaleFactor(HMONITOR monitor) {
    constexpr float kReferencePixels = 1080.0f;

    const RECT monitorRect = MonitorRectForMonitor(monitor);
    const int width = std::max(1, static_cast<int>(monitorRect.right - monitorRect.left));
    const int height = std::max(1, static_cast<int>(monitorRect.bottom - monitorRect.top));
    const int referenceDimension = std::max(1, std::min(width, height));
    return static_cast<float>(referenceDimension) / kReferencePixels;
}

int ScalePixelsForMonitor(HMONITOR monitor, int pixels) {
    if (pixels <= 0) {
        return 0;
    }

    return std::max(1, static_cast<int>(std::lround(static_cast<float>(pixels) * MonitorScaleFactor(monitor))));
}

void SetReservedTopInset(int inset) {
    (void)inset;
}

int ReservedTopInset() {
    return 0;
}

}  // namespace quicktile::WindowGeometry