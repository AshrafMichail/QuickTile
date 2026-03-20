#include "app/shortcuts.h"

#include "app/app_commands.h"
#include "app/app_state.h"
#include "platform/logger.h"
#include "platform/shell_integration.h"
#include "config/settings.h"
#include "windows/window_manager.h"
#include "workspace/workspace_manager.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quicktile {

namespace {

enum class ShortcutAction : std::uint8_t {
    ToggleTiling,
    ToggleTopBar,
    Retile,
    FocusLeft,
    FocusUp,
    FocusRight,
    FocusDown,
    MoveLeft,
    MoveUp,
    MoveRight,
    MoveDown,
    GrowLeft,
    GrowUp,
    GrowRight,
    GrowDown,
    ShrinkLeft,
    ShrinkUp,
    ShrinkRight,
    ShrinkDown,
    ToggleFloating,
    LayoutFloating,
    LayoutMainStack,
    LayoutVerticalColumns,
    LayoutMonocle,
    LayoutSpiral,
    ShowHelp,
    InspectWindow,
    SwitchWorkspace1,
    SwitchWorkspace2,
    SwitchWorkspace3,
    SwitchWorkspace4,
    SwitchWorkspace5,
    Exit,
    Count,
};

enum class ShortcutHandlerKind : std::uint8_t {
    PostCommand,
    Retile,
    FocusDirection,
    MoveDirection,
    ResizeDirection,
    SwitchWorkspace,
    Exit,
};

struct ShortcutDefinition {
    ShortcutAction action;
    const std::vector<std::wstring> ShortcutSettings::*member;
    ShortcutHandlerKind handlerKind;
    UINT_PTR command = 0;
    WindowManager::FocusDirection direction = WindowManager::FocusDirection::Left;
    bool grow = true;
    int workspaceNumber = 0;
};

struct LaunchShortcutRegistration {
    ShortcutBinding binding;
    std::wstring command;
};

struct ModifierTokenDefinition {
    std::wstring_view token;
    UINT modifier;
};

struct VirtualKeyTokenDefinition {
    std::wstring_view token;
    UINT virtualKey;
};

struct ChordBindingRegistration {
    ShortcutBinding binding;
    ShortcutAction action;
};

HHOOK g_keyboardHook = nullptr;
AppState* g_keyboardApp = nullptr;
bool g_winLeftHandled = false;
bool g_winRightHandled = false;
bool g_windowsChordHandled = false;
std::unordered_map<int, ShortcutAction> g_hotkeyActions;
std::unordered_map<int, std::wstring> g_hotkeyLaunchCommands;
std::vector<ChordBindingRegistration> g_chordBindings;
std::vector<LaunchShortcutRegistration> g_chordLaunchCommands;
std::unordered_set<UINT> g_activeChordTriggerKeys;
int g_nextHotkeyId = 1;

void HandleShortcutAction(AppState& app, ShortcutAction action, HWND window);
void HandleLaunchCommand(AppState& app, const std::wstring& command);

std::wstring TrimShortcutToken(std::wstring_view token) {
    std::size_t start = 0;
    while (start < token.size() && std::iswspace(token[start]) != 0) {
        ++start;
    }

    std::size_t end = token.size();
    while (end > start && std::iswspace(token[end - 1]) != 0) {
        --end;
    }

    return std::wstring(token.substr(start, end - start));
}

std::wstring Uppercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return value;
}

bool SetShortcutError(std::wstring* errorMessage, const std::wstring& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }

    return false;
}

