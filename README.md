# mod-progression

A configurable realm level cap for AzerothCore WotLK, independent of `Expansion`.
Start at 60, then advance to 70 and 80 with a GM command. Keep `Expansion = 2`:
Blood Elves, Draenei, Horde Paladins and Alliance Shamans retain their normal
availability. Includes a configurable shared raid-reset calendar. The raid feature
requires the small scheduling-hook patch below; database schema and race/class
restrictions are unchanged.

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
- New normal and GM characters start no higher than the cap. Stock Death Knights
  retain their required level-55 minimum when the configured cap is lower.
- Existing characters/bots above a lowered cap are **not demoted**. They cannot
  gain XP or advance further; administrative downward level changes are allowed.
- Online players receive the new maximum immediately; raising the cap requires
  no relog. A login message announces the active cap.
- Dungeon Finder follows the cap-derived expansion phase. Future expansion
  categories and specific dungeons are locked using each `LFGDungeons.dbc`
  entry's explicit expansion metadata, and random queues are redirected to the
  current phase through AzerothCore's supported random-dungeon hook.
- The included world update adds one custom lootable starter-cache item. Commands
  use security level `SEC_GAMEMASTER` (2 or higher) and support the server console.
  Existing command security overrides still apply.

## Install on your VM

From your AzerothCore source directory:

```sh
git clone https://github.com/rfahey79/mod-expansion-progression.git modules/mod-progression
```

Re-run your existing CMake configuration and rebuild/install the server. For a
typical build directory (retain your usual install prefix and other options):

```sh
git apply --check modules/mod-progression/patches/raid-reset-hooks.patch
git apply modules/mod-progression/patches/raid-reset-hooks.patch
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
Progression.LFG.Enable = 1
Progression.LFG.LockFutureDungeons = 1
Progression.LFG.RestrictRandom = 1
Progression.LFG.RestrictSpecific = 1
```

Restart `worldserver` after installing the module. Look for the
`mod-progression: enabled=` startup log. Accounts must also have the expansion
access normally required by your server; this module does not change account flags.

## Universal raid resets

Defaults are `Progression.OverrideRaidReset = 1` and `Progression.RaidResetDays = 3`.
Every raid map/difficulty with a global reset uses one calendar, including Classic,
TBC and Wrath raids. This setting is independent of `Progression.Enable` and the
current level cap. Heroic and normal dungeon behavior is unchanged.

A Wednesday-night run can be available again after Thursday's scheduled reset.
The next reset is three days after that global reset, not three days after entry
or a boss kill. ZG (309, difficulty 0) supplies the calendar; AQ20 (509, difficulty 0)
is the fallback. If both exist but disagree, ZG wins and a warning records this.
If neither exists, the module creates a shared calendar using the core reset hour.

Read [raid reset deployment and verification](docs/raid-resets.md) before upgrading
an existing server. Migration is automatic during startup: future global timestamps
are aligned **before** existing saves/binds load. Your MC map 409 / instance 30 bind
therefore gets the next ZG reset without changing its instance ID, permanent flag,
encounter progress or respawns. No manual migration SQL is required.

The hook patch is required even when the override is disabled; compilation fails
if the hooks are missing. It adds two generic WorldScript hooks and calls them at
startup and recurring reset scheduling. All raid-specific policy stays in the
module; the core's warning queue, reset execution and extension logic remain in use.
Migrated overdue calendars queue a normal reset after binds load, so old expired
binds are not revived until the next shared reset.
Apply the patch once, rebuild the server, and restart. Do not reapply it on each
module update. Check compatibility after updating the core.

Days must be 1..365 (invalid values use 3). Raid periods override the core's
`Rate.InstanceResetTime` scaling. Existing calendars retain their saved reset
hour; `Instance.ResetTimeHour` is used when bootstrapping a missing calendar.
Raid settings are startup-only: `.reload config` warns about changed values and
keeps the running calendar intact until restart.

## Commands and persistence

