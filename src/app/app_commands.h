#pragma once

namespace quicktile {

enum AppCommandId : unsigned {
    CommandToggleTiling = 40001,
    CommandToggleTopBar,
    CommandRetileAll,
    CommandReloadSettings,
    CommandShowHelp,
    CommandInspectWindow,
    CommandOpenSettings,
    CommandResetSettings,
    CommandResetAll,
    CommandOpenLog,
    CommandClearLog,
    CommandToggleFocusedWindowFloating,
    CommandSetLayoutFloating,
    CommandSetLayoutMainStack,
    CommandSetLayoutVerticalColumns,
    CommandSetLayoutMonocle,
    CommandSetLayoutSpiral,
    CommandToggleAutoStart,
    CommandExit,
};

}  // namespace quicktile