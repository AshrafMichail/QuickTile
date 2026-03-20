#include "platform/shell_integration.h"

#include "ui/systray.h"

#include "app/app_commands.h"
#include "app/app_state.h"
#include "platform/logger.h"
#include "config/settings.h"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace quicktile {

namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"QuickTile";
constexpr wchar_t kTrayTooltipEnabled[] = L"QuickTile - tiling enabled";
constexpr wchar_t kTrayTooltipDisabled[] = L"QuickTile - tiling disabled";
constexpr UINT kTrayIconId = 1;
constexpr int kTrayIconDimension = 32;
constexpr std::size_t kTrayIconPixelCount = static_cast<std::size_t>(kTrayIconDimension) * static_cast<std::size_t>(kTrayIconDimension);

struct IconPixel {
    BYTE blue = 0;
    BYTE green = 0;
    BYTE red = 0;
    BYTE alpha = 0;
};

HICON g_enabledTrayIcon = nullptr;
HICON g_disabledTrayIcon = nullptr;

std::wstring TrimLaunchCommand(std::wstring_view value) {
    std::size_t start = 0;
    while (start < value.size() && iswspace(value[start]) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && iswspace(value[end - 1]) != 0) {
        --end;
    }

    return std::wstring(value.substr(start, end - start));
}

std::wstring QuoteLaunchArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = std::any_of(argument.begin(), argument.end(), [](wchar_t ch) {
        return iswspace(ch) != 0 || ch == L'\"';
    });
    if (!needsQuotes) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'\"');
    int trailingBackslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++trailingBackslashes;
            quoted.push_back(ch);
            continue;
        }

        if (ch == L'\"') {
            quoted.append(static_cast<std::size_t>(trailingBackslashes), L'\\');
            trailingBackslashes = 0;
            quoted.push_back(L'\\');
            quoted.push_back(L'\"');
            continue;
        }

        trailingBackslashes = 0;
        quoted.push_back(ch);
    }

    quoted.append(static_cast<std::size_t>(trailingBackslashes), L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool LaunchShellTarget(const std::wstring& file, const std::wstring& parameters) {
    if (file.empty()) {
        return false;
    }

    const wchar_t* parameterText = parameters.empty() ? nullptr : parameters.c_str();
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file.c_str(), parameterText, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

bool OpenPathWithShellExecute(const std::wstring& path, DWORD& error) {
    error = ERROR_SUCCESS;
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) {
        return true;
    }

    error = result <= 32 ? static_cast<DWORD>(result) : GetLastError();
    return false;
}

bool OpenPathWithNotepad(const std::wstring& path) {
    return LaunchShellTarget(L"notepad.exe", QuoteLaunchArgument(path));
}

std::wstring CommandProcessorPath() {
    const DWORD required = GetEnvironmentVariableW(L"ComSpec", nullptr, 0);
    if (required == 0) {
        return L"cmd.exe";
    }

    std::wstring path(static_cast<std::size_t>(required - 1), L'\0');
    GetEnvironmentVariableW(L"ComSpec", path.data(), required);
    return path;
}

bool LaunchWithCreateProcess(const std::wstring& commandLine, DWORD creationFlags = 0) {
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!created) {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool LaunchWithCommandProcessorShellStart(const std::wstring& commandLine) {
    const std::wstring shellCommand =
        QuoteLaunchArgument(CommandProcessorPath()) + L" /S /C start \"\" " + commandLine;
    return LaunchWithCreateProcess(shellCommand, CREATE_NO_WINDOW);
}

bool LaunchWithShellExecuteFromCommandLine(const std::wstring& commandLine) {
    int argumentCount = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argumentCount);
    if (argv == nullptr || argumentCount <= 0) {
        if (argv != nullptr) {
            LocalFree(static_cast<HLOCAL>(argv));
        }
        return false;
    }

    std::wstring parameters;
    for (int index = 1; index < argumentCount; ++index) {
        if (!parameters.empty()) {
            parameters += L' ';
        }
        parameters += QuoteLaunchArgument(argv[index]);
    }

    const bool launched = LaunchShellTarget(argv[0], parameters);
    LocalFree(static_cast<HLOCAL>(argv));
    return launched;
}

bool LaunchWithCommandProcessor(const std::wstring& commandLine) {
    const std::wstring wrappedCommand = QuoteLaunchArgument(commandLine);
    const std::wstring shellCommand = QuoteLaunchArgument(CommandProcessorPath()) + L" /S /C " + wrappedCommand;
    return LaunchWithCreateProcess(shellCommand, CREATE_NO_WINDOW);
}

std::size_t PixelIndex(int x, int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kTrayIconDimension) + static_cast<std::size_t>(x);
}