| Command | Behavior |
| --- | --- |
| `.progression status` | Show enable state, progression cap and core maximum. |
| `.progression cap 60` | Apply level 60 immediately to the realm. |
| `.progression cap 70` | Open leveling through 70. |
| `.progression cap 80` | Open leveling through 80. |
| `.progression clamp preview Name` | Show the changes required to normalize an online character. |
| `.progression clamp Name` | Normalize one online character to the current safe target. With no name, use the selected character or yourself. |
| `.progression clamp all` | Normalize every currently online character and bot above its safe target. |

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

## Dungeon Finder progression

Dungeon Finder uses the realm cap as its phase source, not the player's level:

| Progression cap | Phase | Available Dungeon Finder content |
| ---: | --- | --- |
| 1-60 | Classic | Classic specific dungeons and Random Classic Dungeon |
| 61-70 | Burning Crusade | Classic + TBC specific dungeons; random queues capped at TBC |
| 71-80 | Wrath | Classic + TBC + Wrath |

`Progression.LFG.LockFutureDungeons` is the master switch for marking later
specific DBC entries unavailable. `RestrictSpecific` applies that lock to
specific dungeons. `RestrictRandom` rewrites a queued future random category to
the current phase but deliberately leaves the client-offered category selectable;
this is required when a level-60 client offers Random BC instead of Random
Classic. At Classic, all random requests become category 258. At TBC, Wrath
normal/heroic requests become categories 259/260. The actual specific-dungeon expansion classification comes from the
DBC `ExpansionLevel` copied into AzerothCore's `LFGDungeonData`, rather than
from a level-range guess or the queuing player's level.

This deliberately leaves `Expansion = 2`, race/class availability, account
expansion flags and AzerothCore source unchanged. Config reloads apply the LFG
switches and current cap-derived phase immediately; cached player lock lists are
rebuilt by AzerothCore whenever it next initializes that player's LFG data.

## Playerbots

There are no Playerbots headers, libraries or runtime detection requirements.
Bots represented by `Player` use the same XP/level hooks. The inspected
`mod-playerbots` random-level selection also clamps to
the effective `CONFIG_MAX_PLAYER_LEVEL`, which follows the phase cap with a
level-55 safety floor for the stock Death Knight data.

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

## Existing-character clamp

The hard cap preserves existing characters above it by default. A GM can inspect
and deliberately normalize an online character before bringing it into an earlier
phase:

```text
.progression clamp preview Arthas
.progression clamp Arthas
```

Clamping lowers the character to its safe target, clears XP, resets both talent
specs and pet talents, lowers active and stored Hunter/Warlock pets to the same
safe target, clears their XP, removes class spells above that target, and
unequips items whose `RequiredLevel` exceeds it. Pet clamping is enabled by
`Progression.Clamp.Pets = 1`.
Running `.progression clamp <name>` also repairs pets for an owner who is already
at the safe target; it does not repeat the owner's talent, spell, or gear changes.
Equipment is placed in ordinary bags when possible. Overflow is returned through
mail in groups of at most 12 attachments, preserving the original item instances,
enchants, gems, durability and ownership.

When `Progression.Clamp.StarterKit = 1`, each emptied equipment slot is matched
with a green item the clamped character can use. The selector respects the target
level, equipment slot, class armor progression, weapon type and class-relevant
stats. It also excludes quest-bound, unique, conjured, loot-container and
zone-restricted items. Vanilla, TBC and Wrath item-level ceilings keep lower
progression kits from pulling equipment forward from later content.

The replacements arrive in a separate **Progression Starter Equipment Cache**
mail attachment. It uses the stock wooden chest icon and opens through the normal
loot window like a lockbox. Its per-character contents persist in
`item_loot_storage` until collected. The world
updates include the required `item_loot_template` anchor so AzerothCore can
reconstruct those contents when the mailed cache is loaded. Full bags do
not destroy uncollected cache contents. The original high-level equipment remains
in bags or its separate safekeeping mail.

The spell filter deliberately leaves racials, professions, mounts, companions,
quest rewards and other generic spells alone. It removes high-level ranks as a
complete chain and then relearns the highest rank in that chain that the character
already knew and can use at the cap.

For a Death Knight and a cap below 55, the safe clamp target is 55. This prevents
the character from being written at a level for which stock AzerothCore has no
Death Knight base stats.

To apply the same normalization when an offline character next logs in, set:

