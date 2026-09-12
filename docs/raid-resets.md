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
Subsequent resets use the existing core scheduling formula and warning queue.

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

   Leave the existing `Instance.ResetTimeHour` unchanged. The module uses the
   saved anchor's exact timestamp for migration; core recurrence uses the configured
   hour. Do not change the hour or `Rate.InstanceResetTime` through a live config
   reload during this test. Raid setting changes themselves require restart.
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

## Validation scope

Standalone C++ tests execute the actual module hooks with narrow core doubles.
They cover the MC-30 migration/new-bind model, all raid difficulties, unaffected
dungeons, invalid/custom settings, reload protection, anchor selection, bootstrap,
calendar boundaries and the core recurrence formula. Bind persistence and in-game
behavior require the VM acceptance steps above; the tests do not run a live realm.
The optional `PROGRESSION_CORE_SOURCE` CMake option extracts and executes the
patched core's actual `LoadResetTimes` function against database/event-storage
doubles. It verifies cached and persisted MC expiry, missing raid rows, restart
stability, immediate overdue reset events, and unchanged overdue dungeon/disabled
behavior. The GitHub workflow runs this against both pinned upstream and Playerbots
cores, then compiles the module and modified core files against real headers.
See its run results for the exact revision tested.

Source/schema references: [global reset table](https://www.azerothcore.org/wiki/instance_reset),
[instance table](https://www.azerothcore.org/wiki/instance),
[core reset manager](https://github.com/azerothcore/azerothcore-wotlk/blob/db533ad7537a0641d076b13e5611f29f33558d06/src/server/game/Instances/InstanceSaveMgr.cpp).
