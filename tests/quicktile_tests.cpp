#include "app/app_state.h"
#include "layout/direction_resize.h"
#include "windows/event_router.h"
#include "layout/layout_engine.h"
#include "app/shortcuts.h"
#include "platform/single_instance.h"
#include "config/settings.h"
#include "windows/window_classifier.h"
#include "workspace/workspace_model.h"
#include "workspace/workspace_manager.h"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quicktile {

}  // namespace quicktile

namespace {

using quicktile::AppState;
using quicktile::DirectionResize;
using quicktile::EventRouter;
using quicktile::LayoutEngine;
using quicktile::LayoutMode;
using quicktile::MonitorState;
using quicktile::ShortcutBinding;
using quicktile::WindowExceptionMatchSource;
using quicktile::Settings;
using quicktile::SingleInstanceGuard;
using quicktile::Shortcuts;
using quicktile::TopBarWidgetPosition;
using quicktile::TopBarWidgetKind;
using quicktile::WindowClassifier;
using quicktile::WindowIdentity;
using quicktile::WindowManager;
using quicktile::WorkspaceManager;
using quicktile::WorkspaceModel;

template <typename Handle>
Handle FakeHandle(std::uintptr_t value) {
    return reinterpret_cast<Handle>(value);
}

RECT MakeRect(int left, int top, int right, int bottom) {
    RECT rect{};
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    return rect;
}

void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

template <typename T, typename U>
void ExpectEqual(const T& actual, const U& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream stream;
        stream << file << ':' << line << ": expected " << actualExpr << " == " << expectedExpr;
        Fail(stream.str());
    }
}

void ExpectTrue(bool value, const char* expr, const char* file, int line) {
    if (!value) {
        std::ostringstream stream;
        stream << file << ':' << line << ": expected true: " << expr;
        Fail(stream.str());
    }
}

void ExpectNear(float actual, float expected, float tolerance, const char* actualExpr, const char* expectedExpr, const char* file, int line) {
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream stream;
        stream << file << ':' << line << ": expected " << actualExpr << " ~= " << expectedExpr;
        Fail(stream.str());
    }
}

#define EXPECT_EQ(actual, expected) ExpectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_TRUE(expr) ExpectTrue((expr), #expr, __FILE__, __LINE__)
#define EXPECT_NEAR(actual, expected, tolerance) ExpectNear((actual), (expected), (tolerance), #actual, #expected, __FILE__, __LINE__)

WorkspaceModel BuildSampleWorkspace() {
    const HWND main = FakeHandle<HWND>(1);
    const HWND stackTop = FakeHandle<HWND>(2);
    const HWND stackBottom = FakeHandle<HWND>(3);
    const HWND rightMain = FakeHandle<HWND>(4);

    WorkspaceModel::MonitorData leftMonitor;
    leftMonitor.handle = FakeHandle<HMONITOR>(101);
    leftMonitor.rect = MakeRect(0, 0, 1000, 1000);
    leftMonitor.layoutMode = quicktile::LayoutMode::MainStack;
    leftMonitor.mainWidthRatio = 0.6f;
    leftMonitor.splitWeights = {0.25f, 0.75f};
    leftMonitor.tiles.push_back(WorkspaceModel::TileData{main, MakeRect(0, 0, 600, 1000)});
    leftMonitor.tiles.push_back(WorkspaceModel::TileData{stackTop, MakeRect(600, 0, 1000, 500)});
    leftMonitor.tiles.push_back(WorkspaceModel::TileData{stackBottom, MakeRect(600, 500, 1000, 1000)});

    WorkspaceModel::MonitorData rightMonitor;
    rightMonitor.handle = FakeHandle<HMONITOR>(202);
    rightMonitor.rect = MakeRect(1000, 0, 2000, 1000);
    rightMonitor.layoutMode = quicktile::LayoutMode::MainStack;
    rightMonitor.mainWidthRatio = 0.6f;
    rightMonitor.tiles.push_back(WorkspaceModel::TileData{rightMain, MakeRect(1000, 0, 2000, 1000)});

    std::vector<WorkspaceModel::MonitorData> monitors;
    monitors.push_back(leftMonitor);
    monitors.push_back(rightMonitor);
    return WorkspaceModel::BuildForTesting(std::move(monitors));
}

void TestWorkspaceModelFocusNavigation() {
    const WorkspaceModel model = BuildSampleWorkspace();

    EXPECT_EQ(model.FirstWindow(), FakeHandle<HWND>(1));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(1), WindowManager::FocusDirection::Right), FakeHandle<HWND>(2));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(3), WindowManager::FocusDirection::Up), FakeHandle<HWND>(2));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(2), WindowManager::FocusDirection::Left), FakeHandle<HWND>(1));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(3), WindowManager::FocusDirection::Right), FakeHandle<HWND>(4));

    WorkspaceModel::MonitorData spiralMonitor;
    spiralMonitor.handle = FakeHandle<HMONITOR>(606);
    spiralMonitor.rect = MakeRect(0, 0, 1000, 800);
    spiralMonitor.layoutMode = LayoutMode::Spiral;
    spiralMonitor.mainWidthRatio = 0.6f;
    spiralMonitor.splitWeights = {0.4f, 0.6f};
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(61), MakeRect(0, 0, 600, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(62), MakeRect(600, 0, 1000, 320)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(63), MakeRect(760, 320, 1000, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(64), MakeRect(600, 320, 760, 800)});
    const WorkspaceModel spiralModel = WorkspaceModel::BuildForTesting({spiralMonitor});

    EXPECT_EQ(spiralModel.FindFocusTarget(FakeHandle<HWND>(61), WindowManager::FocusDirection::Right), FakeHandle<HWND>(64));
    EXPECT_EQ(spiralModel.FindFocusTarget(FakeHandle<HWND>(62), WindowManager::FocusDirection::Down), FakeHandle<HWND>(63));
}

void TestWorkspaceModelMovePlans() {
    const WorkspaceModel populated = BuildSampleWorkspace();
    const WorkspaceModel::MovePlan swapPlan = populated.BuildMovePlan(FakeHandle<HWND>(1), WindowManager::FocusDirection::Right);
    EXPECT_EQ(swapPlan.kind, WorkspaceModel::MovePlan::Kind::SwapWithWindow);
    EXPECT_EQ(swapPlan.targetWindow, FakeHandle<HWND>(2));
    EXPECT_EQ(swapPlan.destinationMonitor, FakeHandle<HMONITOR>(101));

    WorkspaceModel::MonitorData leftMonitor;
    leftMonitor.handle = FakeHandle<HMONITOR>(101);
    leftMonitor.rect = MakeRect(0, 0, 1000, 1000);
    leftMonitor.layoutMode = quicktile::LayoutMode::MainStack;
    leftMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(11), MakeRect(0, 0, 600, 1000)});
    leftMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(12), MakeRect(600, 0, 1000, 1000)});

    WorkspaceModel::MonitorData emptyRightMonitor;
    emptyRightMonitor.handle = FakeHandle<HMONITOR>(202);
    emptyRightMonitor.rect = MakeRect(1000, 0, 2000, 1000);

    std::vector<WorkspaceModel::MonitorData> monitors;
    monitors.push_back(leftMonitor);
    monitors.push_back(emptyRightMonitor);
    const WorkspaceModel emptyDestination = WorkspaceModel::BuildForTesting(std::move(monitors));
    const WorkspaceModel::MovePlan monitorPlan = emptyDestination.BuildMovePlan(FakeHandle<HWND>(12), WindowManager::FocusDirection::Right);
    EXPECT_EQ(monitorPlan.kind, WorkspaceModel::MovePlan::Kind::MoveToMonitor);
    EXPECT_EQ(monitorPlan.destinationMonitor, FakeHandle<HMONITOR>(202));
    EXPECT_EQ(monitorPlan.destinationWorkArea.right, 2000);
}

