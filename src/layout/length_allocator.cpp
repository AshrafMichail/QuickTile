#include "layout/length_allocator.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace quicktile::LengthAllocator {

std::vector<int> AllocateIgnoringMinimums(int totalLength, const std::vector<float>& preferences) {
    if (preferences.empty()) {
        return {};
    }

    totalLength = std::max(static_cast<int>(preferences.size()), totalLength);
    std::vector<int> lengths(preferences.size(), 1);
    const int remaining = totalLength - static_cast<int>(preferences.size());
    if (remaining <= 0) {
        return lengths;
    }

    std::vector<float> normalizedPreferences(preferences.size(), 0.0);
    float totalPreference = 0.0f;
    for (std::size_t index = 0; index < preferences.size(); ++index) {
        const float preference = preferences[index] > 0.0f ? preferences[index] : 0.0f;
        normalizedPreferences[index] = preference;
        totalPreference += preference;
    }

    if (totalPreference <= 0.0f) {
        const float equalPreference = 1.0f / static_cast<float>(normalizedPreferences.size());
        std::fill(normalizedPreferences.begin(), normalizedPreferences.end(), equalPreference);
    } else {
        for (float& preference : normalizedPreferences) {
            preference /= totalPreference;
        }
    }

    struct FractionalShare {
        std::size_t index = 0;
        float fraction = 0.0f;
    };

    std::vector<FractionalShare> fractionalShares;
    fractionalShares.reserve(normalizedPreferences.size());
    int distributed = 0;
    for (std::size_t index = 0; index < normalizedPreferences.size(); ++index) {
        const float exactShare = normalizedPreferences[index] * static_cast<float>(remaining);
        const int wholeShare = static_cast<int>(std::floor(exactShare));
        lengths[index] += wholeShare;
        distributed += wholeShare;
        fractionalShares.push_back(FractionalShare{index, exactShare - static_cast<float>(wholeShare)});
    }

    std::sort(fractionalShares.begin(), fractionalShares.end(), [](const FractionalShare& left, const FractionalShare& right) {
        if (left.fraction != right.fraction) {
            return left.fraction > right.fraction;
        }
        return left.index < right.index;
    });

    const int remainder = remaining - distributed;
    for (int extraIndex = 0; extraIndex < remainder; ++extraIndex) {
        ++lengths[fractionalShares[static_cast<std::size_t>(extraIndex % static_cast<int>(fractionalShares.size()))].index];
    }

    return lengths;
}

std::vector<int> ResolveLengthsWithMinimums(int totalLength, const std::vector<float>& preferences, const std::vector<int>& minimumLengths) {
    if (preferences.size() != minimumLengths.size()) {
        return {};
    }

    if (preferences.empty()) {
        return {};
    }

    totalLength = std::max(static_cast<int>(preferences.size()), totalLength);
    std::vector<int> clampedMinimums;
    clampedMinimums.reserve(minimumLengths.size());
    int minimumTotal = 0;
    for (int minimumLength : minimumLengths) {
        const int clampedMinimum = std::max(1, minimumLength);
        clampedMinimums.push_back(clampedMinimum);
        minimumTotal += clampedMinimum;
    }

    if (minimumTotal > totalLength) {
        std::vector<float> overflowPreferences;
        overflowPreferences.reserve(clampedMinimums.size());
        for (int minimumLength : clampedMinimums) {
            overflowPreferences.push_back(static_cast<float>(minimumLength));
        }
        return AllocateIgnoringMinimums(totalLength, overflowPreferences);
    }

    std::vector<int> lengths(preferences.size(), 0);
    std::vector<std::size_t> remainingIndices(preferences.size(), 0);
    std::iota(remainingIndices.begin(), remainingIndices.end(), 0);
    int remainingBudget = totalLength;

    while (!remainingIndices.empty()) {
        std::vector<float> remainingPreferences;
        remainingPreferences.reserve(remainingIndices.size());
        for (std::size_t index : remainingIndices) {
            remainingPreferences.push_back(preferences[index]);
        }

        const std::vector<int> proposedLengths = AllocateIgnoringMinimums(remainingBudget, remainingPreferences);

        std::vector<std::size_t> nextRemaining;
        nextRemaining.reserve(remainingIndices.size());
        bool fixedAny = false;
        for (std::size_t localIndex = 0; localIndex < remainingIndices.size(); ++localIndex) {
            const std::size_t originalIndex = remainingIndices[localIndex];
            if (proposedLengths[localIndex] < clampedMinimums[originalIndex]) {
                lengths[originalIndex] = clampedMinimums[originalIndex];
                remainingBudget -= clampedMinimums[originalIndex];
                fixedAny = true;
            } else {
                nextRemaining.push_back(originalIndex);
            }
        }

        if (!fixedAny) {
            for (std::size_t localIndex = 0; localIndex < remainingIndices.size(); ++localIndex) {
                lengths[remainingIndices[localIndex]] = proposedLengths[localIndex];
            }
            break;
        }

        remainingIndices = std::move(nextRemaining);
    }

    return lengths;
}

}  // namespace quicktile::LengthAllocator