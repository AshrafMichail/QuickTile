#include "ui/top_bar.h"

#include "app/app_state.h"
#include "layout/layout_types.h"
#include "platform/logger.h"
#include "windows/window_geometry.h"
#include "windows/window_classifier.h"
#include "workspace/workspace_manager.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace quicktile {

struct TopBar::Impl {
public:
    Impl(HINSTANCE instance, Logger& logger)
        : logger_(logger), instance_(instance) {
    }

    ~Impl() {
        Shutdown();
    }

    void SetOwnerWindow(HWND ownerWindow) {
        ownerWindow_ = ownerWindow;
        for (auto& [_, bar] : bars_) {
            if (bar->hwnd != nullptr) {
                SetWindowLongPtrW(bar->hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerWindow_));
                if (bar->appBarRegistered) {
                    UnregisterAppBar(*bar);
                }
                UpdateWindowBounds(*bar);
            }
        }
    }

    void SetAppState(AppState* appState) {
        appState_ = appState;
    }

    void SetEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) {
            HideAll();
            return;
        }

        if (appState_ != nullptr) {
            Refresh(*appState_);
        }
    }

    void Refresh(const AppState& app) {
        appState_ = const_cast<AppState*>(&app);
        if (!enabled_) {
            HideAll();
            return;
        }

        if (!EnsureSharedResources()) {
            return;
        }

        const std::vector<HMONITOR> monitors = EnumerateMonitors();
        if (monitors.empty()) {
            HideAll();
            return;
        }

        for (HMONITOR monitor : monitors) {
            BarWindow* bar = GetOrCreateBar(monitor);
            if (bar == nullptr || bar->hwnd == nullptr) {
                continue;
            }

            if (FontForMonitor(bar->monitor) == nullptr) {
                continue;
            }

            UpdateBarText(*bar, app);
            SetLayeredWindowAttributes(bar->hwnd, 0, BarAlpha(), LWA_ALPHA);
            UpdateWindowBounds(*bar);
            InvalidateRect(bar->hwnd, nullptr, TRUE);
            UpdateWindow(bar->hwnd);
            SetTimer(bar->hwnd, kClockTimerId, kClockRefreshIntervalMs, nullptr);
        }

        DestroyBarsNotIn(monitors);
    }

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* bar = static_cast<BarWindow*>(createStruct->lpCreateParams);
            if (bar == nullptr) {
                return FALSE;
            }

            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(bar));
            bar->hwnd = hwnd;
        }

        auto* bar = reinterpret_cast<BarWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return bar != nullptr && bar->owner != nullptr ? bar->owner->WindowProc(*bar, hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

private:
    struct BarWindow {
        struct TextRun {
            enum class Kind {
                Text,
                LayoutIndicator,
            };

            static TextRun MakeText(std::wstring value, COLORREF valueColor = RGB(236, 240, 244)) {
                TextRun run;
                run.kind = Kind::Text;
                run.text = std::move(value);
                run.color = valueColor;
                return run;
            }

            static TextRun MakeLayoutIndicator(LayoutMode modeValue, int width, COLORREF valueColor = RGB(236, 240, 244)) {
                TextRun run;
                run.kind = Kind::LayoutIndicator;
                run.color = valueColor;
                run.layoutMode = modeValue;
                run.advanceWidth = width;
                return run;
            }

            Kind kind = Kind::Text;
            std::wstring text;
            COLORREF color = RGB(236, 240, 244);
            LayoutMode layoutMode = DefaultLayoutMode();
            int advanceWidth = 0;
        };

        Impl* owner = nullptr;
        HMONITOR monitor = nullptr;
        HWND hwnd = nullptr;
        bool appBarRegistered = false;
        std::vector<TextRun> leftRuns;
        std::vector<TextRun> centerRuns;
        std::vector<TextRun> rightRuns;
    };

    static constexpr const wchar_t* kTopBarClassName = L"QuickTileTopBar";
    static constexpr const wchar_t* kTopBarWindowTitle = L"QuickTile Top Bar";
    static constexpr const wchar_t* kTopBarAppBarMessageName = L"QuickTileTopBarAppBarMessage";
    static constexpr UINT_PTR kClockTimerId = 1;
    static constexpr UINT kClockRefreshIntervalMs = 1000;
    static constexpr int kHorizontalPadding = 12;
    static constexpr COLORREF kTextColor = RGB(236, 240, 244);
    static constexpr COLORREF kDimTextColor = RGB(118, 120, 122);

    bool EnsureSharedResources() {
        if (instance_ == nullptr || ownerWindow_ == nullptr) {
            return false;
        }

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &WindowProcThunk;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kTopBarClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        const ATOM classAtom = RegisterClassW(&windowClass);
        if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            logger_.ErrorLastWin32(L"Failed to register top bar window class", GetLastError());
            return false;
        }

        if (appBarMessage_ == 0) {
            appBarMessage_ = RegisterWindowMessageW(kTopBarAppBarMessageName);
        }

        return true;
    }

    void Shutdown() {
        DestroyAllBars();
        for (auto& [_, font] : fontsByHeight_) {
            if (font != nullptr) {
                DeleteObject(font);
            }
        }
        fontsByHeight_.clear();
    }

    void HideAll() {
        for (auto& [_, bar] : bars_) {
            if (bar->hwnd != nullptr) {
                KillTimer(bar->hwnd, kClockTimerId);
                UnregisterAppBar(*bar);
                ShowWindow(bar->hwnd, SW_HIDE);
            }
        }
    }

    static int DefaultBarHeight() {
        return Settings::Defaults().topBarHeight;
    }

    static float DefaultBarOpacity() {
        return Settings::Defaults().topBarOpacity;
    }

    int BarHeightForMonitor(HMONITOR monitor) const {
        const int configuredHeight = appState_ != nullptr ? appState_->settings.topBarHeight : DefaultBarHeight();
        return WindowGeometry::ScalePixelsForMonitor(monitor, std::clamp(configuredHeight, 1, 256));
    }

    float BarOpacity() const {
        const float configuredOpacity = appState_ != nullptr ? appState_->settings.topBarOpacity : DefaultBarOpacity();
        return std::clamp(configuredOpacity, 0.1f, 1.0f);
    }

    BYTE BarAlpha() const {
        return static_cast<BYTE>(std::lround(BarOpacity() * 255.0f));
    }

    int FontHeightForMonitor(HMONITOR monitor) const {
        return std::max(1, BarHeightForMonitor(monitor) / 2);
    }

    HFONT FontForMonitor(HMONITOR monitor) const {
        const int fontHeight = FontHeightForMonitor(monitor);
        const auto iterator = fontsByHeight_.find(fontHeight);
        if (iterator != fontsByHeight_.end()) {
            return iterator->second;
        }

        HFONT font = CreateFontW(
            -fontHeight,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (font == nullptr) {
            logger_.ErrorLastWin32(L"Failed to create top bar font", GetLastError());
            return nullptr;
        }

        fontsByHeight_.emplace(fontHeight, font);
        return font;
    }

    bool EnsureAppBarRegistered(BarWindow& bar) {
        if (bar.hwnd == nullptr) {
            return false;
        }

        if (bar.appBarRegistered) {
            return true;
        }

        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = bar.hwnd;
        data.uCallbackMessage = appBarMessage_;
        if (SHAppBarMessage(ABM_NEW, &data) == FALSE) {
            logger_.Error(L"Failed to register top bar as an appbar");
            return false;
        }

        bar.appBarRegistered = true;
        return true;
    }

    void UnregisterAppBar(BarWindow& bar) {
        if (!bar.appBarRegistered || bar.hwnd == nullptr) {
            return;
        }

        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = bar.hwnd;
        SHAppBarMessage(ABM_REMOVE, &data);
        bar.appBarRegistered = false;
    }

    std::vector<HMONITOR> EnumerateMonitors() const {
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(
            nullptr,
            nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
                auto* result = reinterpret_cast<std::vector<HMONITOR>*>(lParam);
                result->push_back(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        return monitors;
    }

    BarWindow* GetOrCreateBar(HMONITOR monitor) {
        const auto iterator = bars_.find(monitor);
        if (iterator != bars_.end()) {
            return iterator->second.get();
        }

        auto bar = std::make_unique<BarWindow>();
        bar->owner = this;
        bar->monitor = monitor;
        bar->hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            kTopBarClassName,
            kTopBarWindowTitle,
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            ownerWindow_,
            nullptr,
            instance_,
            bar.get());
        if (bar->hwnd == nullptr) {
            logger_.ErrorLastWin32(L"Failed to create top bar window", GetLastError());
            return nullptr;
        }

        SetLayeredWindowAttributes(bar->hwnd, 0, BarAlpha(), LWA_ALPHA);

        BarWindow* rawBar = bar.get();
        bars_.emplace(monitor, std::move(bar));
        return rawBar;
    }

    void DestroyBarsNotIn(const std::vector<HMONITOR>& monitors) {
        std::vector<HMONITOR> staleMonitors;
        staleMonitors.reserve(bars_.size());
        for (const auto& [monitor, _] : bars_) {
            if (std::find(monitors.begin(), monitors.end(), monitor) == monitors.end()) {
                staleMonitors.push_back(monitor);
            }
        }

        for (HMONITOR monitor : staleMonitors) {
            auto iterator = bars_.find(monitor);
            if (iterator == bars_.end()) {
                continue;
            }

            if (iterator->second->hwnd != nullptr) {
                KillTimer(iterator->second->hwnd, kClockTimerId);
                UnregisterAppBar(*iterator->second);
                DestroyWindow(iterator->second->hwnd);
                iterator->second->hwnd = nullptr;
            }
            bars_.erase(iterator);
        }
    }

    void DestroyAllBars() {
        for (auto& [_, bar] : bars_) {
            if (bar->hwnd != nullptr) {
                KillTimer(bar->hwnd, kClockTimerId);
                UnregisterAppBar(*bar);
                DestroyWindow(bar->hwnd);
                bar->hwnd = nullptr;
            }
        }
        bars_.clear();
    }

    static LayoutMode LayoutModeForMonitor(const AppState& app, HMONITOR monitor) {
        const auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
        const auto iterator = workspace.find(monitor);
        if (iterator != workspace.end()) {
            return iterator->second.layoutMode;
        }

        return app.settings.defaultLayoutMode;
    }

    static int CurrentWorkspaceIndex(const AppState& app) {
        return WorkspaceManager::CurrentWorkspaceIndex(app);
    }

    int LayoutIndicatorWidth(HMONITOR monitor) const {
        const int barHeight = BarHeightForMonitor(monitor);
        return std::max(10, (barHeight * 4) / 5);
    }

    static void AppendSeparator(std::vector<BarWindow::TextRun>& region) {
        if (!region.empty()) {
            region.push_back(BarWindow::TextRun::MakeText(L"  ", kTextColor));
        }
    }

    static void AppendRegionText(std::vector<BarWindow::TextRun>& region, const std::wstring& text, COLORREF color = kTextColor) {
        if (text.empty()) {
            return;
        }

        AppendSeparator(region);
        region.push_back(BarWindow::TextRun::MakeText(text, color));
    }

    static void AppendRegionLayoutIndicator(std::vector<BarWindow::TextRun>& region, LayoutMode layoutMode, int width, COLORREF color = kTextColor) {
        AppendSeparator(region);
        region.push_back(BarWindow::TextRun::MakeLayoutIndicator(layoutMode, width, color));
    }

    static void AppendWorkspaceStrip(std::vector<BarWindow::TextRun>& region, int currentWorkspaceIndex) {
        AppendSeparator(region);
        for (int workspaceIndex = 0; workspaceIndex < kWorkspaceCount; ++workspaceIndex) {
            if (workspaceIndex != 0) {
                region.push_back(BarWindow::TextRun::MakeText(L" ", kDimTextColor));
            }

            const COLORREF color = workspaceIndex == currentWorkspaceIndex ? kTextColor : kDimTextColor;
            region.push_back(BarWindow::TextRun::MakeText(std::to_wstring(workspaceIndex + 1), color));
        }
    }

    static void AppendForPosition(BarWindow& bar, TopBarWidgetPosition position, const std::wstring& text, COLORREF color = kTextColor) {
        switch (position) {
        case TopBarWidgetPosition::Disabled:
            return;
        case TopBarWidgetPosition::Left:
            AppendRegionText(bar.leftRuns, text, color);
            return;
        case TopBarWidgetPosition::Center:
            AppendRegionText(bar.centerRuns, text, color);
            return;
        case TopBarWidgetPosition::Right:
            AppendRegionText(bar.rightRuns, text, color);
            return;
        }
    }

    void AppendLayoutIndicatorForPosition(BarWindow& bar, TopBarWidgetPosition position, LayoutMode layoutMode, COLORREF color = kTextColor) const {
        switch (position) {
        case TopBarWidgetPosition::Disabled:
            return;
        case TopBarWidgetPosition::Left:
            AppendRegionLayoutIndicator(bar.leftRuns, layoutMode, LayoutIndicatorWidth(bar.monitor), color);
            return;
        case TopBarWidgetPosition::Center:
            AppendRegionLayoutIndicator(bar.centerRuns, layoutMode, LayoutIndicatorWidth(bar.monitor), color);
            return;
        case TopBarWidgetPosition::Right:
            AppendRegionLayoutIndicator(bar.rightRuns, layoutMode, LayoutIndicatorWidth(bar.monitor), color);
            return;
        }
    }

    static void AppendWorkspacesForPosition(BarWindow& bar, TopBarWidgetPosition position, int currentWorkspaceIndex) {
        switch (position) {
        case TopBarWidgetPosition::Disabled:
            return;
        case TopBarWidgetPosition::Left:
            AppendWorkspaceStrip(bar.leftRuns, currentWorkspaceIndex);
            return;
        case TopBarWidgetPosition::Center:
            AppendWorkspaceStrip(bar.centerRuns, currentWorkspaceIndex);
            return;
        case TopBarWidgetPosition::Right:
            AppendWorkspaceStrip(bar.rightRuns, currentWorkspaceIndex);
            return;
        }
    }

    static std::wstring FocusedAppLabel() {
        const HWND foreground = GetForegroundWindow();
        if (foreground == nullptr || !IsWindow(foreground)) {
            return L"QuickTile";
        }

        const std::wstring processName = WindowClassifier::ProcessNameForWindow(foreground);
        if (processName.empty()) {
            return L"QuickTile";
        }

        const std::filesystem::path processPath(processName);
        const std::wstring stem = processPath.stem().wstring();
        return stem.empty() ? processName : stem;
    }

    void AppendWidgetByKind(BarWindow& bar, TopBarWidgetKind kind, const AppState& app, const std::wstring& timeText, const std::wstring& dateText) const {
        switch (kind) {
        case TopBarWidgetKind::Clock:
            AppendForPosition(bar, app.settings.topBarWidgets.clock, timeText);
            return;
        case TopBarWidgetKind::Date:
            AppendForPosition(bar, app.settings.topBarWidgets.date, dateText);
            return;
        case TopBarWidgetKind::AppName:
            AppendForPosition(bar, app.settings.topBarWidgets.appName, FocusedAppLabel());
            return;
        case TopBarWidgetKind::LayoutType:
            AppendLayoutIndicatorForPosition(bar, app.settings.topBarWidgets.layoutType, LayoutModeForMonitor(app, bar.monitor));
            return;
        case TopBarWidgetKind::Workspaces:
            AppendWorkspacesForPosition(bar, app.settings.topBarWidgets.workspaces, CurrentWorkspaceIndex(app));
            return;
        }
    }

    void UpdateBarText(BarWindow& bar, const AppState& app) const {
        bar.leftRuns.clear();
        bar.centerRuns.clear();
        bar.rightRuns.clear();

        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);

        wchar_t timeBuffer[64] = {};
        wchar_t dateBuffer[64] = {};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &localTime, nullptr, timeBuffer, static_cast<int>(std::size(timeBuffer)));
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &localTime, nullptr, dateBuffer, static_cast<int>(std::size(dateBuffer)), nullptr);

        const std::wstring timeText(timeBuffer);
        const std::wstring dateText(dateBuffer);
        for (TopBarWidgetKind kind : app.settings.topBarWidgets.order) {
            AppendWidgetByKind(bar, kind, app, timeText, dateText);
        }
    }

    void UpdateWindowBounds(BarWindow& bar) {
        if (bar.hwnd == nullptr) {
            return;
        }

        if (!EnsureAppBarRegistered(bar)) {
            return;
        }

        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = bar.hwnd;
        data.uEdge = ABE_TOP;
        const int barHeight = BarHeightForMonitor(bar.monitor);
        data.rc = WindowGeometry::MonitorRectForMonitor(bar.monitor);
        data.rc.bottom = data.rc.top + barHeight;
        SHAppBarMessage(ABM_QUERYPOS, &data);
        data.rc.bottom = data.rc.top + barHeight;
        SHAppBarMessage(ABM_SETPOS, &data);

        const int width = std::max(1, static_cast<int>(data.rc.right - data.rc.left));
        SetWindowPos(
            bar.hwnd,
            HWND_TOPMOST,
            data.rc.left,
            data.rc.top,
            width,
                barHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    static int MeasureTextWidth(HDC dc, const std::wstring& text) {
        if (text.empty()) {
            return 0;
        }

        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        return size.cx;
    }

    static int MeasureRunWidth(HDC dc, const BarWindow::TextRun& run) {
        if (run.kind == BarWindow::TextRun::Kind::LayoutIndicator) {
            return run.advanceWidth;
        }

        return MeasureTextWidth(dc, run.text);
    }

    static int MeasureRunsWidth(HDC dc, const std::vector<BarWindow::TextRun>& runs) {
        int width = 0;
        for (const BarWindow::TextRun& run : runs) {
            width += MeasureRunWidth(dc, run);
        }
        return width;
    }

    static RECT CenteredIndicatorRect(const RECT& rect, int advanceWidth) {
        const int availableHeight = std::max(6, static_cast<int>(rect.bottom - rect.top) - 8);
        const int width = std::max(8, std::min(advanceWidth, availableHeight + std::max(2, availableHeight / 3)));
        const int height = std::max(6, availableHeight);
        const int left = rect.left + std::max(0, (advanceWidth - width) / 2);
        const int top = rect.top + std::max(0, (static_cast<int>(rect.bottom - rect.top) - height) / 2);
        return RECT{left, top, left + width, top + height};
    }

    static void DrawRectOutline(HDC dc, const RECT& rect) {
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    }

    static void DrawLine(HDC dc, int x1, int y1, int x2, int y2) {
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
    }

    static void DrawLayoutIndicator(HDC dc, const RECT& rect, const BarWindow::TextRun& run) {
        const RECT iconRect = CenteredIndicatorRect(rect, run.advanceWidth);
        const int width = std::max(1, static_cast<int>(iconRect.right - iconRect.left));
        const int height = std::max(1, static_cast<int>(iconRect.bottom - iconRect.top));

        const HPEN pen = CreatePen(PS_SOLID, 1, run.color);
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));

        switch (run.layoutMode) {
        case LayoutMode::MainStack: {
            DrawRectOutline(dc, iconRect);
            const int splitX = iconRect.left + (width * 3) / 5;
            const int stackSplitY = iconRect.top + height / 2;
            DrawLine(dc, splitX, iconRect.top, splitX, iconRect.bottom);
            DrawLine(dc, splitX, stackSplitY, iconRect.right, stackSplitY);
            break;
        }
        case LayoutMode::VerticalColumns: {
            DrawRectOutline(dc, iconRect);
            const int splitOneX = iconRect.left + width / 3;
            const int splitTwoX = iconRect.left + (width * 2) / 3;
            DrawLine(dc, splitOneX, iconRect.top, splitOneX, iconRect.bottom);
            DrawLine(dc, splitTwoX, iconRect.top, splitTwoX, iconRect.bottom);
            break;
        }
        case LayoutMode::Monocle: {
            DrawRectOutline(dc, iconRect);
            const RECT innerRect{
                iconRect.left + 2,
                iconRect.top + 2,
                static_cast<LONG>(std::max(static_cast<int>(iconRect.left) + 3, static_cast<int>(iconRect.right) - 2)),
                static_cast<LONG>(std::max(static_cast<int>(iconRect.top) + 3, static_cast<int>(iconRect.bottom) - 2)),
            };
            if (innerRect.right - innerRect.left >= 3 && innerRect.bottom - innerRect.top >= 3) {
                DrawRectOutline(dc, innerRect);
            }
            break;
        }
        case LayoutMode::Floating: {
            const RECT backRect{
                iconRect.left,
                iconRect.top,
                static_cast<LONG>(std::max(static_cast<int>(iconRect.left) + 4, static_cast<int>(iconRect.right) - 4)),
                static_cast<LONG>(std::max(static_cast<int>(iconRect.top) + 4, static_cast<int>(iconRect.bottom) - 3)),
            };
            const RECT frontRect{
                std::min(iconRect.right - 3, iconRect.left + 4),
                std::min(iconRect.bottom - 3, iconRect.top + 3),
                iconRect.right,
                iconRect.bottom,
            };
            DrawRectOutline(dc, backRect);
            DrawRectOutline(dc, frontRect);
            break;
        }
        case LayoutMode::Spiral: {
            DrawRectOutline(dc, iconRect);
            const int splitOuterX = iconRect.left + width / 2;
            const int splitOuterY = iconRect.top + height / 2;
            const int splitInnerX = splitOuterX + (static_cast<int>(iconRect.right) - splitOuterX) / 2;
            DrawLine(dc, splitOuterX, iconRect.top, splitOuterX, iconRect.bottom);
            DrawLine(dc, splitOuterX, splitOuterY, iconRect.right, splitOuterY);
            DrawLine(dc, splitInnerX, splitOuterY, splitInnerX, iconRect.bottom);
            break;
        }
        }

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    static void DrawRuns(HDC dc, const RECT& rect, const std::vector<BarWindow::TextRun>& runs, UINT alignment) {
        if (runs.empty()) {
            return;
        }

        const int totalWidth = MeasureRunsWidth(dc, runs);
        TEXTMETRICW textMetrics{};
        GetTextMetricsW(dc, &textMetrics);

        int x = rect.left;
        if (alignment == DT_CENTER) {
            const int rectWidth = static_cast<int>(rect.right - rect.left);
            x = static_cast<int>(rect.left) + std::max(0, (rectWidth - totalWidth) / 2);
        } else if (alignment == DT_RIGHT) {
            x = static_cast<int>(rect.right) - totalWidth;
        }

        const int rectHeight = static_cast<int>(rect.bottom - rect.top);
        const int textHeight = static_cast<int>(textMetrics.tmHeight);
        const int y = static_cast<int>(rect.top) + std::max(0, (rectHeight - textHeight) / 2);
        for (const BarWindow::TextRun& run : runs) {
            if (run.kind == BarWindow::TextRun::Kind::LayoutIndicator) {
                DrawLayoutIndicator(dc, RECT{x, rect.top, x + run.advanceWidth, rect.bottom}, run);
                x += run.advanceWidth;
                if (x >= rect.right) {
                    break;
                }
                continue;
            }

            if (run.text.empty()) {
                continue;
            }

            SetTextColor(dc, run.color);
            ExtTextOutW(dc, x, y, ETO_CLIPPED, &rect, run.text.c_str(), static_cast<UINT>(run.text.size()), nullptr);
            x += MeasureRunWidth(dc, run);
            if (x >= rect.right) {
                break;
            }
        }
    }

    void Paint(const BarWindow& bar, HDC dc) const {
        RECT client{};
        GetClientRect(bar.hwnd, &client);

        const HBRUSH backgroundBrush = CreateSolidBrush(RGB(22, 26, 31));
        FillRect(dc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);

        const HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(54, 61, 72));
        const HGDIOBJ oldPen = SelectObject(dc, borderPen);
        MoveToEx(dc, client.left, client.bottom - 1, nullptr);
        LineTo(dc, client.right, client.bottom - 1);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);

        const HFONT font = FontForMonitor(bar.monitor);
        const HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);

        RECT leftRect = client;
        leftRect.left += kHorizontalPadding;
        leftRect.right -= kHorizontalPadding;
        DrawRuns(dc, leftRect, bar.leftRuns, DT_LEFT);

        RECT centerRect = client;
        centerRect.left += kHorizontalPadding;
        centerRect.right -= kHorizontalPadding;
        DrawRuns(dc, centerRect, bar.centerRuns, DT_CENTER);

        RECT rightRect = client;
        rightRect.left += kHorizontalPadding;
        rightRect.right -= kHorizontalPadding;
        DrawRuns(dc, rightRect, bar.rightRuns, DT_RIGHT);

        if (oldFont != nullptr) {
            SelectObject(dc, oldFont);
        }
    }

    LRESULT WindowProc(BarWindow& bar, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == appBarMessage_) {
            if (wParam == ABN_POSCHANGED) {
                UpdateWindowBounds(bar);
            }
            return 0;
        }

        switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wParam == kClockTimerId) {
                if (appState_ != nullptr) {
                    UpdateBarText(bar, *appState_);
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            Paint(bar, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_NCDESTROY:
            bar.hwnd = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    Logger& logger_;
    HINSTANCE instance_ = nullptr;
    HWND ownerWindow_ = nullptr;
    AppState* appState_ = nullptr;
    UINT appBarMessage_ = 0;
    bool enabled_ = false;
    mutable std::unordered_map<int, HFONT> fontsByHeight_;
    std::unordered_map<HMONITOR, std::unique_ptr<BarWindow>> bars_;
};

TopBar::TopBar(HINSTANCE instance, Logger& logger)
    : impl_(std::make_unique<Impl>(instance, logger)) {
}

TopBar::~TopBar() = default;

void TopBar::SetOwnerWindow(HWND ownerWindow) {
    impl_->SetOwnerWindow(ownerWindow);
}

void TopBar::SetAppState(AppState* appState) {
    impl_->SetAppState(appState);
}

void TopBar::SetEnabled(bool enabled) {
    impl_->SetEnabled(enabled);
}

void TopBar::Refresh(const AppState& app) {
    impl_->Refresh(app);
}

}  // namespace quicktile