void TestWorkspaceModelResizePlanAndLayout() {
    const WorkspaceModel model = BuildSampleWorkspace();

    const WorkspaceModel::ResizePlan mainPlan = model.BuildResizePlan(FakeHandle<HWND>(1), WindowManager::FocusDirection::Right, false, 0.05f);
    EXPECT_EQ(mainPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustMainSplit);
    EXPECT_NEAR(mainPlan.mainDelta, -0.05f, 0.0001f);

    const WorkspaceModel::ResizePlan stackWidthPlan = model.BuildResizePlan(FakeHandle<HWND>(3), WindowManager::FocusDirection::Left, true, 0.05f);
    EXPECT_EQ(stackWidthPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustMainSplit);
    EXPECT_NEAR(stackWidthPlan.mainDelta, -0.05f, 0.0001f);

    const WorkspaceModel::ResizePlan stackPlan = model.BuildResizePlan(FakeHandle<HWND>(3), WindowManager::FocusDirection::Up, false, 0.05f);
    EXPECT_EQ(stackPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustStackWindow);
    EXPECT_EQ(stackPlan.targetIndex, 1u);
    EXPECT_TRUE(!stackPlan.growTarget);

    const WorkspaceModel::LayoutPlan layoutPlan = model.BuildLayoutPlan(FakeHandle<HMONITOR>(101), 20, 10);
    EXPECT_EQ(layoutPlan.placements.size(), 3u);
    EXPECT_EQ(layoutPlan.placements[0].window, FakeHandle<HWND>(1));
    EXPECT_EQ(layoutPlan.placements[0].rect.left, 10);
    EXPECT_EQ(layoutPlan.placements[0].rect.right, 586);
    EXPECT_EQ(layoutPlan.placements[1].rect.left, 606);
    EXPECT_EQ(layoutPlan.placements[1].rect.top, 10);
    EXPECT_EQ(layoutPlan.placements[1].rect.bottom, 251);
    EXPECT_EQ(layoutPlan.placements[2].rect.top, 271);
    EXPECT_EQ(layoutPlan.placements[2].rect.bottom, 990);

    WorkspaceModel::MonitorData columnsMonitor;
    columnsMonitor.handle = FakeHandle<HMONITOR>(303);
    columnsMonitor.rect = MakeRect(0, 0, 1200, 900);
    columnsMonitor.layoutMode = quicktile::LayoutMode::VerticalColumns;
    columnsMonitor.splitWeights = {0.2f, 0.5f, 0.3f};
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(31), MakeRect(0, 0, 400, 900)});
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(32), MakeRect(400, 0, 800, 900)});
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(33), MakeRect(800, 0, 1200, 900)});
    const WorkspaceModel columnsModel = WorkspaceModel::BuildForTesting({columnsMonitor});

    const WorkspaceModel::ResizePlan columnsPlan = columnsModel.BuildResizePlan(FakeHandle<HWND>(32), WindowManager::FocusDirection::Right, true, 0.05f);
    EXPECT_EQ(columnsPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustColumnWindow);
    EXPECT_EQ(columnsPlan.targetIndex, 1u);
    EXPECT_TRUE(columnsPlan.growTarget);

    WorkspaceModel::MonitorData twoWindowMainStack;
    twoWindowMainStack.handle = FakeHandle<HMONITOR>(404);
    twoWindowMainStack.rect = MakeRect(0, 0, 1200, 900);
    twoWindowMainStack.layoutMode = quicktile::LayoutMode::MainStack;
    twoWindowMainStack.mainWidthRatio = 0.6f;
    twoWindowMainStack.splitWeights = {1.0f};
    twoWindowMainStack.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(41), MakeRect(0, 0, 720, 900)});
    twoWindowMainStack.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(42), MakeRect(720, 0, 1200, 900)});
    const WorkspaceModel twoWindowMainStackModel = WorkspaceModel::BuildForTesting({twoWindowMainStack});

    const WorkspaceModel::ResizePlan twoWindowMainStackPlan = twoWindowMainStackModel.BuildResizePlan(FakeHandle<HWND>(42), WindowManager::FocusDirection::Left, true, 0.05f);
    EXPECT_EQ(twoWindowMainStackPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustMainSplit);
    EXPECT_NEAR(twoWindowMainStackPlan.mainDelta, -0.05f, 0.0001f);

    WorkspaceModel::MonitorData twoColumnMonitor;
    twoColumnMonitor.handle = FakeHandle<HMONITOR>(505);
    twoColumnMonitor.rect = MakeRect(0, 0, 1200, 900);
    twoColumnMonitor.layoutMode = quicktile::LayoutMode::VerticalColumns;
    twoColumnMonitor.splitWeights = {0.5f, 0.5f};
    twoColumnMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(51), MakeRect(0, 0, 600, 900)});
    twoColumnMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(52), MakeRect(600, 0, 1200, 900)});
    const WorkspaceModel twoColumnModel = WorkspaceModel::BuildForTesting({twoColumnMonitor});

    const WorkspaceModel::ResizePlan twoColumnPlan = twoColumnModel.BuildResizePlan(FakeHandle<HWND>(52), WindowManager::FocusDirection::Left, true, 0.05f);
    EXPECT_EQ(twoColumnPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustColumnWindow);
    EXPECT_EQ(twoColumnPlan.targetIndex, 1u);
    EXPECT_TRUE(twoColumnPlan.growTarget);

    WorkspaceModel::MonitorData spiralMonitor;
    spiralMonitor.handle = FakeHandle<HMONITOR>(606);
    spiralMonitor.rect = MakeRect(0, 0, 1000, 800);
    spiralMonitor.layoutMode = LayoutMode::Spiral;
    spiralMonitor.mainWidthRatio = 0.6f;
    spiralMonitor.splitWeights = {0.4f, 0.6f};
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(61), MakeRect(0, 0, 600, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(62), MakeRect(600, 0, 1000, 320)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(63), MakeRect(760, 320, 1000, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(64), MakeRect(600, 320, 760, 800)});
    const WorkspaceModel spiralModel = WorkspaceModel::BuildForTesting({spiralMonitor});

    const WorkspaceModel::ResizePlan spiralLeftPlan = spiralModel.BuildResizePlan(FakeHandle<HWND>(61), WindowManager::FocusDirection::Right, true, 0.05f);
    EXPECT_EQ(spiralLeftPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustSpiralSplit);
    EXPECT_EQ(spiralLeftPlan.targetIndex, 0u);
    EXPECT_NEAR(spiralLeftPlan.mainDelta, 0.05f, 0.0001f);

    const WorkspaceModel::ResizePlan spiralTopPlan = spiralModel.BuildResizePlan(FakeHandle<HWND>(62), WindowManager::FocusDirection::Down, true, 0.05f);
    EXPECT_EQ(spiralTopPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustSpiralSplit);
    EXPECT_EQ(spiralTopPlan.targetIndex, 1u);
    EXPECT_NEAR(spiralTopPlan.mainDelta, 0.05f, 0.0001f);

    const WorkspaceModel::ResizePlan spiralRemainderPlan = spiralModel.BuildResizePlan(FakeHandle<HWND>(64), WindowManager::FocusDirection::Right, true, 0.05f);
    EXPECT_EQ(spiralRemainderPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustSpiralSplit);
    EXPECT_EQ(spiralRemainderPlan.targetIndex, 2u);
    EXPECT_NEAR(spiralRemainderPlan.mainDelta, -0.05f, 0.0001f);

    WorkspaceModel::MonitorData twoWindowSpiralMonitor;
    twoWindowSpiralMonitor.handle = FakeHandle<HMONITOR>(707);
    twoWindowSpiralMonitor.rect = MakeRect(0, 0, 1000, 800);
    twoWindowSpiralMonitor.layoutMode = LayoutMode::Spiral;
    twoWindowSpiralMonitor.mainWidthRatio = 0.6f;
    twoWindowSpiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(71), MakeRect(0, 0, 600, 800)});
    twoWindowSpiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(72), MakeRect(600, 0, 1000, 800)});
    const WorkspaceModel twoWindowSpiralModel = WorkspaceModel::BuildForTesting({twoWindowSpiralMonitor});

    const WorkspaceModel::ResizePlan twoWindowSpiralRemainderPlan = twoWindowSpiralModel.BuildResizePlan(FakeHandle<HWND>(72), WindowManager::FocusDirection::Left, true, 0.05f);
    EXPECT_EQ(twoWindowSpiralRemainderPlan.kind, WorkspaceModel::ResizePlan::Kind::AdjustSpiralSplit);
    EXPECT_EQ(twoWindowSpiralRemainderPlan.targetIndex, 0u);
    EXPECT_NEAR(twoWindowSpiralRemainderPlan.mainDelta, -0.05f, 0.0001f);

    const WorkspaceModel::ResizePlan spiralInvalidPlan = spiralModel.BuildResizePlan(FakeHandle<HWND>(62), WindowManager::FocusDirection::Left, true, 0.05f);
    EXPECT_EQ(spiralInvalidPlan.kind, WorkspaceModel::ResizePlan::Kind::None);

    const std::optional<std::size_t> containedDropIndex = WorkspaceModel::FindDropTargetIndex(layoutPlan, POINT{700, 120});
    EXPECT_TRUE(containedDropIndex.has_value());
    EXPECT_EQ(*containedDropIndex, 1u);

    const std::optional<std::size_t> nearestDropIndex = WorkspaceModel::FindDropTargetIndex(layoutPlan, POINT{995, 995});
    EXPECT_TRUE(nearestDropIndex.has_value());
    EXPECT_EQ(*nearestDropIndex, 2u);
}

