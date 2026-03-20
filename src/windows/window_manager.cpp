#include "windows/window_manager.h"

#include "windows/event_router.h"
#include "windows/focus_tracker.h"
#include "workspace/workspace_manager.h"
#include "workspace/virtual_desktop.h"
#include "windows/window_classifier.h"

#include <algorithm>
#include <cwchar>
#include <string>

namespace quicktile {

namespace {

void InstallWinEventHooks(AppState& app) {
    const std::array<std::pair<DWORD, DWORD>, 11> ranges = {
        std::pair{EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART},
        std::pair{EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
        std::pair{EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        std::pair{EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH},
        std::pair{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        std::pair{EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS},
        std::pair{EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW},
        std::pair{EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE},
        std::pair{EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY},
        std::pair{EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
    };

    for (const auto& [minEvent, maxEvent] : ranges) {
        if (HWINEVENTHOOK hook = SetWinEventHook(minEvent, maxEvent, nullptr, EventRouter::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS)) {
            app.eventHooks.push_back(hook);
        } else {
            app.logger.ErrorLastWin32(L"SetWinEventHook failed", GetLastError());
        }
    }
}

void UninstallWinEventHooks(AppState& app) {
    for (HWINEVENTHOOK hook : app.eventHooks) {
        UnhookWinEvent(hook);
    }

    app.eventHooks.clear();
}

HWND FloatingToggleTargetWindow(AppState& app) {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return nullptr;
    }

    const std::array<HWND, 3> candidates = {
        foreground,
        GetAncestor(foreground, GA_ROOTOWNER),
        GetAncestor(foreground, GA_ROOT),
    };

    for (HWND candidate : candidates) {
        if (candidate != nullptr && IsWindow(candidate) && candidate != app.window) {
            return candidate;
        }
    }

    return nullptr;
}

bool MatchesExactProcessFloatRule(const WindowRuleSetting& rule, const std::wstring& processName) {
    return rule.action == WindowRuleAction::Float &&
        rule.className.empty() &&
        rule.windowTitle.empty() &&
        !rule.processName.empty() &&
        _wcsicmp(rule.processName.c_str(), processName.c_str()) == 0;
}

bool MatchesProcessName(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::wstring WindowTitleForWindow(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return L"Untitled Window";
    }

    std::wstring title(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    if (title.empty()) {
        return L"Untitled Window";
    }

    return title;
}

}  // namespace

HMONITOR WindowManager::MonitorKey(HMONITOR monitor) {
    return monitor;
}

bool WindowManager::ContainsWindow(const std::vector<HWND>& windows, HWND hwnd) {
    return std::find(windows.begin(), windows.end(), hwnd) != windows.end();
}

std::vector<HWND> WindowManager::CollectTiledWindows(AppState& app) {
    std::vector<HWND> windows;

    for (auto& [_, state] : WorkspaceManager::WorkspaceMonitors(app)) {
        if (state.layoutMode == LayoutMode::Floating) {
            continue;
        }

        state.windows.erase(
            std::remove_if(state.windows.begin(), state.windows.end(), [](HWND hwnd) { return !IsWindow(hwnd); }),
            state.windows.end());

        for (HWND hwnd : state.windows) {
            if (!ContainsWindow(windows, hwnd)) {
                windows.push_back(hwnd);
            }
        }
    }

    return windows;
}

std::vector<HWND> WindowManager::EnumerateManagedWindows(AppState& app, HMONITOR targetMonitor) {
    struct EnumData {
        AppState* app = nullptr;
        HMONITOR monitor;
        std::vector<HWND> windows;
    } data{&app, targetMonitor, {}};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto& enumData = *reinterpret_cast<EnumData*>(lParam);

            if (!WindowManager::IsManagedWindow(*enumData.app, hwnd)) {
                return TRUE;
            }

            if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != enumData.monitor) {
                return TRUE;
            }

            enumData.windows.push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));

    return data.windows;
}

std::vector<HWND> WindowManager::BuildOrderedWindows(HMONITOR monitor, const std::vector<HWND>& previous, const std::vector<HWND>& discovered) {
    (void)monitor;

    std::vector<HWND> ordered;
    ordered.reserve(discovered.size());

    for (HWND hwnd : previous) {
        if (std::find(discovered.begin(), discovered.end(), hwnd) != discovered.end()) {
            ordered.push_back(hwnd);
        }
    }

    for (HWND hwnd : discovered) {
        if (!ContainsWindow(ordered, hwnd)) {
            ordered.push_back(hwnd);
        }
    }

    return ordered;
}

ManagedWindowDecision WindowManager::GetManagedWindowDecision(AppState& app, HWND hwnd) {
    if (hwnd == nullptr) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::NullWindow};
    }

