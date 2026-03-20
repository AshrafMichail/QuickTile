#pragma once

#include <windows.h>

namespace quicktile::WindowGeometry {

SIZE MinimumTrackSizeForWindow(HWND window);
RECT MonitorRectForMonitor(HMONITOR monitor);
RECT RawWorkAreaForMonitor(HMONITOR monitor);
RECT WorkAreaForMonitor(HMONITOR monitor);
float MonitorScaleFactor(HMONITOR monitor);
int ScalePixelsForMonitor(HMONITOR monitor, int pixels);
void SetReservedTopInset(int inset);
int ReservedTopInset();

}  // namespace quicktile::WindowGeometry