void TestDirectionResizeAdjustStackWindow() {
    std::vector<float> stackWeights = {0.3f, 0.4f, 0.3f};
    EXPECT_TRUE(DirectionResize::AdjustStackWindow(stackWeights, 1, true, 0.10f));
    EXPECT_NEAR(stackWeights[0], 0.25f, 0.0001f);
    EXPECT_NEAR(stackWeights[1], 0.50f, 0.0001f);
    EXPECT_NEAR(stackWeights[2], 0.25f, 0.0001f);

    EXPECT_TRUE(DirectionResize::AdjustStackWindow(stackWeights, 1, false, 0.10f));
    EXPECT_NEAR(stackWeights[0], 0.30f, 0.0001f);
    EXPECT_NEAR(stackWeights[1], 0.40f, 0.0001f);
    EXPECT_NEAR(stackWeights[2], 0.30f, 0.0001f);
}

void TestLayoutEngineAdjustWeightedWindowLengths() {
    std::vector<float> weights = {0.5f, 0.5f};
    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({500, 500}, weights, {100, 100}, 1000, 0, true, 0.05f));
    EXPECT_NEAR(weights[0], 0.55f, 0.0001f);
    EXPECT_NEAR(weights[1], 0.45f, 0.0001f);

    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({550, 450}, weights, {100, 100}, 1000, 0, false, 0.05f));
    EXPECT_NEAR(weights[0], 0.50f, 0.0001f);
    EXPECT_NEAR(weights[1], 0.50f, 0.0001f);
}

void TestWorkspaceModelAlternativeLayouts() {
    WorkspaceModel::MonitorData columnsMonitor;
    columnsMonitor.handle = FakeHandle<HMONITOR>(303);
    columnsMonitor.rect = MakeRect(0, 0, 1200, 900);
    columnsMonitor.layoutMode = quicktile::LayoutMode::VerticalColumns;
    columnsMonitor.splitWeights = {0.2f, 0.5f, 0.3f};
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(31), MakeRect(0, 0, 400, 900)});
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(32), MakeRect(400, 0, 800, 900)});
    columnsMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(33), MakeRect(800, 0, 1200, 900)});

    WorkspaceModel::MonitorData monocleMonitor;
    monocleMonitor.handle = FakeHandle<HMONITOR>(404);
    monocleMonitor.rect = MakeRect(1200, 0, 2200, 900);
    monocleMonitor.layoutMode = quicktile::LayoutMode::Monocle;
    monocleMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(41), MakeRect(1200, 0, 2200, 900)});
    monocleMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(42), MakeRect(1200, 0, 2200, 900)});

    std::vector<WorkspaceModel::MonitorData> monitors;
    monitors.push_back(columnsMonitor);
    monitors.push_back(monocleMonitor);
    const WorkspaceModel model = WorkspaceModel::BuildForTesting(std::move(monitors));

    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(31), WindowManager::FocusDirection::Right), FakeHandle<HWND>(32));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(33), WindowManager::FocusDirection::Left), FakeHandle<HWND>(32));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(41), WindowManager::FocusDirection::Down), FakeHandle<HWND>(42));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(42), WindowManager::FocusDirection::Up), FakeHandle<HWND>(41));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(41), WindowManager::FocusDirection::Left), FakeHandle<HWND>(33));
    EXPECT_EQ(model.FindFocusTarget(FakeHandle<HWND>(42), WindowManager::FocusDirection::Left), FakeHandle<HWND>(33));

    const WorkspaceModel::LayoutPlan columnsPlan = model.BuildLayoutPlan(FakeHandle<HMONITOR>(303), 24, 12);
    EXPECT_EQ(columnsPlan.placements.size(), 3u);
    EXPECT_EQ(columnsPlan.placements[0].rect.left, 12);
    EXPECT_EQ(columnsPlan.placements[1].rect.left, 262);
    EXPECT_EQ(columnsPlan.placements[2].rect.right, 1188);

    const WorkspaceModel::LayoutPlan monoclePlan = model.BuildLayoutPlan(FakeHandle<HMONITOR>(404), 12, 10);
    EXPECT_EQ(monoclePlan.placements.size(), 2u);
    EXPECT_EQ(monoclePlan.placements[0].rect.left, 1210);
    EXPECT_EQ(monoclePlan.placements[0].rect.right, 2190);
    EXPECT_EQ(monoclePlan.placements[1].rect.top, 10);
}

void TestWorkspaceModelSpiralLayout() {
    WorkspaceModel::MonitorData spiralMonitor;
    spiralMonitor.handle = FakeHandle<HMONITOR>(606);
    spiralMonitor.rect = MakeRect(0, 0, 1000, 800);
    spiralMonitor.layoutMode = LayoutMode::Spiral;
    spiralMonitor.mainWidthRatio = 0.6f;
    spiralMonitor.splitWeights = {0.4f, 0.6f};
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(61), MakeRect(0, 0, 600, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(62), MakeRect(600, 0, 1000, 320)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(63), MakeRect(760, 320, 1000, 800)});
    spiralMonitor.tiles.push_back(WorkspaceModel::TileData{FakeHandle<HWND>(64), MakeRect(600, 320, 760, 800)});

    const WorkspaceModel model = WorkspaceModel::BuildForTesting({spiralMonitor});
    const WorkspaceModel::LayoutPlan plan = model.BuildLayoutPlan(FakeHandle<HMONITOR>(606), 0, 0);
    EXPECT_EQ(plan.placements.size(), 4u);
    EXPECT_EQ(plan.placements[0].rect.left, 0);
    EXPECT_EQ(plan.placements[0].rect.right, 600);
    EXPECT_EQ(plan.placements[1].rect.top, 0);
    EXPECT_EQ(plan.placements[1].rect.bottom, 320);
    EXPECT_EQ(plan.placements[2].rect.left, 760);
    EXPECT_EQ(plan.placements[2].rect.right, 1000);
    EXPECT_EQ(plan.placements[3].rect.left, 600);
    EXPECT_EQ(plan.placements[3].rect.right, 760);

}

void TestLayoutEngineGenericWidthAdjustmentUsesActualWidths() {
    std::vector<float> columnWeights = {1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f};
    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({300, 300, 300}, columnWeights, {100, 100, 100}, 900, 1, true, 150.0f / 900.0f));
    EXPECT_NEAR(columnWeights[0], 0.25f, 0.0001f);
    EXPECT_NEAR(columnWeights[1], 0.5f, 0.0001f);
    EXPECT_NEAR(columnWeights[2], 0.25f, 0.0001f);
}

