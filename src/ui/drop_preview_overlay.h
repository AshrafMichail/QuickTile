#pragma once

#include <windows.h>

#include <memory>

namespace quicktile {

class Logger;

class DropPreviewOverlay {
public:
    DropPreviewOverlay(HINSTANCE instance, Logger& logger);
    ~DropPreviewOverlay();

    DropPreviewOverlay(const DropPreviewOverlay&) = delete;
    DropPreviewOverlay& operator=(const DropPreviewOverlay&) = delete;
    DropPreviewOverlay(DropPreviewOverlay&&) = delete;
    DropPreviewOverlay& operator=(DropPreviewOverlay&&) = delete;

    void SetOwnerWindow(HWND ownerWindow);
    void ShowReplacement(const RECT& bounds);
    void Hide();
    bool IsShowingBounds(const RECT& bounds) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quicktile