constexpr std::array<ShortcutDefinition, static_cast<std::size_t>(ShortcutAction::Count)> kShortcutDefinitions = {{
    {ShortcutAction::ToggleTiling, &ShortcutSettings::toggleTiling, ShortcutHandlerKind::PostCommand, CommandToggleTiling},
    {ShortcutAction::ToggleTopBar, &ShortcutSettings::toggleTopBar, ShortcutHandlerKind::PostCommand, CommandToggleTopBar},
    {ShortcutAction::Retile, &ShortcutSettings::retile, ShortcutHandlerKind::Retile},
    {ShortcutAction::FocusLeft, &ShortcutSettings::focusLeft, ShortcutHandlerKind::FocusDirection, 0, WindowManager::FocusDirection::Left},
    {ShortcutAction::FocusUp, &ShortcutSettings::focusUp, ShortcutHandlerKind::FocusDirection, 0, WindowManager::FocusDirection::Up},
    {ShortcutAction::FocusRight, &ShortcutSettings::focusRight, ShortcutHandlerKind::FocusDirection, 0, WindowManager::FocusDirection::Right},
    {ShortcutAction::FocusDown, &ShortcutSettings::focusDown, ShortcutHandlerKind::FocusDirection, 0, WindowManager::FocusDirection::Down},
    {ShortcutAction::MoveLeft, &ShortcutSettings::moveLeft, ShortcutHandlerKind::MoveDirection, 0, WindowManager::FocusDirection::Left},
    {ShortcutAction::MoveUp, &ShortcutSettings::moveUp, ShortcutHandlerKind::MoveDirection, 0, WindowManager::FocusDirection::Up},
    {ShortcutAction::MoveRight, &ShortcutSettings::moveRight, ShortcutHandlerKind::MoveDirection, 0, WindowManager::FocusDirection::Right},
    {ShortcutAction::MoveDown, &ShortcutSettings::moveDown, ShortcutHandlerKind::MoveDirection, 0, WindowManager::FocusDirection::Down},
    {ShortcutAction::GrowLeft, &ShortcutSettings::growLeft, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Left, true},
    {ShortcutAction::GrowUp, &ShortcutSettings::growUp, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Up, true},
    {ShortcutAction::GrowRight, &ShortcutSettings::growRight, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Right, true},
    {ShortcutAction::GrowDown, &ShortcutSettings::growDown, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Down, true},
    {ShortcutAction::ShrinkLeft, &ShortcutSettings::shrinkLeft, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Left, false},
    {ShortcutAction::ShrinkUp, &ShortcutSettings::shrinkUp, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Up, false},
    {ShortcutAction::ShrinkRight, &ShortcutSettings::shrinkRight, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Right, false},
    {ShortcutAction::ShrinkDown, &ShortcutSettings::shrinkDown, ShortcutHandlerKind::ResizeDirection, 0, WindowManager::FocusDirection::Down, false},
    {ShortcutAction::ToggleFloating, &ShortcutSettings::toggleFloating, ShortcutHandlerKind::PostCommand, CommandToggleFocusedWindowFloating},
    {ShortcutAction::LayoutFloating, &ShortcutSettings::layoutFloating, ShortcutHandlerKind::PostCommand, CommandSetLayoutFloating},
    {ShortcutAction::LayoutMainStack, &ShortcutSettings::layoutMainStack, ShortcutHandlerKind::PostCommand, CommandSetLayoutMainStack},
    {ShortcutAction::LayoutVerticalColumns, &ShortcutSettings::layoutVerticalColumns, ShortcutHandlerKind::PostCommand, CommandSetLayoutVerticalColumns},
    {ShortcutAction::LayoutMonocle, &ShortcutSettings::layoutMonocle, ShortcutHandlerKind::PostCommand, CommandSetLayoutMonocle},
    {ShortcutAction::LayoutSpiral, &ShortcutSettings::layoutSpiral, ShortcutHandlerKind::PostCommand, CommandSetLayoutSpiral},
    {ShortcutAction::ShowHelp, &ShortcutSettings::showHelp, ShortcutHandlerKind::PostCommand, CommandShowHelp},
    {ShortcutAction::InspectWindow, &ShortcutSettings::inspectWindow, ShortcutHandlerKind::PostCommand, CommandInspectWindow},
    {ShortcutAction::SwitchWorkspace1, &ShortcutSettings::switchWorkspace1, ShortcutHandlerKind::SwitchWorkspace, 0, WindowManager::FocusDirection::Left, true, 1},
    {ShortcutAction::SwitchWorkspace2, &ShortcutSettings::switchWorkspace2, ShortcutHandlerKind::SwitchWorkspace, 0, WindowManager::FocusDirection::Left, true, 2},
    {ShortcutAction::SwitchWorkspace3, &ShortcutSettings::switchWorkspace3, ShortcutHandlerKind::SwitchWorkspace, 0, WindowManager::FocusDirection::Left, true, 3},
    {ShortcutAction::SwitchWorkspace4, &ShortcutSettings::switchWorkspace4, ShortcutHandlerKind::SwitchWorkspace, 0, WindowManager::FocusDirection::Left, true, 4},
    {ShortcutAction::SwitchWorkspace5, &ShortcutSettings::switchWorkspace5, ShortcutHandlerKind::SwitchWorkspace, 0, WindowManager::FocusDirection::Left, true, 5},
    {ShortcutAction::Exit, &ShortcutSettings::exit, ShortcutHandlerKind::Exit},
}};

