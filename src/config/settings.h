#pragma once

#include "config/config.h"
#include "layout/layout_types.h"

#include <string>
#include <vector>

namespace quicktile {

class Logger;

enum class WindowRuleAction {
    Float,
};

struct WindowRuleSetting {
    std::wstring className;
    std::wstring windowTitle;
    std::wstring processName;
    WindowRuleAction action = WindowRuleAction::Float;
};

struct LaunchShortcutSetting {
    std::wstring friendlyName;
    std::wstring launchCommand;
    std::wstring shortcut;
};

enum class TopBarWidgetPosition {
    Disabled,
    Left,
    Center,
    Right,
};

enum class TopBarWidgetKind {
    Clock,
    Date,
    AppName,
    LayoutType,
    Workspaces,
};

struct TopBarWidgetSettings {
    TopBarWidgetPosition clock = TopBarWidgetPosition::Right;
    TopBarWidgetPosition date = TopBarWidgetPosition::Right;
    TopBarWidgetPosition appName = TopBarWidgetPosition::Center;
    TopBarWidgetPosition layoutType = TopBarWidgetPosition::Left;
    TopBarWidgetPosition workspaces = TopBarWidgetPosition::Left;
    std::vector<TopBarWidgetKind> order{
        TopBarWidgetKind::Clock,
        TopBarWidgetKind::Date,
        TopBarWidgetKind::AppName,
        TopBarWidgetKind::LayoutType,
        TopBarWidgetKind::Workspaces,
    };
};

struct ShortcutSettings {
    std::vector<std::wstring> toggleTiling{L"Alt+T"};
    std::vector<std::wstring> toggleTopBar{L"Alt+Shift+B"};
    std::vector<std::wstring> retile{L"Alt+Shift+W"};
    std::vector<std::wstring> focusLeft{L"Alt+H", L"Alt+Left"};
    std::vector<std::wstring> focusUp{L"Alt+K", L"Alt+Up"};
    std::vector<std::wstring> focusRight{L"Alt+L", L"Alt+Right"};
    std::vector<std::wstring> focusDown{L"Alt+J", L"Alt+Down"};
    std::vector<std::wstring> moveLeft{L"Alt+Shift+H", L"Alt+Shift+Left"};
    std::vector<std::wstring> moveUp{L"Alt+Shift+K", L"Alt+Shift+Up"};
    std::vector<std::wstring> moveRight{L"Alt+Shift+L", L"Alt+Shift+Right"};
    std::vector<std::wstring> moveDown{L"Alt+Shift+J", L"Alt+Shift+Down"};
    std::vector<std::wstring> growLeft{L"Alt+Ctrl+Left"};
    std::vector<std::wstring> growUp{L"Alt+Ctrl+Up"};
    std::vector<std::wstring> growRight{L"Alt+Ctrl+Right"};
    std::vector<std::wstring> growDown{L"Alt+Ctrl+Down"};
    std::vector<std::wstring> shrinkLeft{L"Alt+Ctrl+Shift+Left"};
    std::vector<std::wstring> shrinkUp{L"Alt+Ctrl+Shift+Up"};
    std::vector<std::wstring> shrinkRight{L"Alt+Ctrl+Shift+Right"};
    std::vector<std::wstring> shrinkDown{L"Alt+Ctrl+Shift+Down"};
    std::vector<std::wstring> toggleFloating{L"Alt+Shift+F"};
    std::vector<std::wstring> layoutFloating{L"Alt+Shift+1"};
    std::vector<std::wstring> layoutMainStack{L"Alt+Shift+2"};
    std::vector<std::wstring> layoutVerticalColumns{L"Alt+Shift+3"};
    std::vector<std::wstring> layoutMonocle{L"Alt+Shift+5"};
    std::vector<std::wstring> layoutSpiral{L"Alt+Shift+4"};
    std::vector<std::wstring> showHelp{L"Alt+Shift+F1"};
    std::vector<std::wstring> inspectWindow{L"Alt+Shift+I"};
    std::vector<std::wstring> switchWorkspace1{L"Alt+Ctrl+1"};
    std::vector<std::wstring> switchWorkspace2{L"Alt+Ctrl+2"};
    std::vector<std::wstring> switchWorkspace3{L"Alt+Ctrl+3"};
    std::vector<std::wstring> switchWorkspace4{L"Alt+Ctrl+4"};
    std::vector<std::wstring> switchWorkspace5{L"Alt+Ctrl+5"};
    std::vector<std::wstring> exit{L"Alt+Shift+Q"};
};

class Settings {
public:
    bool tilingEnabled = true;
    bool autoStart = true;
    bool changeNotifications = true;
    bool topBarEnabled = true;
    LayoutMode defaultLayoutMode = DefaultLayoutMode();
    int innerGap = kDefaultInnerGap;
    int outerGap = kDefaultOuterGap;
    int topBarHeight = 22;
    float topBarOpacity = kDefaultTopBarOpacity;
    COLORREF focusedBorderColor = kFocusedBorderColor;
    float resizeStepRatio = kResizeStepRatio;
    TopBarWidgetSettings topBarWidgets;
    std::vector<WindowRuleSetting> windowRules;
    std::vector<LaunchShortcutSetting> launchShortcuts;
    ShortcutSettings shortcuts;
    std::string fileContents;
    std::string lastInternalWriteContents;
    bool pendingInternalWrite = false;

    static Settings Defaults();
    static std::wstring FilePath();
    static bool ReadFileContents(std::string& contents);
    static bool LoadFromYaml(const std::string& yaml, Settings& settings, std::wstring* errorMessage = nullptr);
    bool Load(Logger& logger, std::wstring* errorMessage = nullptr);
    bool Save() const;
};

}  // namespace quicktile