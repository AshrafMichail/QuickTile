# QuickTile

QuickTile is a minimal native C++ tiling window manager prototype for Windows.

It runs as a single background process and tiles normal top-level application windows on the active monitor.

QuickTile currently targets Windows 11 and builds with MSVC through CMake or the included PowerShell build script.

## Features

- Automatic tiling on the active monitor
- Virtual desktop aware tiling and focus behavior
- Layout modes for main/stack, vertical columns, monocle, and floating
- Focus movement across tiled windows
- Window reordering within the tiled layout
- Focused-window border highlighting
- Resize-aware split adjustment when dragging tiled windows
- Floating treatment for auxiliary windows that should not be tiled
- Small status overlay for tiling and settings actions

Windows that stay floating instead of being tiled include:

- Child and owned windows
- Classic dialog windows such as the Run dialog
- Windows security prompts
- Remote Desktop windows

## Running QuickTile

After building, start the app by launching:

```powershell
.\build\Release\QuickTile.exe
```

QuickTile does not open a normal UI window. It runs in the background and installs global hotkeys.

On first run, QuickTile creates a settings file automatically at `%LOCALAPPDATA%\QuickTile\settings.yaml`.
If that file changes while QuickTile is running, the app automatically reloads it and reapplies the updated settings.
QuickTile only manages windows on the current Windows virtual desktop.

## Settings

QuickTile writes a default YAML file on first run. The generated file includes the following top-level settings and sections:

```yaml
# QuickTile reloads this file automatically after you save it.
version: 1
tilingEnabled: true
autoStart: true
changeNotifications: true
topBarEnabled: true
topBarHeight: 22
topBarWidgets:
  clock: right
  date: right
  appName: center
  layoutType: left
  workspaces: left
defaultLayoutType: "spiral"
focusedBorderColor: "#0078D7"
innerGap: 2
outerGap: 4
resizeStepRatio: 0.05
windowRules:
  - action: "float"
    className: "#32770"
  - action: "float"
    processName: "Taskmgr.exe"
launchShortcuts:
  - friendly_name: "Edge"
    launch_command: "microsoft-edge:"
    shortcut: "Alt+Ctrl+B"
shortcuts:
  toggleTiling:
    - "Alt+T"
  retile:
    - "Alt+Shift+W"
  growLeft:
    - "Alt+Ctrl+Left"
  shrinkLeft:
    - "Alt+Ctrl+Shift+Left"
  layoutSpiral:
    - "Alt+Shift+4"
  layoutMonocle:
    - "Alt+Shift+5"
  switchWorkspace1:
    - "Alt+Ctrl+1"
  exit:
    - "Alt+Shift+Q"
```

The generated file includes the full built-in floating rules, launch shortcuts, workspace shortcuts, and directional grow/shrink bindings.

Field meanings:

- `tilingEnabled`: turns tiling on or off at startup.
- `autoStart`: starts QuickTile automatically when you sign in.
- `changeNotifications`: shows or hides the status overlay for settings and toggle actions.
- `topBarEnabled`: shows or hides the top bar.
- `topBarHeight`: sets the top bar height in pixels.
- `topBarWidgets`: controls which top bar widgets appear and where they are placed.
- `defaultLayoutType`: selects the startup layout for each monitor workspace.
- `focusedBorderColor`: border color applied to the currently focused managed window.
- `innerGap`: inner gap in pixels between tiled windows.
- `outerGap`: outer gap in pixels between tiled windows and the monitor work area edges.
- `resizeStepRatio`: amount used by the resize hotkeys when adjusting the main split or stack weights.
- `windowRules`: ordered exact-match rules with the `float` action.
- `launchShortcuts`: launcher entries that register global shortcuts for starting apps or URLs.
- `shortcuts`: configurable hotkey lists for each QuickTile action. Each value is a list of strings like `Alt+T` or `Alt+Ctrl+Left`.

Window rules are exact matches and run in order. `float` keeps matching windows out of tiling.
The Toggle Floating action persists an exact process-level `float` rule in `windowRules`.
Shortcut strings support `Alt`, `Ctrl`, `Shift`, `Win`, letters, digits, arrow keys, `Tab`, `Enter`, `Escape`, and function keys.

Public repo note: GitHub-specific automation files are intentionally omitted from the mirrored public repository, so local validation is the authoritative build workflow.

To stop it:

- Press `Alt+Shift+Q`
- Or end the `QuickTile.exe` process from Task Manager or PowerShell

## Keyboard Shortcuts

### Tiling

- `Alt+T`: toggle tiling on or off
- `Alt+Shift+W`: retile the current monitor
- `Alt+Shift+F`: toggle the focused window's process between floating and tiled

### Layout switching