    if (hwnd == app.window) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::QuickTileWindow};
    }

    if (!IsWindow(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::InvalidWindow};
    }

    if (!IsWindowVisible(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::NotVisible};
    }

    if (IsIconic(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::Minimized};
    }

    if (IsZoomed(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::Maximized};
    }

    if (GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::NotRoot};
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::OwnedWindow};
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) != 0) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::ChildWindow};
    }

    if (!WindowClassifier::HasTilingWindowStyles(style)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::MissingTilingStyles};
    }

    if ((style & WS_POPUP) != 0 && (style & WS_CAPTION) == 0) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::PopupWithoutCaption};
    }

    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::ToolWindow};
    }

    if ((exStyle & WS_EX_NOACTIVATE) != 0) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::NoActivate};
    }

    if ((exStyle & WS_EX_TOPMOST) != 0) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::Topmost};
    }

    if (WindowClassifier::IsCloakedWindow(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::Cloaked};
    }

    if (!VirtualDesktop::IsWindowOnCurrentDesktop(hwnd)) {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::DifferentVirtualDesktop};
    }

    const WindowIdentity identity = WindowClassifier::IdentityForWindow(hwnd);
    const WindowExceptionMatch exceptionMatch = WindowClassifier::FindExceptionMatch(identity, &app.settings);
    if (exceptionMatch.matches) {
        const ManagedWindowDecisionReason reason = exceptionMatch.source == WindowExceptionMatchSource::UserRule
            ? ManagedWindowDecisionReason::UserFloatRule
            : ManagedWindowDecisionReason::BuiltInFloatRule;
        return ManagedWindowDecision{false, reason};
    }

    if (identity.className == L"Shell_TrayWnd" || identity.className == L"Progman" || identity.className == L"WorkerW") {
        return ManagedWindowDecision{false, ManagedWindowDecisionReason::ShellWindow};
    }

    return ManagedWindowDecision{true, ManagedWindowDecisionReason::Managed};
}

const wchar_t* WindowManager::ManagedWindowDecisionReasonText(ManagedWindowDecisionReason reason) {
    switch (reason) {
    case ManagedWindowDecisionReason::Managed:
        return L"eligible";
    case ManagedWindowDecisionReason::NullWindow:
        return L"no focused window";
    case ManagedWindowDecisionReason::QuickTileWindow:
        return L"quicktile internal window";
    case ManagedWindowDecisionReason::InvalidWindow:
        return L"invalid window handle";
    case ManagedWindowDecisionReason::NotVisible:
        return L"window is not visible";
    case ManagedWindowDecisionReason::Minimized:
        return L"window is minimized";
    case ManagedWindowDecisionReason::Maximized:
        return L"window is maximized";
    case ManagedWindowDecisionReason::NotRoot:
        return L"window is not a root top-level window";
    case ManagedWindowDecisionReason::OwnedWindow:
        return L"window has an owner";
    case ManagedWindowDecisionReason::ChildWindow:
        return L"window is a child window";
    case ManagedWindowDecisionReason::MissingTilingStyles:
        return L"window lacks resizable tiling styles";
    case ManagedWindowDecisionReason::PopupWithoutCaption:
        return L"popup window without caption";
    case ManagedWindowDecisionReason::ToolWindow:
        return L"tool window";
    case ManagedWindowDecisionReason::NoActivate:
        return L"no-activate window";
    case ManagedWindowDecisionReason::Topmost:
        return L"topmost transient window";
    case ManagedWindowDecisionReason::Cloaked:
        return L"window is cloaked";
    case ManagedWindowDecisionReason::DifferentVirtualDesktop:
        return L"window is on a different virtual desktop";
    case ManagedWindowDecisionReason::UserFloatRule:
        return L"matches a user float rule";
    case ManagedWindowDecisionReason::BuiltInFloatRule:
        return L"matches a built-in float rule";
    case ManagedWindowDecisionReason::ShellWindow:
        return L"shell desktop window";
    }

    return L"unknown";
}

