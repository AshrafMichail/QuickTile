#include "windows/inspect_window.h"

#include "app/app_state.h"
#include "layout/layout_engine.h"
#include "platform/logger.h"
#include "ui/status_overlay.h"
#include "workspace/virtual_desktop.h"
#include "windows/window_classifier.h"
#include "windows/window_manager.h"
#include "workspace/workspace_manager.h"

#include <algorithm>
#include <sstream>

namespace quicktile {

namespace {

std::wstring HexHandleValue(HANDLE handle) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << reinterpret_cast<std::uintptr_t>(handle);
    return stream.str();
}

std::wstring BoolText(bool value) {
    return value ? L"true" : L"false";
}

std::wstring RectText(const RECT& rect) {
    std::wostringstream stream;
    stream << L"[" << rect.left << L", " << rect.top << L", " << rect.right << L", " << rect.bottom << L"]";
    return stream.str();
}

std::wstring StyleHexText(LONG_PTR value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << static_cast<unsigned long long>(value);
    return stream.str();
}

struct ReportSection {
    std::wstring heading;
    std::wstring body;
};

void AppendReportSection(std::wostringstream& stream, const std::wstring& heading, const std::wstring& body) {
    if (stream.tellp() > 0) {
        stream << L"\n\n";
    }

    stream << heading << L":\n" << body;
}

std::wstring MonitorLayoutContextText(const AppState& app, HWND hwnd, HMONITOR monitor) {
    if (hwnd == nullptr || monitor == nullptr) {
        return L"Not available";
    }

    const auto& workspace = WorkspaceManager::WorkspaceMonitors(app);
    const auto iterator = workspace.find(monitor);
    if (iterator == workspace.end()) {
        return L"No tracked monitor state";
    }

    const auto& state = iterator->second;
    auto windowIterator = std::find(state.windows.begin(), state.windows.end(), hwnd);
    std::wostringstream stream;
    stream << L"Layout: " << LayoutModeDisplayName(state.layoutMode);
    if (windowIterator != state.windows.end()) {
        const std::size_t index = static_cast<std::size_t>(std::distance(state.windows.begin(), windowIterator));
        stream << L"\nTile: " << (index + 1) << L" of " << state.windows.size();
    } else {
        stream << L"\nTile: not in tracked order";
    }
    stream << L"\nMain ratio: " << state.mainWidthRatio;
    stream << L"\nSplit weights: ";
    const std::vector<float> splitWeights = LayoutEngine::ExportMonitorSplitState(state).splitWeights;
    if (splitWeights.empty()) {
        stream << L"[]";
    } else {
        stream << L'[';
        for (std::size_t index = 0; index < splitWeights.size(); ++index) {
            if (index != 0) {
                stream << L", ";
            }
            stream << splitWeights[index];
        }
        stream << L']';
    }
    return stream.str();
}

std::wstring InspectionReportText(const std::vector<ReportSection>& sections) {
    std::wostringstream stream;
    for (const ReportSection& section : sections) {
        AppendReportSection(stream, section.heading, section.body);
    }

    return stream.str();
}

}  // namespace

