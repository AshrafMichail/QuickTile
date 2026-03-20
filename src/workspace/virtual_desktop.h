#pragma once

#include <windows.h>

#include <string>

namespace quicktile {

class VirtualDesktop {
public:
    static bool Initialize();
    static void Shutdown();
    static bool IsWindowOnCurrentDesktop(HWND hwnd);
    static std::wstring DetectCurrentDesktopKey();
};

}  // namespace quicktile