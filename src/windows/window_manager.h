#pragma once

#include "app/app_state.h"

namespace quicktile {

struct FloatingToggleResult {
	bool handled = false;
	bool enabled = false;
	std::wstring windowTitle;
};

struct MonitorTilingToggleResult {
	bool handled = false;
	bool enabled = false;
};

struct LayoutChangeResult {
	bool handled = false;
	LayoutMode layoutMode = DefaultLayoutMode();
};

enum class ManagedWindowDecisionReason {
	Managed,
	NullWindow,
	QuickTileWindow,
	InvalidWindow,
	NotVisible,
	Minimized,
	Maximized,
	NotRoot,
	OwnedWindow,
	ChildWindow,
	MissingTilingStyles,
	PopupWithoutCaption,
	ToolWindow,
	NoActivate,
	Topmost,
	Cloaked,
	DifferentVirtualDesktop,
	UserFloatRule,
	BuiltInFloatRule,
	ShellWindow,
};

struct ManagedWindowDecision {
	bool managed = false;
	ManagedWindowDecisionReason reason = ManagedWindowDecisionReason::NullWindow;
};

class WindowManager {
public:
	enum class FocusDirection {
		Left,
		Right,
		Up,
		Down,
	};

	static HMONITOR MonitorKey(HMONITOR monitor);
	static bool ContainsWindow(const std::vector<HWND>& windows, HWND hwnd);
	static std::vector<HWND> CollectTiledWindows(AppState& app);
	static std::vector<HWND> EnumerateManagedWindows(AppState& app, HMONITOR targetMonitor);
	static std::vector<HWND> BuildOrderedWindows(HMONITOR monitor, const std::vector<HWND>& previous, const std::vector<HWND>& discovered);
	static ManagedWindowDecision GetManagedWindowDecision(AppState& app, HWND hwnd);
	static const wchar_t* ManagedWindowDecisionReasonText(ManagedWindowDecisionReason reason);
	static bool IsManagedWindow(AppState& app, HWND hwnd);
	static HMONITOR ActiveMonitor();
	static bool IsMonitorTilingEnabled(AppState& app, HMONITOR monitor);
	static void TileMonitor(AppState& app, HMONITOR monitor);
	static void TileActiveMonitor(AppState& app);
	static void TileAllKnownMonitors(AppState& app);
	static void RefreshSettingsEffects(AppState& app);
	static bool ToggleProcessFloatingRule(Settings& settings, const std::wstring& processName);
	static void FocusInDirection(AppState& app, FocusDirection direction);
	static void MoveFocusedWindowInDirection(AppState& app, FocusDirection direction);
	static void ResizeFocusedWindowInDirection(AppState& app, FocusDirection direction, bool grow);
	static FloatingToggleResult ToggleFocusedWindowFloating(AppState& app);
	static MonitorTilingToggleResult ToggleActiveMonitorTiling(AppState& app);
	static LayoutChangeResult SetActiveMonitorLayout(AppState& app, LayoutMode layoutMode);
	static void ReorderWindowByDrop(AppState& app, HWND hwnd, HMONITOR sourceMonitor, HMONITOR destinationMonitor);
	static void InstallEventHooks(AppState& app);
	static void RemoveEventHooks(AppState& app);
};

}  // namespace quicktile