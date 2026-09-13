// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PROGRESSION_RAID_RESET_POLICY_H
#define PROGRESSION_RAID_RESET_POLICY_H

#include <cstdint>
#include <ctime>

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

inline std::tm LocalCalendar(std::time_t value)
{
    std::tm calendar{};
#ifdef _WIN32
    localtime_s(&calendar, &value);
#else
    localtime_r(&value, &calendar);
#endif
    return calendar;
}

inline std::time_t LocalEpoch(std::tm calendar)
{
    calendar.tm_isdst = -1; // Let the OS timezone database determine the offset.
    return std::mktime(&calendar);
}

// UTC is used only to count civil dates without DST affecting their distance.
inline std::int64_t CalendarDay(std::tm calendar)
{
    calendar.tm_hour = 12;
    calendar.tm_min = calendar.tm_sec = 0;
#ifdef _WIN32
    return _mkgmtime(&calendar) / Day;
#else
    return timegm(&calendar) / Day;
#endif
}

// Move the old shared boundary to the first requested local hour at or after
// it. 04:00 UTC (=21:00 PDT) therefore becomes the following morning at 04:00
// PDT, without expiring active binds early. An already-aligned anchor is stable.
inline std::time_t AlignLocalHour(std::time_t anchor, int hour)
{
    std::tm calendar = LocalCalendar(anchor);
    calendar.tm_hour = hour;
    calendar.tm_min = calendar.tm_sec = 0;
    std::time_t aligned = LocalEpoch(calendar);
    if (aligned < anchor)
    {
        ++calendar.tm_mday;
        aligned = LocalEpoch(calendar);
    }
    return aligned;
}

inline std::time_t NextLocalReset(std::time_t anchor, std::time_t now, std::uint32_t days, int hour = -1)
{
    if (anchor <= 0 || !days)
        return 0;
    if (anchor >= now)
        return anchor;
    std::tm calendar = LocalCalendar(anchor);
    if (hour >= 0)
        calendar.tm_hour = hour;
    std::int64_t const elapsedDays = CalendarDay(LocalCalendar(now)) - CalendarDay(calendar);
    calendar.tm_mday += static_cast<int>((elapsedDays / days) * days);
    std::time_t next = LocalEpoch(calendar);
    if (next < now)
    {
        calendar.tm_mday += days;
        next = LocalEpoch(calendar);
    }
    return next;
}
}
#endif
