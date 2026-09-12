// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PROGRESSION_RAID_RESET_POLICY_H
#define PROGRESSION_RAID_RESET_POLICY_H

#include <cstdint>

namespace Progression::RaidReset
{
constexpr std::uint32_t Day = 86400;
constexpr std::uint32_t DefaultDays = 3;

inline std::uint32_t ValidDays(int days)
{
    return days >= 1 && days <= 365 ? static_cast<std::uint32_t>(days) : DefaultDays;
}

// Preserve the calendar phase, including a reset due exactly now. Never use
// the time a character enters or becomes bound as the start of a lockout.
inline std::int64_t NextReset(std::int64_t anchor, std::int64_t now, std::uint32_t period)
{
    if (anchor <= 0 || period == 0)
        return 0;
    if (anchor >= now)
        return anchor;
    return anchor + ((now - anchor + period - 1) / period) * period;
}
}
#endif
