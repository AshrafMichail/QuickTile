#pragma once

#include <vector>

namespace quicktile::LengthAllocator {

std::vector<int> AllocateIgnoringMinimums(int totalLength, const std::vector<float>& preferences);
std::vector<int> ResolveLengthsWithMinimums(int totalLength, const std::vector<float>& preferences, const std::vector<int>& minimumLengths);

}  // namespace quicktile::LengthAllocator