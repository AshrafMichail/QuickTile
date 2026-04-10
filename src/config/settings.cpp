#include "config/settings.h"

#include "layout/layout_metadata.h"
#include "layout/layout_policy.h"
#include "platform/logger.h"
#include "app/shortcuts.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace quicktile {

namespace {

constexpr int kSettingsVersion = 1;

std::string Trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) {
        return "";
    }

    return std::string(first, last);
}

std::string_view TrimView(const std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

int LeadingIndentCount(const std::string& value) {
    int indent = 0;
    while (indent < static_cast<int>(value.size()) && std::isspace(static_cast<unsigned char>(value[static_cast<std::size_t>(indent)])) != 0) {
        ++indent;
    }

    return indent;
}

bool StartsWith(const std::string& value, const std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string StripYamlComment(const std::string& line) {
    bool inQuotes = false;
    bool escaping = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (escaping) {
            escaping = false;
            continue;
        }

        if (ch == '\\') {
            escaping = inQuotes;
            continue;
        }

        if (ch == '"') {
            inQuotes = !inQuotes;
            continue;
        }

        if (ch == '#' && !inQuotes) {
            return line.substr(0, index);
        }
    }

    return line;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "";
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring SettingsDirectoryPath() {
    const DWORD requiredSize = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (requiredSize == 0) {
        return L".";
    }

    std::wstring localAppData(static_cast<std::size_t>(requiredSize - 1), L'\0');
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), requiredSize);
    return localAppData + L"\\QuickTile";
}

bool ParseColor(const std::string& text, COLORREF& color) {
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }

    const auto parseChannel = [&](std::size_t offset) -> int {
        return std::strtol(text.substr(offset, 2).c_str(), nullptr, 16);
    };

    color = RGB(parseChannel(1), parseChannel(3), parseChannel(5));
    return true;
}

std::string NormalizeText(std::string text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    return text;
}

std::string EscapeYamlDoubleQuoted(const std::string& text) {
    std::ostringstream stream;
    for (char ch : text) {
        switch (ch) {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            stream << ch;
            break;
        }
    }

    return stream.str();
}

std::vector<WindowRuleSetting> DefaultWindowRules() {
    std::vector<WindowRuleSetting> rules;
    rules.reserve(kFloatingWindowExceptions.size());
    for (const auto& exception : kFloatingWindowExceptions) {
        WindowRuleSetting rule;
        if (exception.className != nullptr) {
            rule.className = exception.className;
        }
        if (exception.windowTitle != nullptr) {
            rule.windowTitle = exception.windowTitle;
        }
        if (exception.processName != nullptr) {
            rule.processName = exception.processName;
        }
        rule.action = WindowRuleAction::Float;
        rules.push_back(std::move(rule));
    }

    return rules;
}

std::vector<LaunchShortcutSetting> DefaultLaunchShortcuts() {
    return {
        LaunchShortcutSetting{L"Edge", L"microsoft-edge:", L"Alt+Ctrl+B"},
        LaunchShortcutSetting{L"Terminal", L"wt.exe", L"Alt+Ctrl+T"},
        LaunchShortcutSetting{L"VS Code", L"code", L"Alt+Ctrl+C"},
    };
}

void WriteShortcutList(std::ostringstream& stream, const char* key, const std::vector<std::wstring>& values) {
    stream << "  " << key << ":\n";
    for (const auto& value : values) {
        stream << "    - \"" << EscapeYamlDoubleQuoted(WideToUtf8(value)) << "\"\n";
    }
}

const char* WindowRuleActionName(WindowRuleAction action) {
    switch (action) {
    case WindowRuleAction::Float:
        return "float";
    }

    return "float";
}

bool TryParseWindowRuleAction(const std::string& value, WindowRuleAction& action) {
    if (value == "float") {
        action = WindowRuleAction::Float;
        return true;
    }

    return false;
}

struct ShortcutFieldDefinition {
    const char* key;
    std::vector<std::wstring> ShortcutSettings::*member;
};

struct ShortcutFieldAliasDefinition {
    const char* key;
    std::vector<std::wstring> ShortcutSettings::*member;
};

