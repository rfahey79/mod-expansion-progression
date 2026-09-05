# mod-progression

A configurable realm level cap for AzerothCore WotLK, independent of `Expansion`.
Start at 60, then advance to 70 and 80 with a GM command. Keep `Expansion = 2`:
Blood Elves, Draenei, Horde Paladins and Alliance Shamans retain their normal
availability. No AzerothCore core files, database schema or race/class restrictions
are changed.

Repository: [rfahey79/mod-expansion-progression](https://github.com/rfahey79/mod-expansion-progression).
The module supports either directory name, `mod-progression` or
`mod-expansion-progression`. Install only one copy.

## v1 behavior

- XP works normally below the cap. At or above it, XP awards are blocked.
- Large XP awards, rested XP and Recruit-a-Friend bonuses cannot level past it.
  The final capped award's remainder is discarded on the next player update,
  before saving, and before raising/disabling the cap; it is never banked for
  the next phase. The client can briefly see the remainder before the next tick.
- `Player::GiveLevel` requests above the cap are rejected, including GM grants
  and bot paths using that API. GMs have no leveling bypass.
- New characters' normal, heroic and GM starting levels are clamped to the cap.
- Existing characters/bots above a lowered cap are **not demoted**. They cannot
  gain XP or advance further; administrative downward level changes are allowed.
- Online players receive the new maximum immediately; raising the cap requires
  no relog. A login message announces the active cap.
- No SQL is required. Commands use security level `SEC_GAMEMASTER` (2 or higher)
  and support the server console. Existing command security overrides still apply.

## Install on your VM

From your AzerothCore source directory:

```sh
git clone https://github.com/rfahey79/mod-expansion-progression.git modules/mod-progression
```

Re-run your existing CMake configuration and rebuild/install the server. For a
typical build directory (retain your usual install prefix and other options):

```sh
cmake -S . -B build -DMODULES=static
cmake --build build --parallel 2
cmake --install build
```

Check the CMake module list includes `mod-progression`. AzerothCore automatically
collects `src` files, generates the loader and installs `conf/*.conf.dist`.
The root `CMakeLists.txt` documents this integration and supports standalone tests;
legacy `AC_ADD_SCRIPT` registration is deliberately unnecessary.

Copy the installed `etc/modules/mod_progression.conf.dist` to
`etc/modules/mod_progression.conf` under your server install prefix. Set:

```ini
# worldserver.conf
Expansion = 2
MaxPlayerLevel = 80
```

```ini
# modules/mod_progression.conf
Progression.Enable = 1
Progression.LevelCap = 60
Progression.AnnounceOnLogin = 1
```

Restart `worldserver` after installing the module. Look for the
`mod-progression: enabled=` startup log. Accounts must also have the expansion
access normally required by your server; this module does not change account flags.

## Commands and persistence

| Command | Behavior |
| --- | --- |
| `.progression status` | Show enable state, progression cap, runtime maximum and original maximum. |
| `.progression cap 60` | Apply level 60 immediately to the realm. |
| `.progression cap 70` | Open leveling through 70. |
| `.progression cap 80` | Open leveling through 80. |

Omit the leading dot in the server console. Allowed caps are
`1..min(80, original MaxPlayerLevel)`. Invalid commands leave the cap unchanged.
Malformed/missing/extra arguments are handled by AzerothCore's typed command parser.
The cap command is rejected while the module is disabled.

**The command changes runtime state only.** After advancing to 70, also edit
`Progression.LevelCap = 70` in the module config to survive reloads/restarts.
`.reload config` loads the file again and discards command overrides. Set
`Progression.Enable = 0` and reload to restore the original core maximum and
configured starting levels. Invalid configured caps log an error and use
`min(60, original MaxPlayerLevel)`.

Keep `MaxPlayerLevel = 80` in the main config if you intend to progress to 80.
The module respects the original core maximum; it cannot raise past it.

## Playerbots

There are no Playerbots headers, libraries or runtime detection requirements.
Bots represented by `Player` use the same XP/level hooks. The inspected
`mod-playerbots` random-level selection also clamps to
`CONFIG_MAX_PLAYER_LEVEL`, which this module updates.

For your initial level-60 VM, also align the bot configuration:

```ini
AiPlayerbot.RandomBotMinLevel = 1
AiPlayerbot.RandomBotMaxLevel = 60
```

When advancing phases, raise the bot maximum to 70/80 and reload/restart using
your Playerbots version's supported procedure. Keep the bot minimum no higher
than the phase cap. An explicit bot maximum of 60 will continue limiting random
generation after the realm cap is raised. Existing higher-level bots are not
reset, re-equipped or down-leveled automatically. Test random generation and
manual bot leveling on your VM before populating the realm.

Direct SQL edits or third-party code calling `SetLevel`/writing level fields
instead of `GiveLevel` can bypass script hooks. No dependency-free module can
intercept arbitrary direct writes. This module does not claim compatibility
with every bot fork or module that rewrites the same maximum.

## Implementation and boundaries

The world script overrides the runtime `CONFIG_MAX_PLAYER_LEVEL` and clamps
starting-level settings. It restores the original maximum before core config
reload validation and reapplies the selected progression cap afterward.
It does not edit `worldserver.conf`, `Expansion`, account data or core source.
Player hooks veto XP and upward `GiveLevel` calls, expose the cap to the client,
announce it and clear capped XP. Live transitions synchronize registered Player
objects, including bots, through `ObjectAccessor` under its container lock.

Using the core runtime maximum is intentional: the `GiveXP` loop applies bonuses
and may award multiple levels; an XP-source hook or level veto alone does not
provide equivalent behavior. Other core systems that consult MaxPlayerLevel
(including capped quest XP-to-money conversion) also see this maximum.
Do not combine with another module that owns or overwrites MaxPlayerLevel.
Trial-account restrictions, voluntary XP locks and other core restrictions may
still impose a lower limit; the module does not clear those flags.

This is a leveling progression module, not a content phase system. It does not
gate raids, Outland/Northrend access, items, professions, talents, Death Knights,
or achievements, and it does not detect raid completion. Wrath gameplay rules
remain active. Caps below the normal Death Knight starting level are supported
as level limits, but their starting content is not redesigned for those levels.

## Validation

Standalone behavioral tests compile the actual module implementation with narrow
core test doubles, plus a model of the relevant `GiveXP` loop ordering:

```sh
# Build OUTSIDE the module folder: core recursively collects C++ source files.
cmake -S modules/mod-progression -B progression-tests
cmake --build progression-tests
ctest --test-dir progression-tests --output-on-failure
```

Tests cover multi-level/bonus awards, direct XP grants, all source-hook inputs,
GM level vetoes, raising/lowering caps, session-less bots, remainder clearing,
disabled mode, invalid values, start-level clamps, reloads and the original core
maximum. The GitHub Actions workflow separately compiles the module and generated
loader against real headers from the two pinned core revisions below. These
checks do **not** substitute for a live worldserver/database/client test.

### VM acceptance checklist

1. Start with Expansion 2 and cap 60. Create a Blood Elf Paladin and Draenei
   Shaman; confirm the announcement and normal XP gain below 60.
2. At 59, turn in a large quest and test a rested kill. Confirm level stops at
   60, XP clears, and relogging preserves zero capped XP. Also test exploration,
   dungeon-finder/BG XP and a direct XP-awarding addon/module if installed.
3. At 60, attempt `.levelup 1` and `.levelup 20`; verify no advancement.
   Confirm an ordinary player cannot execute either progression command.
4. Run `.progression cap 70` without relogging. Earn XP and confirm leveling
   resumes; then test cap 80. Verify no earlier excess XP appears.
5. Lower to 60 with a level-61 character online; confirm level is preserved and
   advancement stops. Test a logged-in bot and a newly generated random bot.
6. Test invalid/missing/extra arguments; confirm status is unchanged. Test config
   reload, restart persistence and disabling the module as described above.

## Source references

API/source review uses these exact revisions, rather than assuming older examples
still match the current hooks:

- [AzerothCore module guide](https://www.azerothcore.org/wiki/create-a-module)
- [Upstream Player XP loop and GiveLevel](https://github.com/azerothcore/azerothcore-wotlk/blob/70dda745ba9e9e2ed96f8b4b1be10bb98e5ca9c3/src/server/game/Entities/Player/Player.cpp)
- [Upstream PlayerScript hooks](https://github.com/azerothcore/azerothcore-wotlk/blob/70dda745ba9e9e2ed96f8b4b1be10bb98e5ca9c3/src/server/game/Scripting/ScriptDefines/PlayerScript.h)
- [Playerbots core hooks](https://github.com/mod-playerbots/azerothcore-wotlk/blob/413bea61a85e20d9caef7d66fc601a661fdddd9d/src/server/game/Scripting/ScriptDefines/PlayerScript.h)
- [Playerbots random-level selection](https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Bot/RandomPlayerbotMgr.cpp)

License: GPL-2.0-or-later; see [LICENSE](LICENSE).
