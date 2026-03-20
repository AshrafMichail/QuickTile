#pragma once

#include "app/app_commands.h"
#include "config/settings.h"

#include <array>

namespace quicktile {

struct LayoutMetadata {
    LayoutMode mode;
    UINT_PTR commandId;
    const char* shortcutKey;
    std::vector<std::wstring> ShortcutSettings::*shortcutMember;
};

constexpr std::array<LayoutMetadata, 5> kLayoutMetadata = {{
    {LayoutMode::Floating, CommandSetLayoutFloating, "layoutFloating", &ShortcutSettings::layoutFloating},
    {LayoutMode::MainStack, CommandSetLayoutMainStack, "layoutMainStack", &ShortcutSettings::layoutMainStack},
    {LayoutMode::VerticalColumns, CommandSetLayoutVerticalColumns, "layoutVerticalColumns", &ShortcutSettings::layoutVerticalColumns},
    {LayoutMode::Monocle, CommandSetLayoutMonocle, "layoutMonocle", &ShortcutSettings::layoutMonocle},
    {LayoutMode::Spiral, CommandSetLayoutSpiral, "layoutSpiral", &ShortcutSettings::layoutSpiral},
}};

constexpr const LayoutMetadata* FindLayoutMetadataByCommand(UINT_PTR commandId) {
    for (const LayoutMetadata& metadata : kLayoutMetadata) {
        if (metadata.commandId == commandId) {
            return &metadata;
        }
    }

    return nullptr;
}

}  // namespace quicktile