void TestLayoutEngineWeightsAndSync() {
    std::vector<HWND> previousWindows = {FakeHandle<HWND>(1), FakeHandle<HWND>(2), FakeHandle<HWND>(3)};
    std::vector<float> previousWeights = {0.3f, 0.7f};
    std::vector<HWND> ordered = {FakeHandle<HWND>(1), FakeHandle<HWND>(3), FakeHandle<HWND>(4)};

    std::vector<float> weights = LayoutEngine::BuildStackWeights(previousWindows, previousWeights, ordered);
    EXPECT_EQ(weights.size(), 2u);
    EXPECT_NEAR(weights[0], 0.7f, 0.0001f);
    EXPECT_NEAR(weights[1], 1.0f, 0.0001f);

    LayoutEngine::NormalizeWeights(weights);
    EXPECT_NEAR(weights[0], 0.4117647f, 0.0001f);
    EXPECT_NEAR(weights[1], 0.5882352f, 0.0001f);

    AppState app;
    app.settings.defaultMainWidthRatio = 0.65f;

    MonitorState state;
    state.windows = previousWindows;
    LayoutEngine::RestoreMonitorSplitState(state, quicktile::LayoutMode::MainStack, 0.0f, previousWeights, app.settings.defaultMainWidthRatio);
    LayoutEngine::SyncMonitorWindows(state, ordered, app.settings.defaultMainWidthRatio);
    EXPECT_EQ(state.windows.size(), 3u);
    EXPECT_EQ(state.windows[1], FakeHandle<HWND>(3));
    EXPECT_NEAR(state.mainWidthRatio, 0.65f, 0.0001f);
        const LayoutEngine::MonitorSplitStateData syncedState = LayoutEngine::ExportMonitorSplitState(state);
    EXPECT_EQ(syncedState.splitWeights.size(), 2u);
    EXPECT_NEAR(syncedState.splitWeights.at(0), 0.4117647f, 0.0001f);
    EXPECT_NEAR(syncedState.splitWeights.at(1), 0.5882352f, 0.0001f);

    MonitorState restoredState;
    LayoutEngine::RestoreMonitorSplitState(restoredState, quicktile::LayoutMode::VerticalColumns, 0.65f, {0.2f, 0.5f, 0.3f}, app.settings.defaultMainWidthRatio);
    LayoutEngine::SyncMonitorWindows(restoredState, {FakeHandle<HWND>(11), FakeHandle<HWND>(12), FakeHandle<HWND>(13)}, app.settings.defaultMainWidthRatio);
    const LayoutEngine::MonitorSplitStateData restoredSplitState = LayoutEngine::ExportMonitorSplitState(restoredState);
    EXPECT_EQ(restoredSplitState.splitWeights.size(), 3u);
    EXPECT_NEAR(restoredSplitState.splitWeights.at(0), 0.2f, 0.0001f);
    EXPECT_NEAR(restoredSplitState.splitWeights.at(1), 0.5f, 0.0001f);
    EXPECT_NEAR(restoredSplitState.splitWeights.at(2), 0.3f, 0.0001f);

    MonitorState expandedColumnsState;
    LayoutEngine::RestoreMonitorSplitState(expandedColumnsState, quicktile::LayoutMode::VerticalColumns, 0.65f, {0.2f, 0.5f, 0.3f}, app.settings.defaultMainWidthRatio);
    LayoutEngine::SyncMonitorWindows(expandedColumnsState, {FakeHandle<HWND>(21), FakeHandle<HWND>(22), FakeHandle<HWND>(23), FakeHandle<HWND>(24)}, app.settings.defaultMainWidthRatio);
    const LayoutEngine::MonitorSplitStateData expandedColumnsSplitState = LayoutEngine::ExportMonitorSplitState(expandedColumnsState);
    EXPECT_EQ(expandedColumnsSplitState.splitWeights.size(), 4u);
    EXPECT_NEAR(expandedColumnsSplitState.splitWeights.at(0), 0.1f, 0.0001f);
    EXPECT_NEAR(expandedColumnsSplitState.splitWeights.at(1), 0.25f, 0.0001f);
    EXPECT_NEAR(expandedColumnsSplitState.splitWeights.at(2), 0.15f, 0.0001f);
    EXPECT_NEAR(expandedColumnsSplitState.splitWeights.at(3), 0.5f, 0.0001f);
}

void TestLayoutEngineGenericHeightAdjustmentUsesActualHeights() {
    std::vector<float> stackWeights = {0.5f, 0.5f};
    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({300, 300}, stackWeights, {100, 100}, 600, 0, true, 100.0f / 600.0f));
    EXPECT_NEAR(stackWeights[0], 0.6666667f, 0.0001f);
    EXPECT_NEAR(stackWeights[1], 0.3333333f, 0.0001f);

    std::vector<float> keyboardWeights = {0.5f, 0.5f};
    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({500, 500}, keyboardWeights, {100, 100}, 1000, 0, true, 0.05f));
    EXPECT_NEAR(keyboardWeights[0], 0.55f, 0.0001f);
    EXPECT_NEAR(keyboardWeights[1], 0.45f, 0.0001f);
}

void TestLayoutEngineGenericHeightIgnoresMinimumsDuringResize() {
    std::vector<float> resizedWeights = {0.5f, 0.5f};
    EXPECT_TRUE(LayoutEngine::AdjustWeightedWindowLengths({300, 300}, resizedWeights, {100, 400}, 600, 0, false, 250.0f / 600.0f));
    EXPECT_NEAR(resizedWeights[0], 0.0833333f, 0.0001f);
    EXPECT_NEAR(resizedWeights[1], 0.9166667f, 0.0001f);
}

void TestWindowManagerVirtualDesktopContextSwap() {
    AppState state;
    state.currentDesktopKey = L"desktop-a";

    MonitorState activeMonitorState;
    activeMonitorState.layoutMode = LayoutMode::Monocle;
    activeMonitorState.mainWidthRatio = 0.63f;
    activeMonitorState.windows = {FakeHandle<HWND>(10)};
    state.monitorState.workspaceStatesByDesktop[L"desktop-a"].workspaces[0].emplace(FakeHandle<HMONITOR>(101), activeMonitorState);

    MonitorState otherDesktopMonitorState;
    otherDesktopMonitorState.layoutMode = LayoutMode::VerticalColumns;
    otherDesktopMonitorState.mainWidthRatio = 0.47f;
    otherDesktopMonitorState.windows = {FakeHandle<HWND>(20), FakeHandle<HWND>(21)};
    LayoutEngine::RestoreMonitorSplitState(otherDesktopMonitorState, quicktile::LayoutMode::MainStack, otherDesktopMonitorState.mainWidthRatio, {}, state.settings.defaultMainWidthRatio);
    otherDesktopMonitorState.layoutMode = LayoutMode::VerticalColumns;
    state.monitorState.workspaceStatesByDesktop[L"desktop-b"].workspaces[0].emplace(FakeHandle<HMONITOR>(101), otherDesktopMonitorState);

    WorkspaceManager::SetVirtualDesktopContext(state, L"desktop-b");
    EXPECT_EQ(state.currentDesktopKey, std::wstring(L"desktop-b"));
    EXPECT_EQ(state.currentDesktopName, std::wstring(L"Desktop 2"));
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).size(), 1u);
    EXPECT_EQ(WorkspaceManager::CurrentWorkspaceIndex(state), 0);
    EXPECT_TRUE(state.monitorState.workspaceStatesByDesktop.find(L"desktop-a") != state.monitorState.workspaceStatesByDesktop.end());
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).windows.size(), 2u);
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).layoutMode, LayoutMode::VerticalColumns);
    EXPECT_NEAR(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).mainWidthRatio, 0.47f, 0.0001f);
    EXPECT_EQ(state.monitorState.workspaceStatesByDesktop.at(L"desktop-a").workspaces[0].at(FakeHandle<HMONITOR>(101)).layoutMode, LayoutMode::Monocle);

    WorkspaceManager::SetVirtualDesktopContext(state, L"desktop-a");
    EXPECT_EQ(state.currentDesktopKey, std::wstring(L"desktop-a"));
    EXPECT_EQ(state.currentDesktopName, std::wstring(L"Desktop 1"));
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).size(), 1u);
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).windows.size(), 1u);
    EXPECT_EQ(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).layoutMode, LayoutMode::Monocle);
    EXPECT_NEAR(WorkspaceManager::WorkspaceMonitors(state).at(FakeHandle<HMONITOR>(101)).mainWidthRatio, 0.63f, 0.0001f);
}

void TestWindowManagerInitializesMonitorWorkspaces() {
    AppState state;
    MonitorState& monitorState = WorkspaceManager::GetOrCreateMonitorState(state, FakeHandle<HMONITOR>(303));

    EXPECT_EQ(WorkspaceManager::CurrentWorkspaceIndex(state), 0);
    EXPECT_EQ(monitorState.layoutMode, state.settings.defaultLayoutMode);
    EXPECT_NEAR(monitorState.mainWidthRatio, state.settings.defaultMainWidthRatio, 0.0001f);
}

void TestWorkspaceModelDistributeLengths() {
    const std::vector<int> equalized = WorkspaceModel::DistributeLengths(150, {80, 20, 20}, {});
    EXPECT_EQ(equalized.size(), 3u);
    EXPECT_EQ(equalized[0], 80);
    EXPECT_EQ(equalized[1], 35);
    EXPECT_EQ(equalized[2], 35);

    const std::vector<int> weighted = WorkspaceModel::DistributeLengths(100, {20, 20}, {0.75f, 0.25f});
    EXPECT_EQ(weighted.size(), 2u);
    EXPECT_EQ(weighted[0], 75);
    EXPECT_EQ(weighted[1], 25);

    const std::vector<int> constrained = WorkspaceModel::DistributeLengths(100, {80, 40}, {});
    EXPECT_EQ(constrained.size(), 2u);
    EXPECT_EQ(constrained[0] + constrained[1], 100);
    EXPECT_EQ(constrained[0], 66);
    EXPECT_EQ(constrained[1], 34);
}

