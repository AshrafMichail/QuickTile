#include "workspace/virtual_desktop.h"

#include <shobjidl_core.h>

namespace quicktile {

namespace {

constexpr CLSID kVirtualDesktopManagerClsid{
    0xAA509086,
    0x5CA9,
    0x4C25,
    {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}};

IVirtualDesktopManager* g_virtualDesktopManager = nullptr;

std::wstring GuidToString(const GUID& guid) {
    wchar_t buffer[40] = {};
    const int length = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 1) {
        return L"global";
    }

    return std::wstring(buffer, static_cast<std::size_t>(length - 1));
}

bool TryGetWindowDesktopId(HWND hwnd, GUID& desktopId) {
    if (g_virtualDesktopManager == nullptr || hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    return SUCCEEDED(g_virtualDesktopManager->GetWindowDesktopId(hwnd, &desktopId));
}

HWND FindAnyWindowOnCurrentDesktop() {
    struct EnumData {
        HWND window = nullptr;
    } data{};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto& data = *reinterpret_cast<EnumData*>(lParam);
            if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
                return TRUE;
            }

            if (!VirtualDesktop::IsWindowOnCurrentDesktop(hwnd)) {
                return TRUE;
            }

            GUID desktopId{};
            if (!TryGetWindowDesktopId(hwnd, desktopId)) {
                return TRUE;
            }

            data.window = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&data));

    return data.window;
}

}  // namespace

bool VirtualDesktop::Initialize() {
    if (g_virtualDesktopManager != nullptr) {
        return true;
    }

    return SUCCEEDED(CoCreateInstance(
        kVirtualDesktopManagerClsid,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&g_virtualDesktopManager)));
}

void VirtualDesktop::Shutdown() {
    if (g_virtualDesktopManager != nullptr) {
        g_virtualDesktopManager->Release();
        g_virtualDesktopManager = nullptr;
    }
}

bool VirtualDesktop::IsWindowOnCurrentDesktop(HWND hwnd) {
    if (g_virtualDesktopManager == nullptr || hwnd == nullptr || !IsWindow(hwnd)) {
        return true;
    }

    BOOL onCurrentDesktop = TRUE;
    if (FAILED(g_virtualDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrentDesktop))) {
        return true;
    }

    return onCurrentDesktop != FALSE;
}

std::wstring VirtualDesktop::DetectCurrentDesktopKey() {
    if (g_virtualDesktopManager == nullptr) {
        return L"global";
    }

    const HWND foreground = GetForegroundWindow();
    GUID desktopId{};
    if (foreground != nullptr && IsWindowOnCurrentDesktop(foreground) && TryGetWindowDesktopId(foreground, desktopId)) {
        return GuidToString(desktopId);
    }

    if (const HWND candidate = FindAnyWindowOnCurrentDesktop(); candidate != nullptr && TryGetWindowDesktopId(candidate, desktopId)) {
        return GuidToString(desktopId);
    }

    return L"";
}

}  // namespace quicktile