constexpr std::array<ShortcutFieldDefinition, 28> kShortcutFields = {{
    {"toggleTiling", &ShortcutSettings::toggleTiling},
    {"toggleTopBar", &ShortcutSettings::toggleTopBar},
    {"retile", &ShortcutSettings::retile},
    {"focusLeft", &ShortcutSettings::focusLeft},
    {"focusUp", &ShortcutSettings::focusUp},
    {"focusRight", &ShortcutSettings::focusRight},
    {"focusDown", &ShortcutSettings::focusDown},
    {"moveLeft", &ShortcutSettings::moveLeft},
    {"moveUp", &ShortcutSettings::moveUp},
    {"moveRight", &ShortcutSettings::moveRight},
    {"moveDown", &ShortcutSettings::moveDown},
    {"growLeft", &ShortcutSettings::growLeft},
    {"growUp", &ShortcutSettings::growUp},
    {"growRight", &ShortcutSettings::growRight},
    {"growDown", &ShortcutSettings::growDown},
    {"shrinkLeft", &ShortcutSettings::shrinkLeft},
    {"shrinkUp", &ShortcutSettings::shrinkUp},
    {"shrinkRight", &ShortcutSettings::shrinkRight},
    {"shrinkDown", &ShortcutSettings::shrinkDown},
    {"toggleFloating", &ShortcutSettings::toggleFloating},
    {"showHelp", &ShortcutSettings::showHelp},
    {"inspectWindow", &ShortcutSettings::inspectWindow},
    {"switchWorkspace1", &ShortcutSettings::switchWorkspace1},
    {"switchWorkspace2", &ShortcutSettings::switchWorkspace2},
    {"switchWorkspace3", &ShortcutSettings::switchWorkspace3},
    {"switchWorkspace4", &ShortcutSettings::switchWorkspace4},
    {"switchWorkspace5", &ShortcutSettings::switchWorkspace5},
    {"exit", &ShortcutSettings::exit},
}};

constexpr std::array<ShortcutFieldAliasDefinition, 4> kShortcutFieldAliases = {{
    {"resizeLeft", &ShortcutSettings::growLeft},
    {"resizeUp", &ShortcutSettings::growUp},
    {"resizeRight", &ShortcutSettings::growRight},
    {"resizeDown", &ShortcutSettings::growDown},
}};

struct LayoutSettingDefinition {
    const char* key;
    LayoutMode Settings::*member;
    const char* errorName;
};

constexpr std::array<LayoutSettingDefinition, 2> kLayoutSettings = {{
    {"defaultLayoutType", &Settings::defaultLayoutMode, "defaultLayoutType"},
    {"defaultLayoutMode", &Settings::defaultLayoutMode, "defaultLayoutType"},
}};

struct BoolSettingDefinition {
    const char* key;
    bool Settings::*member;
    const char* errorName;
};

constexpr std::array<BoolSettingDefinition, 6> kBoolSettings = {{
    {"tilingEnabled", &Settings::tilingEnabled, "tilingEnabled"},
    {"autoStart", &Settings::autoStart, "autoStart"},
    {"changeNotifications", &Settings::changeNotifications, "changeNotifications"},
    {"topBarEnabled", &Settings::topBarEnabled, "topBarEnabled"},
    {"rememberWindowPlacements", nullptr, "removed memory setting"},
    {"rememberMonitorSplits", nullptr, "removed memory setting"},
}};

struct IntSettingDefinition {
    const char* key;
    int Settings::*member;
    const char* errorName;
    int minimum;
    int maximum;
};

constexpr std::array<IntSettingDefinition, 3> kIntSettings = {{
    {"innerGap", &Settings::innerGap, "innerGap", 0, 256},
    {"outerGap", &Settings::outerGap, "outerGap", 0, 256},
    {"topBarHeight", &Settings::topBarHeight, "topBarHeight", 1, 256},
}};

struct TopBarWidgetSettingDefinition {
    const char* key;
    TopBarWidgetKind kind;
    TopBarWidgetPosition TopBarWidgetSettings::*member;
    const char* errorName;
};

constexpr std::array<TopBarWidgetSettingDefinition, 5> kTopBarWidgetSettings = {{
    {"clock", TopBarWidgetKind::Clock, &TopBarWidgetSettings::clock, "clock"},
    {"date", TopBarWidgetKind::Date, &TopBarWidgetSettings::date, "date"},
    {"appName", TopBarWidgetKind::AppName, &TopBarWidgetSettings::appName, "appName"},
    {"layoutType", TopBarWidgetKind::LayoutType, &TopBarWidgetSettings::layoutType, "layoutType"},
    {"workspaces", TopBarWidgetKind::Workspaces, &TopBarWidgetSettings::workspaces, "workspaces"},
}};

float ClampResizeStepRatioValue(float value) {
    return std::clamp(value, 0.01f, 0.25f);
}

float ClampTopBarOpacityValue(float value) {
    return std::clamp(value, 0.5f, 1.0f);
}