```ini
Progression.ClampExistingCharacters = 1
```

After an automatic login clamp, the character receives the result summary and a
separate on-screen notification and chat message that their talents and pet
talents were reset. The default
remains `0` because lowering levels, resetting talents, removing spells and
moving equipment are intentional character changes. `clamp all` affects the
online roster only; the login setting handles the rest over time.
Playerbots in `ObjectAccessor` follow the same path without a Playerbots compile
dependency.

The default overflow letter is delivered by AzerothCore's existing Postmaster
creature entry and signed "Keepers of the Realm." The subject, body, signature and
sender creature entry are configurable. A custom visible sender name requires a
matching `creature_template` entry; set `Progression.Mail.SenderEntry` to that
entry if you add one.

## Implementation and boundaries

The world script keeps `CONFIG_MAX_PLAYER_LEVEL` at the original core maximum
until AzerothCore loads complete level-stat and XP arrays. After startup, the
effective core maximum follows the phase cap with a level-55 Death Knight safety
floor. Player hooks enforce lower caps for ordinary characters and block Death
Knights from advancing while that floor is active.
It does not edit `worldserver.conf`, `Expansion` or account data. The raid feature
uses the separately applied core hook patch described above.
Player hooks veto XP and upward `GiveLevel` calls, expose the cap to the client,
announce it and clear capped XP. Live transitions synchronize registered Player
objects, including bots, through `ObjectAccessor` under its container lock.

XP-source hooks discard awards at the cap, the level hook rejects upward changes,
and the next update clears any remainder produced by a direct `GiveXP` caller.
Loading all data at the configured maximum makes later live phase changes safe.
Trial-account restrictions, voluntary XP locks and other core restrictions may
still impose a lower limit; the module does not clear those flags.

Outside Dungeon Finder, this remains a leveling progression module. It does not
gate raids, physical Outland/Northrend access, items, professions, talents,
Death Knights, or achievements, and it does not detect raid completion. Wrath
gameplay rules remain active. AzerothCore has no stock Death Knight stats below
level 55, so a cap below 55 leaves Death Knights at level 55 and blocks further
advancement.

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
7. With an expendable character above the cap, equip an over-level item and fill
   its bags. Run `.progression clamp preview Name`, then `.progression clamp Name`.
   Verify level/XP, both talent specs, class spell ranks, bag placement and mailed
   overflow. Repeat with an online bot, then enable login clamping and test an
   offline character on its next login.
8. At cap 60, open Specific Dungeons and confirm TBC/Wrath entries are locked;
   queue a random dungeon and confirm it uses Random Classic Dungeon. At cap 70,
   confirm Classic/TBC specifics are available, Wrath specifics are locked, and
   Wrath normal/heroic random selections redirect to their TBC counterparts.
   At cap 80, confirm all three expansions are available.

## Source references

API/source review uses these exact revisions, rather than assuming older examples
still match the current hooks:

- [AzerothCore module guide](https://www.azerothcore.org/wiki/create-a-module)
- [Upstream Player XP loop and GiveLevel](https://github.com/azerothcore/azerothcore-wotlk/blob/db533ad7537a0641d076b13e5611f29f33558d06/src/server/game/Entities/Player/Player.cpp)
- [Upstream PlayerScript hooks](https://github.com/azerothcore/azerothcore-wotlk/blob/db533ad7537a0641d076b13e5611f29f33558d06/src/server/game/Scripting/ScriptDefines/PlayerScript.h)
- [Upstream LFG lock hooks and dungeon metadata](https://github.com/azerothcore/azerothcore-wotlk/tree/master/src/server/game/DungeonFinding)
- [AzerothCore mod-rdf-expansion](https://github.com/azerothcore/mod-rdf-expansion)
- [Playerbots core hooks](https://github.com/mod-playerbots/azerothcore-wotlk/blob/413bea61a85e20d9caef7d66fc601a661fdddd9d/src/server/game/Scripting/ScriptDefines/PlayerScript.h)
- [Playerbots random-level selection](https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Bot/RandomPlayerbotMgr.cpp)

License: GPL-2.0-or-later; see [LICENSE](LICENSE).
