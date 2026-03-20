#include "ui/status_overlay.h"

#include "ui/status_overlay_internal.h"

#include "windows/focus_tracker.h"
#include "platform/logger.h"
#include "windows/window_manager.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windows.h>

#include <array>
#include <limits>
#include <memory>

namespace quicktile {

using namespace status_overlay_internal;

struct StatusOverlay::Impl {
public:
    Impl(HINSTANCE instance, Logger& logger)
        : logger_(logger), instance_(instance) {
        scalePercent_ = 0;
        EnsureCurrentScaleMetrics();
    }

    ~Impl() {
        Shutdown();
    }

    void SetInstance(HINSTANCE instance) {
        instance_ = instance;
    }

    void SetOwnerWindow(HWND ownerWindow) {
        ownerWindow_ = ownerWindow;
        if (window_ != nullptr) {
            SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerWindow_));
            return;
        }

        EnsureWindow();
    }

    bool EnsureWindow() {
        if (window_ != nullptr) {
            return true;
        }
        if (instance_ == nullptr || ownerWindow_ == nullptr) {
            return false;
        }

        EnsureCurrentScaleMetrics();

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &WindowProcThunk;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kStatusOverlayClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        const ATOM classAtom = RegisterClassW(&windowClass);
        if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            logger_.ErrorLastWin32(L"Failed to register status overlay window class", GetLastError());
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            kStatusOverlayClassName,
            kStatusOverlayWindowTitle,
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            metrics_.detailedOverlayWidth,
            metrics_.minimumWindowHeight,
            ownerWindow_,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            logger_.ErrorLastWin32(L"Failed to create status overlay window", GetLastError());
            return false;
        }

        SetLayeredWindowAttributes(window_, 0, kOverlayAlpha, LWA_ALPHA);
        return true;
    }

    void Shutdown() {
        if (window_ != nullptr) {
            KillTimer(window_, kHideTimerId);
            RemoveMouseHook();
            RemoveKeyboardHook();
            DestroyWindow(window_);
            window_ = nullptr;
        }

        title_.clear();
        detail_.clear();
        options_ = OverlayOptions{};
        lastShowTick_ = 0;
    }

    void Show(const std::wstring& title, const std::wstring& detail) {
        OverlayOptions options{};
        options.width = 0;
        ShowDetailed(title, detail, options);
    }

    void ShowDetailed(const std::wstring& title, const std::wstring& detail, const OverlayOptions& options) {
        if (!EnsureWindow()) {
            return;
        }

        title_ = title;
        detail_ = detail;
        options_ = options;
        if (options_.width < 0) {
            options_.width = scalePercent_ > 0 ? MulDiv(metrics_.overlayWidth, 100, scalePercent_) : kBaseMetrics.overlayWidth;
        }
        if (options_.durationMs == 0) {
            options_.durationMs = kVisibleDurationMs;
        }

        ShowOverlayWindow();
    }

    static Impl* FromWindow(HWND hwnd) {
        return reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    static Impl* ActiveInstance() {
        const HWND window = FindWindowW(kStatusOverlayClassName, kStatusOverlayWindowTitle);
        return window != nullptr ? FromWindow(window) : nullptr;
    }

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* self = static_cast<Impl*>(createStruct->lpCreateParams);
            if (self == nullptr) {
                return FALSE;
            }

            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = hwnd;
        }

        Impl* self = FromWindow(hwnd);
        if (self != nullptr) {
            return self->WindowProc(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK LowLevelMouseProcThunk(int code, WPARAM wParam, LPARAM lParam) {
        Impl* self = ActiveInstance();
        if (self == nullptr) {
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        return self->LowLevelMouseProc(code, wParam, lParam);
    }

    static LRESULT CALLBACK LowLevelKeyboardProcThunk(int code, WPARAM wParam, LPARAM lParam) {
        Impl* self = ActiveInstance();
        if (self == nullptr) {
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        return self->LowLevelKeyboardProc(code, wParam, lParam);
    }

private:
    struct OverlayFonts {
        GDIHandle<HFONT> title;
        GDIHandle<HFONT> detail;
        GDIHandle<HFONT> sectionHeading;
        GDIHandle<HFONT> emphasis;
        GDIHandle<HFONT> keycap;

        void Refresh(const Impl& overlay) {
            title.Reset(overlay.CreateOverlayFont(14, FW_SEMIBOLD, L"Segoe UI Variable Display"));
            detail.Reset(overlay.CreateOverlayFont(10, FW_NORMAL, L"Segoe UI Variable Text"));
            sectionHeading.Reset(overlay.CreateOverlayFont(11, FW_BOLD, L"Segoe UI Variable Text"));
            emphasis.Reset(overlay.CreateOverlayFont(9, FW_SEMIBOLD, L"Segoe UI Variable Text"));
            keycap.Reset(overlay.CreateOverlayFont(8, FW_SEMIBOLD, L"Segoe UI Variable Text"));
        }
    };

    static constexpr const wchar_t* kStatusOverlayClassName = L"QuickTileStatusOverlay";
    static constexpr const wchar_t* kStatusOverlayWindowTitle = L"QuickTile Status Overlay";
    static constexpr UINT_PTR kHideTimerId = 1;
    static constexpr UINT kVisibleDurationMs = 1200;
    static constexpr BYTE kOverlayAlpha = 255; // fully opaque to avoid background color bleed-through
    static constexpr ULONGLONG kDismissHookArmDelayMs = 150;
    static constexpr std::array<UINT, 4> kHookDismissMouseMessages = {
        WM_LBUTTONDOWN,
        WM_RBUTTONDOWN,
        WM_MBUTTONDOWN,
        WM_XBUTTONDOWN,
    };
    static constexpr std::array<UINT, 2> kHookDismissKeyboardMessages = {
        WM_KEYDOWN,
        WM_SYSKEYDOWN,
    };
    static constexpr std::array<UINT, 10> kOverlayDismissWindowMessages = {
        WM_LBUTTONDOWN,
        WM_LBUTTONUP,
        WM_RBUTTONDOWN,
        WM_RBUTTONUP,
        WM_MBUTTONDOWN,
        WM_MBUTTONUP,
        WM_NCLBUTTONDOWN,
        WM_NCLBUTTONUP,
        WM_NCRBUTTONDOWN,
        WM_NCRBUTTONUP,
    };

    static COLORREF ThemeAccentColor() {
        DWORD colorization = 0;
        BOOL opaqueBlend = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaqueBlend))) {
            return RGB(
                static_cast<BYTE>((colorization >> 16) & 0xFF),
                static_cast<BYTE>((colorization >> 8) & 0xFF),
                static_cast<BYTE>(colorization & 0xFF));
        }

        return GetSysColor(COLOR_HIGHLIGHT);
    }

    int ScaleMetric(int value) const {
        return MulDiv(value, scalePercent_, 100);
    }

    HFONT TitleFont() const {
        return fonts_.title.Get();
    }

    HFONT DetailFont() const {
        return fonts_.detail.Get();
    }

    HFONT EmphasisFont() const {
        return fonts_.emphasis.Get();
    }

    HFONT KeycapFont() const {
        return fonts_.keycap.Get();
    }

    HFONT DefaultSectionHeadingFont() const {
        return fonts_.sectionHeading.Get();
    }

    HFONT CreateOverlayFont(int points, int weight, const wchar_t* faceName) const {
        return CreateFontW(
            FontHeightForScale(points, scalePercent_),
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
                faceName);
    }

    HMONITOR OverlayMonitor() const {
        HMONITOR monitor = WindowManager::ActiveMonitor();
        if (monitor == nullptr) {
            POINT point{};
            GetCursorPos(&point);
            monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
        }

        return monitor;
    }

    int QueryOverlayScalePercent() const {
        const HMONITOR monitor = OverlayMonitor();
        if (monitor != nullptr) {
            DEVICE_SCALE_FACTOR scaleFactor = SCALE_100_PERCENT;
            if (SUCCEEDED(GetScaleFactorForMonitor(monitor, &scaleFactor))) {
                return static_cast<int>(scaleFactor);
            }
        }

        return 100;
    }

    void EnsureCurrentScaleMetrics() {
        const int scalePercent = QueryOverlayScalePercent();
        if (scalePercent != scalePercent_) {
            scalePercent_ = scalePercent;
            metrics_ = ScaleMetricsForPercent(scalePercent_);
            fonts_.Refresh(*this);
        }
    }

    bool HasDetailText() const {
        return !detail_.empty();
    }

    bool HasCustomLayout() const {
        return !options_.nodes.empty();
    }

    RECT OverlayWorkArea() const {
        RECT workArea{};
        HMONITOR monitor = OverlayMonitor();
        if (monitor == nullptr) {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
            return workArea;
        }

        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        return info.rcWork;
    }

    OverlayPalette CurrentOverlayPalette() const {
        const COLORREF windowColor = GetSysColor(COLOR_WINDOW);
        const COLORREF textColor = GetSysColor(COLOR_WINDOWTEXT);
        const COLORREF borderColor = GetSysColor(COLOR_ACTIVEBORDER);
        const COLORREF accentColor = ThemeAccentColor();
        const COLORREF mutedAccent = BlendColors(accentColor, borderColor, 52);
        const bool darkTheme = IsDarkColor(windowColor);
        const COLORREF darkBase = darkTheme ? ScaleColor(windowColor, kPaletteMix.darkBaseScale) : textColor;
        const COLORREF darkSurface = BlendColors(darkBase, mutedAccent, kPaletteMix.surfaceAccent);
        const COLORREF darkRaisedSurface = BlendColors(darkSurface, mutedAccent, kPaletteMix.raisedSurfaceAccent);
        const COLORREF lightForeground = darkTheme ? textColor : windowColor;
        const COLORREF softForeground = BlendColors(lightForeground, darkSurface, 10);

        OverlayPalette palette;
        palette.background = darkSurface;
        palette.border = BlendColors(darkSurface, borderColor, kPaletteMix.borderBlend);
        palette.innerBorder = BlendColors(darkSurface, lightForeground, kPaletteMix.innerBorderBlend);
        palette.divider = BlendColors(darkRaisedSurface, lightForeground, kPaletteMix.dividerBlend);
        palette.titleText = softForeground;
        palette.headingText = BlendColors(lightForeground, mutedAccent, kPaletteMix.headingAccent);
        palette.labelText = BlendColors(lightForeground, darkSurface, kPaletteMix.labelContrast);
        palette.bodyText = BlendColors(lightForeground, darkSurface, kPaletteMix.bodyContrast);
        palette.emphasisText = BlendColors(lightForeground, mutedAccent, kPaletteMix.emphasisAccent);
        palette.keycapFill = BlendColors(darkRaisedSurface, borderColor, 8);
        palette.keycapBorder = BlendColors(darkRaisedSurface, lightForeground, kPaletteMix.keycapBorderBlend);
        palette.keycapText = softForeground;
        palette.keycapSymbolText = BlendColors(lightForeground, darkSurface, kPaletteMix.keycapSymbolContrast);
        palette.shadow = ScaleColor(darkSurface, kPaletteMix.shadowScale);
        return palette;
    }

    OverlayRenderContext MakeRenderContext(HDC dc, const OverlayPalette& palette) const {
        return OverlayRenderContext{
            dc,
            metrics_,
            palette,
            TitleFont(),
            DetailFont(),
            DefaultSectionHeadingFont(),
            EmphasisFont(),
            KeycapFont()};
    }

    int MeasureLongestLineWidth(HDC dc, HFONT font, const std::wstring& text) const {
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        int width = 0;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = text.find(L'\n', start);
            const std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            RECT rect{0, 0, std::numeric_limits<int>::max() / 4, 0};
            DrawTextW(dc, line.c_str(), -1, &rect, DT_CALCRECT | DT_LEFT | DT_SINGLELINE);
            width = std::max(width, static_cast<int>(rect.right - rect.left));
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }

        SelectObject(dc, oldFont);
        return width;
    }

    int OverlayWidth() {
        EnsureCurrentScaleMetrics();

        int requestedWidth = options_.width;
        if (requestedWidth <= 0) {
            HDC dc = window_ != nullptr ? GetDC(window_) : nullptr;
            if (dc != nullptr) {
                requestedWidth = std::max(
                    metrics_.minimumAutoOverlayWidth,
                    std::max(MeasureLongestLineWidth(dc, TitleFont(), title_), MeasureLongestLineWidth(dc, DetailFont(), detail_)) +
                        (metrics_.horizontalPadding * 2) + metrics_.contentExtraWidth);
                ReleaseDC(window_, dc);
            } else {
                requestedWidth = metrics_.minimumAutoOverlayWidth;
            }
        } else {
            requestedWidth = ScaleMetric(requestedWidth);
        }

        if (requestedWidth <= 0) {
            requestedWidth = metrics_.overlayWidth;
        }

        const RECT workArea = OverlayWorkArea();
        const int workAreaWidth = static_cast<int>(workArea.right - workArea.left);
        const int maximumWidth = std::max(metrics_.overlayWidth, workAreaWidth - metrics_.overlayWindowMargin);
        return std::min(requestedWidth, maximumWidth);
    }

    int SectionSpacing() const {
        return metrics_.sectionSpacing;
    }

    int TitleSpacing() const {
        return options_.titleSpacing > 0 ? ScaleMetric(options_.titleSpacing) : metrics_.textSpacing;
    }

    void FrameRoundedRect(HDC dc, const RECT& rect, COLORREF color) const {
        ScopedPenBrushSelection selection(
            dc,
            CreatePen(PS_SOLID, 1, color),
            static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH)),
            false);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, metrics_.cornerRadius, metrics_.cornerRadius);
    }

    void PaintOverlayBackground(HDC dc, const RECT& client, const OverlayPalette& palette) const {
        GDIHandle<HBRUSH> backgroundBrush(CreateSolidBrush(palette.background));
        FillRect(dc, &client, backgroundBrush.Get());

        RECT innerRect = client;
        InflateRect(&innerRect, -1, -1);
        FrameRoundedRect(dc, client, palette.border);
        FrameRoundedRect(dc, innerRect, palette.innerBorder);
    }

    StackDirection ToInternalDirection(OverlayStackDirection direction) const {
        return direction == OverlayStackDirection::Horizontal ? StackDirection::Horizontal : StackDirection::Vertical;
    }

    RenderNodePtr BuildNode(const OverlayNode& node) const {
        switch (node.kind) {
        case OverlayNode::Kind::Heading:
            return std::make_unique<HeadingNode>(node.text);
        case OverlayNode::Kind::Text:
            return std::make_unique<TextNode>(node.text, TextStyle::Body, node.centered ? TextAlignment::Center : TextAlignment::Left);
        case OverlayNode::Kind::ShortcutList: {
            auto shortcutList = std::make_unique<ShortcutListNode>(node.shortcutItems, options_.renderShortcutBadges);
            shortcutList->SetMetrics(metrics_.rowColumnGap, metrics_.rowSpacing);
            return shortcutList;
        }
        case OverlayNode::Kind::Separator:
            return std::make_unique<SeparatorNode>(node.spacing > 0 ? ScaleMetric(node.spacing) : SectionSpacing());
        case OverlayNode::Kind::Stack: {
            std::vector<RenderNodePtr> children;
            children.reserve(node.children.size());
            for (const OverlayNode& child : node.children) {
                children.push_back(BuildNode(child));
            }
            const int spacing = node.spacing > 0 ? ScaleMetric(node.spacing) : 0;
            const int leadingSpace = node.leadingSpace > 0 ? ScaleMetric(node.leadingSpace) : 0;
            return std::make_unique<StackNode>(ToInternalDirection(node.direction), std::move(children), spacing, leadingSpace, node.drawSeparators);
        }
        }

        return std::make_unique<TextNode>(L"", TextStyle::Body, TextAlignment::Left);
    }

    std::vector<RenderNodePtr> BuildNodes(const std::vector<OverlayNode>& nodes) const {
        std::vector<RenderNodePtr> builtNodes;
        builtNodes.reserve(nodes.size());
        for (const OverlayNode& node : nodes) {
            builtNodes.push_back(BuildNode(node));
        }
        return builtNodes;
    }

    OverlayLayout BuildOverlayLayout(HDC dc, int overlayWidth, const OverlayPalette& palette) const {
        OverlayRenderContext context = MakeRenderContext(dc, palette);
        OverlayLayout layout(title_);

        if (HasCustomLayout()) {
            layout.AddStack(StackDirection::Vertical, BuildNodes(options_.nodes));
        } else if (HasDetailText()) {
            std::vector<RenderNodePtr> nodes;
            nodes.push_back(std::make_unique<TextNode>(
                detail_,
                TextStyle::Body,
                options_.detailCentered ? TextAlignment::Center : TextAlignment::Left));
            layout.AddStack(StackDirection::Vertical, std::move(nodes));
        }

        layout.MeasureLayout(
            context,
            overlayWidth,
            TitleSpacing(),
            metrics_.verticalPadding,
            metrics_.horizontalPadding,
            options_.titleVerticalOffset > 0 ? ScaleMetric(options_.titleVerticalOffset) : 0,
            metrics_.minimumWindowHeight);
        return layout;
    }

    int CalculateOverlayHeight(HWND window) {
        EnsureCurrentScaleMetrics();

        HDC dc = GetDC(window);
        if (dc == nullptr) {
            return metrics_.minimumWindowHeight;
        }

        const OverlayPalette palette = CurrentOverlayPalette();
        const OverlayLayout layout = BuildOverlayLayout(dc, OverlayWidth(), palette);
        ReleaseDC(window, dc);
        return layout.Height();
    }

    RECT OverlayBounds(int width, int height) const {
        const RECT workArea = OverlayWorkArea();

        RECT rect{};
        rect.left = workArea.left + ((workArea.right - workArea.left) - width) / 2;
        rect.top = workArea.top + metrics_.topOffset;
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        return rect;
    }

    void ApplyWindowRegion(HWND window, int width, int height) const {
        GDIHandle<HRGN> region(CreateRoundRectRgn(0, 0, width + 1, height + 1, metrics_.cornerRadius, metrics_.cornerRadius));
        if (region.Get() != nullptr && SetWindowRgn(window, region.Get(), TRUE) != 0) {
            region.Release();
        }
    }

    void RemoveHook(HHOOK& hook) {
        if (hook != nullptr) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
        }
    }

    void RemoveMouseHook() {
        RemoveHook(mouseHook_);
    }

    void RemoveKeyboardHook() {
        RemoveHook(keyboardHook_);
    }

    void EnsureHook(HHOOK& hook, int hookId, HOOKPROC proc, const wchar_t* errorMessage) {
        if (hook != nullptr || instance_ == nullptr) {
            return;
        }

        hook = SetWindowsHookExW(hookId, proc, instance_, 0);
        if (hook == nullptr) {
            logger_.ErrorLastWin32(errorMessage, GetLastError());
        }
    }

    void HideOverlayWindow(HWND window) {
        if (window == nullptr) {
            return;
        }

        RemoveMouseHook();
        RemoveKeyboardHook();
        KillTimer(window, kHideTimerId);
        ShowWindow(window, SW_HIDE);
    }

    void DismissOverlayIfArmed() {
        if (GetTickCount64() - lastShowTick_ >= kDismissHookArmDelayMs) {
            HideOverlayWindow(window_);
        }
    }

    LRESULT LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
        if (code < 0 || lParam == 0) {
            return CallNextHookEx(mouseHook_, code, wParam, lParam);
        }

        if (ContainsMessage(kHookDismissMouseMessages, static_cast<UINT>(wParam))) {
            DismissOverlayIfArmed();
        }

        return CallNextHookEx(mouseHook_, code, wParam, lParam);
    }

    LRESULT LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
        if (code < 0 || lParam == 0) {
            return CallNextHookEx(keyboardHook_, code, wParam, lParam);
        }

        if (ContainsMessage(kHookDismissKeyboardMessages, static_cast<UINT>(wParam))) {
            DismissOverlayIfArmed();
        }

        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }

    void EnsureMouseHook() {
        EnsureHook(mouseHook_, WH_MOUSE_LL, &LowLevelMouseProcThunk, L"Failed to install overlay mouse hook");
    }

    void EnsureKeyboardHook() {
        EnsureHook(keyboardHook_, WH_KEYBOARD_LL, &LowLevelKeyboardProcThunk, L"Failed to install overlay keyboard hook");
    }

    void ShowOverlayWindow() {
        if (window_ == nullptr) {
            return;
        }

        const int height = CalculateOverlayHeight(window_);
        const int width = OverlayWidth();
        const RECT bounds = OverlayBounds(width, height);
        ApplyWindowRegion(window_, width, height);
        SetWindowPos(
            window_,
            HWND_TOPMOST,
            bounds.left,
            bounds.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(window_, nullptr, TRUE);
        UpdateWindow(window_);
        KillTimer(window_, kHideTimerId);
        lastShowTick_ = GetTickCount64();
        EnsureMouseHook();
        EnsureKeyboardHook();
        SetTimer(window_, kHideTimerId, options_.durationMs, nullptr);
    }

    void PaintLayout(HDC dc, const OverlayLayout& layout, const OverlayPalette& palette) const {
        PaintOverlayBackground(dc, layout.ClientRect(), palette);
        SetBkMode(dc, TRANSPARENT);
        OverlayRenderContext context = MakeRenderContext(dc, palette);
        layout.Render(context);
    }

    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (ContainsMessage(kOverlayDismissWindowMessages, message)) {
            HideOverlayWindow(hwnd);
            return 0;
        }

        switch (message) {
        case WM_NCDESTROY:
            if (window_ == hwnd) {
                window_ = nullptr;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_MOUSEACTIVATE:
            HideOverlayWindow(hwnd);
            return MA_NOACTIVATEANDEAT;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wParam == kHideTimerId) {
                HideOverlayWindow(hwnd);
                return 0;
            }
            break;
        case WM_PAINT: {
            EnsureCurrentScaleMetrics();
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            if (dc == nullptr) {
                EndPaint(hwnd, &paint);
                return 0;
            }

            RECT client{};
            GetClientRect(hwnd, &client);

            const OverlayPalette palette = CurrentOverlayPalette();
            const OverlayLayout layout = BuildOverlayLayout(dc, static_cast<int>(client.right - client.left), palette);
            PaintLayout(dc, layout, palette);
            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    Logger& logger_;
    HWND window_ = nullptr;
    HWND ownerWindow_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    std::wstring title_;
    std::wstring detail_;
    OverlayOptions options_{};
    ULONGLONG lastShowTick_ = 0;

    int scalePercent_ = 100;
    OverlayMetrics metrics_{};
    OverlayFonts fonts_{};
};

StatusOverlay::StatusOverlay(HINSTANCE instance, Logger& logger)
    : impl_(std::make_unique<Impl>(instance, logger)) {
}

StatusOverlay::~StatusOverlay() = default;

void StatusOverlay::SetInstance(HINSTANCE instance) {
    impl_->SetInstance(instance);
}

void StatusOverlay::SetOwnerWindow(HWND ownerWindow) {
    impl_->SetOwnerWindow(ownerWindow);
}

void StatusOverlay::Show(const std::wstring& title, const std::wstring& detail) {
    impl_->Show(title, detail);
}

void StatusOverlay::ShowDetailed(const std::wstring& title, const std::wstring& detail, const OverlayOptions& options) {
    impl_->ShowDetailed(title, detail, options);
}

}  // namespace quicktile