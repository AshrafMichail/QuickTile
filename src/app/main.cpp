#include "app/app_state.h"
#include "windows/event_router.h"
#include "platform/file_watcher.h"
#include "windows/focus_tracker.h"
#include "generated_version.h"
#include "platform/logger.h"
#include "platform/shell_integration.h"
#include "platform/single_instance.h"
#include "config/settings.h"
#include "ui/status_overlay.h"
#include "app/shortcuts.h"
#include "ui/systray.h"
#include "workspace/virtual_desktop.h"
#include "windows/window_manager.h"
#include "workspace/workspace_manager.h"

#include <objbase.h>
#include <memory>

namespace {

constexpr UINT kInitializeTilesMessage = WM_APP + 1;
constexpr UINT kSettingsChangedMessage = WM_APP + 2;

}  // namespace

namespace quicktile {

namespace {

AppState* AppForWindow(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void EnableDpiAwareness(Logger& logger) {
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE) {
        logger.Info(L"Enabled Per-Monitor V2 DPI awareness");
        return;
    }

    const DWORD error = GetLastError();
    if (SetProcessDPIAware() != FALSE) {
        logger.Info(L"Enabled system DPI awareness fallback");
        return;
    }

    if (error != ERROR_ACCESS_DENIED) {
        logger.ErrorLastWin32(L"Failed to enable Per-Monitor V2 DPI awareness", error);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* app = reinterpret_cast<AppState*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return TRUE;
    }

    AppState* app = AppForWindow(hwnd);
    switch (message) {
    case WM_CREATE:
    {
        if (app == nullptr) {
            return -1;
        }

        app->window = hwnd;
        EventRouter::SetCallbackApp(app);
        app->statusOverlay.SetOwnerWindow(hwnd);
        app->topBar.SetOwnerWindow(hwnd);
        app->topBar.SetAppState(app);
        app->topBar.SetEnabled(app->settings.topBarEnabled);
        app->topBar.Refresh(*app);
        Systray::Initialize(*app, hwnd);
        Shortcuts::RegisterHotkeys(*app, hwnd);
        WindowManager::InstallEventHooks(*app);
        app->fileWatcher = std::make_unique<FileWatcher>(
            Settings::FilePath(),
            hwnd,
            kSettingsChangedMessage,
            app->logger);
        PostMessageW(hwnd, kInitializeTilesMessage, 0, 0);
        return 0;
    }
    case kTrayIconMessage: {
        if (app != nullptr && Systray::HandleNotificationMessage(*app, hwnd, lParam)) {
            return 0;
        }
        break;
    }
    case kInitializeTilesMessage:
        if (app != nullptr) {
            WindowManager::TileAllKnownMonitors(*app);
        }
        return 0;
    case kSettingsChangedMessage:
        if (app != nullptr) {
            Systray::ReloadSettings(*app);
        }
        return 0;
    case quicktile::kQueuedWinEventMessage:
        if (app != nullptr) {
            EventRouter::DrainPostedEvents(*app);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (app != nullptr) {
            WindowManager::TileAllKnownMonitors(*app);
        }
        return 0;
    case WM_TIMER:
        if (app != nullptr && FocusTracker::HandleTimer(*app, wParam)) {
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (app != nullptr && Shortcuts::HandleHotkey(*app, wParam, hwnd)) {
            return 0;
        }
        break;
    case WM_COMMAND:
        if (app != nullptr && Systray::HandleCommand(*app, static_cast<UINT>(LOWORD(wParam)), hwnd)) {
            return 0;
        }
        break;
    case WM_DESTROY:
        if (app != nullptr) {
            app->topBar.SetEnabled(false);
            app->fileWatcher.reset();
            WindowManager::RemoveEventHooks(*app);
            Shortcuts::UnregisterHotkeys(*app, hwnd);
            EventRouter::SetCallbackApp(nullptr);
            if (!ShellIntegration::SetWindowArrangingEnabled(true)) {
                app->logger.Error(L"Failed to re-enable Windows snap during shutdown");
            }
        }
        Systray::Shutdown(hwnd);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateMessageWindow(HINSTANCE instance, AppState& app) {
    constexpr wchar_t kClassName[] = L"QuickTileWindowClass";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;

    RegisterClassW(&windowClass);

    return CreateWindowExW(
        0,
        kClassName,
        L"QuickTile",
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance,
        &app);
}

}  // namespace

}  // namespace quicktile

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    quicktile::AppState app(instance);

    quicktile::SingleInstanceGuard singleInstanceGuard;
    switch (singleInstanceGuard.TryAcquire()) {
    case quicktile::SingleInstanceGuard::AcquireResult::Acquired:
        break;
    case quicktile::SingleInstanceGuard::AcquireResult::AlreadyRunning:
        app.logger.Info(L"Another QuickTile instance is already running; aborting startup");
        quicktile::ShowAlreadyRunningWarning();
        return 0;
    case quicktile::SingleInstanceGuard::AcquireResult::Failed:
        app.logger.ErrorLastWin32(L"Failed to create single-instance mutex", singleInstanceGuard.lastError());
        return 1;
    }

    app.logger.Info(std::wstring(L"QuickTile version ") + quicktile::kQuickTileBuildVersion);
    quicktile::EnableDpiAwareness(app.logger);

    const HRESULT comInitializationResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitializeCom = SUCCEEDED(comInitializationResult);
    quicktile::VirtualDesktop::Initialize();

    std::wstring settingsError;
    if (!app.settings.Load(app.logger, &settingsError)) {
        app.logger.Error(L"Failed to load settings at startup: " + settingsError);
    }

    if (!quicktile::Settings::ReadFileContents(app.settings.fileContents)) {
        app.logger.Info(L"settings.yaml is not yet readable; continuing with current settings state");
    }
    app.tilingEnabled = app.settings.tilingEnabled;
    quicktile::WorkspaceManager::RefreshVirtualDesktopContext(app);
    if (!quicktile::ShellIntegration::SetAutoStartEnabled(app.settings.autoStart)) {
        app.logger.Error(L"Failed to apply auto-start setting during startup");
    }
    if (!quicktile::ShellIntegration::SetWindowArrangingEnabled(false)) {
        app.logger.Error(L"Failed to disable Windows snap during startup");
    }

    app.window = quicktile::CreateMessageWindow(instance, app);
    if (app.window == nullptr) {
        if (!quicktile::ShellIntegration::SetWindowArrangingEnabled(true)) {
            app.logger.Error(L"Failed to re-enable Windows snap after startup failure");
        }
        app.logger.Error(L"Failed to create QuickTile message window");
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!quicktile::ShellIntegration::SetWindowArrangingEnabled(true)) {
        app.logger.Error(L"Failed to re-enable Windows snap during process shutdown");
    }
    quicktile::VirtualDesktop::Shutdown();
    if (shouldUninitializeCom) {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}