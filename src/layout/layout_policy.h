#pragma once

#include <vector>

namespace quicktile::LayoutPolicy {

float ClampMainWidthRatio(float ratio);
void NormalizeWeights(std::vector<float>& weights);
std::vector<float> NormalizedWeights(std::size_t count, const std::vector<float>& sourceWeights, float fallbackWeight = 1.0);

}  // namespace quicktile::LayoutPolicy