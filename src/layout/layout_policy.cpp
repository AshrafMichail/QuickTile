#include "layout/layout_policy.h"

#include "config/config.h"

#include <algorithm>

namespace quicktile::LayoutPolicy {

float ClampMainWidthRatio(float ratio) {
    return std::clamp(ratio, kMinMainWidthRatio, kMaxMainWidthRatio);
}

void NormalizeWeights(std::vector<float>& weights) {
    if (weights.empty()) {
        return;
    }

    float total = 0.0f;
    for (float& weight : weights) {
        if (weight < 0.0f) {
            weight = 0.0f;
        }

        total += weight;
    }

    if (total <= 0.0f) {
        const float equalWeight = 1.0f / static_cast<float>(weights.size());
        for (float& weight : weights) {
            weight = equalWeight;
        }
        return;
    }

    for (float& weight : weights) {
        weight /= total;
    }
}

std::vector<float> NormalizedWeights(std::size_t count, const std::vector<float>& sourceWeights, float fallbackWeight) {
    if (count == 0) {
        return {};
    }

    std::vector<float> weights(count, fallbackWeight);
    for (std::size_t index = 0; index < std::min(count, sourceWeights.size()); ++index) {
        if (sourceWeights[index] > 0.0f) {
            weights[index] = sourceWeights[index];
        }
    }

    NormalizeWeights(weights);
    return weights;
}

}  // namespace quicktile::LayoutPolicy