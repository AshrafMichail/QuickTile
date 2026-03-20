#include "layout/direction_resize.h"

#include "layout/layout_engine.h"

#include <algorithm>

namespace quicktile {

bool DirectionResize::AdjustStackPair(std::vector<float>& stackWeights, std::size_t growIndex, std::size_t shrinkIndex, float delta) {
    if (growIndex >= stackWeights.size() || shrinkIndex >= stackWeights.size() || delta <= 0.0) {
        return false;
    }

    LayoutEngine::NormalizeWeights(stackWeights);
    const float available = stackWeights[shrinkIndex] - kMinStackWeight;
    if (available <= 0.0) {
        return false;
    }

    const float appliedDelta = std::min(delta, available);
    stackWeights[growIndex] += appliedDelta;
    stackWeights[shrinkIndex] -= appliedDelta;
    LayoutEngine::NormalizeWeights(stackWeights);
    return true;
}

bool DirectionResize::AdjustStackWindow(std::vector<float>& stackWeights, std::size_t targetIndex, bool grow, float delta) {
    if (targetIndex >= stackWeights.size() || delta <= 0.0 || stackWeights.size() < 2) {
        return false;
    }

    LayoutEngine::NormalizeWeights(stackWeights);

    std::vector<std::size_t> neighborIndices;
    if (targetIndex > 0) {
        neighborIndices.push_back(targetIndex - 1);
    }
    if (targetIndex + 1 < stackWeights.size()) {
        neighborIndices.push_back(targetIndex + 1);
    }
    if (neighborIndices.empty()) {
        return false;
    }

    if (grow) {
        float remainingDelta = delta;
        bool changed = false;
        while (remainingDelta > 0.000001) {
            std::vector<std::size_t> activeNeighbors;
            for (std::size_t neighborIndex : neighborIndices) {
                if (stackWeights[neighborIndex] > kMinStackWeight + 0.000001) {
                    activeNeighbors.push_back(neighborIndex);
                }
            }

            if (activeNeighbors.empty()) {
                break;
            }

            const float requestedShare = remainingDelta / static_cast<float>(activeNeighbors.size());
            float transferred = 0.0;
            for (std::size_t neighborIndex : activeNeighbors) {
                const float available = stackWeights[neighborIndex] - kMinStackWeight;
                const float applied = std::min(requestedShare, available);
                if (applied <= 0.0) {
                    continue;
                }

                stackWeights[neighborIndex] -= applied;
                stackWeights[targetIndex] += applied;
                transferred += applied;
                changed = true;
            }

            if (transferred <= 0.0) {
                break;
            }

            remainingDelta -= transferred;
        }

        if (changed) {
            LayoutEngine::NormalizeWeights(stackWeights);
        }
        return changed;
    }

    const float available = stackWeights[targetIndex] - kMinStackWeight;
    if (available <= 0.0) {
        return false;
    }

    const float appliedDelta = std::min(delta, available);
    stackWeights[targetIndex] -= appliedDelta;
    const float distributedShare = appliedDelta / static_cast<float>(neighborIndices.size());
    for (std::size_t neighborIndex : neighborIndices) {
        stackWeights[neighborIndex] += distributedShare;
    }

    LayoutEngine::NormalizeWeights(stackWeights);
    return true;
}

}  // namespace quicktile