struct DoubleSettingDefinition {
    const char* key;
    float Settings::*member;
    const char* errorName;
    float (*transform)(float value);
};

constexpr std::array<DoubleSettingDefinition, 2> kDoubleSettings = {{
    {"resizeStepRatio", &Settings::resizeStepRatio, "resizeStepRatio", ClampResizeStepRatioValue},
    {"topBarOpacity", &Settings::topBarOpacity, "topBarOpacity", ClampTopBarOpacityValue},
}};

constexpr std::array<const char*, 4> kLegacyFloatingSettings = {{
    "defaultFloatingProcesses",
    "defaultFloatingClasses",
    "defaultFloatingTitles",
    "floatingWindowExceptions",
}};

struct WindowRuleStringFieldDefinition {
    const char* key;
    std::wstring WindowRuleSetting::*member;
    const char* errorName;
};

constexpr std::array<WindowRuleStringFieldDefinition, 3> kWindowRuleStringFields = {{
    {"className", &WindowRuleSetting::className, "className"},
    {"windowTitle", &WindowRuleSetting::windowTitle, "windowTitle"},
    {"processName", &WindowRuleSetting::processName, "processName"},
}};

struct LaunchShortcutStringFieldDefinition {
    const char* key;
    std::wstring LaunchShortcutSetting::*member;
    const char* errorName;
};

constexpr std::array<LaunchShortcutStringFieldDefinition, 3> kLaunchShortcutStringFields = {{
    {"friendly_name", &LaunchShortcutSetting::friendlyName, "friendly_name"},
    {"launch_command", &LaunchShortcutSetting::launchCommand, "launch_command"},
    {"shortcut", &LaunchShortcutSetting::shortcut, "shortcut"},
}};

template <typename Definition, typename Definitions>
const Definition* FindSettingDefinition(const Definitions& definitions, const std::string& key) {
    const auto it = std::find_if(definitions.begin(), definitions.end(), [&](const Definition& definition) {
        return key == definition.key;
    });
    return it == definitions.end() ? nullptr : &(*it);
}

std::vector<std::wstring>* ShortcutBindingsForKey(ShortcutSettings& shortcuts, const std::string& key) {
    const ShortcutFieldDefinition* definition = FindSettingDefinition<ShortcutFieldDefinition>(kShortcutFields, key);
    if (definition != nullptr) {
        return &(shortcuts.*(definition->member));
    }

    const ShortcutFieldAliasDefinition* aliasDefinition = FindSettingDefinition<ShortcutFieldAliasDefinition>(kShortcutFieldAliases, key);
    if (aliasDefinition != nullptr) {
        return &(shortcuts.*(aliasDefinition->member));
    }

    for (const LayoutMetadata& metadata : kLayoutMetadata) {
        if (key == metadata.shortcutKey) {
            return &(shortcuts.*(metadata.shortcutMember));
        }
    }

    return nullptr;
}

bool TryParseYamlQuotedString(const std::string& value, std::string& result) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }

    result.clear();
    result.reserve(value.size() - 2);
    bool escaping = false;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char ch = value[index];
        if (!escaping) {
            if (ch == '\\') {
                escaping = true;
                continue;
            }

            result.push_back(ch);
            continue;
        }

        switch (ch) {
        case '\\':
            result.push_back('\\');
            break;
        case '"':
            result.push_back('"');
            break;
        case 'n':
            result.push_back('\n');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 't':
            result.push_back('\t');
            break;
        default:
            return false;
        }

        escaping = false;
    }

    return !escaping;
}

bool ParseYamlBool(const std::string& value, bool& parsed) {
    if (value == "true") {
        parsed = true;
        return true;
    }

    if (value == "false") {
        parsed = false;
        return true;
    }

    return false;
}

bool ParseYamlNumber(const std::string& value, float& parsed) {
    char* end = nullptr;
    const float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || !Trim(end).empty()) {
        return false;
    }

    parsed = number;
    return true;
}

bool ParseYamlString(const std::string& value, std::wstring& parsed) {
    if (value == "null") {
        parsed.clear();
        return true;
    }

    std::string utf8;
    if (TryParseYamlQuotedString(value, utf8)) {
        parsed = Utf8ToWide(utf8);
        return true;
    }

    parsed = Utf8ToWide(value);
    return true;
}

bool SetYamlError(std::wstring* errorMessage, int lineNumber, std::string_view message) {
    if (errorMessage == nullptr) {
        return false;
    }

    std::wostringstream stream;
    if (lineNumber > 0) {
        stream << L"line " << lineNumber << L": ";
    }

    stream << Utf8ToWide(std::string(message));
    *errorMessage = stream.str();
    return false;
}

