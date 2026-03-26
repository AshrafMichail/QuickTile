#include "ui/drop_preview_overlay.h"

#include "config/config.h"
#include "platform/logger.h"

#include <windows.h>

namespace quicktile {

struct DropPreviewOverlay::Impl {
public:
    Impl(HINSTANCE instance, Logger& logger)
        : instance_(instance), logger_(logger) {
        SetRectEmpty(&bounds_);
    }

    ~Impl() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
    }

    void SetOwnerWindow(HWND ownerWindow) {
        ownerWindow_ = ownerWindow;
        if (window_ != nullptr) {
            SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerWindow_));
        }
    }

    bool IsShowingBounds(const RECT& bounds) const {
        return visible_ && EqualRect(&bounds_, &bounds) != FALSE;
    }

    void ShowReplacement(const RECT& bounds) {
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
            Hide();
            return;
        }

        if (!EnsureWindow()) {
            return;
        }

        if (IsShowingBounds(bounds)) {
            return;
        }

        bounds_ = bounds;
        const int width = std::max(1, static_cast<int>(bounds.right - bounds.left));
        const int height = std::max(1, static_cast<int>(bounds.bottom - bounds.top));

        SetWindowPos(
            window_,
            HWND_TOPMOST,
            bounds.left,
            bounds.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible_ = true;
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

    void Hide() {
        if (!visible_ && IsRectEmpty(&bounds_) != FALSE) {
            return;
        }

        SetRectEmpty(&bounds_);
        visible_ = false;
        if (window_ != nullptr) {
            ShowWindow(window_, SW_HIDE);
        }
    }

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* overlay = static_cast<Impl*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
            overlay->window_ = hwnd;
            return TRUE;
        }

        auto* overlay = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return overlay != nullptr ? overlay->WindowProc(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

private:
    static constexpr const wchar_t* kWindowClassName = L"QuickTileDropPreviewOverlay";
    static constexpr COLORREF kTransparentKey = RGB(255, 0, 255);

    bool EnsureWindow() {
        if (instance_ == nullptr || ownerWindow_ == nullptr) {
            return false;
        }

        if (window_ != nullptr) {
            return true;
        }

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &WindowProcThunk;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        const ATOM classAtom = RegisterClassW(&windowClass);
        if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            logger_.ErrorLastWin32(L"Failed to register drop preview window class", GetLastError());
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            kWindowClassName,
            L"QuickTile Drop Preview",
            WS_POPUP,
            0,
            0,
            0,
            0,
            ownerWindow_,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            logger_.ErrorLastWin32(L"Failed to create drop preview window", GetLastError());
            return false;
        }

        SetLayeredWindowAttributes(window_, kTransparentKey, 255, LWA_COLORKEY);
        return true;
    }

    void Paint(HWND hwnd) const {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);

        RECT client{};
        GetClientRect(hwnd, &client);

        const HBRUSH transparentBrush = CreateSolidBrush(kTransparentKey);
        FillRect(dc, &client, transparentBrush);
        DeleteObject(transparentBrush);

        const int thickness = std::max(1, kDropPreviewBorderThickness);
        const HBRUSH borderBrush = CreateSolidBrush(kDropPreviewBorderColor);

        RECT top = client;
        top.bottom = std::min(client.bottom, client.top + thickness);
        FillRect(dc, &top, borderBrush);

        RECT bottom = client;
        bottom.top = std::max(client.top, client.bottom - thickness);
        FillRect(dc, &bottom, borderBrush);

        RECT left = client;
        left.right = std::min(client.right, client.left + thickness);
        left.top += thickness;
        left.bottom -= thickness;
        FillRect(dc, &left, borderBrush);

        RECT right = client;
        right.left = std::max(client.left, client.right - thickness);
        right.top += thickness;
        right.bottom -= thickness;
        FillRect(dc, &right, borderBrush);

        DeleteObject(borderBrush);
        EndPaint(hwnd, &paint);
    }

    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    HINSTANCE instance_ = nullptr;
    Logger& logger_;
    HWND ownerWindow_ = nullptr;
    HWND window_ = nullptr;
    RECT bounds_{};
    bool visible_ = false;
};

DropPreviewOverlay::DropPreviewOverlay(HINSTANCE instance, Logger& logger)
    : impl_(std::make_unique<Impl>(instance, logger)) {
}

DropPreviewOverlay::~DropPreviewOverlay() = default;

void DropPreviewOverlay::SetOwnerWindow(HWND ownerWindow) {
    impl_->SetOwnerWindow(ownerWindow);
}

void DropPreviewOverlay::ShowReplacement(const RECT& bounds) {
    impl_->ShowReplacement(bounds);
}

void DropPreviewOverlay::Hide() {
    impl_->Hide();
}

bool DropPreviewOverlay::IsShowingBounds(const RECT& bounds) const {
    return impl_->IsShowingBounds(bounds);
}

}  // namespace quicktile