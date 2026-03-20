#pragma once

#include <windows.h>

#include <memory>

namespace quicktile {

struct AppState;
class Logger;

class TopBar {
public:
    TopBar(HINSTANCE instance, Logger& logger);
    ~TopBar();

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;
    TopBar(TopBar&&) = delete;
    TopBar& operator=(TopBar&&) = delete;

    void SetOwnerWindow(HWND ownerWindow);
    void SetAppState(AppState* appState);
    void SetEnabled(bool enabled);
    void Refresh(const AppState& app);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quicktile