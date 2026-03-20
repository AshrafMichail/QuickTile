#include "windows/window_classifier.h"

#include <cwchar>

#include <dwmapi.h>

namespace quicktile {

bool WindowClassifier::HasTilingWindowStyles(LONG_PTR style) {
    return (style & WS_MAXIMIZEBOX) != 0 && (style & WS_THICKFRAME) != 0;
}

WindowIdentity WindowClassifier::IdentityForWindow(HWND hwnd) {
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

    return WindowIdentity{className, title, ProcessNameForWindow(hwnd)};
}

bool WindowClassifier::IsCloakedWindow(HWND hwnd) {
    DWORD cloaked = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return false;
    }

    return cloaked != 0;
}

std::wstring WindowClassifier::ProcessNameForWindow(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return L"";
    }

    std::wstring imagePath(MAX_PATH, L'\0');
    DWORD size = static_cast<DWORD>(imagePath.size());
    if (!QueryFullProcessImageNameW(process, 0, imagePath.data(), &size)) {
        CloseHandle(process);
        return L"";
    }

    CloseHandle(process);
    imagePath.resize(size);

    const std::size_t separator = imagePath.find_last_of(L"\\/");
    return separator == std::wstring::npos ? imagePath : imagePath.substr(separator + 1);
}

bool WindowClassifier::MatchesWindowException(
    const wchar_t* className,
    const wchar_t* title,
    const std::wstring& processName,
    std::wstring_view exceptionClassName,
    std::wstring_view exceptionWindowTitle,
    std::wstring_view exceptionProcessName) {
    const bool classMatches = exceptionClassName.empty() || std::wstring_view(className) == exceptionClassName;
    const bool titleMatches = exceptionWindowTitle.empty() || std::wstring_view(title) == exceptionWindowTitle;
    const bool processMatches =
        exceptionProcessName.empty() || _wcsicmp(processName.c_str(), std::wstring(exceptionProcessName).c_str()) == 0;
    return classMatches && titleMatches && processMatches;
}

bool WindowClassifier::MatchesWindowRule(const WindowIdentity& identity, const WindowRuleSetting& rule) {
    return MatchesWindowException(
        identity.className.c_str(),
        identity.title.c_str(),
        identity.processName,
        rule.className,
        rule.windowTitle,
        rule.processName);
}

bool WindowClassifier::ContainsExactString(const std::vector<std::wstring>& values, std::wstring_view candidate, bool caseInsensitive) {
    for (const auto& value : values) {
        if (!caseInsensitive) {
            if (candidate == value) {
                return true;
            }
            continue;
        }

        if (_wcsicmp(value.c_str(), std::wstring(candidate).c_str()) == 0) {
            return true;
        }
    }

    return false;
}

const WindowRuleAction* WindowClassifier::MatchingWindowRuleAction(const WindowIdentity& identity, const Settings* settings) {
    if (settings == nullptr) {
        return nullptr;
    }

    for (const auto& rule : settings->windowRules) {
        if (MatchesWindowRule(identity, rule)) {
            return &rule.action;
        }
    }

    return nullptr;
}

WindowExceptionMatch WindowClassifier::FindExceptionMatch(const WindowIdentity& identity, const Settings* settings) {
    if (settings != nullptr) {
        const WindowRuleAction* action = MatchingWindowRuleAction(identity, settings);
        if (action != nullptr && *action == WindowRuleAction::Float) {
            return WindowExceptionMatch{true, WindowExceptionMatchSource::UserRule};
        }
    }

    for (const auto& exception : kFloatingWindowExceptions) {
        if (MatchesWindowException(
                identity.className.c_str(),
                identity.title.c_str(),
                identity.processName,
                exception.className != nullptr ? std::wstring_view(exception.className) : std::wstring_view{},
                exception.windowTitle != nullptr ? std::wstring_view(exception.windowTitle) : std::wstring_view{},
                exception.processName != nullptr ? std::wstring_view(exception.processName) : std::wstring_view{})) {
            return WindowExceptionMatch{true, WindowExceptionMatchSource::BuiltInException};
        }
    }

    return WindowExceptionMatch{};
}

bool WindowClassifier::IsExceptionWindowIdentity(const WindowIdentity& identity, const Settings* settings) {
    return FindExceptionMatch(identity, settings).matches;
}

bool WindowClassifier::IsExceptionWindow(HWND hwnd) {
    return IsExceptionWindowIdentity(IdentityForWindow(hwnd));
}

}  // namespace quicktile