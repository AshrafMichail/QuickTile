#pragma once

#include "ui/status_overlay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quicktile::status_overlay_internal {

template <typename HandleType>
class GDIHandle {
public:
    GDIHandle() = default;

    explicit GDIHandle(HandleType handle)
        : handle_(handle) {
    }

    GDIHandle(const GDIHandle&) = delete;
    GDIHandle& operator=(const GDIHandle&) = delete;

    GDIHandle(GDIHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {
    }

    GDIHandle& operator=(GDIHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    ~GDIHandle() {
        Reset();
    }

    HandleType Get() const {
        return handle_;
    }

    HandleType Release() {
        return std::exchange(handle_, nullptr);
    }

    void Reset(HandleType handle = nullptr) {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }

        handle_ = handle;
    }

private:
    HandleType handle_ = nullptr;
};

class ScopedPenBrushSelection {
public:
    ScopedPenBrushSelection(HDC dc, HPEN pen, HBRUSH brush, bool ownsBrush)
        : dc_(dc), pen_(pen), ownedBrush_(ownsBrush ? brush : nullptr), brush_(brush) {
        if (pen_.Get() != nullptr) {
            oldPen_ = SelectObject(dc_, pen_.Get());
        }
        if (brush_ != nullptr) {
            oldBrush_ = SelectObject(dc_, brush_);
        }
    }

    ScopedPenBrushSelection(const ScopedPenBrushSelection&) = delete;
    ScopedPenBrushSelection& operator=(const ScopedPenBrushSelection&) = delete;

