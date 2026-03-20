#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace quicktile {

class Logger;

enum class OverlayStackDirection {
    Vertical,
    Horizontal,
};

struct OverlayShortcutItem {
    std::wstring label;
    std::wstring value;
};

struct OverlayNode {
    enum class Kind {
        Stack,
        Heading,
        Text,
        ShortcutList,
        Separator,
    };

    Kind kind = Kind::Text;
    OverlayStackDirection direction = OverlayStackDirection::Vertical;
    std::wstring text;
    bool centered = false;
    int spacing = 0;
    int leadingSpace = 0;
    bool drawSeparators = false;
    std::vector<OverlayShortcutItem> shortcutItems;
    std::vector<OverlayNode> children;

    static OverlayNode Stack(
        OverlayStackDirection directionValue,
        std::vector<OverlayNode> childNodes,
        int spacingValue = 0,
        int leadingSpaceValue = 0,
        bool drawSeparatorsValue = false) {
        OverlayNode node;
        node.kind = Kind::Stack;
        node.direction = directionValue;
        node.spacing = spacingValue;
        node.leadingSpace = leadingSpaceValue;
        node.drawSeparators = drawSeparatorsValue;
        node.children = std::move(childNodes);
        return node;
    }

    static OverlayNode Heading(std::wstring textValue) {
        OverlayNode node;
        node.kind = Kind::Heading;
        node.text = std::move(textValue);
        return node;
    }

    static OverlayNode Text(std::wstring textValue, bool centeredValue = false) {
        OverlayNode node;
        node.kind = Kind::Text;
        node.text = std::move(textValue);
        node.centered = centeredValue;
        return node;
    }

    static OverlayNode ShortcutList(std::vector<OverlayShortcutItem> items) {
        OverlayNode node;
        node.kind = Kind::ShortcutList;
        node.shortcutItems = std::move(items);
        return node;
    }

    static OverlayNode Separator(int spacingValue) {
        OverlayNode node;
        node.kind = Kind::Separator;
        node.spacing = spacingValue;
        return node;
    }
};

struct OverlayOptions {
    int width = 240;
    UINT durationMs = 1200;
    bool detailCentered = true;
    int titleVerticalOffset = 0;
    int titleSpacing = 0;
    bool renderShortcutBadges = false;
    std::vector<OverlayNode> nodes;
};

class StatusOverlay {
public:
    StatusOverlay(HINSTANCE instance, Logger& logger);
    ~StatusOverlay();

    StatusOverlay(const StatusOverlay&) = delete;
    StatusOverlay& operator=(const StatusOverlay&) = delete;
    StatusOverlay(StatusOverlay&&) = delete;
    StatusOverlay& operator=(StatusOverlay&&) = delete;

    void SetInstance(HINSTANCE instance);
    void SetOwnerWindow(HWND ownerWindow);
    void Show(const std::wstring& title, const std::wstring& detail = L"");
    void ShowDetailed(const std::wstring& title, const std::wstring& detail, const OverlayOptions& options);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quicktile