void TestWindowClassifierExceptionRules() {
    EXPECT_TRUE(WindowClassifier::ContainsExactString({L"cmd.exe", L"Code.exe"}, L"code.exe", true));
    EXPECT_TRUE(WindowClassifier::MatchesWindowException(L"Dialog", L"Confirm", std::wstring(L"SETUP.EXE"), L"Dialog", L"Confirm", L"setup.exe"));
    EXPECT_TRUE(WindowClassifier::HasTilingWindowStyles(WS_OVERLAPPEDWINDOW));
    EXPECT_TRUE(!WindowClassifier::HasTilingWindowStyles(WS_THICKFRAME | WS_CAPTION));
    EXPECT_TRUE(!WindowClassifier::HasTilingWindowStyles(WS_MAXIMIZEBOX | WS_CAPTION));

    const Settings settings = Settings::Defaults();

    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"#32770", L"Anything", L"app.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Xaml_WindowedPopupClass", L"Windows Security", L"app.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"msiexec.exe"}, &settings));
}

void TestWindowClassifierBuiltInTransientIdentities() {
    const Settings settings = Settings::Defaults();

    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Credential Dialog Xaml Host", L"Anything", L"app.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"TscShellContainerClass", L"Remote Desktop", L"mstsc.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"PowerToys.Settings.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"microsoft.cmdpal.ui.exe"}, &settings));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"taskmgr.exe"}, &settings));
    EXPECT_TRUE(!WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Chrome_WidgetWin_1", L"Regular Browser", L"msedge.exe"}, &settings));
}

void TestWindowClassifierExceptionMatchSource() {
    Settings settings;
    settings.windowRules.insert(settings.windowRules.begin(), quicktile::WindowRuleSetting{L"Chrome_WidgetWin_1", L"Inspector", L"msedge.exe", quicktile::WindowRuleAction::Float});

    const quicktile::WindowExceptionMatch userRuleMatch = WindowClassifier::FindExceptionMatch(
        WindowIdentity{L"Chrome_WidgetWin_1", L"Inspector", L"msedge.exe"},
        &settings);
    EXPECT_TRUE(userRuleMatch.matches);
    EXPECT_EQ(userRuleMatch.source, WindowExceptionMatchSource::UserRule);

    const quicktile::WindowExceptionMatch builtInMatch = WindowClassifier::FindExceptionMatch(
        WindowIdentity{L"Credential Dialog Xaml Host", L"Anything", L"app.exe"},
        nullptr);
    EXPECT_TRUE(builtInMatch.matches);
    EXPECT_EQ(builtInMatch.source, WindowExceptionMatchSource::BuiltInException);
}

void TestWindowClassifierWindowRules() {
    Settings settings;
    settings.windowRules.push_back(quicktile::WindowRuleSetting{L"DialogClass", L"", L"", quicktile::WindowRuleAction::Float});

    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"DialogClass", L"Any", L"app.exe"}, &settings));
    EXPECT_TRUE(!WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"code.exe"}, &settings));
}

void TestToggleProcessFloatingRuleUsesWindowRules() {
    Settings settings;

    EXPECT_TRUE(WindowManager::ToggleProcessFloatingRule(settings, L"Code.exe"));
    EXPECT_EQ(settings.windowRules.size(), 1u);
    EXPECT_EQ(settings.windowRules[0].action, quicktile::WindowRuleAction::Float);
    EXPECT_EQ(settings.windowRules[0].processName, std::wstring(L"Code.exe"));
    EXPECT_TRUE(WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"code.exe"}, &settings));

    EXPECT_TRUE(!WindowManager::ToggleProcessFloatingRule(settings, L"Code.exe"));
    EXPECT_EQ(settings.windowRules.size(), 0u);
    EXPECT_TRUE(!WindowClassifier::IsExceptionWindowIdentity(WindowIdentity{L"Any", L"Any", L"code.exe"}, &settings));
}

void TestToggleProcessFloatingRuleRemovesDuplicateFloatRules() {
    Settings settings;
    settings.windowRules.push_back(quicktile::WindowRuleSetting{L"", L"", L"Code.exe", quicktile::WindowRuleAction::Float});
    settings.windowRules.push_back(quicktile::WindowRuleSetting{L"", L"", L"code.exe", quicktile::WindowRuleAction::Float});

    EXPECT_TRUE(!WindowManager::ToggleProcessFloatingRule(settings, L"Code.exe"));
    EXPECT_EQ(settings.windowRules.size(), 0u);
}

void TestLayoutEngineBuildOrderedWindowsPreservesPreviousOrder() {
    AppState app;

    const HMONITOR monitor = FakeHandle<HMONITOR>(101);
    const std::vector<HWND> previous = {FakeHandle<HWND>(1), FakeHandle<HWND>(2), FakeHandle<HWND>(3)};
    const std::vector<HWND> discovered = {FakeHandle<HWND>(3), FakeHandle<HWND>(2), FakeHandle<HWND>(1)};

    const std::vector<HWND> ordered = WindowManager::BuildOrderedWindows(monitor, previous, discovered);
    EXPECT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0], FakeHandle<HWND>(1));
    EXPECT_EQ(ordered[1], FakeHandle<HWND>(2));
    EXPECT_EQ(ordered[2], FakeHandle<HWND>(3));
}

void TestLayoutEngineBuildOrderedWindowsAppendsNewWindowsAfterKnownOrder() {
    AppState app;

    const HMONITOR monitor = FakeHandle<HMONITOR>(101);
    const std::vector<HWND> previous = {FakeHandle<HWND>(1), FakeHandle<HWND>(2)};
    const std::vector<HWND> discovered = {FakeHandle<HWND>(3), FakeHandle<HWND>(2), FakeHandle<HWND>(1)};

    const std::vector<HWND> ordered = WindowManager::BuildOrderedWindows(monitor, previous, discovered);
    EXPECT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0], FakeHandle<HWND>(1));
    EXPECT_EQ(ordered[1], FakeHandle<HWND>(2));
    EXPECT_EQ(ordered[2], FakeHandle<HWND>(3));
}

void TestSettingsParsesAutoStart() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\nautoStart: true\ntilingEnabled: true\n", settings));
    EXPECT_TRUE(settings.autoStart);
}

void TestSettingsParsesChangeNotifications() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\nchangeNotifications: false\n", settings));
    EXPECT_TRUE(!settings.changeNotifications);
}

void TestSettingsParsesTopBarEnabled() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\ntopBarEnabled: true\n", settings));
    EXPECT_TRUE(settings.topBarEnabled);
}

void TestSettingsParsesInnerGap() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\ninnerGap: 12\nouterGap: 18\n", settings));
    EXPECT_EQ(settings.innerGap, 12);
    EXPECT_EQ(settings.outerGap, 18);
}

void TestSettingsParsesTopBarHeight() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\ntopBarHeight: 32\n", settings));
    EXPECT_EQ(settings.topBarHeight, 32);
}

void TestSettingsParsesTopBarWidgets() {
    Settings settings;
    const std::string yaml =
        "version: 1\n"
        "topBarWidgets:\n"
        "  clock: left\n"
        "  date: disabled\n"
        "  appName: right\n"
        "  layoutType: left\n"
        "  workspaces: right\n";

    EXPECT_TRUE(Settings::LoadFromYaml(yaml, settings));
    EXPECT_EQ(settings.topBarWidgets.clock, TopBarWidgetPosition::Left);
    EXPECT_EQ(settings.topBarWidgets.date, TopBarWidgetPosition::Disabled);
    EXPECT_EQ(settings.topBarWidgets.appName, TopBarWidgetPosition::Right);
    EXPECT_EQ(settings.topBarWidgets.layoutType, TopBarWidgetPosition::Left);
    EXPECT_EQ(settings.topBarWidgets.workspaces, TopBarWidgetPosition::Right);
    EXPECT_EQ(settings.topBarWidgets.order.size(), 5u);
    EXPECT_EQ(settings.topBarWidgets.order[0], TopBarWidgetKind::Clock);
    EXPECT_EQ(settings.topBarWidgets.order[1], TopBarWidgetKind::Date);
    EXPECT_EQ(settings.topBarWidgets.order[2], TopBarWidgetKind::AppName);
    EXPECT_EQ(settings.topBarWidgets.order[3], TopBarWidgetKind::LayoutType);
    EXPECT_EQ(settings.topBarWidgets.order[4], TopBarWidgetKind::Workspaces);
}

void TestSettingsIgnoresRemovedMemorySettings() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\nrememberWindowPlacements: false\nrememberMonitorSplits: true\n", settings));
}

void TestSettingsParsesLegacyMasterWidthKey() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\ndefaultMasterWidthRatio: 0.65\n", settings));
    EXPECT_NEAR(settings.defaultMainWidthRatio, 0.65f, 0.0001f);
}

