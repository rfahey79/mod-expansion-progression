// SPDX-License-Identifier: GPL-2.0-or-later
#include "ProgressionRaidResetPolicy.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "World.h"
#include "WorldScript.h"

namespace
{
class ProgressionRaidResetScript final : public WorldScript
{
public:
    ProgressionRaidResetScript() : WorldScript("ProgressionRaidResetScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_INSTANCE_RESET_PERIOD,
          WORLDHOOK_ON_INSTANCE_RESET_SCHEDULE }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        bool const enabled = sConfigMgr->GetOption<bool>("Progression.OverrideRaidReset", true);
        int const days = sConfigMgr->GetOption<int>("Progression.RaidResetDays", 3);
        if (reload)
        {
            if (enabled != _enabled || Progression::RaidReset::ValidDays(days) != _days)
                LOG_WARN("module", "mod-progression: raid reset config changed; restart worldserver to apply it.");
            return;
        }
        _enabled = enabled;
        _days = Progression::RaidReset::ValidDays(days);
        if (days < 1 || days > 365)
            LOG_ERROR("module", "mod-progression: RaidResetDays must be 1..365; using 3.");
    }

    void OnInstanceResetPeriod(uint32 mapId, uint32 /*difficulty*/, uint32& period) override
    {
        if (_enabled && IsRaid(mapId))
            period = _days * Progression::RaidReset::Day;
    }

    void OnInstanceResetSchedule(uint32 mapId, uint32 /*difficulty*/, time_t& resetTime) override
    {
        if (!_enabled)
            return;

        // This hook runs after ALL persisted schedules load, but before any
        // InstanceSave, bind, or reset event is created. Snapshot anchors once
        // so iteration order cannot change the selected calendar.
        if (!_anchorLoaded)
        {
            _anchorLoaded = true;
            time_t const zg = sInstanceSaveMgr->GetResetTimeFor(309, Difficulty(0));
            time_t const aq = sInstanceSaveMgr->GetResetTimeFor(509, Difficulty(0));
            time_t const now = GameTime::GetGameTime().count();
            uint32 const period = _days * Progression::RaidReset::Day;
            time_t const nextZG = Progression::RaidReset::NextReset(zg, now, period);
            time_t const nextAQ = Progression::RaidReset::NextReset(aq, now, period);
            _anchor = nextZG ? nextZG : nextAQ;
            if (!_anchor)
            {
                // Bootstrap one calendar at the core's configured reset hour.
                _anchor = (now / Progression::RaidReset::Day) * Progression::RaidReset::Day
                    + period + sWorld->getIntConfig(CONFIG_INSTANCE_RESET_TIME_HOUR) * HOUR;
                LOG_WARN("module", "mod-progression: no ZG/AQ20 reset anchor; initializing shared calendar at {}.",
                    uint64(_anchor));
            }
            else
            {
                LOG_INFO("module", "mod-progression: raid resets every {} days; anchor map={}, next reset={}.",
                    _days, nextZG ? 309 : 509, uint64(_anchor));
                if (nextZG && nextAQ && nextZG != nextAQ)
                    LOG_WARN("module", "mod-progression: ZG/AQ20 calendars differ; using ZG for all raids.");
            }
        }
        if (_anchor && IsRaid(mapId))
            resetTime = _anchor;
    }

private:
    static bool IsRaid(uint32 mapId)
    {
        MapEntry const* map = sMapStore.LookupEntry(mapId);
        return map && map->IsRaid();
    }

    bool _enabled = true;
    bool _anchorLoaded = false;
    uint32 _days = Progression::RaidReset::DefaultDays;
    time_t _anchor = 0;
};
}

void AddProgressionRaidResetScripts()
{
    new ProgressionRaidResetScript();
}
