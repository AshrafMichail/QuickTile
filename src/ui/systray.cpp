#include "ui/systray.h"

#include "app/app_commands.h"
#include "app/app_state.h"
#include "windows/inspect_window.h"
#include "layout/layout_engine.h"
#include "layout/layout_metadata.h"
#include "platform/logger.h"
#include "config/settings.h"
#include "platform/shell_integration.h"
#include "app/shortcuts.h"
#include "ui/status_overlay.h"
#include "windows/window_manager.h"
#include "workspace/workspace_manager.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

namespace quicktile {

namespace {

void MarkInternalSettingsWrite(AppState& app, const std::string& contents) {
    app.settings.lastInternalWriteContents = contents;
    app.settings.pendingInternalWrite = true;
}

void ShowChangeNotification(AppState& app, const std::wstring& title, const std::wstring& detail = L"") {
    if (!app.settings.changeNotifications) {
        return;
    }

    OverlayOptions options{};
    options.width = 0;
    if (title == L"Layout Changed") {
        options.titleVerticalOffset = 3;
    }

    app.statusOverlay.ShowDetailed(title, detail, options);
}

std::wstring JoinBindings(const std::vector<std::wstring>& bindings) {
    if (bindings.empty()) {
        return L"Unbound";
    }

    std::wstring text;
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        if (index != 0) {
            text += L", ";
        }
        text += bindings[index];
    }

    return text;
}

std::wstring LaunchShortcutLabel(const LaunchShortcutSetting& launchShortcut) {
    if (!launchShortcut.friendlyName.empty()) {
        return launchShortcut.friendlyName;
    }

    if (!launchShortcut.launchCommand.empty()) {
        return launchShortcut.launchCommand;
    }

    return L"Launch";
}

void ShowShortcutHelpOverlay(AppState& app) {
    const auto& shortcuts = app.settings.shortcuts;
    auto buildShortcutItems = [](const std::vector<std::pair<std::wstring, std::wstring>>& lines) {
        std::vector<OverlayShortcutItem> rows;
        rows.reserve(lines.size());
        for (const auto& [label, value] : lines) {
            rows.push_back(OverlayShortcutItem{label, value});
        }
        return rows;
    };
    auto buildSectionNode = [&](const std::wstring& heading, std::vector<OverlayShortcutItem> items) {
        std::vector<OverlayNode> children;
        children.push_back(OverlayNode::Heading(heading));
        children.push_back(OverlayNode::ShortcutList(std::move(items)));
        return OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(children), 6, 6);
    };

    std::vector<std::pair<std::wstring, std::wstring>> layoutLines;
    layoutLines.reserve(kLayoutMetadata.size());
    for (const LayoutMetadata& metadata : kLayoutMetadata) {
        layoutLines.emplace_back(LayoutModeDisplayName(metadata.mode), JoinBindings(shortcuts.*(metadata.shortcutMember)));
    }

    std::vector<OverlayNode> column0;
    std::vector<OverlayNode> column1;
    std::vector<OverlayNode> column2;

    column0.push_back(buildSectionNode(
        L"Tiling",
        buildShortcutItems({
            {L"Toggle tiling", JoinBindings(shortcuts.toggleTiling)},
            {L"Toggle top bar", JoinBindings(shortcuts.toggleTopBar)},
            {L"Retile", JoinBindings(shortcuts.retile)},
            {L"Focused floating", JoinBindings(shortcuts.toggleFloating)},
        })));
    column2.push_back(buildSectionNode(L"Layouts", buildShortcutItems(layoutLines)));
    if (!app.settings.launchShortcuts.empty()) {
        std::vector<std::pair<std::wstring, std::wstring>> launchLines;
        launchLines.reserve(app.settings.launchShortcuts.size());
        for (const LaunchShortcutSetting& launchShortcut : app.settings.launchShortcuts) {
            launchLines.emplace_back(
                LaunchShortcutLabel(launchShortcut),
                launchShortcut.shortcut.empty() ? std::wstring(L"Unbound") : launchShortcut.shortcut);
        }

        column0.push_back(buildSectionNode(L"Launch Apps", buildShortcutItems(launchLines)));
    }
    column1.push_back(buildSectionNode(
        L"Move Windows",
        buildShortcutItems({
            {L"Left", JoinBindings(shortcuts.moveLeft)},
            {L"Right", JoinBindings(shortcuts.moveRight)},
            {L"Up", JoinBindings(shortcuts.moveUp)},
            {L"Down", JoinBindings(shortcuts.moveDown)},
        })));
    column1.push_back(buildSectionNode(
        L"Focus Window",
        buildShortcutItems({
            {L"Left", JoinBindings(shortcuts.focusLeft)},
            {L"Right", JoinBindings(shortcuts.focusRight)},
            {L"Up", JoinBindings(shortcuts.focusUp)},
            {L"Down", JoinBindings(shortcuts.focusDown)},
        })));
    column1.push_back(buildSectionNode(
        L"Workspaces",
        buildShortcutItems({
            {L"Workspace 1", JoinBindings(shortcuts.switchWorkspace1)},
            {L"Workspace 2", JoinBindings(shortcuts.switchWorkspace2)},
            {L"Workspace 3", JoinBindings(shortcuts.switchWorkspace3)},
            {L"Workspace 4", JoinBindings(shortcuts.switchWorkspace4)},
            {L"Workspace 5", JoinBindings(shortcuts.switchWorkspace5)},
        })));
    column2.push_back(buildSectionNode(
        L"Resize",
        buildShortcutItems({
            {L"Grow left edge", JoinBindings(shortcuts.growLeft)},
            {L"Grow right edge", JoinBindings(shortcuts.growRight)},
            {L"Grow top edge", JoinBindings(shortcuts.growUp)},
            {L"Grow bottom edge", JoinBindings(shortcuts.growDown)},
            {L"Shrink left edge", JoinBindings(shortcuts.shrinkLeft)},
            {L"Shrink right edge", JoinBindings(shortcuts.shrinkRight)},
            {L"Shrink top edge", JoinBindings(shortcuts.shrinkUp)},
            {L"Shrink bottom edge", JoinBindings(shortcuts.shrinkDown)},
        })));
    column0.push_back(buildSectionNode(
        L"System",
        buildShortcutItems({
            {L"Help", JoinBindings(shortcuts.showHelp)},
            {L"Inspect focused window", JoinBindings(shortcuts.inspectWindow)},
            {L"Exit", JoinBindings(shortcuts.exit)},
        })));

