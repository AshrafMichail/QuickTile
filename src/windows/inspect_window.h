#pragma once

#include "app/app_state.h"

#include <windows.h>

namespace quicktile {

class InspectWindow {
public:
    static void ShowFocusedWindowOverlay(AppState& app);
};

}  // namespace quicktile