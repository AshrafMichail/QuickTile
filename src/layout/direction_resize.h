#pragma once

#include "app/app_state.h"

namespace quicktile {

class DirectionResize {
public:
    static bool AdjustStackPair(std::vector<float>& stackWeights, std::size_t growIndex, std::size_t shrinkIndex, float delta);
    static bool AdjustStackWindow(std::vector<float>& stackWeights, std::size_t targetIndex, bool grow, float delta);
};

}  // namespace quicktile
