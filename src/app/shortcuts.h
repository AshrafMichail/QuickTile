#pragma once

#include "app/app_state.h"

#include "config/config.h"

#include <string>
#include <string_view>

namespace quicktile {

struct ShortcutBinding {
    UINT modifiers = 0;
	UINT chordVirtualKey = 0;
    UINT virtualKey = 0;
};

class Shortcuts {
public:
	static bool TryParseBinding(std::wstring_view text, ShortcutBinding& binding, std::wstring* errorMessage = nullptr);
	static void RegisterHotkeys(AppState& app, HWND window);
	static void UnregisterHotkeys(AppState& app, HWND window);
	static bool HandleHotkey(AppState& app, WPARAM hotkeyId, HWND window);
};

}  // namespace quicktile