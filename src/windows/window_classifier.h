#pragma once

#include "app/app_state.h"

#include <string>
#include <string_view>

namespace quicktile {

struct WindowIdentity {
    std::wstring className;
    std::wstring title;
    std::wstring processName;
};

enum class WindowExceptionMatchSource {
    None,
    UserRule,
    BuiltInException,
};

struct WindowExceptionMatch {
    bool matches = false;
    WindowExceptionMatchSource source = WindowExceptionMatchSource::None;
};

class WindowClassifier {
public:
    static bool IsCloakedWindow(HWND hwnd);
    static bool HasTilingWindowStyles(LONG_PTR style);
    static WindowIdentity IdentityForWindow(HWND hwnd);
    static std::wstring ProcessNameForWindow(HWND hwnd);
    static bool MatchesWindowException(
        const wchar_t* className,
        const wchar_t* title,
        const std::wstring& processName,
        std::wstring_view exceptionClassName,
        std::wstring_view exceptionWindowTitle,
        std::wstring_view exceptionProcessName);
    static bool MatchesWindowRule(const WindowIdentity& identity, const WindowRuleSetting& rule);
    static bool ContainsExactString(const std::vector<std::wstring>& values, std::wstring_view candidate, bool caseInsensitive = false);
    static const WindowRuleAction* MatchingWindowRuleAction(const WindowIdentity& identity, const Settings* settings = nullptr);
    static WindowExceptionMatch FindExceptionMatch(const WindowIdentity& identity, const Settings* settings = nullptr);
    static bool IsExceptionWindowIdentity(const WindowIdentity& identity, const Settings* settings = nullptr);
    static bool IsExceptionWindow(HWND hwnd);
};

}  // namespace quicktile