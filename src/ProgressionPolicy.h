// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MOD_PROGRESSION_POLICY_H
#define MOD_PROGRESSION_POLICY_H

#include <algorithm>
#include <cstdint>

namespace Progression
{
constexpr std::uint32_t DefaultCap = 60;
constexpr std::uint32_t ClientMaximum = 80;

inline bool ValidCap(std::uint32_t cap, std::uint32_t coreMaximum)
{
    return cap >= 1 && cap <= std::min(coreMaximum, ClientMaximum);
}

inline bool CanGiveLevel(bool enabled, std::uint32_t cap,
    std::uint32_t currentLevel, std::uint32_t requestedLevel)
{
    // Permit recovery/down-leveling of existing over-cap characters.
    return !enabled || requestedLevel <= cap || requestedLevel < currentLevel;
}

inline bool DiscardXP(bool enabled, std::uint32_t cap, std::uint32_t level)
{
    return enabled && level >= cap;
}
}
#endif