constexpr std::array<ModifierTokenDefinition, 6> kModifierTokens = {{
    {L"ALT", MOD_ALT},
    {L"SHIFT", MOD_SHIFT},
    {L"CTRL", MOD_CONTROL},
    {L"CONTROL", MOD_CONTROL},
    {L"WIN", MOD_WIN},
    {L"WINDOWS", MOD_WIN},
}};

constexpr std::array<VirtualKeyTokenDefinition, 9> kVirtualKeyTokens = {{
    {L"LEFT", VK_LEFT},
    {L"RIGHT", VK_RIGHT},
    {L"UP", VK_UP},
    {L"DOWN", VK_DOWN},
    {L"TAB", VK_TAB},
    {L"ESC", VK_ESCAPE},
    {L"ESCAPE", VK_ESCAPE},
    {L"ENTER", VK_RETURN},
    {L"RETURN", VK_RETURN},
}};

const ShortcutDefinition& ShortcutDefinitionForAction(ShortcutAction action) {
    return kShortcutDefinitions[static_cast<std::size_t>(action)];
}

bool TryApplyModifierToken(std::wstring_view token, ShortcutBinding& binding) {
    const auto it = std::find_if(kModifierTokens.begin(), kModifierTokens.end(), [&](const ModifierTokenDefinition& definition) {
        return token == definition.token;
    });
    if (it == kModifierTokens.end()) {
        return false;
    }

    binding.modifiers |= it->modifier;
    return true;
}

bool TryParseFunctionKey(std::wstring_view token, UINT& virtualKey, std::wstring* errorMessage) {
    if (token.size() <= 1 || token[0] != L'F') {
        return false;
    }

    int functionNumber = 0;
    try {
        functionNumber = std::stoi(std::wstring(token.substr(1)));
    } catch (const std::exception&) {
        return SetShortcutError(errorMessage, L"unsupported function key in shortcut");
    }

    if (functionNumber < 1 || functionNumber > 24) {
        return SetShortcutError(errorMessage, L"unsupported function key in shortcut");
    }

    virtualKey = static_cast<UINT>(VK_F1 + functionNumber - 1);
    return true;
}

bool TryParseNonModifierToken(std::wstring_view token, UINT& virtualKey, std::wstring* errorMessage) {
    const auto it = std::find_if(kVirtualKeyTokens.begin(), kVirtualKeyTokens.end(), [&](const VirtualKeyTokenDefinition& definition) {
        return token == definition.token;
    });
    if (it != kVirtualKeyTokens.end()) {
        virtualKey = it->virtualKey;
        return true;
    }

    if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') || (token[0] >= L'0' && token[0] <= L'9'))) {
        virtualKey = static_cast<UINT>(token[0]);
        return true;
    }

    if (TryParseFunctionKey(token, virtualKey, errorMessage)) {
        return true;
    }

    return SetShortcutError(errorMessage, L"unsupported key in shortcut");
}

UINT CurrentModifierState() {
    UINT modifiers = 0;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= MOD_ALT;
    }
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= MOD_SHIFT;
    }
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= MOD_CONTROL;
    }
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

bool TryHandleChordShortcut(UINT virtualKey, bool keyDown, bool keyUp, HWND window) {
    if (g_keyboardApp == nullptr) {
        return false;
    }

    if (keyUp) {
        const auto iterator = g_activeChordTriggerKeys.find(virtualKey);
        if (iterator == g_activeChordTriggerKeys.end()) {
            return false;
        }

        g_activeChordTriggerKeys.erase(iterator);
        return true;
    }

    if (!keyDown) {
        return false;
    }

    if (g_activeChordTriggerKeys.find(virtualKey) != g_activeChordTriggerKeys.end()) {
        return true;
    }

    const UINT currentModifiers = CurrentModifierState();
    for (const ChordBindingRegistration& registration : g_chordBindings) {
        if (registration.binding.virtualKey != virtualKey) {
            continue;
        }

        if (registration.binding.chordVirtualKey == 0) {
            continue;
        }

        if (registration.binding.modifiers != currentModifiers) {
            continue;
        }

        const int chordVirtualKey = static_cast<int>(registration.binding.chordVirtualKey);
        if ((GetAsyncKeyState(chordVirtualKey) & 0x8000) == 0) {
            continue;
        }

        g_activeChordTriggerKeys.insert(virtualKey);
        HandleShortcutAction(*g_keyboardApp, registration.action, window);
        return true;
    }

    for (const LaunchShortcutRegistration& registration : g_chordLaunchCommands) {
        if (registration.binding.virtualKey != virtualKey) {
            continue;
        }

        if (registration.binding.chordVirtualKey == 0) {
            continue;
        }

        if (registration.binding.modifiers != currentModifiers) {
            continue;
        }

        const int chordVirtualKey = static_cast<int>(registration.binding.chordVirtualKey);
        if ((GetAsyncKeyState(chordVirtualKey) & 0x8000) == 0) {
            continue;
        }

        g_activeChordTriggerKeys.insert(virtualKey);
        HandleLaunchCommand(*g_keyboardApp, registration.command);
        return true;
    }

    return false;
}

