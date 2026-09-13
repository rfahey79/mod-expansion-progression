# Shared raid calendar: deployment and acceptance

## What changes

`Progression.OverrideRaidReset = 1` enables all raid map/difficulty pairs that
already have global resets; `Progression.RaidResetDays = 3` sets their interval.
Both are independent of the leveling-cap configuration. Valid days: 1..365;
invalid values log an error and use 3. The module overrides raid period scaling
after `Rate.InstanceResetTime`; dungeon intervals keep their original behavior.

The module snapshots saved ZG difficulty-0 reset time before any calendar changes.
If ZG has no timestamp, it uses AQ20 difficulty 0. Conflicting calendars log a
warning and use ZG. An expired anchor advances by whole configured periods; a
reset due exactly now is retained. A completely new database gets one shared
calendar at the configured core reset hour, using the core's initialization rule.
Subsequent resets use the next shared calendar boundary and the existing core
warning queue. Existing calendars retain their saved hour; changing
`Instance.ResetTimeHour` does not move an anchored calendar while this override
is enabled. That setting supplies the hour only when bootstrapping a new calendar.

Two generic hooks are added by `patches/raid-reset-hooks.patch`:

- `OnInstanceResetPeriod`: called in startup and recurring reset calculations.
- `OnInstanceResetSchedule`: called after all persisted global times load and
  before reset events, instance saves and character binds are loaded; also called
  when scheduling the next reset, with `loading = false`.

The second hook supplies the shared timestamp. The patch persists changed values
to `characters.instance_reset` with a map/difficulty upsert. For future existing
resets, it also changes the manager's cached timestamp before `AddInstanceSave`.
This is the migration step that changing only the period would miss.

No schema change or manual migration SQL is needed. For active future binds, no
`instance` or `character_instance` rows are rewritten by this feature. Stock raid
`instance.resettime` remains zero; it is not the effective raid expiry. Instance
30's identity, permanent bind, encounter data and respawn records are preserved.
The patch leaves already-expired cached timestamps expired and queues the normal
core reset path for the first world update after binds load. This prevents
reviving overdue binds during migration. That path clears ordinary binds and
honors the core's existing extension flag. The runtime hook then schedules the
next reset on the shared calendar, even when the old due time was off-calendar.
Test downtime and extended binds on your actual core before relying on them.

The patch also retains the current queue iterator when removing a processed
event. A reset shortly before the shared boundary can insert missed warnings
ahead of that event; erasing the queue's new first element could otherwise repeat
the reset. The normal warning/reset sequence is preserved.

## Install on the existing VM

1. Record `.instance listbinds` on the MC test character and run the read-only
   queries in `raid-reset-verify.sql` against the **characters** database. Save
   the numeric ZG/AQ20 timestamps; SQL display timezone can differ from the game.
2. Stop worldserver and back up the characters database. Do not clear instance 30
   or remove its bind. Do not edit the reset tables while worldserver is running.
3. In the existing module checkout, run `git pull --ff-only`. From the AzerothCore
   source directory apply the hook patch once:

   ```sh
   git apply --check modules/mod-progression/patches/raid-reset-hooks.patch
   git apply modules/mod-progression/patches/raid-reset-hooks.patch
   ```

   Substitute `mod-expansion-progression` if that is the installed folder name.
   If already applied, `git apply --reverse --check <patch>` succeeds. Do not
   force a patch that fails both checks; it needs review against that core revision.
4. Reconfigure using your existing build options, rebuild and install worldserver.
   Updating the module alone without rebuilding the core will not install hooks:

   ```sh
   cmake -S . -B build -DMODULES=static
   cmake --build build --parallel 2
   cmake --install build
   ```

5. Add to your installed `etc/modules/mod_progression.conf`:

   ```ini
   Progression.OverrideRaidReset = 1
   Progression.RaidResetDays = 3
   ```

   The module preserves the saved anchor's exact hour. Raid setting changes
   require restart; `.reload config` cannot change their active values.
6. Start worldserver. Confirm the `mod-progression: raid resets every 3 days`
   log with anchor map 309 (or fallback 509) and the expected numeric next reset.
   Inspect startup logs for database errors before allowing players to connect.

## Verify existing and new binds

1. Log in on the original MC character. `.instance listbinds` must still show
   **map 409, instance 30, permanent yes, difficulty 0, canReset no**. TTR must
   now end at the same timestamp as ZG/AQ20, rather than the old ~6d20h expiry.
   It may be less than one day; it is not necessarily a full three days.
2. Enter MC. Confirm the same saved encounter progress and previously killed
   bosses remain killed. The migration must not produce a fresh MC instance.
3. With an unbound character in a separate raid group, enter a fresh MC and kill
   a boss to establish a permanent bind. `.instance listbinds` should show a
   different instance ID but the same expiry as instance 30. Repeat in another
   raid; its expiry should match too. Verify a heroic dungeon keeps its own reset.
4. Restart again before the reset. Both MC binds must keep the same expiry; the
   restart must not grant another three days. Re-run the SQL comparisons.
5. Let the real global reset occur. Ordinary, non-extended binds should clear
   through normal core behavior. A new permanent raid bind must end exactly
   three days after the prior global reset, at the same core reset hour. Repeat
   over another boundary to rule out a one-time-only override.