    ~ScopedPenBrushSelection() {
        if (oldBrush_ != nullptr) {
            SelectObject(dc_, oldBrush_);
        }
        if (oldPen_ != nullptr) {
            SelectObject(dc_, oldPen_);
        }
    }

private:
    HDC dc_ = nullptr;
    GDIHandle<HPEN> pen_;
    GDIHandle<HBRUSH> ownedBrush_;
    HBRUSH brush_ = nullptr;
    HGDIOBJ oldPen_ = nullptr;
    HGDIOBJ oldBrush_ = nullptr;
};

struct OverlayPalette {
    COLORREF background = 0;
    COLORREF border = 0;
    COLORREF innerBorder = 0;
    COLORREF divider = 0;
    COLORREF titleText = 0;
    COLORREF headingText = 0;
    COLORREF labelText = 0;
    COLORREF bodyText = 0;
    COLORREF emphasisText = 0;
    COLORREF keycapFill = 0;
    COLORREF keycapBorder = 0;
    COLORREF keycapText = 0;
    COLORREF keycapSymbolText = 0;
    COLORREF shadow = 0;
};

struct OverlayMetrics {
    int overlayWidth = 0;
    int detailedOverlayWidth = 0;
    int minimumAutoOverlayWidth = 0;
    int horizontalPadding = 0;
    int verticalPadding = 0;
    int textSpacing = 0;
    int sectionSpacing = 0;
    int topOffset = 0;
    int rowSpacing = 0;
    int rowColumnGap = 0;
    int minimumRowValueWidth = 0;
    int minimumRowLabelWidth = 0;
    int sectionHeadingTopSpacing = 0;
    int cornerRadius = 0;
    int keycapHorizontalPadding = 0;
    int keycapVerticalPadding = 0;
    int keycapGap = 0;
    int shortcutAlternativeGap = 0;
    int contentExtraWidth = 0;
    int overlayWindowMargin = 0;
    int minimumStructuredRowWidth = 0;
    int keycapCornerRadius = 0;
    int minimumWindowHeight = 0;
};

struct PaletteMix {
    int darkBaseScale = 40;
    int surfaceAccent = 16;
    int raisedSurfaceAccent = 24;
    int borderBlend = 12;
    int innerBorderBlend = 10;
    int dividerBlend = 7;
    int headingAccent = 32;
    int labelContrast = 22;
    int bodyContrast = 16;
    int emphasisAccent = 26;
    int keycapBorderBlend = 18;
    int keycapSymbolContrast = 26;
    int shadowScale = 78;
};

inline constexpr OverlayMetrics kBaseMetrics{
    260,
    760,
    320,
    18,
    14,
    6,
    14,
    28,
    7,
    18,
    180,
    96,
    6,
    20,
    8,
    3,
    4,
    4,
    20,
    32,
    160,
    8,
    44,
};
inline constexpr PaletteMix kPaletteMix{};

inline int FontHeightForScale(int points, int scalePercent) {
    const int effectiveDpi = MulDiv(96, scalePercent, 100);
    return -MulDiv(points, effectiveDpi, 72);
}

inline int ScaleValue(int value, int scalePercent) {
    return MulDiv(value, scalePercent, 100);
}

inline OverlayMetrics ScaleMetricsForPercent(int scalePercent) {
    const auto scale = [scalePercent](int value) {
        return ScaleValue(value, scalePercent);
    };

    return OverlayMetrics{
        scale(kBaseMetrics.overlayWidth),
        scale(kBaseMetrics.detailedOverlayWidth),
        scale(kBaseMetrics.minimumAutoOverlayWidth),
        scale(kBaseMetrics.horizontalPadding),
        scale(kBaseMetrics.verticalPadding),
        scale(kBaseMetrics.textSpacing),
        scale(kBaseMetrics.sectionSpacing),
        scale(kBaseMetrics.topOffset),
        scale(kBaseMetrics.rowSpacing),
        scale(kBaseMetrics.rowColumnGap),
        scale(kBaseMetrics.minimumRowValueWidth),
        scale(kBaseMetrics.minimumRowLabelWidth),
        scale(kBaseMetrics.sectionHeadingTopSpacing),
        scale(kBaseMetrics.cornerRadius),
        scale(kBaseMetrics.keycapHorizontalPadding),
        scale(kBaseMetrics.keycapVerticalPadding),
        scale(kBaseMetrics.keycapGap),
        scale(kBaseMetrics.shortcutAlternativeGap),
        scale(kBaseMetrics.contentExtraWidth),
        scale(kBaseMetrics.overlayWindowMargin),
        scale(kBaseMetrics.minimumStructuredRowWidth),
        scale(kBaseMetrics.keycapCornerRadius),
        scale(kBaseMetrics.minimumWindowHeight),
    };
}

inline int ClampChannel(int value) {
    return std::clamp(value, 0, 255);
}

inline COLORREF ScaleColor(COLORREF color, int percent) {
    return RGB(
        ClampChannel((static_cast<int>(GetRValue(color)) * percent) / 100),
        ClampChannel((static_cast<int>(GetGValue(color)) * percent) / 100),
        ClampChannel((static_cast<int>(GetBValue(color)) * percent) / 100));
}

inline COLORREF BlendColors(COLORREF base, COLORREF accent, int accentPercent) {
    const int basePercent = 100 - accentPercent;
    return RGB(
        ClampChannel(((static_cast<int>(GetRValue(base)) * basePercent) + (static_cast<int>(GetRValue(accent)) * accentPercent)) / 100),
        ClampChannel(((static_cast<int>(GetGValue(base)) * basePercent) + (static_cast<int>(GetGValue(accent)) * accentPercent)) / 100),
        ClampChannel(((static_cast<int>(GetBValue(base)) * basePercent) + (static_cast<int>(GetBValue(accent)) * accentPercent)) / 100));
}

inline bool IsDarkColor(COLORREF color) {
    const int luminance =
        (static_cast<int>(GetRValue(color)) * 299) +
        (static_cast<int>(GetGValue(color)) * 587) +
        (static_cast<int>(GetBValue(color)) * 114);
    return (luminance / 1000) < 128;
}

template <typename Messages>
bool ContainsMessage(const Messages& messages, UINT message) {
    return std::find(messages.begin(), messages.end(), message) != messages.end();
}

inline constexpr std::uint32_t HashText(std::wstring_view text) {
    std::uint32_t hash = 2166136261u;
    for (const wchar_t ch : text) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619u;
    }

    return hash;
}

inline std::wstring UppercaseText(std::wstring_view text) {
    std::wstring upper(text);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return upper;
}

inline std::wstring TrimText(std::wstring_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::iswspace(text[start]) != 0) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::iswspace(text[end - 1]) != 0) {
        --end;
    }

    return std::wstring(text.substr(start, end - start));
}

inline std::wstring TitleCaseToken(std::wstring_view token) {
    std::wstring text(token);
    bool uppercaseNext = true;
    for (wchar_t& ch : text) {
        if (std::iswalpha(ch) == 0) {
            uppercaseNext = true;
            continue;
        }

        ch = uppercaseNext ? static_cast<wchar_t>(std::towupper(ch)) : static_cast<wchar_t>(std::towlower(ch));
        uppercaseNext = false;
    }

    return text;
}