void HandleLaunchCommand(AppState& app, const std::wstring& command) {
    if (!ShellIntegration::LaunchCommand(command, app.logger)) {
        app.logger.Error(L"Failed to launch shortcut command: " + command);
    }
}

void HandleShortcutAction(AppState& app, ShortcutAction action, HWND window) {
    const ShortcutDefinition& definition = ShortcutDefinitionForAction(action);
    switch (definition.handlerKind) {
    case ShortcutHandlerKind::PostCommand:
        PostMessageW(window, WM_COMMAND, definition.command, 0);
        return;
    case ShortcutHandlerKind::Retile:
        WindowManager::TileActiveMonitor(app);
        return;
    case ShortcutHandlerKind::FocusDirection:
        WindowManager::FocusInDirection(app, definition.direction);
        return;
    case ShortcutHandlerKind::MoveDirection:
        WindowManager::MoveFocusedWindowInDirection(app, definition.direction);
        return;
    case ShortcutHandlerKind::ResizeDirection:
        WindowManager::ResizeFocusedWindowInDirection(app, definition.direction, definition.grow);
        return;
    case ShortcutHandlerKind::SwitchWorkspace:
        WorkspaceManager::SwitchWorkspace(app, definition.workspaceNumber);
        return;
    case ShortcutHandlerKind::Exit:
        DestroyWindow(window);
        return;
    }
}

void SuppressStartMenu() {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CONTROL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

bool IsWindowsMoveChordDown() {
    const bool windowsDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    return windowsDown && !shiftDown && !controlDown;
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || lParam == 0) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const auto* keyboardInfo = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if ((keyboardInfo->flags & LLKHF_INJECTED) != 0) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
    const bool isWindowsKey = keyboardInfo->vkCode == VK_LWIN || keyboardInfo->vkCode == VK_RWIN;
    const bool isLeft = keyboardInfo->vkCode == VK_LEFT;
    const bool isRight = keyboardInfo->vkCode == VK_RIGHT;

    const HWND activeWindow = g_keyboardApp != nullptr ? g_keyboardApp->window : nullptr;
    if (TryHandleChordShortcut(keyboardInfo->vkCode, keyDown, keyUp, activeWindow)) {
        return 1;
    }

    if (isWindowsKey) {
        if (g_windowsChordHandled) {
            if (keyUp) {
                g_windowsChordHandled = false;
                SuppressStartMenu();
            }
            return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
        }

        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (!isLeft && !isRight) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    bool& handled = isLeft ? g_winLeftHandled : g_winRightHandled;
    if (keyUp) {
        const bool shouldConsume = handled;
        handled = false;
        return shouldConsume ? 1 : CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (!keyDown || !IsWindowsMoveChordDown()) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (g_keyboardApp == nullptr) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (handled) {
        return 1;
    }

    handled = true;
    g_windowsChordHandled = true;
    WindowManager::MoveFocusedWindowInDirection(
        *g_keyboardApp,
        isLeft ? WindowManager::FocusDirection::Left : WindowManager::FocusDirection::Right);
    return 1;
}

void InstallKeyboardHook(AppState& app) {
    if (g_keyboardHook != nullptr) {
        return;
    }

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, app.instance, 0);
    if (g_keyboardHook == nullptr) {
        app.logger.ErrorLastWin32(L"Failed to install keyboard hook", GetLastError());
    }
}

void RemoveKeyboardHook() {
    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }

    g_winLeftHandled = false;
    g_winRightHandled = false;
    g_windowsChordHandled = false;
    g_keyboardApp = nullptr;
}

}  // namespace