bool WindowManager::IsManagedWindow(AppState& app, HWND hwnd) {
    return GetManagedWindowDecision(app, hwnd).managed;
}

HMONITOR WindowManager::ActiveMonitor() {
    if (const HWND foreground = GetForegroundWindow(); foreground != nullptr) {
        return MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY);
    }

    POINT point{};
    GetCursorPos(&point);
    return MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
}

bool WindowManager::IsMonitorTilingEnabled(AppState& app, HMONITOR monitor) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    if (monitor == nullptr) {
        return monitor != nullptr;
    }

    return WorkspaceManager::GetOrCreateMonitorState(app, monitor).layoutMode != LayoutMode::Floating;
}

void WindowManager::ReorderWindowByDrop(AppState& app, HWND hwnd, HMONITOR sourceMonitor, HMONITOR destinationMonitor) {
    WorkspaceManager::RefreshVirtualDesktopContext(app);

    if (!IsManagedWindow(app, hwnd)) {
        return;
    }

    WorkspaceManager::ReorderWindowByDrop(app, hwnd, sourceMonitor, destinationMonitor);
}

bool WindowManager::ToggleProcessFloatingRule(Settings& settings, const std::wstring& processName) {
    auto& rules = settings.windowRules;
    const auto newEnd = std::remove_if(rules.begin(), rules.end(), [&processName](const WindowRuleSetting& rule) {
        return MatchesExactProcessFloatRule(rule, processName);
    });
    const bool wasEnabled = newEnd != rules.end();
    rules.erase(newEnd, rules.end());

    if (wasEnabled) {
        return false;
    }

    WindowRuleSetting rule;
    rule.processName = processName;
    rule.action = WindowRuleAction::Float;

    const auto insertBefore = std::find_if(rules.begin(), rules.end(), [&processName](const WindowRuleSetting& existingRule) {
        return !existingRule.processName.empty() && MatchesProcessName(existingRule.processName, processName);
    });
    rules.insert(insertBefore, std::move(rule));
    return true;
}

FloatingToggleResult WindowManager::ToggleFocusedWindowFloating(AppState& app) {
    FloatingToggleResult result;
    const HWND hwnd = FloatingToggleTargetWindow(app);
    if (hwnd == nullptr) {
        return result;
    }

    result.windowTitle = WindowTitleForWindow(hwnd);

    const std::wstring processName = WindowClassifier::ProcessNameForWindow(hwnd);
    if (processName.empty()) {
        return result;
    }

    const Settings previousSettings = app.settings;
    result.enabled = ToggleProcessFloatingRule(app.settings, processName);

    if (!app.settings.Save()) {
        app.settings = previousSettings;
        return FloatingToggleResult{};
    }

    if (Settings::ReadFileContents(app.settings.fileContents)) {
        app.settings.lastInternalWriteContents = app.settings.fileContents;
        app.settings.pendingInternalWrite = true;
    }
    RefreshSettingsEffects(app);
    result.handled = true;
    return result;
}


void WindowManager::InstallEventHooks(AppState& app) {
    InstallWinEventHooks(app);
}

void WindowManager::RemoveEventHooks(AppState& app) {
    FocusTracker::UpdateFocusedWindowBorder(app, nullptr);
    UninstallWinEventHooks(app);
}

}  // namespace quicktile