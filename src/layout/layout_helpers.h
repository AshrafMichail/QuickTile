#pragma once

#include "windows/window_manager.h"

#include <algorithm>
#include <cmath>

namespace quicktile::LayoutHelpers {

enum class Axis {
    Horizontal,
    Vertical,
};

struct DirectionalMetrics {
    bool matchesDirection = true;
    bool overlapsAxis = false;
    int primaryDistance = 0;
    int secondaryDistance = 0;
};

inline RECT MakeRect(int left, int top, int right, int bottom) {
    RECT rect{};
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    return rect;
}

inline int AxisLength(const RECT& rect, Axis axis) {
    return axis == Axis::Horizontal
        ? std::max(1, static_cast<int>(rect.right - rect.left))
        : std::max(1, static_cast<int>(rect.bottom - rect.top));
}

inline RECT ChildRect(const RECT& rect, Axis axis, int start, int length) {
    if (axis == Axis::Horizontal) {
        return MakeRect(rect.left + start, rect.top, rect.left + start + length, rect.bottom);
    }

    return MakeRect(rect.left, rect.top + start, rect.right, rect.top + start + length);
}

inline POINT RectCenter(const RECT& rect) {
    POINT point{};
    point.x = rect.left + ((rect.right - rect.left) / 2);
    point.y = rect.top + ((rect.bottom - rect.top) / 2);
    return point;
}

inline bool ContainsPoint(const RECT& rect, const POINT& point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

inline long long SquaredDistance(const POINT& left, const POINT& right) {
    const long long deltaX = static_cast<long long>(left.x) - static_cast<long long>(right.x);
    const long long deltaY = static_cast<long long>(left.y) - static_cast<long long>(right.y);
    return (deltaX * deltaX) + (deltaY * deltaY);
}

inline bool OverlapsOnVerticalAxis(const RECT& left, const RECT& right) {
    return left.top < right.bottom && right.top < left.bottom;
}

inline bool OverlapsOnHorizontalAxis(const RECT& top, const RECT& bottom) {
    return top.left < bottom.right && bottom.left < top.right;
}

inline Axis AxisForDirection(WindowManager::FocusDirection direction) {
    switch (direction) {
    case WindowManager::FocusDirection::Left:
    case WindowManager::FocusDirection::Right:
        return Axis::Horizontal;
    case WindowManager::FocusDirection::Up:
    case WindowManager::FocusDirection::Down:
        return Axis::Vertical;
    }

    return Axis::Horizontal;
}

inline bool IsNegativeDirection(WindowManager::FocusDirection direction) {
    return direction == WindowManager::FocusDirection::Left || direction == WindowManager::FocusDirection::Up;
}

inline DirectionalMetrics MeasureDirectionalMetrics(
    const RECT& referenceRect,
    const RECT& candidateRect,
    WindowManager::FocusDirection direction) {
    const POINT referenceCenter = RectCenter(referenceRect);
    const POINT candidateCenter = RectCenter(candidateRect);

    switch (direction) {
    case WindowManager::FocusDirection::Left:
        return DirectionalMetrics{
            candidateCenter.x < referenceCenter.x,
            OverlapsOnVerticalAxis(referenceRect, candidateRect),
            referenceCenter.x - candidateCenter.x,
            std::abs(candidateCenter.y - referenceCenter.y),
        };
    case WindowManager::FocusDirection::Right:
        return DirectionalMetrics{
            candidateCenter.x > referenceCenter.x,
            OverlapsOnVerticalAxis(referenceRect, candidateRect),
            candidateCenter.x - referenceCenter.x,
            std::abs(candidateCenter.y - referenceCenter.y),
        };
    case WindowManager::FocusDirection::Up:
        return DirectionalMetrics{
            candidateCenter.y < referenceCenter.y,
            OverlapsOnHorizontalAxis(referenceRect, candidateRect),
            referenceCenter.y - candidateCenter.y,
            std::abs(candidateCenter.x - referenceCenter.x),
        };
    case WindowManager::FocusDirection::Down:
        return DirectionalMetrics{
            candidateCenter.y > referenceCenter.y,
            OverlapsOnHorizontalAxis(referenceRect, candidateRect),
            candidateCenter.y - referenceCenter.y,
            std::abs(candidateCenter.x - referenceCenter.x),
        };
    }

    return {};
}

inline bool IsBetterDirectionalCandidate(const DirectionalMetrics& candidate, bool hasBest, const DirectionalMetrics& best) {
    if (!hasBest) {
        return true;
    }

    if (candidate.overlapsAxis != best.overlapsAxis) {
        return candidate.overlapsAxis;
    }

    if (candidate.primaryDistance != best.primaryDistance) {
        return candidate.primaryDistance < best.primaryDistance;
    }

    return candidate.secondaryDistance < best.secondaryDistance;
}

}  // namespace quicktile::LayoutHelpers