void TestSettingsParsesDefaultLayoutType() {
    Settings settings;
    EXPECT_TRUE(Settings::LoadFromYaml("version: 1\ndefaultLayoutType: main_stack\n", settings));
    EXPECT_EQ(settings.defaultLayoutMode, LayoutMode::MainStack);
}

void TestSettingsDefaultsIncludeBuiltInValues() {
    const Settings settings = Settings::Defaults();
    const auto hasRule = [&](const wchar_t* className, const wchar_t* windowTitle, const wchar_t* processName) {
        for (const auto& rule : settings.windowRules) {
            if (rule.action != quicktile::WindowRuleAction::Float) {
                continue;
            }

            if (rule.className == className && rule.windowTitle == windowTitle && rule.processName == processName) {
                return true;
            }
        }

        return false;
    };

    EXPECT_TRUE(hasRule(L"#32770", L"", L""));
    EXPECT_TRUE(hasRule(L"", L"", L"msiexec.exe"));
    EXPECT_TRUE(hasRule(L"Xaml_WindowedPopupClass", L"Windows Security", L""));
    EXPECT_TRUE(hasRule(L"", L"", L"PowerToys.Settings.exe"));
    EXPECT_TRUE(hasRule(L"", L"", L"Microsoft.CmdPal.UI.exe"));
    EXPECT_TRUE(hasRule(L"", L"", L"Taskmgr.exe"));
    EXPECT_EQ(settings.innerGap, 2);
    EXPECT_EQ(settings.outerGap, 4);
    EXPECT_EQ(settings.topBarHeight, 22);
    EXPECT_EQ(settings.defaultLayoutMode, LayoutMode::Spiral);
    EXPECT_EQ(settings.shortcuts.toggleTiling.size(), 1u);
    EXPECT_EQ(settings.shortcuts.toggleTiling[0], std::wstring(L"Alt+T"));
    EXPECT_EQ(settings.shortcuts.toggleTopBar.size(), 1u);
    EXPECT_EQ(settings.shortcuts.toggleTopBar[0], std::wstring(L"Alt+Shift+B"));
    EXPECT_EQ(settings.shortcuts.layoutFloating[0], std::wstring(L"Alt+Shift+1"));
    EXPECT_EQ(settings.shortcuts.layoutMainStack[0], std::wstring(L"Alt+Shift+2"));
    EXPECT_EQ(settings.shortcuts.layoutVerticalColumns[0], std::wstring(L"Alt+Shift+3"));
    EXPECT_EQ(settings.shortcuts.layoutMonocle[0], std::wstring(L"Alt+Shift+5"));
    EXPECT_EQ(settings.shortcuts.layoutSpiral[0], std::wstring(L"Alt+Shift+4"));
    EXPECT_EQ(settings.launchShortcuts.size(), 3u);
    EXPECT_EQ(settings.launchShortcuts[0].friendlyName, std::wstring(L"Edge"));
    EXPECT_EQ(settings.launchShortcuts[0].launchCommand, std::wstring(L"microsoft-edge:"));
    EXPECT_EQ(settings.launchShortcuts[0].shortcut, std::wstring(L"Alt+Ctrl+B"));
    EXPECT_EQ(settings.launchShortcuts[1].friendlyName, std::wstring(L"Terminal"));
    EXPECT_EQ(settings.launchShortcuts[1].launchCommand, std::wstring(L"wt.exe"));
    EXPECT_EQ(settings.launchShortcuts[1].shortcut, std::wstring(L"Alt+Ctrl+T"));
    EXPECT_EQ(settings.launchShortcuts[2].friendlyName, std::wstring(L"VS Code"));
    EXPECT_EQ(settings.launchShortcuts[2].launchCommand, std::wstring(L"code"));
    EXPECT_EQ(settings.launchShortcuts[2].shortcut, std::wstring(L"Alt+Ctrl+C"));
    EXPECT_EQ(settings.shortcuts.growLeft[0], std::wstring(L"Alt+Ctrl+Left"));
    EXPECT_EQ(settings.shortcuts.growUp[0], std::wstring(L"Alt+Ctrl+Up"));
    EXPECT_EQ(settings.shortcuts.growRight[0], std::wstring(L"Alt+Ctrl+Right"));
    EXPECT_EQ(settings.shortcuts.growDown[0], std::wstring(L"Alt+Ctrl+Down"));
    EXPECT_EQ(settings.shortcuts.shrinkLeft[0], std::wstring(L"Alt+Ctrl+Shift+Left"));
    EXPECT_EQ(settings.shortcuts.shrinkUp[0], std::wstring(L"Alt+Ctrl+Shift+Up"));
    EXPECT_EQ(settings.shortcuts.shrinkRight[0], std::wstring(L"Alt+Ctrl+Shift+Right"));
    EXPECT_EQ(settings.shortcuts.shrinkDown[0], std::wstring(L"Alt+Ctrl+Shift+Down"));
    EXPECT_EQ(settings.shortcuts.showHelp[0], std::wstring(L"Alt+Shift+F1"));
    EXPECT_EQ(settings.shortcuts.inspectWindow[0], std::wstring(L"Alt+Shift+I"));
    EXPECT_EQ(settings.shortcuts.switchWorkspace1[0], std::wstring(L"Alt+Ctrl+1"));
    EXPECT_EQ(settings.shortcuts.switchWorkspace2[0], std::wstring(L"Alt+Ctrl+2"));
    EXPECT_EQ(settings.shortcuts.switchWorkspace3[0], std::wstring(L"Alt+Ctrl+3"));
    EXPECT_EQ(settings.shortcuts.switchWorkspace4[0], std::wstring(L"Alt+Ctrl+4"));
    EXPECT_EQ(settings.shortcuts.switchWorkspace5[0], std::wstring(L"Alt+Ctrl+5"));
    EXPECT_TRUE(settings.topBarEnabled);
    EXPECT_EQ(settings.topBarWidgets.clock, TopBarWidgetPosition::Right);
    EXPECT_EQ(settings.topBarWidgets.date, TopBarWidgetPosition::Right);
    EXPECT_EQ(settings.topBarWidgets.appName, TopBarWidgetPosition::Center);
    EXPECT_EQ(settings.topBarWidgets.layoutType, TopBarWidgetPosition::Left);
    EXPECT_EQ(settings.topBarWidgets.workspaces, TopBarWidgetPosition::Left);
    EXPECT_EQ(settings.topBarWidgets.order.size(), 5u);
    EXPECT_EQ(settings.topBarWidgets.order[0], TopBarWidgetKind::Clock);
    EXPECT_EQ(settings.topBarWidgets.order[1], TopBarWidgetKind::Date);
    EXPECT_EQ(settings.topBarWidgets.order[2], TopBarWidgetKind::AppName);
    EXPECT_EQ(settings.topBarWidgets.order[3], TopBarWidgetKind::LayoutType);
    EXPECT_EQ(settings.topBarWidgets.order[4], TopBarWidgetKind::Workspaces);
}

void TestSettingsReportsYamlErrors() {
    Settings settings;
    std::wstring errorMessage;
    EXPECT_TRUE(!Settings::LoadFromYaml("version: 1\nautoStart: maybe\n", settings, &errorMessage));
    EXPECT_TRUE(errorMessage.find(L"line 2") != std::wstring::npos);
}

void TestSettingsRejectsLegacyFloatingSettings() {
    Settings settings;
    std::wstring errorMessage;
    const std::string yaml =
        "version: 1\n"
        "defaultFloatingProcesses:\n"
        "  - \"custom.exe\"\n"
        "  - \"custom.exe\"\n";

    EXPECT_TRUE(!Settings::LoadFromYaml(yaml, settings, &errorMessage));
    EXPECT_TRUE(errorMessage.find(L"legacy floating settings") != std::wstring::npos);
}

void TestSettingsParsesWindowRules() {
    Settings settings;
    const std::string yaml =
        "version: 1\n"
        "windowRules:\n"
        "  - action: \"float\"\n"
        "    className: \"DialogClass\"\n"
        "    processName: \"Code.exe\"\n";

    EXPECT_TRUE(Settings::LoadFromYaml(yaml, settings));
    EXPECT_EQ(settings.windowRules.size(), 1u);
    EXPECT_EQ(settings.windowRules[0].className, std::wstring(L"DialogClass"));
    EXPECT_EQ(settings.windowRules[0].processName, std::wstring(L"Code.exe"));
    EXPECT_EQ(settings.windowRules[0].action, quicktile::WindowRuleAction::Float);
}

void TestSettingsRejectsUnknownWindowRuleAction() {
    Settings settings;
    std::wstring errorMessage;
    const std::string yaml =
        "version: 1\n"
        "windowRules:\n"
        "  - action: \"main\"\n"
        "    processName: \"Code.exe\"\n";

    EXPECT_TRUE(!Settings::LoadFromYaml(yaml, settings, &errorMessage));
    EXPECT_TRUE(errorMessage.find(L"line 3") != std::wstring::npos);
}