bool SetInvalidSettingError(std::wstring* errorMessage, int lineNumber, const char* typeName, const char* settingName) {
    std::string message = "invalid ";
    message += typeName;
    message += " for ";
    message += settingName;
    return SetYamlError(errorMessage, lineNumber, message);
}

bool ApplyBoolSetting(const BoolSettingDefinition& definition, const std::string& value, Settings& settings, std::wstring* errorMessage, int lineNumber) {
    bool parsed = false;
    if (!ParseYamlBool(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "boolean", definition.errorName);
    }

    if (definition.member != nullptr) {
        settings.*(definition.member) = parsed;
    }
    return true;
}

bool ApplyIntSetting(const IntSettingDefinition& definition, const std::string& value, Settings& settings, std::wstring* errorMessage, int lineNumber) {
    float parsed = 0.0;
    if (!ParseYamlNumber(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "number", definition.errorName);
    }

    settings.*(definition.member) = std::clamp(static_cast<int>(std::lround(parsed)), definition.minimum, definition.maximum);
    return true;
}

bool ApplyDoubleSetting(const DoubleSettingDefinition& definition, const std::string& value, Settings& settings, std::wstring* errorMessage, int lineNumber) {
    float parsed = 0.0;
    if (!ParseYamlNumber(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "number", definition.errorName);
    }

    settings.*(definition.member) = definition.transform(parsed);
    return true;
}

bool ApplyColorSetting(const std::string& value, Settings& settings, std::wstring* errorMessage, int lineNumber) {
    std::wstring parsed;
    if (!ParseYamlString(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "string", "focusedBorderColor");
    }

    COLORREF color = settings.focusedBorderColor;
    if (ParseColor(WideToUtf8(parsed), color)) {
        settings.focusedBorderColor = color;
    }
    return true;
}

bool ApplyLayoutSetting(const LayoutSettingDefinition& definition, const std::string& value, Settings& settings, std::wstring* errorMessage, int lineNumber) {
    std::wstring parsed;
    if (!ParseYamlString(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "string", definition.errorName);
    }

    LayoutMode parsedLayout = DefaultLayoutMode();
    if (!TryParseLayoutMode(WideToUtf8(parsed), parsedLayout)) {
        return SetYamlError(errorMessage, lineNumber, "unknown layout mode for defaultLayoutType");
    }

    settings.*(definition.member) = parsedLayout;
    return true;
}

const char* TopBarWidgetPositionName(TopBarWidgetPosition position) {
    switch (position) {
    case TopBarWidgetPosition::Disabled:
        return "disabled";
    case TopBarWidgetPosition::Left:
        return "left";
    case TopBarWidgetPosition::Center:
        return "center";
    case TopBarWidgetPosition::Right:
        return "right";
    }

    return "disabled";
}

bool TryParseTopBarWidgetPosition(const std::string& value, TopBarWidgetPosition& position) {
    if (value == "disabled") {
        position = TopBarWidgetPosition::Disabled;
        return true;
    }

    if (value == "left") {
        position = TopBarWidgetPosition::Left;
        return true;
    }

    if (value == "center") {
        position = TopBarWidgetPosition::Center;
        return true;
    }

    if (value == "right") {
        position = TopBarWidgetPosition::Right;
        return true;
    }

    return false;
}

void NormalizeTopBarWidgetOrder(TopBarWidgetSettings& topBarWidgets) {
    const std::array<TopBarWidgetKind, 5> defaultOrder = {
        TopBarWidgetKind::Clock,
        TopBarWidgetKind::Date,
        TopBarWidgetKind::AppName,
        TopBarWidgetKind::LayoutType,
        TopBarWidgetKind::Workspaces,
    };

    std::vector<TopBarWidgetKind> normalized;
    normalized.reserve(defaultOrder.size());

    for (TopBarWidgetKind kind : topBarWidgets.order) {
        if (std::find(normalized.begin(), normalized.end(), kind) == normalized.end()) {
            normalized.push_back(kind);
        }
    }

    for (TopBarWidgetKind kind : defaultOrder) {
        if (std::find(normalized.begin(), normalized.end(), kind) == normalized.end()) {
            normalized.push_back(kind);
        }
    }

    topBarWidgets.order = std::move(normalized);
}

