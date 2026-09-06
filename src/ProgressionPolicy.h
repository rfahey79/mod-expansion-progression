// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MOD_PROGRESSION_POLICY_H
#define MOD_PROGRESSION_POLICY_H

#include <algorithm>
#include <cstdint>

namespace Progression
{
constexpr std::uint32_t DefaultCap = 60;
constexpr std::uint32_t ClientMaximum = 80;
// AzerothCore's stock WotLK player_class_stats data begins Death Knights at
// level 55. Lowering CONFIG_START_HEROIC_PLAYER_LEVEL below this makes
// ObjectMgr abort while loading level stats.
constexpr std::uint32_t DeathKnightMinimumLevel = 55;

inline bool ValidCap(std::uint32_t cap, std::uint32_t coreMaximum)
{
    return cap >= 1 && cap <= std::min(coreMaximum, ClientMaximum);
}

inline bool CanGiveLevel(bool enabled, std::uint32_t cap,
    std::uint32_t currentLevel, std::uint32_t requestedLevel,
    std::uint32_t minimumLevel = 1)
{
    // Permit recovery/down-leveling of existing over-cap characters.
    return !enabled || (requestedLevel >= minimumLevel &&
        (requestedLevel <= cap || requestedLevel < currentLevel));
}

inline bool DiscardXP(bool enabled, std::uint32_t cap, std::uint32_t level)
{
    return enabled && level >= cap;
}
}
#endif
