#pragma once

#include "app/app_state.h"

#include <string_view>
#include <windows.h>

namespace quicktile {

class Logger;

class ShellIntegration {
public:
    static bool InstallTrayIcon(const AppState& app, HWND window);
    static void UpdateTrayIcon(const AppState& app, HWND window);
    static void RemoveTrayIcon(HWND window);
    static UINT HandleTrayMessage(const AppState& app, HWND window, LPARAM lParam);
    static bool LaunchCommand(std::wstring_view commandLine, Logger& logger);
    static bool SetAutoStartEnabled(bool enabled);
    static bool SetWindowArrangingEnabled(bool enabled);
    static bool OpenSettingsFile(Logger& logger);
    static void OpenLogFile(const Logger& logger);
    static bool ClearLogFile(Logger& logger);
};

}  // namespace quicktile