inline std::wstring ShortcutDisplayToken(std::wstring_view token) {
    const std::wstring trimmed = TrimText(token);
    const std::wstring upper = UppercaseText(trimmed);
    switch (HashText(upper)) {
    case HashText(L"CONTROL"):
    case HashText(L"CTRL"):
        return L"Ctrl";
    case HashText(L"ALT"):
        return L"Alt";
    case HashText(L"SHIFT"):
        return L"Shift";
    case HashText(L"WIN"):
    case HashText(L"WINDOWS"):
        return L"Win";
    case HashText(L"LEFT"):
        return std::wstring(1, static_cast<wchar_t>(0x2190));
    case HashText(L"RIGHT"):
        return std::wstring(1, static_cast<wchar_t>(0x2192));
    case HashText(L"UP"):
        return std::wstring(1, static_cast<wchar_t>(0x2191));
    case HashText(L"DOWN"):
        return std::wstring(1, static_cast<wchar_t>(0x2193));
    case HashText(L"ESC"):
    case HashText(L"ESCAPE"):
        return L"Esc";
    case HashText(L"ENTER"):
    case HashText(L"RETURN"):
        return L"Enter";
    case HashText(L"TAB"):
        return L"Tab";
    default:
        break;
    }

    if (!upper.empty() && upper[0] == L'F') {
        return upper;
    }
    if (upper.size() == 1) {
        return upper;
    }
    return TitleCaseToken(trimmed);
}

enum class StackDirection {
    Vertical,
    Horizontal,
};

enum class TextStyle {
    Title,
    Heading,
    Body,
    Label,
    Emphasis,
};

enum class TextAlignment {
    Left,
    Center,
};

class OverlayRenderContext {
public:
    OverlayRenderContext(
        HDC dcValue,
        const OverlayMetrics& metricsValue,
        const OverlayPalette& paletteValue,
        HFONT titleFontValue,
        HFONT detailFontValue,
        HFONT sectionHeadingFontValue,
        HFONT emphasisFontValue,
                HFONT keycapFontValue)
        : dc(dcValue),
          metrics(metricsValue),
          palette(paletteValue),
          titleFont(titleFontValue),
          detailFont(detailFontValue),
          sectionHeadingFont(sectionHeadingFontValue),
          emphasisFont(emphasisFontValue),
                    keycapFont(keycapFontValue) {
    }

    HFONT FontFor(TextStyle style) const {
        switch (style) {
        case TextStyle::Title:
            return titleFont;
        case TextStyle::Heading:
            return sectionHeadingFont;
        case TextStyle::Label:
        case TextStyle::Body:
            return detailFont;
        case TextStyle::Emphasis:
            return emphasisFont;
        }

        return detailFont;
    }

    COLORREF ColorFor(TextStyle style) const {
        switch (style) {
        case TextStyle::Title:
            return palette.titleText;
        case TextStyle::Heading:
            return palette.headingText;
        case TextStyle::Label:
            return palette.labelText;
        case TextStyle::Body:
            return palette.bodyText;
        case TextStyle::Emphasis:
            return palette.emphasisText;
        }

        return palette.bodyText;
    }