void TestShortcutsParseBinding() {
    ShortcutBinding binding;
    std::wstring errorMessage;
    EXPECT_TRUE(Shortcuts::TryParseBinding(L"Alt+Shift+W", binding, &errorMessage));
    EXPECT_EQ(binding.modifiers, static_cast<UINT>(MOD_ALT | MOD_SHIFT));
    EXPECT_EQ(binding.virtualKey, static_cast<UINT>('W'));

    EXPECT_TRUE(Shortcuts::TryParseBinding(L"Ctrl+Left", binding, &errorMessage));
    EXPECT_EQ(binding.modifiers, static_cast<UINT>(MOD_CONTROL));
    EXPECT_EQ(binding.virtualKey, static_cast<UINT>(VK_LEFT));
}

void TestShortcutsRejectInvalidBinding() {
    ShortcutBinding binding;
    std::wstring errorMessage;
    EXPECT_TRUE(!Shortcuts::TryParseBinding(L"Alt+Shift", binding, &errorMessage));
    EXPECT_TRUE(!errorMessage.empty());
}

void TestSettingsParsesShortcutSection() {
    Settings settings;
    const std::string yaml =
        "version: 1\n"
        "launchShortcuts:\n"
        "  - friendly_name: \"Browser\"\n"
        "    launch_command: \"firefox.exe\"\n"
        "    shortcut: \"Ctrl+Alt+B\"\n"
        "  - friendly_name: \"Editor\"\n"
        "    launch_command: \"code --reuse-window\"\n"
        "    shortcut: \"Ctrl+Alt+C\"\n"
        "shortcuts:\n"
        "  toggleTiling:\n"
        "    - \"Ctrl+Shift+T\"\n"
        "  toggleTopBar:\n"
        "    - \"Ctrl+Shift+B\"\n"
        "  focusLeft:\n"
        "    - \"Ctrl+Alt+Left\"\n"
        "    - \"Ctrl+Alt+H\"\n"
        "  growLeft:\n"
        "    - \"Ctrl+Alt+R+Left\"\n"
        "  shrinkLeft:\n"
        "    - \"Ctrl+Alt+Shift+R+Left\"\n"
        "  layoutFloating:\n"
        "    - \"Ctrl+Alt+1\"\n"
        "  layoutMainStack:\n"
        "    - \"Ctrl+Alt+2\"\n"
        "  layoutVerticalColumns:\n"
        "    - \"Ctrl+Alt+3\"\n"
        "  layoutMonocle:\n"
        "    - \"Ctrl+Alt+4\"\n"
        "  layoutSpiral:\n"
        "    - \"Ctrl+Alt+5\"\n"
        "  showHelp:\n"
        "    - \"Ctrl+Alt+F1\"\n"
        "  inspectWindow:\n"
        "    - \"Ctrl+Alt+I\"\n"
        "  exit:\n"
        "    - \"Ctrl+Alt+Q\"\n";

    EXPECT_TRUE(Settings::LoadFromYaml(yaml, settings));
    EXPECT_EQ(settings.shortcuts.toggleTiling.size(), 1u);
    EXPECT_EQ(settings.shortcuts.toggleTiling[0], std::wstring(L"Ctrl+Shift+T"));
    EXPECT_EQ(settings.shortcuts.toggleTopBar.size(), 1u);
    EXPECT_EQ(settings.shortcuts.toggleTopBar[0], std::wstring(L"Ctrl+Shift+B"));
    EXPECT_EQ(settings.shortcuts.focusLeft.size(), 2u);
    EXPECT_EQ(settings.shortcuts.focusLeft[0], std::wstring(L"Ctrl+Alt+Left"));
    EXPECT_EQ(settings.shortcuts.focusLeft[1], std::wstring(L"Ctrl+Alt+H"));
    EXPECT_EQ(settings.shortcuts.growLeft[0], std::wstring(L"Ctrl+Alt+R+Left"));
    EXPECT_EQ(settings.shortcuts.shrinkLeft[0], std::wstring(L"Ctrl+Alt+Shift+R+Left"));
    EXPECT_EQ(settings.shortcuts.layoutFloating[0], std::wstring(L"Ctrl+Alt+1"));
    EXPECT_EQ(settings.shortcuts.layoutMainStack[0], std::wstring(L"Ctrl+Alt+2"));
    EXPECT_EQ(settings.shortcuts.layoutVerticalColumns[0], std::wstring(L"Ctrl+Alt+3"));
    EXPECT_EQ(settings.shortcuts.layoutMonocle[0], std::wstring(L"Ctrl+Alt+4"));
    EXPECT_EQ(settings.shortcuts.layoutSpiral[0], std::wstring(L"Ctrl+Alt+5"));
    EXPECT_EQ(settings.launchShortcuts.size(), 2u);
    EXPECT_EQ(settings.launchShortcuts[0].friendlyName, std::wstring(L"Browser"));
    EXPECT_EQ(settings.launchShortcuts[0].launchCommand, std::wstring(L"firefox.exe"));
    EXPECT_EQ(settings.launchShortcuts[0].shortcut, std::wstring(L"Ctrl+Alt+B"));
    EXPECT_EQ(settings.launchShortcuts[1].friendlyName, std::wstring(L"Editor"));
    EXPECT_EQ(settings.launchShortcuts[1].launchCommand, std::wstring(L"code --reuse-window"));
    EXPECT_EQ(settings.launchShortcuts[1].shortcut, std::wstring(L"Ctrl+Alt+C"));
    EXPECT_EQ(settings.shortcuts.showHelp[0], std::wstring(L"Ctrl+Alt+F1"));
    EXPECT_EQ(settings.shortcuts.inspectWindow[0], std::wstring(L"Ctrl+Alt+I"));
    EXPECT_EQ(settings.shortcuts.exit.size(), 1u);
    EXPECT_EQ(settings.shortcuts.exit[0], std::wstring(L"Ctrl+Alt+Q"));
    EXPECT_EQ(settings.shortcuts.retile[0], std::wstring(L"Alt+Shift+W"));
}

void TestSettingsRejectsInvalidLaunchShortcutBinding() {
    Settings settings;
    std::wstring errorMessage;
    const std::string yaml =
        "version: 1\n"
        "launchShortcuts:\n"
        "  - friendly_name: \"Broken\"\n"
        "    launch_command: \"broken.exe\"\n"
        "    shortcut: \"Alt+Shift\"\n";

    EXPECT_TRUE(!Settings::LoadFromYaml(yaml, settings, &errorMessage));
    EXPECT_TRUE(errorMessage.find(L"line 5") != std::wstring::npos);
}

void TestSettingsRejectsInvalidShortcutBinding() {
    Settings settings;
    std::wstring errorMessage;
    const std::string yaml =
        "version: 1\n"
        "shortcuts:\n"
        "  toggleTiling:\n"
        "    - \"Alt+Shift\"\n";

    EXPECT_TRUE(!Settings::LoadFromYaml(yaml, settings, &errorMessage));
    EXPECT_TRUE(errorMessage.find(L"line 4") != std::wstring::npos);
}

void TestEventRouterClassifiesEvents() {
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_SYSTEM_MOVESIZESTART, 0, 0), EventRouter::Action::MoveSizeStart);
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_SYSTEM_MINIMIZESTART, 0, 0), EventRouter::Action::WindowStateChanged);
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_SYSTEM_MINIMIZEEND, 0, 0), EventRouter::Action::WindowStateChanged);
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_OBJECT_SHOW, OBJID_WINDOW, CHILDID_SELF), EventRouter::Action::WindowShown);
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_OBJECT_HIDE, OBJID_WINDOW, CHILDID_SELF), EventRouter::Action::WindowHiddenOrDestroyed);
    EXPECT_EQ(EventRouter::ClassifyEvent(EVENT_OBJECT_LOCATIONCHANGE, OBJID_CLIENT, CHILDID_SELF), EventRouter::Action::Ignore);
}