void InspectWindow::ShowFocusedWindowOverlay(AppState& app) {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || !IsWindow(foreground)) {
        app.statusOverlay.Show(L"QuickTile Inspect", L"No focused window");
        return;
    }

    const WindowIdentity identity = WindowClassifier::IdentityForWindow(foreground);
    const ManagedWindowDecision decision = WindowManager::GetManagedWindowDecision(app, foreground);
    const LONG_PTR style = GetWindowLongPtrW(foreground, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(foreground, GWL_EXSTYLE);
    RECT rect{};
    GetWindowRect(foreground, &rect);
    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    const RECT workArea = LayoutEngine::WorkAreaForMonitor(monitor);
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);

    auto buildShortcutItems = [](const std::vector<std::pair<std::wstring, std::wstring>>& items) {
        std::vector<OverlayShortcutItem> shortcutItems;
        shortcutItems.reserve(items.size());
        for (const auto& [label, value] : items) {
            shortcutItems.push_back(OverlayShortcutItem{label, value});
        }
        return shortcutItems;
    };
    auto buildSectionNode = [&](const std::wstring& heading, const std::wstring& body, std::vector<OverlayShortcutItem> items = {}) {
        std::vector<OverlayNode> children;
        children.push_back(OverlayNode::Heading(heading));
        if (!items.empty()) {
            children.push_back(OverlayNode::ShortcutList(std::move(items)));
        } else {
            children.push_back(OverlayNode::Text(body));
        }
        return OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(children), 6, 6);
    };

    const std::wstring identityBody = std::wstring(L"HWND: ") + HexHandleValue(foreground) +
        L"\nPID: " + std::to_wstring(processId) +
        L"\nProcess: " + identity.processName +
        L"\nClass: " + identity.className +
        L"\nTitle: " + identity.title;
    const std::wstring windowStateBody = std::wstring(L"Visible: ") + BoolText(IsWindowVisible(foreground) != FALSE) +
        L"\nMinimized: " + BoolText(IsIconic(foreground) != FALSE) +
        L"\nRoot: " + BoolText(GetAncestor(foreground, GA_ROOT) == foreground) +
        L"\nOwned: " + BoolText(GetWindow(foreground, GW_OWNER) != nullptr) +
        L"\nCloaked: " + BoolText(WindowClassifier::IsCloakedWindow(foreground)) +
        L"\nCurrent desktop: " + BoolText(VirtualDesktop::IsWindowOnCurrentDesktop(foreground)) +
        L"\nRect: " + RectText(rect) +
        L"\nWork area: " + RectText(workArea) +
        L"\nStyle: " + StyleHexText(style) +
        L"\nExStyle: " + StyleHexText(exStyle);
    const std::wstring decisionBody = std::wstring(L"Managed: ") + BoolText(decision.managed) +
        L"\nReason: " + std::wstring(WindowManager::ManagedWindowDecisionReasonText(decision.reason));
    const std::wstring layoutContextBody = MonitorLayoutContextText(app, foreground, monitor);
    const std::wstring savedBody = std::wstring(L"Detailed information written to:\n") + app.logger.FilePath();

    std::vector<ReportSection> reportSections{{L"Identity", identityBody}, {L"Window State", windowStateBody}, {L"QuickTile Decision", decisionBody}, {L"Layout Context", layoutContextBody}, {L"Saved", savedBody}};

    app.logger.Info(std::wstring(L"Inspect focused window\n\n") + InspectionReportText(reportSections));

    std::vector<OverlayNode> leftColumn;
    leftColumn.push_back(buildSectionNode(L"Identity", identityBody));
    leftColumn.push_back(buildSectionNode(L"Window State", windowStateBody));

    std::vector<OverlayNode> rightColumn;
    rightColumn.push_back(buildSectionNode(L"QuickTile Decision", L"", buildShortcutItems({
        {L"Managed", BoolText(decision.managed)},
        {L"Reason", std::wstring(WindowManager::ManagedWindowDecisionReasonText(decision.reason))},
    })));
    rightColumn.push_back(buildSectionNode(L"Layout Context", layoutContextBody));
    rightColumn.push_back(buildSectionNode(L"Saved", savedBody));

    OverlayOptions options;
    options.width = 760;
    options.durationMs = 12000;
    options.detailCentered = false;
    options.titleSpacing = 20;
    options.renderShortcutBadges = false;
    options.nodes.push_back(OverlayNode::Stack(
        OverlayStackDirection::Horizontal,
        {
            OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(leftColumn), 16),
            OverlayNode::Stack(OverlayStackDirection::Vertical, std::move(rightColumn), 16),
        },
        18,
        0,
        true));

    app.statusOverlay.ShowDetailed(
        L"QuickTile Inspect",
        L"",
        options);
}

}  // namespace quicktile