    int MeasureTextHeight(TextStyle style, const std::wstring& text, int width, UINT flags) const {
        if (text.empty()) {
            return 0;
        }

        RECT rect{0, 0, width, 0};
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, FontFor(style)));
        ::DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | flags);
        SelectObject(dc, oldFont);
        return rect.bottom - rect.top;
    }

    int MeasureTextWidth(HFONT font, const std::wstring& text) const {
        if (text.empty()) {
            return 0;
        }

        RECT rect{0, 0, 1 << 20, 0};
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        ::DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, oldFont);
        return rect.right - rect.left;
    }

    int MeasureTextWidth(TextStyle style, const std::wstring& text) const {
        return MeasureTextWidth(FontFor(style), text);
    }

    int FontLineHeight(HFONT font) const {
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, oldFont);
        return tm.tmHeight;
    }

    int FontBoxHeight(HFONT font) const {
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, oldFont);
        return tm.tmHeight + tm.tmExternalLeading;
    }

    int KeycapHeight() const {
        return FontLineHeight(keycapFont) + (metrics.keycapVerticalPadding * 2);
    }

    std::vector<std::vector<std::wstring>> ParseShortcutAlternatives(const std::wstring& value) const {
        if (value.empty() || value.find(L'\n') != std::wstring::npos) {
            return {};
        }

        const std::wstring trimmedValue = TrimText(value);
        if (trimmedValue.empty() || UppercaseText(trimmedValue) == L"UNBOUND") {
            return {};
        }

        std::vector<std::vector<std::wstring>> alternatives;
        std::size_t start = 0;
        while (start <= trimmedValue.size()) {
            const std::size_t comma = trimmedValue.find(L',', start);
            const std::wstring alternativeText = TrimText(trimmedValue.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start));
            if (alternativeText.empty()) {
                return {};
            }

            std::vector<std::wstring> tokens;
            std::size_t tokenStart = 0;
            while (tokenStart <= alternativeText.size()) {
                const std::size_t plus = alternativeText.find(L'+', tokenStart);
                const std::wstring token = ShortcutDisplayToken(
                    alternativeText.substr(tokenStart, plus == std::wstring::npos ? std::wstring::npos : plus - tokenStart));
                if (token.empty()) {
                    return {};
                }

                tokens.push_back(token);
                if (plus == std::wstring::npos) {
                    break;
                }

                tokenStart = plus + 1;
            }

            alternatives.push_back(std::move(tokens));
            if (comma == std::wstring::npos) {
                break;
            }

            start = comma + 1;
        }

        return alternatives;
    }

    int MeasureShortcutSequenceWidth(const std::vector<std::wstring>& sequence) const {
        int width = 0;
        const int plusWidth = MeasureTextWidth(keycapFont, L"+");
        for (std::size_t index = 0; index < sequence.size(); ++index) {
            width += MeasureTextWidth(keycapFont, sequence[index]) + (metrics.keycapHorizontalPadding * 2);
            if (index + 1 < sequence.size()) {
                width += plusWidth + (metrics.keycapGap * 2);
            }
        }
        return width;
    }

    int MeasureShortcutAlternativesWidth(const std::vector<std::vector<std::wstring>>& alternatives) const {
        int width = 0;
        for (const std::vector<std::wstring>& alternative : alternatives) {
            width = std::max(width, MeasureShortcutSequenceWidth(alternative));
        }
        return width;
    }

    int MeasureShortcutAlternativesHeight(const std::vector<std::vector<std::wstring>>& alternatives) const {
        if (alternatives.empty()) {
            return 0;
        }

        return static_cast<int>(alternatives.size()) * KeycapHeight() +
            static_cast<int>(std::max<std::size_t>(0, alternatives.size() - 1)) * metrics.shortcutAlternativeGap;
    }

    void DrawText(TextStyle style, const RECT& rect, const std::wstring& text, UINT flags) const {
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, FontFor(style)));
        SetTextColor(dc, ColorFor(style));
        RECT drawRect = rect;
        ::DrawTextW(dc, text.c_str(), -1, &drawRect, flags);
        SelectObject(dc, oldFont);
    }

    void DrawHorizontalDivider(const RECT& rect) const {
        ScopedPenBrushSelection selection(dc, CreatePen(PS_SOLID, 1, palette.divider), nullptr, false);
        const int y = rect.top + ((rect.bottom - rect.top) / 2);
        MoveToEx(dc, rect.left, y, nullptr);
        LineTo(dc, rect.right, y);
    }

    void DrawVerticalDivider(int x, int top, int bottom) const {
        ScopedPenBrushSelection selection(dc, CreatePen(PS_SOLID, 1, palette.divider), nullptr, false);
        MoveToEx(dc, x, top, nullptr);
        LineTo(dc, x, bottom);
    }

    void FillRoundedRect(const RECT& rect, COLORREF border, COLORREF fill, int radius) const {
        ScopedPenBrushSelection selection(dc, CreatePen(PS_SOLID, 1, border), CreateSolidBrush(fill), true);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    }

    void DrawShortcutAlternatives(const RECT& rect, const std::vector<std::vector<std::wstring>>& alternatives) const {
        const int badgeHeight = KeycapHeight();
        const int plusWidth = MeasureTextWidth(keycapFont, L"+");
        int rowTop = rect.top;
        for (const std::vector<std::wstring>& sequence : alternatives) {
            int cursor = rect.left;
            for (std::size_t index = 0; index < sequence.size(); ++index) {
                const int tokenWidth = MeasureTextWidth(keycapFont, sequence[index]) + (metrics.keycapHorizontalPadding * 2);

                RECT shadowRect{cursor + 1, rowTop + 1, cursor + tokenWidth + 1, rowTop + badgeHeight + 1};
                FillRoundedRect(shadowRect, palette.shadow, palette.shadow, metrics.keycapCornerRadius);

                RECT badgeRect{cursor, rowTop, cursor + tokenWidth, rowTop + badgeHeight};
                FillRoundedRect(badgeRect, palette.keycapBorder, palette.keycapFill, metrics.keycapCornerRadius);

                RECT textRect = badgeRect;
                textRect.left += metrics.keycapHorizontalPadding;
                textRect.right -= metrics.keycapHorizontalPadding;
                HFONT oldFont = static_cast<HFONT>(SelectObject(dc, keycapFont));
                SetTextColor(dc, palette.keycapText);
                ::DrawTextW(dc, sequence[index].c_str(), -1, &textRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                SelectObject(dc, oldFont);

                cursor += tokenWidth;
                if (index + 1 < sequence.size()) {
                    RECT plusRect{cursor + metrics.keycapGap, rowTop, cursor + metrics.keycapGap + plusWidth, rowTop + badgeHeight};
                    SetTextColor(dc, palette.keycapSymbolText);
                    ::DrawTextW(dc, L"+", -1, &plusRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                    cursor = plusRect.right + metrics.keycapGap;
                }
            }

            rowTop += badgeHeight + metrics.shortcutAlternativeGap;
        }
    }

    HDC dc;
    const OverlayMetrics& metrics;
    const OverlayPalette& palette;
    HFONT titleFont = nullptr;
    HFONT detailFont = nullptr;
    HFONT sectionHeadingFont = nullptr;
    HFONT emphasisFont = nullptr;
    HFONT keycapFont = nullptr;
};

class RenderNode {
public:
    virtual ~RenderNode() = default;

    virtual SIZE MeasureLayout(OverlayRenderContext& context, int maxWidth) = 0;
    virtual void Arrange(const RECT& bounds) {
        bounds_ = bounds;
    }
    virtual void Render(OverlayRenderContext& context) const = 0;

    const RECT& Bounds() const {
        return bounds_;
    }

    SIZE MeasuredSize() const {
        return measuredSize_;
    }

protected:
    RECT bounds_{};
    SIZE measuredSize_{};
};

using RenderNodePtr = std::unique_ptr<RenderNode>;

class TextNode : public RenderNode {
public:
    TextNode(std::wstring textValue, TextStyle styleValue, TextAlignment alignmentValue, bool uppercaseValue = false, bool singleLineValue = false)
        : text_(std::move(textValue)),
          style_(styleValue),
          alignment_(alignmentValue),
          uppercase_(uppercaseValue),
          singleLine_(singleLineValue) {
    }

    SIZE MeasureLayout(OverlayRenderContext& context, int maxWidth) override {
        measuredText_ = uppercase_ ? UppercaseText(text_) : text_;
        const UINT flags = MeasureFlags();
        measuredSize_ = SIZE{maxWidth, context.MeasureTextHeight(style_, measuredText_, maxWidth, flags)};
        return measuredSize_;
    }

    void Render(OverlayRenderContext& context) const override {
        context.DrawText(style_, bounds_, measuredText_, DrawFlags());
    }

protected:
    UINT AlignmentFlags() const {
        return alignment_ == TextAlignment::Center ? DT_CENTER : DT_LEFT;
    }

    UINT MeasureFlags() const {
        return AlignmentFlags() | (singleLine_ ? DT_SINGLELINE | DT_VCENTER : DT_WORDBREAK);
    }

    UINT DrawFlags() const {
        return MeasureFlags();
    }

private:
    std::wstring text_;
    TextStyle style_ = TextStyle::Body;
    TextAlignment alignment_ = TextAlignment::Left;
    bool uppercase_ = false;
    bool singleLine_ = false;
    std::wstring measuredText_;
};

class HeadingNode final : public TextNode {
public:
    explicit HeadingNode(std::wstring textValue)
        : TextNode(std::move(textValue), TextStyle::Heading, TextAlignment::Left, false, false) {
    }
};

class SeparatorNode final : public RenderNode {
public:
    explicit SeparatorNode(int heightValue)
        : height_(heightValue) {
    }

    SIZE MeasureLayout(OverlayRenderContext&, int maxWidth) override {
        measuredSize_ = SIZE{maxWidth, height_};
        return measuredSize_;
    }

    void Render(OverlayRenderContext& context) const override {
        context.DrawHorizontalDivider(bounds_);
    }

private:
    int height_ = 0;
};

class ShortcutListNode final : public RenderNode {
public:
    ShortcutListNode(std::vector<OverlayShortcutItem> rowsValue, bool renderShortcutBadgesValue)
        : rows_(std::move(rowsValue)), renderShortcutBadges_(renderShortcutBadgesValue) {
    }

    SIZE MeasureLayout(OverlayRenderContext& context, int maxWidth) override {
        rowLayouts_.clear();

        int naturalValueWidth = context.metrics.minimumRowValueWidth;
        for (const OverlayShortcutItem& row : rows_) {
            const std::vector<std::vector<std::wstring>> alternatives = renderShortcutBadges_ ? context.ParseShortcutAlternatives(row.value) : std::vector<std::vector<std::wstring>>{};
            naturalValueWidth = std::max(
                naturalValueWidth,
                alternatives.empty() ? context.MeasureTextWidth(TextStyle::Emphasis, row.value) : context.MeasureShortcutAlternativesWidth(alternatives));
        }

        const int totalRowWidth = std::max(context.metrics.minimumStructuredRowWidth, maxWidth - context.metrics.rowColumnGap);
        const int maximumLabelWidth = std::max(context.metrics.minimumRowLabelWidth, totalRowWidth - context.metrics.minimumRowValueWidth);
        rowLabelWidth_ = std::clamp((totalRowWidth * 45) / 100, context.metrics.minimumRowLabelWidth, maximumLabelWidth);
        rowValueWidth_ = std::max(context.metrics.minimumRowValueWidth, totalRowWidth - rowLabelWidth_);

        int totalHeight = 0;
        for (const OverlayShortcutItem& row : rows_) {
            MeasuredRow measuredRow;
            measuredRow.row = row;
            measuredRow.shortcutAlternatives = renderShortcutBadges_ ? context.ParseShortcutAlternatives(row.value) : std::vector<std::vector<std::wstring>>{};
            measuredRow.renderValueAsShortcut = !measuredRow.shortcutAlternatives.empty();
            measuredRow.labelHeight = context.MeasureTextHeight(TextStyle::Label, row.label, rowLabelWidth_, DT_RIGHT | DT_WORDBREAK);
            measuredRow.valueHeight = measuredRow.renderValueAsShortcut
                ? context.MeasureShortcutAlternativesHeight(measuredRow.shortcutAlternatives)
                : context.MeasureTextHeight(TextStyle::Emphasis, row.value, rowValueWidth_, DT_LEFT | DT_WORDBREAK);
            measuredRow.height = std::max(measuredRow.labelHeight, measuredRow.valueHeight);
            totalHeight += measuredRow.height;
            rowLayouts_.push_back(std::move(measuredRow));
        }

        if (!rowLayouts_.empty()) {
            totalHeight += static_cast<int>(rowLayouts_.size() - 1) * context.metrics.rowSpacing;
        }

        measuredSize_ = SIZE{maxWidth, totalHeight};
        return measuredSize_;
    }

    void Arrange(const RECT& bounds) override {
        RenderNode::Arrange(bounds);
        int rowTop = bounds.top;
        for (MeasuredRow& row : rowLayouts_) {
            row.labelRect = RECT{bounds.left, rowTop, bounds.left + rowLabelWidth_, rowTop + row.labelHeight};
            row.valueRect = RECT{
                bounds.left + rowLabelWidth_ + rowColumnGapForBounds(),
                rowTop,
                bounds.left + rowLabelWidth_ + rowColumnGapForBounds() + rowValueWidth_,
                rowTop + row.valueHeight};
            rowTop += row.height + rowSpacingForBounds();
        }
    }

    void Render(OverlayRenderContext& context) const override {
        for (const MeasuredRow& row : rowLayouts_) {
            context.DrawText(TextStyle::Label, row.labelRect, row.row.label, DT_RIGHT | DT_WORDBREAK);
            if (row.renderValueAsShortcut) {
                context.DrawShortcutAlternatives(row.valueRect, row.shortcutAlternatives);
            } else {
                context.DrawText(TextStyle::Emphasis, row.valueRect, row.row.value, DT_LEFT | DT_WORDBREAK);
            }
        }
    }

private:
    struct MeasuredRow {
        OverlayShortcutItem row;
        int labelHeight = 0;
        int valueHeight = 0;
        int height = 0;
        bool renderValueAsShortcut = false;
        std::vector<std::vector<std::wstring>> shortcutAlternatives;
        RECT labelRect{};
        RECT valueRect{};
    };

    int rowColumnGapForBounds() const {
        return rowColumnGap_;
    }

    int rowSpacingForBounds() const {
        return rowSpacing_;
    }

public:
    void SetMetrics(int rowColumnGapValue, int rowSpacingValue) {
        rowColumnGap_ = rowColumnGapValue;
        rowSpacing_ = rowSpacingValue;
    }

private:
    std::vector<OverlayShortcutItem> rows_;
    std::vector<MeasuredRow> rowLayouts_;
    bool renderShortcutBadges_ = false;
    int rowLabelWidth_ = 0;
    int rowValueWidth_ = 0;
    int rowColumnGap_ = 0;
    int rowSpacing_ = 0;
};

class StackNode final : public RenderNode {
public:
    StackNode(
        StackDirection directionValue,
        std::vector<RenderNodePtr> childrenValue = {},
        int gapValue = 0,
        int leadingSpaceValue = 0,
        bool drawSeparatorsValue = false)
        : direction_(directionValue),
          children_(std::move(childrenValue)),
          gap_(gapValue),
          leadingSpace_(leadingSpaceValue),
          drawSeparators_(drawSeparatorsValue) {
    }

    void AddChild(RenderNodePtr child) {
        children_.push_back(std::move(child));
    }

    bool Empty() const {
        return children_.empty();
    }

    SIZE MeasureLayout(OverlayRenderContext& context, int maxWidth) override {
        childSizes_.clear();
        if (children_.empty()) {
            measuredSize_ = SIZE{maxWidth, 0};
            return measuredSize_;
        }

        if (direction_ == StackDirection::Vertical) {
            int totalHeight = leadingSpace_;
            int maxChildWidth = 0;
            for (std::size_t index = 0; index < children_.size(); ++index) {
                const SIZE childSize = children_[index]->MeasureLayout(context, maxWidth);
                childSizes_.push_back(childSize);
                totalHeight += childSize.cy;
                maxChildWidth = std::max(maxChildWidth, static_cast<int>(childSize.cx));
                if (index + 1 < children_.size()) {
                    totalHeight += gap_;
                }
            }
            measuredSize_ = SIZE{maxWidth > maxChildWidth ? maxWidth : maxChildWidth, totalHeight};
            return measuredSize_;
        }

        const int availableWidth = maxWidth - (static_cast<int>(children_.size()) - 1) * gap_;
        const int childWidth = std::max(1, availableWidth / static_cast<int>(children_.size()));
        int maxHeight = 0;
        for (RenderNodePtr& child : children_) {
            const SIZE childSize = child->MeasureLayout(context, childWidth);
            childSizes_.push_back(childSize);
            maxHeight = std::max(maxHeight, static_cast<int>(childSize.cy));
        }
        measuredSize_ = SIZE{maxWidth, leadingSpace_ + maxHeight};
        return measuredSize_;
    }

    void Arrange(const RECT& bounds) override {
        RenderNode::Arrange(bounds);
        dividerXs_.clear();
        if (children_.empty()) {
            return;
        }

        if (direction_ == StackDirection::Vertical) {
            int cursor = bounds.top + leadingSpace_;
            for (std::size_t index = 0; index < children_.size(); ++index) {
                RECT childRect{bounds.left, cursor, bounds.right, cursor + childSizes_[index].cy};
                children_[index]->Arrange(childRect);
                cursor = childRect.bottom + gap_;
            }
            return;
        }

        const int availableWidth = static_cast<int>(bounds.right - bounds.left) - (static_cast<int>(children_.size()) - 1) * gap_;
        const int childWidth = std::max(1, availableWidth / static_cast<int>(children_.size()));
        int cursor = bounds.left;
        for (std::size_t index = 0; index < children_.size(); ++index) {
            RECT childRect{cursor, bounds.top + leadingSpace_, cursor + childWidth, bounds.top + leadingSpace_ + childSizes_[index].cy};
            children_[index]->Arrange(childRect);
            cursor = childRect.right;
            if (drawSeparators_ && index + 1 < children_.size()) {
                dividerXs_.push_back(cursor + (gap_ / 2));
            }
            cursor += gap_;
        }
    }

    void Render(OverlayRenderContext& context) const override {
        if (direction_ == StackDirection::Horizontal && drawSeparators_) {
            for (int dividerX : dividerXs_) {
                context.DrawVerticalDivider(dividerX, bounds_.top, bounds_.bottom);
            }
        }

        for (const RenderNodePtr& child : children_) {
            child->Render(context);
        }
    }

private:
    StackDirection direction_ = StackDirection::Vertical;
    std::vector<RenderNodePtr> children_;
    std::vector<SIZE> childSizes_;
    std::vector<int> dividerXs_;
    int gap_ = 0;
    int leadingSpace_ = 0;
    bool drawSeparators_ = false;
};

class OverlayLayout {
public:
    explicit OverlayLayout(std::wstring titleValue)
        : title_(std::move(titleValue)), bodyRoot_(StackDirection::Vertical) {
    }

    StackNode& AddStack(StackDirection direction, std::vector<RenderNodePtr> nodes, int gap = 0, int leadingSpace = 0, bool drawSeparators = false) {
        auto stack = std::make_unique<StackNode>(direction, std::move(nodes), gap, leadingSpace, drawSeparators);
        StackNode& reference = *stack;
        bodyRoot_.AddChild(std::move(stack));
        return reference;
    }

    SIZE MeasureLayout(
        OverlayRenderContext& context,
        int overlayWidth,
        int titleSpacing,
        int verticalPadding,
        int horizontalPadding,
        int titleVerticalOffset,
        int minimumWindowHeight) {
        const int titleWidth = overlayWidth - (horizontalPadding * 2);
        const int titleHeight = context.FontBoxHeight(context.FontFor(TextStyle::Title)) + std::max(4, context.metrics.textSpacing / 2);
        const bool hasBody = !bodyRoot_.Empty();
        const SIZE bodySize = bodyRoot_.MeasureLayout(context, titleWidth);
        const int bodyHeight = static_cast<int>(bodySize.cy);
        const int contentHeight = titleHeight + (hasBody ? titleSpacing + bodyHeight : 0);

        clientRect = RECT{0, 0, overlayWidth, std::max(verticalPadding + titleHeight + verticalPadding + (hasBody ? titleSpacing + bodyHeight : 0), minimumWindowHeight)};

        const int contentTop = hasBody
            ? verticalPadding
            : std::max(verticalPadding, (static_cast<int>(clientRect.bottom) - contentHeight) / 2);
        const int titleTop = std::max(0, contentTop - titleVerticalOffset);

        titleRect_ = RECT{horizontalPadding, titleTop, overlayWidth - horizontalPadding, titleTop + titleHeight};
        dividerRect_ = RECT{horizontalPadding, contentTop + titleHeight, overlayWidth - horizontalPadding, contentTop + titleHeight + titleSpacing};
        bodyRect_ = RECT{horizontalPadding, dividerRect_.bottom, overlayWidth - horizontalPadding, dividerRect_.bottom + bodyHeight};
        dividerY_ = dividerRect_.top + ((dividerRect_.bottom - dividerRect_.top) / 2);
        if (hasBody) {
            bodyRoot_.Arrange(bodyRect_);
        }

        measuredSize_ = SIZE{overlayWidth, static_cast<int>(clientRect.bottom)};
        return measuredSize_;
    }

    void Render(OverlayRenderContext& context) const {
        context.DrawText(TextStyle::Title, titleRect_, title_, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT lineRect = dividerRect_;
        lineRect.top = dividerY_;
        lineRect.bottom = dividerY_ + 1;
        context.DrawHorizontalDivider(lineRect);
        if (!bodyRoot_.Empty()) {
            bodyRoot_.Render(context);
        }
    }

    const RECT& ClientRect() const {
        return clientRect;
    }

    int Height() const {
        return measuredSize_.cy;
    }

private:
    std::wstring title_;
    StackNode bodyRoot_;
    SIZE measuredSize_{};
    RECT clientRect{};
    RECT titleRect_{};
    RECT dividerRect_{};
    RECT bodyRect_{};
    int dividerY_ = 0;
};

}  // namespace quicktile::status_overlay_internal