    OverlayOptions options;
    options.width = 1380;
    options.durationMs = 12000;
    options.detailCentered = false;
    options.titleSpacing = 18;
    options.renderShortcutBadges = true;
    options.nodes.push_back(OverlayNode::Stack(
        OverlayStackDirection::Horizontal,
        {
            OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(column0), 18),
            OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(column1), 18),
            OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(column2), 18),
        },
        18,
        0,
        true));

    app.statusOverlay.ShowDetailed(
        L"QuickTile Shortcuts",
        L"",
        options);
}

void ApplyLoadedSettings(AppState& app, const Settings&, const Settings& newSettings) {
    app.settings = newSettings;
    app.tilingEnabled = newSettings.tilingEnabled;
    ShellIntegration::SetAutoStartEnabled(newSettings.autoStart);
    Shortcuts::UnregisterHotkeys(app, app.window);
    Shortcuts::RegisterHotkeys(app, app.window);

    WindowManager::RefreshSettingsEffects(app);
    ShellIntegration::UpdateTrayIcon(app, app.window);
}

bool ResetSettingsToDefaults(AppState& app, HWND hwnd) {
    const int choice = MessageBoxW(
        hwnd,
        L"Reset settings.yaml to the default QuickTile configuration? This will overwrite your current settings file.",
        L"QuickTile",
        MB_OKCANCEL | MB_ICONWARNING);
    if (choice != IDOK) {
        return true;
    }

    const Settings previousSettings = app.settings;
    Settings defaultSettings = Settings::Defaults();
    if (!defaultSettings.Save()) {
        app.logger.Error(L"Failed to save default settings.yaml");
        MessageBoxW(hwnd, L"Unable to write the default settings file.", L"QuickTile", MB_OK | MB_ICONERROR);
        return true;
    }

    std::string contents;
    if (!Settings::ReadFileContents(contents)) {
        app.logger.Error(L"Failed to read settings.yaml after resetting defaults");
        MessageBoxW(hwnd, L"Settings were reset, but the saved file could not be read back.", L"QuickTile", MB_OK | MB_ICONERROR);
        return true;
    }

    defaultSettings.fileContents = contents;
    defaultSettings.lastInternalWriteContents = contents;
    defaultSettings.pendingInternalWrite = true;
    ApplyLoadedSettings(app, previousSettings, defaultSettings);
    if (app.tilingEnabled) {
        WindowManager::TileAllKnownMonitors(app);
    }

    app.logger.Info(L"Reset settings.yaml to defaults");
    ShowChangeNotification(app, L"Settings Reset", L"Restored default settings.yaml");
    return true;
}

void PersistSettingsSnapshot(AppState& app) {
    if (!app.settings.Save()) {
        app.logger.Error(L"Failed to save settings snapshot");
        return;
    }

    if (!Settings::ReadFileContents(app.settings.fileContents)) {
        app.logger.Error(L"Failed to refresh in-memory settings snapshot after save");
        return;
    }

    MarkInternalSettingsWrite(app, app.settings.fileContents);
}

}  // namespace

bool Systray::Initialize(AppState& app, HWND window) {
    return ShellIntegration::InstallTrayIcon(app, window);
}

void Systray::Shutdown(HWND window) {
    ShellIntegration::RemoveTrayIcon(window);
}