- `Alt+Shift+1`: set the current monitor on the current virtual desktop to floating
- `Alt+Shift+2`: set the current monitor on the current virtual desktop to main/stack
- `Alt+Shift+3`: set the current monitor on the current virtual desktop to vertical columns
- `Alt+Shift+4`: set the current monitor on the current virtual desktop to spiral
- `Alt+Shift+5`: set the current monitor on the current virtual desktop to monocle

### Help

- `Alt+Shift+F1`: show a categorized shortcut help overlay

### Inspection

- `Alt+Shift+I`: inspect the focused window and show QuickTile classification details

### Focus movement

- `Alt+H`, `Alt+Left`: move focus left
- `Alt+K`, `Alt+Up`: move focus up
- `Alt+L`, `Alt+Right`: move focus right
- `Alt+J`, `Alt+Down`: move focus down

Focus movement is spatial in the main/stack layout:

- Left moves from the stack back to the main pane
- Right moves from the main pane into the closest stacked window
- Up and down move within the vertical stack
- The same directional navigation can cross monitor boundaries to the nearest tiled window in that direction

In vertical columns, left and right move between adjacent columns. In monocle, directional focus cycles through the monitor's windows.

### Window reordering

- `Alt+Shift+H`, `Alt+Shift+Left`: move the focused tiled window left
- `Alt+Shift+K`, `Alt+Shift+Up`: move the focused tiled window up
- `Alt+Shift+L`, `Alt+Shift+Right`: move the focused tiled window right
- `Alt+Shift+J`, `Alt+Shift+Down`: move the focused tiled window down

Window reordering is spatial like focus movement and can also cross monitor boundaries.

### Window resizing

- `Alt+Ctrl+H`, `Alt+Ctrl+Left`: move the focused tile boundary left and retile
- `Alt+Ctrl+K`, `Alt+Ctrl+Up`: grow the focused stacked tile upward when there is a tile above
- `Alt+Ctrl+L`, `Alt+Ctrl+Right`: move the focused tile boundary right and retile
- `Alt+Ctrl+J`, `Alt+Ctrl+Down`: grow the focused stacked tile downward when there is a tile below
- `Alt+Ctrl+Shift+Left`: shrink the focused tile boundary left and retile
- `Alt+Ctrl+Shift+Up`: shrink the focused stacked tile upward
- `Alt+Ctrl+Shift+Right`: shrink the focused tile boundary right and retile
- `Alt+Ctrl+Shift+Down`: shrink the focused stacked tile downward

Horizontal resize adjusts the main/stack split on the focused monitor. Vertical resize adjusts stack heights for the focused stack tile.

### Workspace switching

- `Alt+Ctrl+1`: switch to workspace 1 on the active monitor
- `Alt+Ctrl+2`: switch to workspace 2 on the active monitor
- `Alt+Ctrl+3`: switch to workspace 3 on the active monitor
- `Alt+Ctrl+4`: switch to workspace 4 on the active monitor
- `Alt+Ctrl+5`: switch to workspace 5 on the active monitor

### Exit

- `Alt+Shift+Q`: exit QuickTile

These bindings are the default QuickTile shortcuts.

## Tray Menu

QuickTile adds a notification area icon with commands for:

- Enable or disable tiling
- Enable or disable tiling on the current monitor
- Retile all monitors
- Settings submenu with Open Settings... and Reset Settings...
- Show Shortcuts...
- Open Log...
- Toggle start on login
- Exit QuickTile

## Build Instructions

### Option 1: CMake

If `cmake` is available on `PATH`:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable will be generated at:

```text
build\Release\QuickTile.exe
```

### Option 2: MSBuild with an existing generated solution

If Visual Studio is installed and the solution has already been generated:

```powershell
./build.ps1 -Configuration Release
```

This script auto-detects a supported Visual Studio installation and can also run `/analyze` and `clang-tidy`:

```powershell
./build.ps1 -Configuration Release -Analyze
./build.ps1 -Configuration Release -ClangTidy
```

## Recommended Validation

Before publishing or sending changes for review, run:

```powershell
./build.ps1
./build.ps1 -Analyze -ClangTidy
```

See [contributions.md](contributions.md) for the default contribution workflow.

## Notes

- QuickTile tiles windows on the active monitor.
- QuickTile only enumerates, tiles, and navigates windows on the current Windows virtual desktop.
- The first tiled window on a monitor is the main pane on the left, and remaining tiled windows are stacked vertically on the right.
- If a tiled window is manually maximized and remains focused, other tiled windows on that monitor are minimized until focus leaves that maximized window.
- Focus changes and window visibility updates are handled through WinEvent hooks.
- If the settings file does not exist, QuickTile writes a default YAML file automatically on startup.
