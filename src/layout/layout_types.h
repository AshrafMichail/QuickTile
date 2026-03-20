#pragma once

#include <string_view>

namespace quicktile {

enum class LayoutMode {
    Spiral,
    MainStack,
    VerticalColumns,
    Monocle,
    Floating,
};

constexpr LayoutMode DefaultLayoutMode() {
    return LayoutMode::Spiral;
}

struct LayoutModeNameDefinition {
    LayoutMode mode;
    const wchar_t* displayName;
    const char* persistenceName;
};

struct LayoutModeAliasDefinition {
    std::string_view alias;
    LayoutMode mode;
};

constexpr LayoutModeNameDefinition kLayoutModeNames[] = {
    {LayoutMode::Spiral, L"Spiral", "spiral"},
    {LayoutMode::MainStack, L"Main/Stack", "main_stack"},
    {LayoutMode::VerticalColumns, L"Vertical Columns", "vertical_columns"},
    {LayoutMode::Monocle, L"Monocle", "monocle"},
    {LayoutMode::Floating, L"Floating", "floating"},
};

constexpr LayoutModeAliasDefinition kLayoutModeAliases[] = {
    {"spiral", LayoutMode::Spiral},
    {"main_stack", LayoutMode::MainStack},
    {"mainstack", LayoutMode::MainStack},
    {"vertical_columns", LayoutMode::VerticalColumns},
    {"vertical-columns", LayoutMode::VerticalColumns},
    {"columns", LayoutMode::VerticalColumns},
    {"monocle", LayoutMode::Monocle},
    {"floating", LayoutMode::Floating},
};

constexpr const wchar_t* LayoutModeDisplayName(LayoutMode mode) {
    for (const LayoutModeNameDefinition& definition : kLayoutModeNames) {
        if (definition.mode == mode) {
            return definition.displayName;
        }
    }

    return L"Spiral";
}

constexpr const char* LayoutModePersistenceName(LayoutMode mode) {
    for (const LayoutModeNameDefinition& definition : kLayoutModeNames) {
        if (definition.mode == mode) {
            return definition.persistenceName;
        }
    }

    return "spiral";
}

inline bool TryParseLayoutMode(std::string_view value, LayoutMode& mode) {
    for (const LayoutModeAliasDefinition& definition : kLayoutModeAliases) {
        if (definition.alias == value) {
            mode = definition.mode;
            return true;
        }
    }

    return false;
}

}  // namespace quicktile