bool TryApplyTopBarWidgetField(
    const std::string& key,
    const std::string& value,
    TopBarWidgetSettings& topBarWidgets,
    std::wstring* errorMessage,
    int lineNumber,
    bool& handled) {
    handled = true;

    const TopBarWidgetSettingDefinition* definition = FindSettingDefinition<TopBarWidgetSettingDefinition>(kTopBarWidgetSettings, key);
    if (definition == nullptr) {
        handled = false;
        return true;
    }

    std::wstring parsed;
    if (!ParseYamlString(value, parsed)) {
        return SetInvalidSettingError(errorMessage, lineNumber, "string", definition->errorName);
    }

    TopBarWidgetPosition position = TopBarWidgetPosition::Disabled;
    if (!TryParseTopBarWidgetPosition(WideToUtf8(parsed), position)) {
        return SetYamlError(errorMessage, lineNumber, "unknown topBarWidgets position; expected disabled, left, center, or right");
    }

    topBarWidgets.*(definition->member) = position;
    const auto existing = std::find(topBarWidgets.order.begin(), topBarWidgets.order.end(), definition->kind);
    if (existing != topBarWidgets.order.end()) {
        topBarWidgets.order.erase(existing);
    }
    topBarWidgets.order.push_back(definition->kind);
    return true;
}

bool IsLegacyFloatingSetting(const std::string& key) {
    return std::any_of(kLegacyFloatingSettings.begin(), kLegacyFloatingSettings.end(), [&](const char* setting) {
        return key == setting;
    });
}

bool TryApplyTopLevelSetting(
    const std::string& key,
    const std::string& value,
    Settings& settings,
    std::wstring* errorMessage,
    int lineNumber) {

    if (const BoolSettingDefinition* definition = FindSettingDefinition<BoolSettingDefinition>(kBoolSettings, key)) {
        return ApplyBoolSetting(*definition, value, settings, errorMessage, lineNumber);
    }

    if (key == "focusedBorderColor") {
        return ApplyColorSetting(value, settings, errorMessage, lineNumber);
    }

    if (const IntSettingDefinition* definition = FindSettingDefinition<IntSettingDefinition>(kIntSettings, key)) {
        return ApplyIntSetting(*definition, value, settings, errorMessage, lineNumber);
    }

    if (const DoubleSettingDefinition* definition = FindSettingDefinition<DoubleSettingDefinition>(kDoubleSettings, key)) {
        return ApplyDoubleSetting(*definition, value, settings, errorMessage, lineNumber);
    }

    if (const LayoutSettingDefinition* definition = FindSettingDefinition<LayoutSettingDefinition>(kLayoutSettings, key)) {
        return ApplyLayoutSetting(*definition, value, settings, errorMessage, lineNumber);
    }

    if (IsLegacyFloatingSetting(key)) {
        return SetYamlError(errorMessage, lineNumber, "legacy floating settings are no longer supported; use windowRules");
    }

    return true;
}

bool TryApplyWindowRuleField(
    const std::string& key,
    const std::string& value,
    WindowRuleSetting& rule,
    bool& hasAction,
    std::wstring* errorMessage,
    int lineNumber,
    bool& handled) {
    handled = true;

    if (const WindowRuleStringFieldDefinition* definition =
            FindSettingDefinition<WindowRuleStringFieldDefinition>(kWindowRuleStringFields, key)) {
        if (!ParseYamlString(value, rule.*(definition->member))) {
            return SetInvalidSettingError(errorMessage, lineNumber, "string", definition->errorName);
        }
        return true;
    }

    if (key == "action") {
        std::wstring parsed;
        if (!ParseYamlString(value, parsed)) {
            return SetInvalidSettingError(errorMessage, lineNumber, "string", "action");
        }

        if (!TryParseWindowRuleAction(WideToUtf8(parsed), rule.action)) {
            return SetYamlError(errorMessage, lineNumber, "unknown window rule action");
        }

        hasAction = true;
        return true;
    }

    handled = false;
    return true;
}

bool TryApplyLaunchShortcutField(
    const std::string& key,
    const std::string& value,
    LaunchShortcutSetting& launchShortcut,
    std::wstring* errorMessage,
    int lineNumber,
    bool& handled) {
    handled = true;

    if (const LaunchShortcutStringFieldDefinition* definition =
            FindSettingDefinition<LaunchShortcutStringFieldDefinition>(kLaunchShortcutStringFields, key)) {
        if (!ParseYamlString(value, launchShortcut.*(definition->member))) {
            return SetInvalidSettingError(errorMessage, lineNumber, "string", definition->errorName);
        }
        return true;
    }

    handled = false;
    return true;
}