bool Systray::HandleNotificationMessage(AppState& app, HWND window, LPARAM lParam) {
    const UINT command = ShellIntegration::HandleTrayMessage(app, window, lParam);
    if (command == 0) {
        return false;
    }

    return HandleCommand(app, command, window);
}

void Systray::ReloadSettings(AppState& app) {
    std::string currentContents;
    if (!Settings::ReadFileContents(currentContents)) {
        return;
    }

    const bool isInternalWrite = app.settings.pendingInternalWrite && currentContents == app.settings.lastInternalWriteContents;

    if (currentContents == app.settings.fileContents) {
        if (isInternalWrite) {
            app.settings.pendingInternalWrite = false;
            app.settings.lastInternalWriteContents.clear();
        }
        return;
    }

    const bool suppressNotification = isInternalWrite;
    app.settings.pendingInternalWrite = false;
    app.settings.lastInternalWriteContents.clear();

    Settings reloadedSettings;
    std::wstring errorMessage;
    if (!Settings::LoadFromYaml(currentContents, reloadedSettings, &errorMessage)) {
        app.logger.Error(L"Failed to reload settings.yaml: " + errorMessage);
        return;
    }

    const Settings previousSettings = app.settings;
    reloadedSettings.fileContents = std::move(currentContents);
    ApplyLoadedSettings(app, previousSettings, reloadedSettings);
    app.logger.Info(L"Reloaded settings.yaml");
    if (!suppressNotification) {
        ShowChangeNotification(app, L"Settings Reloaded", L"Applied settings.yaml");
    }
}

bool Systray::HandleCommand(AppState& app, UINT command, HWND hwnd) {
    if (const LayoutMetadata* layout = FindLayoutMetadataByCommand(command); layout != nullptr) {
        if (const LayoutChangeResult result = WindowManager::SetActiveMonitorLayout(app, layout->mode); result.handled) {
            ShowChangeNotification(app, L"Layout Changed", LayoutModeDisplayName(result.layoutMode));
        }
        return true;
    }

    switch (command) {
    case CommandToggleTiling:
        app.tilingEnabled = !app.tilingEnabled;
        if (app.tilingEnabled) {
            WindowManager::TileAllKnownMonitors(app);
        }
        ShellIntegration::UpdateTrayIcon(app, hwnd);
        ShowChangeNotification(app, app.tilingEnabled ? L"Tiling Enabled" : L"Tiling Disabled");
        return true;
    case CommandToggleTopBar:
        app.settings.topBarEnabled = !app.settings.topBarEnabled;
        app.topBar.SetEnabled(app.settings.topBarEnabled);
        app.topBar.Refresh(app);
        PersistSettingsSnapshot(app);
        ShowChangeNotification(app, app.settings.topBarEnabled ? L"Top Bar Enabled" : L"Top Bar Disabled");
        return true;
    case CommandRetileAll:
        WindowManager::TileAllKnownMonitors(app);
        return true;
    case CommandShowHelp:
        ShowShortcutHelpOverlay(app);
        return true;
    case CommandInspectWindow:
        InspectWindow::ShowFocusedWindowOverlay(app);
        return true;
    case CommandReloadSettings:
        ReloadSettings(app);
        return true;
    case CommandOpenSettings:
        if (!ShellIntegration::OpenSettingsFile(app.logger)) {
            MessageBoxW(hwnd, L"Unable to open settings.yaml.", L"QuickTile", MB_OK | MB_ICONERROR);
        }
        return true;
    case CommandResetSettings:
    case CommandResetAll:
        return ResetSettingsToDefaults(app, hwnd);
    case CommandOpenLog:
        ShellIntegration::OpenLogFile(app.logger);
        return true;
    case CommandClearLog:
        if (!ShellIntegration::ClearLogFile(app.logger)) {
            MessageBoxW(hwnd, L"Unable to clear the QuickTile log.", L"QuickTile", MB_OK | MB_ICONERROR);
            return true;
        }
        ShowChangeNotification(app, L"Log Cleared", L"Truncated quicktile.log");
        return true;
    case CommandToggleFocusedWindowFloating:
        if (const FloatingToggleResult result = WindowManager::ToggleFocusedWindowFloating(app); result.handled) {
            ShowChangeNotification(app, result.enabled ? L"Floating Enabled" : L"Floating Disabled", result.windowTitle);
        }
        return true;
    case CommandToggleAutoStart: {
        const bool newValue = !app.settings.autoStart;
        if (!ShellIntegration::SetAutoStartEnabled(newValue)) {
            MessageBoxW(hwnd, L"Unable to update the Start On Login setting.", L"QuickTile", MB_OK | MB_ICONERROR);
            return true;
        }

        app.settings.autoStart = newValue;
        PersistSettingsSnapshot(app);
        ShellIntegration::UpdateTrayIcon(app, hwnd);
        ShowChangeNotification(app, newValue ? L"Start On Login Enabled" : L"Start On Login Disabled");
        return true;
    }
    case CommandExit:
        DestroyWindow(hwnd);
        return true;
    default:
        return false;
    }
}

}  // namespace quicktile