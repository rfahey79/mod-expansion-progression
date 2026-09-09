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
constexpr std::uint32_t ClassicMaximumLevel = 60;
constexpr std::uint32_t BurningCrusadeMaximumLevel = 70;
constexpr std::uint8_t LfgTypeRandom = 6;
constexpr std::uint32_t RandomClassic = 258;
constexpr std::uint32_t RandomBurningCrusade = 259;
constexpr std::uint32_t RandomBurningCrusadeHeroic = 260;
constexpr std::uint32_t RandomWrath = 261;
constexpr std::uint32_t RandomWrathHeroic = 262;

enum class ExpansionPhase : std::uint8_t
{
    Classic = 0,
    BurningCrusade = 1,
    Wrath = 2
};

inline ExpansionPhase PhaseForCap(std::uint32_t cap)
{
    if (cap <= ClassicMaximumLevel)
        return ExpansionPhase::Classic;
    if (cap <= BurningCrusadeMaximumLevel)
        return ExpansionPhase::BurningCrusade;
    return ExpansionPhase::Wrath;
}

inline bool ShouldLockLfgDungeon(bool enabled, bool lfgEnabled, bool lockFuture,
    bool restrictSpecific, std::uint32_t cap,
    std::uint8_t dungeonExpansion, std::uint8_t dungeonType)
{
    if (!enabled || !lfgEnabled || !lockFuture ||
        dungeonExpansion <= static_cast<std::uint8_t>(PhaseForCap(cap)))
        return false;

    // The 3.3.5 client stops offering Random Classic at higher levels and sends
    // Random TBC/Wrath instead. Those category entries must remain selectable
    // so OnPlayerQueueRandomDungeon can translate them to the active phase.
    return dungeonType != LfgTypeRandom && restrictSpecific;
}

inline std::uint32_t RestrictRandomDungeon(std::uint32_t dungeonId, ExpansionPhase phase)
{
    if (phase == ExpansionPhase::Classic)
        return RandomClassic;

    if (phase == ExpansionPhase::BurningCrusade)
    {
        if (dungeonId == RandomWrath)
            return RandomBurningCrusade;
        if (dungeonId == RandomWrathHeroic)
            return RandomBurningCrusadeHeroic;
    }

    return dungeonId;
}

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
