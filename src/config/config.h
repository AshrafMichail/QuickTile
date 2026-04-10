#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <array>

struct QuickTileWindowException {
    const wchar_t* className;
    const wchar_t* windowTitle;
    const wchar_t* processName;
};

namespace quicktile {

constexpr int kDefaultInnerGap = 2;
constexpr int kDefaultOuterGap = 4;
constexpr float kDefaultTopBarOpacity = 0.8f;
constexpr COLORREF kFocusedBorderColor = RGB(0, 120, 215);
constexpr COLORREF kDropPreviewBorderColor = RGB(255, 140, 0);
constexpr int kDropPreviewBorderThickness = 4;
constexpr float kDefaultMainWidthRatio = 0.5f;
constexpr float kMinMainWidthRatio = 0.2f;
constexpr float kMaxMainWidthRatio = 0.8f;
constexpr float kResizeStepRatio = 0.05f;
constexpr float kMinStackWeight = 0.08f;
constexpr std::array<QuickTileWindowException, 15> kFloatingWindowExceptions{{
    {L"#32770", nullptr, nullptr},
    {L"Credential Dialog Xaml Host", nullptr, nullptr},
    {L"Xaml_WindowedPopupClass", L"Windows Security", nullptr},
    {L"TscShellContainerClass", nullptr, nullptr},
    {nullptr, nullptr, L"msiexec.exe"},
    {nullptr, nullptr, L"consent.exe"},
    {nullptr, nullptr, L"CredentialUIBroker.exe"},
    {nullptr, nullptr, L"SystemSettingsAdminFlows.exe"},
    {nullptr, nullptr, L"setup.exe"},
    {nullptr, nullptr, L"vs_installer.exe"},
    {nullptr, nullptr, L"vs_installer.windows.exe"},
    {nullptr, nullptr, L"vs_installershell.exe"},
    {nullptr, nullptr, L"PowerToys.Settings.exe"},
    {nullptr, nullptr, L"Microsoft.CmdPal.UI.exe"},
    {nullptr, nullptr, L"Taskmgr.exe"},
}};

enum HotkeyId : int {
    HotkeyToggleTiling = 1,
    HotkeyRetile,
    HotkeyFocusPrevH,
    HotkeyFocusPrevLeft,
    HotkeyFocusPrevK,
    HotkeyFocusPrevUp,
    HotkeyFocusNextL,
    HotkeyFocusNextRight,
    HotkeyFocusNextJ,
    HotkeyFocusNextDown,
    HotkeyMovePrevH,
    HotkeyMovePrevLeft,
    HotkeyMovePrevK,
    HotkeyMovePrevUp,
    HotkeyMoveNextL,
    HotkeyMoveNextRight,
    HotkeyMoveNextJ,
    HotkeyMoveNextDown,
    HotkeyResizeLeftH,
    HotkeyResizeLeft,
    HotkeyResizeUpK,
    HotkeyResizeUp,
    HotkeyResizeRightL,
    HotkeyResizeRight,
    HotkeyResizeDownJ,
    HotkeyResizeDown,
    HotkeyToggleFloating,
    HotkeyExit,
};

}  // namespace quicktile