void TestEventRouterMonitorChangeDecisions() {
    const HWND moveSizeWindow = FakeHandle<HWND>(1);
    const HMONITOR leftMonitor = FakeHandle<HMONITOR>(101);
    const HMONITOR rightMonitor = FakeHandle<HMONITOR>(202);

    EXPECT_TRUE(!EventRouter::ShouldHandleLocationChange(moveSizeWindow, leftMonitor, rightMonitor));
    EXPECT_TRUE(!EventRouter::ShouldHandleLocationChange(nullptr, nullptr, rightMonitor));
    EXPECT_TRUE(!EventRouter::ShouldHandleLocationChange(nullptr, leftMonitor, leftMonitor));
    EXPECT_TRUE(EventRouter::ShouldHandleLocationChange(nullptr, leftMonitor, rightMonitor));

    EXPECT_TRUE(!EventRouter::ShouldTileSourceMonitorAfterWindowMove(nullptr, rightMonitor));
    EXPECT_TRUE(!EventRouter::ShouldTileSourceMonitorAfterWindowMove(leftMonitor, leftMonitor));
    EXPECT_TRUE(EventRouter::ShouldTileSourceMonitorAfterWindowMove(leftMonitor, rightMonitor));

    EXPECT_TRUE(EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(leftMonitor, leftMonitor, rightMonitor));
    EXPECT_TRUE(EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(rightMonitor, leftMonitor, rightMonitor));
    EXPECT_TRUE(!EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(nullptr, leftMonitor, rightMonitor));
    EXPECT_TRUE(!EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(leftMonitor, leftMonitor, leftMonitor));
    EXPECT_TRUE(!EventRouter::ShouldRestoreMaximizedWindowsBeforeMonitorRetile(FakeHandle<HMONITOR>(303), leftMonitor, rightMonitor));
}

void TestEventRouterRetileDecisionsUseClassifiedWindows() {
    const HMONITOR monitor = FakeHandle<HMONITOR>(101);
    const std::vector<HWND> unchangedPrevious = {FakeHandle<HWND>(1), FakeHandle<HWND>(2)};
    const std::vector<HWND> unchangedClassified = {FakeHandle<HWND>(2), FakeHandle<HWND>(1)};
    const std::vector<HWND> removedClassified = {FakeHandle<HWND>(2)};
    const std::vector<HWND> addedClassified = {FakeHandle<HWND>(1), FakeHandle<HWND>(2), FakeHandle<HWND>(3)};

    EXPECT_TRUE(!EventRouter::ShouldTileForWindowClassificationChange(monitor, unchangedPrevious, unchangedClassified));
    EXPECT_TRUE(EventRouter::ShouldTileForWindowClassificationChange(monitor, unchangedPrevious, removedClassified));
    EXPECT_TRUE(EventRouter::ShouldTileForWindowClassificationChange(monitor, unchangedPrevious, addedClassified));
}

void TestSingleInstanceGuardClassifiesAlreadyRunningErrors() {
    EXPECT_TRUE(SingleInstanceGuard::IsAlreadyRunningError(ERROR_ALREADY_EXISTS));
    EXPECT_TRUE(SingleInstanceGuard::IsAlreadyRunningError(ERROR_ACCESS_DENIED));
    EXPECT_TRUE(!SingleInstanceGuard::IsAlreadyRunningError(ERROR_SUCCESS));
    EXPECT_TRUE(!SingleInstanceGuard::IsAlreadyRunningError(ERROR_FILE_NOT_FOUND));
}

struct TestCase {
    const char* name;
    void (*run)();
};

}  // namespace

int main() noexcept {
    try {
        const std::vector<TestCase> tests = {
            {"WorkspaceModelFocusNavigation", TestWorkspaceModelFocusNavigation},
            {"WorkspaceModelMovePlans", TestWorkspaceModelMovePlans},
            {"WorkspaceModelResizePlanAndLayout", TestWorkspaceModelResizePlanAndLayout},
            {"DirectionResizeAdjustStackWindow", TestDirectionResizeAdjustStackWindow},
            {"LayoutEngineAdjustWeightedWindowLengths", TestLayoutEngineAdjustWeightedWindowLengths},
            {"WorkspaceModelAlternativeLayouts", TestWorkspaceModelAlternativeLayouts},
            {"WorkspaceModelSpiralLayout", TestWorkspaceModelSpiralLayout},
            {"LayoutEngineGenericWidthAdjustmentUsesActualWidths", TestLayoutEngineGenericWidthAdjustmentUsesActualWidths},
            {"LayoutEngineWeightsAndSync", TestLayoutEngineWeightsAndSync},
            {"LayoutEngineGenericHeightAdjustmentUsesActualHeights", TestLayoutEngineGenericHeightAdjustmentUsesActualHeights},
            {"LayoutEngineGenericHeightIgnoresMinimumsDuringResize", TestLayoutEngineGenericHeightIgnoresMinimumsDuringResize},
            {"WindowManagerVirtualDesktopContextSwap", TestWindowManagerVirtualDesktopContextSwap},
            {"WindowManagerInitializesMonitorWorkspaces", TestWindowManagerInitializesMonitorWorkspaces},
            {"WorkspaceModelDistributeLengths", TestWorkspaceModelDistributeLengths},
            {"WindowClassifierExceptionRules", TestWindowClassifierExceptionRules},
            {"WindowClassifierBuiltInTransientIdentities", TestWindowClassifierBuiltInTransientIdentities},
            {"WindowClassifierExceptionMatchSource", TestWindowClassifierExceptionMatchSource},
            {"WindowClassifierWindowRules", TestWindowClassifierWindowRules},
            {"ToggleProcessFloatingRuleUsesWindowRules", TestToggleProcessFloatingRuleUsesWindowRules},
            {"ToggleProcessFloatingRuleRemovesDuplicateFloatRules", TestToggleProcessFloatingRuleRemovesDuplicateFloatRules},
            {"LayoutEngineBuildOrderedWindowsPreservesPreviousOrder", TestLayoutEngineBuildOrderedWindowsPreservesPreviousOrder},
            {"LayoutEngineBuildOrderedWindowsAppendsNewWindowsAfterKnownOrder", TestLayoutEngineBuildOrderedWindowsAppendsNewWindowsAfterKnownOrder},
            {"SettingsParsesAutoStart", TestSettingsParsesAutoStart},
            {"SettingsParsesChangeNotifications", TestSettingsParsesChangeNotifications},
            {"SettingsParsesTopBarEnabled", TestSettingsParsesTopBarEnabled},
            {"SettingsParsesInnerGap", TestSettingsParsesInnerGap},
            {"SettingsParsesTopBarHeight", TestSettingsParsesTopBarHeight},
            {"SettingsParsesTopBarWidgets", TestSettingsParsesTopBarWidgets},
            {"SettingsIgnoresRemovedMemorySettings", TestSettingsIgnoresRemovedMemorySettings},
            {"SettingsParsesLegacyMasterWidthKey", TestSettingsParsesLegacyMasterWidthKey},
            {"SettingsParsesDefaultLayoutType", TestSettingsParsesDefaultLayoutType},
            {"SettingsDefaultsIncludeBuiltInValues", TestSettingsDefaultsIncludeBuiltInValues},
            {"SettingsReportsYamlErrors", TestSettingsReportsYamlErrors},
            {"SettingsRejectsLegacyFloatingSettings", TestSettingsRejectsLegacyFloatingSettings},
            {"SettingsParsesWindowRules", TestSettingsParsesWindowRules},
            {"SettingsRejectsUnknownWindowRuleAction", TestSettingsRejectsUnknownWindowRuleAction},
            {"ShortcutsParseBinding", TestShortcutsParseBinding},
            {"ShortcutsRejectInvalidBinding", TestShortcutsRejectInvalidBinding},
            {"SettingsParsesShortcutSection", TestSettingsParsesShortcutSection},
            {"SettingsRejectsInvalidLaunchShortcutBinding", TestSettingsRejectsInvalidLaunchShortcutBinding},
            {"SettingsRejectsInvalidShortcutBinding", TestSettingsRejectsInvalidShortcutBinding},
            {"EventRouterClassifiesEvents", TestEventRouterClassifiesEvents},
            {"EventRouterMonitorChangeDecisions", TestEventRouterMonitorChangeDecisions},
            {"EventRouterRetileDecisionsUseClassifiedWindows", TestEventRouterRetileDecisionsUseClassifiedWindows},
            {"SingleInstanceGuardClassifiesAlreadyRunningErrors", TestSingleInstanceGuardClassifiesAlreadyRunningErrors},
        };

        int failed = 0;
        for (const TestCase& test : tests) {
            try {
                test.run();
                std::cout << "PASS " << test.name << '\n';
            } catch (const std::exception& exception) {
                ++failed;
                std::cerr << "FAIL " << test.name << ": " << exception.what() << '\n';
            }
        }

        if (failed != 0) {
            std::cerr << failed << " test(s) failed\n";
            return 1;
        }

        std::cout << tests.size() << " test(s) passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL test runner: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL test runner: unknown exception\n";
        return 1;
    }
}