void FillRectPixels(
    std::array<IconPixel, kTrayIconPixelCount>& pixels,
    int left,
    int top,
    int right,
    int bottom,
    BYTE red,
    BYTE green,
    BYTE blue,
    BYTE alpha) {
    const int clampedLeft = std::clamp(left, 0, kTrayIconDimension);
    const int clampedTop = std::clamp(top, 0, kTrayIconDimension);
    const int clampedRight = std::clamp(right, 0, kTrayIconDimension);
    const int clampedBottom = std::clamp(bottom, 0, kTrayIconDimension);
    for (int y = clampedTop; y < clampedBottom; ++y) {
        for (int x = clampedLeft; x < clampedRight; ++x) {
            IconPixel& pixel = pixels[PixelIndex(x, y)];
            pixel.red = red;
            pixel.green = green;
            pixel.blue = blue;
            pixel.alpha = alpha;
        }
    }
}

void ApplyRoundedMask(std::array<IconPixel, kTrayIconPixelCount>& pixels) {
    const std::array<std::pair<int, int>, 12> corners = {
        std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1},
        std::pair{31, 0}, std::pair{30, 0}, std::pair{31, 1},
        std::pair{0, 31}, std::pair{0, 30}, std::pair{1, 31},
        std::pair{31, 31}, std::pair{30, 31}, std::pair{31, 30},
    };

    for (const auto& [x, y] : corners) {
        pixels[PixelIndex(x, y)].alpha = 0;
    }
}

HICON CreateTrayIcon(bool enabled) {
    std::array<IconPixel, kTrayIconPixelCount> pixels{};

    const BYTE backgroundRed = enabled ? 17 : 64;
    const BYTE backgroundGreen = enabled ? 28 : 68;
    const BYTE backgroundBlue = enabled ? 46 : 72;
    const BYTE accentRed = enabled ? 87 : 150;
    const BYTE accentGreen = enabled ? 193 : 154;
    const BYTE accentBlue = enabled ? 255 : 160;

    FillRectPixels(pixels, 2, 2, 30, 30, backgroundRed, backgroundGreen, backgroundBlue, 255);
    FillRectPixels(pixels, 5, 5, 17, 27, accentRed, accentGreen, accentBlue, 255);
    FillRectPixels(pixels, 19, 5, 27, 13, 242, 247, 252, 255);
    FillRectPixels(pixels, 19, 15, 27, 27, 242, 247, 252, 255);
    FillRectPixels(pixels, 17, 5, 19, 27, backgroundRed, backgroundGreen, backgroundBlue, 255);
    FillRectPixels(pixels, 19, 13, 27, 15, backgroundRed, backgroundGreen, backgroundBlue, 255);
    FillRectPixels(pixels, 7, 7, 15, 25, enabled ? 114 : 136, enabled ? 211 : 150, enabled ? 255 : 160, 255);
    ApplyRoundedMask(pixels);

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = 32;
    header.bV5Height = -32;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bitmapBits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(screenDc, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS, &bitmapBits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (colorBitmap == nullptr || bitmapBits == nullptr) {
        if (colorBitmap != nullptr) {
            DeleteObject(colorBitmap);
        }
        return nullptr;
    }

    std::memcpy(bitmapBits, pixels.data(), sizeof(pixels));
    HBITMAP maskBitmap = CreateBitmap(32, 32, 1, 1, nullptr);
    if (maskBitmap == nullptr) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

HICON TrayIconHandle(bool tilingEnabled) {
    HICON& icon = tilingEnabled ? g_enabledTrayIcon : g_disabledTrayIcon;
    if (icon == nullptr) {
        icon = CreateTrayIcon(tilingEnabled);
    }

    return icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

NOTIFYICONDATAW BuildTrayIconData(const AppState& app, HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayIconMessage;
    data.hIcon = TrayIconHandle(app.tilingEnabled);
    const wchar_t* tooltip = app.tilingEnabled ? kTrayTooltipEnabled : kTrayTooltipDisabled;
    wcsncpy_s(data.szTip, tooltip, _TRUNCATE);
    return data;
}

std::wstring ExecutableCommandLine() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return L"";
    }

    std::wstring command = L"\"";
    command.append(buffer, length);
    command += L"\"";
    return command;
}

UINT ShowTrayMenu(const AppState& app, HWND window) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    HMENU settingsMenu = CreatePopupMenu();
    if (settingsMenu == nullptr) {
        DestroyMenu(menu);
        return 0;
    }

    HMENU logMenu = CreatePopupMenu();
    if (logMenu == nullptr) {
        DestroyMenu(menu);
        return 0;
    }

    const UINT tilingFlags = MF_STRING | (app.tilingEnabled ? MF_CHECKED : MF_UNCHECKED);
    const UINT autoStartFlags = MF_STRING | (app.settings.autoStart ? MF_CHECKED : MF_UNCHECKED);

    AppendMenuW(settingsMenu, MF_STRING, CommandOpenSettings, L"Open...");
    AppendMenuW(settingsMenu, MF_STRING, CommandResetSettings, L"Reset...");
    AppendMenuW(logMenu, MF_STRING, CommandOpenLog, L"Open...");
    AppendMenuW(logMenu, MF_STRING, CommandClearLog, L"Clear");

    AppendMenuW(menu, tilingFlags, CommandToggleTiling, L"Enable Tiling");
    AppendMenuW(menu, MF_STRING, CommandRetileAll, L"Retile All");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), L"Settings");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(logMenu), L"Log");
    AppendMenuW(menu, autoStartFlags, CommandToggleAutoStart, L"Start On Login");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandShowHelp, L"Show Shortcuts...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, L"Exit");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        point.x,
        point.y,
        0,
        window,
        nullptr));
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);
    return command;
}

}  // namespace