6. On an expendable character, test an explicitly extended bind through a reset
   and a restart. The module does not clear the extension flag or replace the
   core's extension rules. Also test a reset that occurs while the VM is stopped;
   migrated overdue binds should process through the normal core reset on the
   first world update, with the next reset on the shared calendar.

## Disabling and rollback

Set `Progression.OverrideRaidReset = 0` and restart to restore stock period
calculation. This does **not** restore prior seven-day database timestamps: the
next saved reset remains scheduled, and subsequent intervals use stock rules.
For exact pre-migration rollback, stop the server and use the pre-upgrade backup
with the prior module/core build; restoring a full backup also loses subsequent
character progress. Do not restore old bind tables into a running server.

## 4 AM Pacific time

The original scheduler counted UTC days and retained the existing ZG timestamp.
04:00 UTC is 21:00 PDT the previous day (20:00 PST in winter). Changing
`Instance.ResetTimeHour` alone does not move that saved calendar.

The local-time option uses the **worldserver process's timezone** and the OS
timezone database. Set these in the installed module config:

```ini
Progression.OverrideRaidReset = 1
Progression.RaidResetDays = 3
Progression.RaidResetUseLocalTime = 1
Progression.RaidResetLocalHour = 4
```

Defaults remain local-time mode off, local hour 4. Local hours must be 0..23
(invalid values use 4). Local mode requires 2..365 reset days; 1 falls back to 3
because core clamps extended-reset periods to at least 24 hours, which cannot
represent a spring-forward daily interval. Three-day schedules and extensions
use 71/72/73 hours as appropriate. Prefer an hour such as 4 that exists exactly
once on DST transition days; nonexistent/ambiguous hours use OS normalization.

On the VM, pull the module, rebuild/install as usual, then restart worldserver.
**Do not apply the core hook patch again** if the earlier version is installed;
the hook patch is unchanged by this local-time feature. No manual SQL is needed.
Existing future binds retain their instances and progress while their timestamp
moves to the first configured local hour at or after the old shared boundary.
For example, a saved 21:00 PDT reset moves seven hours later to 04:00 PDT.
Restarting an already-aligned calendar does not move it again.

For a terminal/screen/tmux launcher, set this before its existing launch command:

```sh
export TZ=America/Los_Angeles
# Now run your existing worldserver launch command in this shell.
```

Persist that export in the script you normally use to start worldserver; an
export in an unrelated shell cannot change a running process or systemd service.
For systemd, run `systemctl edit <your-worldserver-service>` and add:

```ini
[Service]
Environment="TZ=America/Los_Angeles"
```

Then run `systemctl daemon-reload` and restart that service. Use the actual service
name. For a container, set `TZ=America/Los_Angeles` in its environment and ensure
the container includes timezone data. These are process-wide timezone settings:
other server features that use local time see the same timezone. The module never
changes the OS timezone or the process environment itself.

`America/Los_Angeles` means 04:00 PDT in summer and 04:00 PST in winter. If you
instead want **fixed PST (UTC-8) year-round**, use `TZ=Etc/GMT+8` on Linux; that
would appear as 05:00 on a Los Angeles clock during summer.

Confirm the startup log says `local raid calendar` and shows the next date at
`04:00:00 PDT -0700` or `04:00:00 PST -0800`. If it says UTC, the launch environment
has not selected Pacific time. On the VM, you can also convert the logged Unix
timestamp using `TZ=America/Los_Angeles date -d @<timestamp>`.
Check `.instance listbinds` again: your active MC instance ID must remain the same,
and TTR should count down to the logged 4 AM Pacific boundary.

The local-time tests exercise the real OS timezone conversions for both 2026 DST
changes, migration from 04:00 UTC, repeat restarts, three-calendar-day recurrence,
extended expiry, and the actual core loader's persisted/cached MC timestamps.

## Validation scope

Standalone C++ tests execute the actual module hooks with narrow core doubles.
They cover the MC-30 migration/new-bind model, all raid difficulties, unaffected
dungeons, invalid/custom settings, reload protection, anchor selection, bootstrap,
calendar boundaries and the core recurrence formula. Bind persistence and in-game
behavior require the VM acceptance steps above; the tests do not run a live realm.
The optional `PROGRESSION_CORE_SOURCE` CMake option extracts and executes the
patched core's actual `LoadResetTimes` and `Update` functions against database/event-storage
doubles. It verifies cached and persisted MC expiry, missing raid rows, restart
stability, immediate overdue reset events, and unchanged overdue dungeon/disabled
behavior. A queue regression fixture verifies one overdue reset and the correct
remaining warning when restarting ten minutes before the next reset. The GitHub
workflow runs this against both pinned upstream and Playerbots
cores, then compiles the module and modified core files against real headers.
See its run results for the exact revision tested.

Source/schema references: [global reset table](https://www.azerothcore.org/wiki/instance_reset),
[instance table](https://www.azerothcore.org/wiki/instance),
[core reset manager](https://github.com/azerothcore/azerothcore-wotlk/blob/db533ad7537a0641d076b13e5611f29f33558d06/src/server/game/Instances/InstanceSaveMgr.cpp).