bool Shortcuts::TryParseBinding(std::wstring_view text, ShortcutBinding& binding, std::wstring* errorMessage) {
    binding = ShortcutBinding{};
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    std::size_t tokenStart = 0;
    int keyCount = 0;
    while (tokenStart <= text.size()) {
        const std::size_t separator = text.find(L'+', tokenStart);
        const std::size_t tokenEnd = separator == std::wstring_view::npos ? text.size() : separator;
        std::wstring token = Uppercase(TrimShortcutToken(text.substr(tokenStart, tokenEnd - tokenStart)));
        if (token.empty()) {
            return SetShortcutError(errorMessage, L"shortcut contains an empty token");
        }

        if (!TryApplyModifierToken(token, binding)) {
            UINT virtualKey = 0;
            if (!TryParseNonModifierToken(token, virtualKey, errorMessage)) {
                return false;
            }

            ++keyCount;
            if (keyCount == 1) {
                binding.virtualKey = virtualKey;
            } else if (keyCount == 2) {
                binding.chordVirtualKey = binding.virtualKey;
                binding.virtualKey = virtualKey;
            } else {
                return SetShortcutError(errorMessage, L"shortcut must contain one or two non-modifier keys");
            }
        }

        if (separator == std::wstring_view::npos) {
            break;
        }

        tokenStart = separator + 1;
    }

    if (keyCount == 0 || binding.virtualKey == 0) {
        return SetShortcutError(errorMessage, L"shortcut must include a key");
    }

    return true;
}

void Shortcuts::RegisterHotkeys(AppState& app, HWND window) {
    g_keyboardApp = &app;
    InstallKeyboardHook(app);

    g_hotkeyActions.clear();
    g_hotkeyLaunchCommands.clear();
    g_chordBindings.clear();
    g_chordLaunchCommands.clear();
    g_activeChordTriggerKeys.clear();
    g_nextHotkeyId = 1;

    for (const auto& definition : kShortcutDefinitions) {
        const auto& bindings = app.settings.shortcuts.*(definition.member);
        for (const auto& bindingText : bindings) {
            ShortcutBinding binding;
            std::wstring errorMessage;
            if (!TryParseBinding(bindingText, binding, &errorMessage)) {
                std::wstring message = L"Invalid shortcut binding '";
                message += bindingText;
                message += L"': ";
                message += errorMessage;
                app.logger.Error(message);
                continue;
            }

            if (binding.chordVirtualKey != 0) {
                g_chordBindings.push_back(ChordBindingRegistration{binding, definition.action});
                continue;
            }

            const int hotkeyId = g_nextHotkeyId++;
            if (!RegisterHotKey(window, hotkeyId, binding.modifiers | MOD_NOREPEAT, binding.virtualKey)) {
                app.logger.ErrorLastWin32(L"RegisterHotKey failed", GetLastError());
                continue;
            }

            g_hotkeyActions[hotkeyId] = definition.action;
        }
    }

    for (const LaunchShortcutSetting& launchShortcut : app.settings.launchShortcuts) {
        if (launchShortcut.shortcut.empty() || launchShortcut.launchCommand.empty()) {
            continue;
        }

        ShortcutBinding binding;
        std::wstring errorMessage;
        if (!TryParseBinding(launchShortcut.shortcut, binding, &errorMessage)) {
            std::wstring message = L"Invalid launch shortcut binding '";
            message += launchShortcut.shortcut;
            message += L"': ";
            message += errorMessage;
            app.logger.Error(message);
            continue;
        }

        if (binding.chordVirtualKey != 0) {
            g_chordLaunchCommands.push_back(LaunchShortcutRegistration{binding, launchShortcut.launchCommand});
            continue;
        }

        const int hotkeyId = g_nextHotkeyId++;
        if (!RegisterHotKey(window, hotkeyId, binding.modifiers | MOD_NOREPEAT, binding.virtualKey)) {
            app.logger.ErrorLastWin32(L"RegisterHotKey failed", GetLastError());
            continue;
        }

        g_hotkeyLaunchCommands.emplace(hotkeyId, launchShortcut.launchCommand);
    }
}

void Shortcuts::UnregisterHotkeys(AppState&, HWND window) {
    RemoveKeyboardHook();

    for (const auto& [id, _] : g_hotkeyActions) {
        UnregisterHotKey(window, id);
    }

    g_hotkeyActions.clear();
    g_hotkeyLaunchCommands.clear();
    g_chordBindings.clear();
    g_chordLaunchCommands.clear();
    g_activeChordTriggerKeys.clear();
    g_nextHotkeyId = 1;
}

bool Shortcuts::HandleHotkey(AppState& app, WPARAM hotkeyId, HWND window) {
    const auto iterator = g_hotkeyActions.find(static_cast<int>(hotkeyId));
    if (iterator == g_hotkeyActions.end()) {
        const auto launchIterator = g_hotkeyLaunchCommands.find(static_cast<int>(hotkeyId));
        if (launchIterator == g_hotkeyLaunchCommands.end()) {
            return false;
        }

        HandleLaunchCommand(app, launchIterator->second);
        return true;
    }

    HandleShortcutAction(app, iterator->second, window);
    return true;
}

}  // namespace quicktile