std::string SettingsYaml(const Settings& settings) {
    std::ostringstream stream;
    stream << "# QuickTile reloads this file automatically after you save it.\n";
    stream << "version: " << kSettingsVersion << "\n";
    stream << "tilingEnabled: " << (settings.tilingEnabled ? "true" : "false") << "\n";
    stream << "autoStart: " << (settings.autoStart ? "true" : "false") << "\n";
    stream << "changeNotifications: " << (settings.changeNotifications ? "true" : "false") << "\n";
    stream << "topBarEnabled: " << (settings.topBarEnabled ? "true" : "false") << "\n";
    stream << "topBarHeight: " << settings.topBarHeight << "\n";
    stream << "topBarOpacity: " << settings.topBarOpacity << "\n";
    stream << "topBarWidgets:\n";
    for (TopBarWidgetKind kind : settings.topBarWidgets.order) {
        const auto it = std::find_if(kTopBarWidgetSettings.begin(), kTopBarWidgetSettings.end(), [kind](const TopBarWidgetSettingDefinition& field) {
            return field.kind == kind;
        });
        if (it != kTopBarWidgetSettings.end()) {
            stream << "  " << it->key << ": " << TopBarWidgetPositionName(settings.topBarWidgets.*(it->member)) << "\n";
        }
    }
    stream << "defaultLayoutType: \"" << LayoutModePersistenceName(settings.defaultLayoutMode) << "\"\n";

    char colorBuffer[8] = {};
    std::snprintf(
        colorBuffer,
        sizeof(colorBuffer),
        "#%02X%02X%02X",
        GetRValue(settings.focusedBorderColor),
        GetGValue(settings.focusedBorderColor),
        GetBValue(settings.focusedBorderColor));

    stream << "focusedBorderColor: \"" << colorBuffer << "\"\n";
    stream << "innerGap: " << settings.innerGap << "\n";
    stream << "outerGap: " << settings.outerGap << "\n";
    stream << "resizeStepRatio: " << settings.resizeStepRatio << "\n";
    if (!settings.windowRules.empty()) {
        stream << "windowRules:\n";
        for (const auto& rule : settings.windowRules) {
            stream << "  - action: \"" << WindowRuleActionName(rule.action) << "\"\n";
            if (!rule.processName.empty()) {
                stream << "    processName: \"" << EscapeYamlDoubleQuoted(WideToUtf8(rule.processName)) << "\"\n";
            }
            if (!rule.className.empty()) {
                stream << "    className: \"" << EscapeYamlDoubleQuoted(WideToUtf8(rule.className)) << "\"\n";
            }
            if (!rule.windowTitle.empty()) {
                stream << "    windowTitle: \"" << EscapeYamlDoubleQuoted(WideToUtf8(rule.windowTitle)) << "\"\n";
            }
        }
    }
    if (!settings.launchShortcuts.empty()) {
        stream << "launchShortcuts:\n";
        for (const auto& launchShortcut : settings.launchShortcuts) {
            stream << "  - friendly_name: \"" << EscapeYamlDoubleQuoted(WideToUtf8(launchShortcut.friendlyName)) << "\"\n";
            stream << "    launch_command: \"" << EscapeYamlDoubleQuoted(WideToUtf8(launchShortcut.launchCommand)) << "\"\n";
            stream << "    shortcut: \"" << EscapeYamlDoubleQuoted(WideToUtf8(launchShortcut.shortcut)) << "\"\n";
        }
    }
    stream << "shortcuts:\n";
    for (const ShortcutFieldDefinition& field : kShortcutFields) {
        WriteShortcutList(stream, field.key, settings.shortcuts.*(field.member));
    }
    for (const LayoutMetadata& metadata : kLayoutMetadata) {
        WriteShortcutList(stream, metadata.shortcutKey, settings.shortcuts.*(metadata.shortcutMember));
    }

    return stream.str();
}

bool ParseYamlKeyValue(const std::string& line, std::string& key, std::string& value) {
    const std::size_t separator = line.find(':');
    if (separator == std::string::npos) {
        return false;
    }

    key = Trim(line.substr(0, separator));
    value = Trim(line.substr(separator + 1));
    return !key.empty();
}

}  // namespace

Settings Settings::Defaults() {
    Settings settings;
    NormalizeTopBarWidgetOrder(settings.topBarWidgets);
    settings.windowRules = DefaultWindowRules();
    settings.launchShortcuts = DefaultLaunchShortcuts();
    return settings;
}

std::wstring Settings::FilePath() {
    return SettingsDirectoryPath() + L"\\settings.yaml";
}

bool Settings::ReadFileContents(std::string& contents) {
    const std::filesystem::path path(FilePath());
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    contents = buffer.str();
    return true;
}