bool ShellIntegration::LaunchCommand(std::wstring_view commandLine, Logger& logger) {
    const std::wstring trimmed = TrimLaunchCommand(commandLine);
    if (trimmed.empty()) {
        logger.Error(L"Launch shortcut command was empty");
        return false;
    }

    if (LaunchWithCreateProcess(trimmed)) {
        logger.Info(L"Launched shortcut via CreateProcess: " + trimmed);
        return true;
    }

    if (LaunchWithShellExecuteFromCommandLine(trimmed)) {
        logger.Info(L"Launched shortcut via ShellExecute: " + trimmed);
        return true;
    }

    if (LaunchWithCommandProcessorShellStart(trimmed)) {
        logger.Info(L"Launched shortcut via cmd start: " + trimmed);
        return true;
    }

    if (LaunchWithCommandProcessor(trimmed)) {
        logger.Info(L"Launched shortcut via cmd /C: " + trimmed);
        return true;
    }

    logger.ErrorLastWin32(L"Failed to launch shortcut command", GetLastError());
    logger.Error(L"Launch command: " + trimmed);
    return false;
}

bool ShellIntegration::InstallTrayIcon(const AppState& app, HWND window) {
    NOTIFYICONDATAW data = BuildTrayIconData(app, window);
    return Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void ShellIntegration::UpdateTrayIcon(const AppState& app, HWND window) {
    NOTIFYICONDATAW data = BuildTrayIconData(app, window);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ShellIntegration::RemoveTrayIcon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);

    if (g_enabledTrayIcon != nullptr) {
        DestroyIcon(g_enabledTrayIcon);
        g_enabledTrayIcon = nullptr;
    }

    if (g_disabledTrayIcon != nullptr) {
        DestroyIcon(g_disabledTrayIcon);
        g_disabledTrayIcon = nullptr;
    }
}

UINT ShellIntegration::HandleTrayMessage(const AppState& app, HWND window, LPARAM lParam) {
    switch (lParam) {
    case WM_CONTEXTMENU:
    case WM_RBUTTONUP:
    case WM_LBUTTONUP:
        return ShowTrayMenu(app, window);
    default:
        return 0;
    }
}

bool ShellIntegration::SetAutoStartEnabled(bool enabled) {
    HKEY key = nullptr;
    const LONG openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        kRunKeyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        nullptr);
    if (openResult != ERROR_SUCCESS) {
        return false;
    }

    bool success = true;
    if (enabled) {
        const std::wstring command = ExecutableCommandLine();
        if (command.empty()) {
            success = false;
        } else {
            const LONG result = RegSetValueExW(
                key,
                kRunValueName,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            success = result == ERROR_SUCCESS;
        }
    } else {
        const LONG result = RegDeleteValueW(key, kRunValueName);
        success = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    RegCloseKey(key);
    return success;
}

bool ShellIntegration::SetWindowArrangingEnabled(bool enabled) {
    BOOL value = enabled ? TRUE : FALSE;
    return SystemParametersInfoW(
        SPI_SETWINARRANGING,
        0,
        &value,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE;
}

bool ShellIntegration::OpenSettingsFile(Logger& logger) {
    const std::wstring path = Settings::FilePath();
    DWORD shellError = ERROR_SUCCESS;
    if (OpenPathWithShellExecute(path, shellError)) {
        logger.Info(L"Opened settings.yaml via ShellExecute");
        return true;
    }

    if (OpenPathWithNotepad(path)) {
        logger.Info(L"Opened settings.yaml via Notepad fallback");
        return true;
    }

    logger.ErrorLastWin32(L"Failed to open settings.yaml", shellError == ERROR_SUCCESS ? GetLastError() : shellError);
    logger.Error(L"Settings path: " + path);
    return false;
}

void ShellIntegration::OpenLogFile(const Logger& logger) {
    const std::wstring path = logger.FilePath();
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool ShellIntegration::ClearLogFile(Logger& logger) {
    return logger.Clear();
}

}  // namespace quicktile