bool Settings::Save() const {
    const std::filesystem::path path(FilePath());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    const std::string yaml = SettingsYaml(*this);
    file.write(yaml.data(), static_cast<std::streamsize>(yaml.size()));
    return file.good();
}

bool Settings::LoadFromYaml(const std::string& yaml, Settings& settings, std::wstring* errorMessage) {
    settings = Settings::Defaults();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    enum class ListSection : std::uint8_t {
        None,
        WindowRules,
        LaunchShortcuts,
        Shortcuts,
        TopBarWidgets,
    };

    std::istringstream stream(NormalizeText(yaml));
    std::string rawLine;
    ListSection currentSection = ListSection::None;
    WindowRuleSetting currentRule;
    LaunchShortcutSetting currentLaunchShortcut;
    bool hasCurrentRule = false;
    bool currentRuleHasAction = false;
    bool hasCurrentLaunchShortcut = false;
    bool hasYamlError = false;
    std::vector<std::wstring>* currentShortcutBindings = nullptr;
    int lineNumber = 0;

    const auto flushCurrentRule = [&]() -> bool {
        if (!hasCurrentRule) {
            return true;
        }

        if (!currentRuleHasAction) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, "window rule action is required");
        }

        if (currentRule.className.empty() && currentRule.windowTitle.empty() && currentRule.processName.empty()) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, "window rule must include at least one match field");
        }

        settings.windowRules.push_back(std::move(currentRule));
        currentRule = WindowRuleSetting{};
        hasCurrentRule = false;
        currentRuleHasAction = false;
        return true;
    };

    const auto flushCurrentLaunchShortcut = [&]() -> bool {
        if (!hasCurrentLaunchShortcut) {
            return true;
        }

        if (currentLaunchShortcut.friendlyName.empty()) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, "launch shortcut friendly_name is required");
        }

        if (currentLaunchShortcut.launchCommand.empty()) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, "launch shortcut launch_command is required");
        }

        if (currentLaunchShortcut.shortcut.empty()) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, "launch shortcut shortcut is required");
        }

        ShortcutBinding binding;
        std::wstring shortcutError;
        if (!Shortcuts::TryParseBinding(currentLaunchShortcut.shortcut, binding, &shortcutError)) {
            hasYamlError = true;
            return SetYamlError(errorMessage, lineNumber, WideToUtf8(shortcutError));
        }

        settings.launchShortcuts.push_back(std::move(currentLaunchShortcut));
        currentLaunchShortcut = LaunchShortcutSetting{};
        hasCurrentLaunchShortcut = false;
        return true;
    };

    while (std::getline(stream, rawLine)) {
        ++lineNumber;
        const std::string uncommented = StripYamlComment(rawLine);
        const std::string_view trimmedView = TrimView(uncommented);
        if (trimmedView.empty()) {
            continue;
        }

        const std::string line(trimmedView);
        const int indent = LeadingIndentCount(uncommented);

        if (indent == 0) {
            if (!flushCurrentRule() || !flushCurrentLaunchShortcut() || hasYamlError) {
                return false;
            }
            currentSection = ListSection::None;
            currentShortcutBindings = nullptr;

            if (line == "windowRules:") {
                currentSection = ListSection::WindowRules;
                settings.windowRules.clear();
                continue;
            }

            if (line == "shortcuts:") {
                currentSection = ListSection::Shortcuts;
                continue;
            }

            if (line == "topBarWidgets:") {
                currentSection = ListSection::TopBarWidgets;
                settings.topBarWidgets.order.clear();
                continue;
            }

            if (line == "launchShortcuts:") {
                currentSection = ListSection::LaunchShortcuts;
                settings.launchShortcuts.clear();
                continue;
            }

            std::string key;
            std::string value;
            if (!ParseYamlKeyValue(line, key, value)) {
                return SetYamlError(errorMessage, lineNumber, "expected key: value pair");
            }

            if (!TryApplyTopLevelSetting(key, value, settings, errorMessage, lineNumber)) {
                return false;
            }

            continue;
        }

        if (currentSection == ListSection::Shortcuts) {
            if (indent <= 2) {
                std::string key;
                std::string value;
                if (!ParseYamlKeyValue(line, key, value) || !value.empty()) {
                    return SetYamlError(errorMessage, lineNumber, "invalid shortcuts entry");
                }

                currentShortcutBindings = ShortcutBindingsForKey(settings.shortcuts, key);
                if (currentShortcutBindings == nullptr) {
                    return SetYamlError(errorMessage, lineNumber, "unknown shortcut action");
                }

                currentShortcutBindings->clear();
                continue;
            }

            if (currentShortcutBindings == nullptr || !StartsWith(line, "- ")) {
                return SetYamlError(errorMessage, lineNumber, "expected shortcut list item beginning with '- '");
            }

            std::wstring parsed;
            if (!ParseYamlString(Trim(line.substr(2)), parsed)) {
                return SetYamlError(errorMessage, lineNumber, "invalid shortcut string");
            }

            ShortcutBinding binding;
            std::wstring shortcutError;
            if (!Shortcuts::TryParseBinding(parsed, binding, &shortcutError)) {
                return SetYamlError(errorMessage, lineNumber, WideToUtf8(shortcutError));
            }

            if (std::find(currentShortcutBindings->begin(), currentShortcutBindings->end(), parsed) == currentShortcutBindings->end()) {
                currentShortcutBindings->push_back(parsed);
            }
            continue;
        }

        if (currentSection == ListSection::TopBarWidgets) {
            if (indent > 2) {
                return SetYamlError(errorMessage, lineNumber, "invalid topBarWidgets entry");
            }

            std::string key;
            std::string value;
            if (!ParseYamlKeyValue(line, key, value) || value.empty()) {
                return SetYamlError(errorMessage, lineNumber, "invalid topBarWidgets entry");
            }

            bool handled = false;
            if (!TryApplyTopBarWidgetField(key, value, settings.topBarWidgets, errorMessage, lineNumber, handled)) {
                return false;
            }

            if (!handled) {
                return SetYamlError(errorMessage, lineNumber, "unknown topBarWidgets field");
            }

            continue;
        }

        if (currentSection == ListSection::LaunchShortcuts) {
            std::string item = line;
            if (StartsWith(item, "- ")) {
                if (!flushCurrentLaunchShortcut() || hasYamlError) {
                    return false;
                }

                hasCurrentLaunchShortcut = true;
                item = Trim(item.substr(2));
            } else if (!hasCurrentLaunchShortcut) {
                return SetYamlError(errorMessage, lineNumber, "expected launch shortcut list item beginning with '- '");
            }

            std::string key;
            std::string value;
            if (!ParseYamlKeyValue(item, key, value)) {
                return SetYamlError(errorMessage, lineNumber, "invalid launchShortcuts entry");
            }

            bool handled = false;
            if (!TryApplyLaunchShortcutField(key, value, currentLaunchShortcut, errorMessage, lineNumber, handled)) {
                return false;
            }

            if (!handled) {
                return SetYamlError(errorMessage, lineNumber, "unknown launchShortcuts field");
            }

            continue;
        }

        if (currentSection == ListSection::WindowRules) {
            std::string item = line;
            if (StartsWith(item, "- ")) {
                if (!flushCurrentRule() || hasYamlError) {
                    return false;
                }
                hasCurrentRule = true;
                currentRuleHasAction = false;
                item = Trim(item.substr(2));
            } else if (!hasCurrentRule) {
                return SetYamlError(errorMessage, lineNumber, "expected window rule list item beginning with '- '");
            }

            std::string key;
            std::string value;
            if (!ParseYamlKeyValue(item, key, value)) {
                return SetYamlError(errorMessage, lineNumber, "invalid windowRules entry");
            }

            bool handled = false;
            if (!TryApplyWindowRuleField(key, value, currentRule, currentRuleHasAction, errorMessage, lineNumber, handled)) {
                return false;
            }

            if (!handled) {
                return SetYamlError(errorMessage, lineNumber, "unknown windowRules field");
            }

            continue;
        }
        return SetYamlError(errorMessage, lineNumber, "unexpected indentation or section contents");
    }

    if (!flushCurrentRule() || !flushCurrentLaunchShortcut() || hasYamlError) {
        return false;
    }
    NormalizeTopBarWidgetOrder(settings.topBarWidgets);
    return true;
}

bool Settings::Load(Logger& logger, std::wstring* errorMessage) {
    Settings loaded = Settings::Defaults();

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const std::filesystem::path path(FilePath());
    if (!std::filesystem::exists(path)) {
        *this = loaded;
        if (!Save()) {
            if (errorMessage != nullptr) {
                *errorMessage = L"failed to create default settings.yaml";
            }
            return false;
        }

        logger.Info(L"Created default settings.yaml");
        return true;
    }

    std::string contents;
    if (!ReadFileContents(contents)) {
        if (errorMessage != nullptr) {
            *errorMessage = L"unable to read settings.yaml";
        }
        return false;
    }

    if (!LoadFromYaml(contents, loaded, errorMessage)) {
        return false;
    }

    *this = loaded;
    return true;
}

}  // namespace quicktile