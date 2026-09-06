// SPDX-License-Identifier: GPL-2.0-or-later
#include "ProgressionPolicy.h"
#include "ProgressionRuntime.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerScript.h"
#include "World.h"
#include "WorldScript.h"

#include <shared_mutex>
#include <string>

namespace
{
struct ProgressionState
{
    bool initialized = false;
    bool enabled = false;
    bool announce = true;
    uint32 coreMaximum = Progression::ClientMaximum;
    uint32 cap = Progression::DefaultCap;
    uint32 startPlayer = 1;
    uint32 startHeroic = 55;
    uint32 startGM = 1;
};

ProgressionState state;

void ClearCappedXP(Player* player, bool enabled, uint32 cap)
{
    if (Progression::DiscardXP(enabled, cap, player->GetLevel()))
        player->SetUInt32Value(PLAYER_XP, 0);
}

void SyncPlayer(Player* player)
{
    ClearCappedXP(player, state.enabled, state.cap);
    player->SetUInt32Value(PLAYER_FIELD_MAX_LEVEL,
        state.enabled ? state.cap : state.coreMaximum);
}

// Called from config/command processing on the world thread, outside map updates.
// ObjectAccessor includes bot Player objects even without ordinary sessions.
void ApplyToOnlinePlayers(bool previouslyEnabled, uint32 previousCap)
{
    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
    for (auto const& entry : ObjectAccessor::GetPlayers())
    {
        Player* player = entry.second;
        // Clear the OLD capped remainder before raising/disabling the cap.
        ClearCappedXP(player, previouslyEnabled, previousCap);
        SyncPlayer(player);
    }
}

void ApplyRuntimeMaximum()
{
    // GiveXP uses this maximum inside its multi-level loop, AFTER rested/RaF
    // bonuses. Merely vetoing GiveLevel or filtering source XP is insufficient.
    sWorld->setIntConfig(CONFIG_MAX_PLAYER_LEVEL,
        state.enabled ? state.cap : state.coreMaximum);
    // Character creation writes its starting level directly (without GiveLevel).
    sWorld->setIntConfig(CONFIG_START_PLAYER_LEVEL,
        state.enabled ? std::min(state.startPlayer, state.cap) : state.startPlayer);
    sWorld->setIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL,
        state.enabled ? std::min(state.startHeroic, state.cap) : state.startHeroic);
    sWorld->setIntConfig(CONFIG_START_GM_LEVEL,
        state.enabled ? std::min(state.startGM, state.cap) : state.startGM);
}

class ProgressionWorldScript final : public WorldScript
{
public:
    ProgressionWorldScript() : WorldScript("ProgressionWorldScript",
        { WORLDHOOK_ON_BEFORE_CONFIG_LOAD, WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnBeforeConfigLoad(bool reload) override
    {
        // MaxPlayerLevel is non-reloadable in current core. Restore its original
        // value before core validates its cache; our override is reapplied below.
        if (reload && state.initialized)
            sWorld->setIntConfig(CONFIG_MAX_PLAYER_LEVEL, state.coreMaximum);
    }

    void OnAfterConfigLoad(bool reload) override
    {
        bool const previouslyEnabled = state.enabled;
        uint32 const previousCap = state.cap;
        if (!state.initialized)
            state.coreMaximum = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        state.startPlayer = sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL);
        state.startHeroic = sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);
        state.startGM = sWorld->getIntConfig(CONFIG_START_GM_LEVEL);

        state.enabled = sConfigMgr->GetOption<bool>("Progression.Enable", true);
        state.announce = sConfigMgr->GetOption<bool>("Progression.AnnounceOnLogin", true);
        int32 const configured = sConfigMgr->GetOption<int32>("Progression.LevelCap", 60);
        if (configured < 1 || !Progression::ValidCap(static_cast<uint32>(configured), state.coreMaximum))
        {
            state.cap = std::min(Progression::DefaultCap, state.coreMaximum);
            LOG_ERROR("module", "mod-progression: invalid LevelCap {}; using {} (allowed 1..{}).",
                configured, state.cap, std::min(state.coreMaximum, Progression::ClientMaximum));
        }
        else
            state.cap = static_cast<uint32>(configured);

        state.initialized = true;
        ApplyRuntimeMaximum();
        if (reload)
            ApplyToOnlinePlayers(previouslyEnabled, previousCap);
        LOG_INFO("module", "mod-progression: enabled={}, cap={}, original MaxPlayerLevel={}. Expansion is unchanged.",
            state.enabled, state.cap, state.coreMaximum);
    }
};

class ProgressionPlayerScript final : public PlayerScript
{
public:
    ProgressionPlayerScript() : PlayerScript("ProgressionPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_GIVE_EXP,
          PLAYERHOOK_ON_CAN_GIVE_LEVEL, PLAYERHOOK_ON_SET_MAX_LEVEL,
          PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_SAVE }) { }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit*, uint8) override
    {
        if (Progression::DiscardXP(state.enabled, state.cap, player->GetLevel()))
            amount = 0;
    }

    bool OnPlayerCanGiveLevel(Player* player, uint8 newLevel) override
    {
        return Progression::CanGiveLevel(state.enabled, state.cap, player->GetLevel(), newLevel);
    }

    void OnPlayerSetMaxLevel(Player*, uint32& maximum) override
    {
        if (state.enabled)
            maximum = std::min(maximum, state.cap);
    }

    void OnPlayerLogin(Player* player) override
    {
        SyncPlayer(player);
        if (state.enabled && state.announce && player->GetSession())
        {
            std::string message = "[Progression] Realm level cap: " + std::to_string(state.cap)
                + ". XP stops at the cap and resumes when it is raised.";
            if (player->GetLevel() > state.cap)
                message += " Your existing level is preserved; further advancement is blocked.";
            ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
        }
    }

    void OnPlayerUpdate(Player* player, uint32) override
    {
        // GiveXP writes its local remainder AFTER OnPlayerLevelChanged returns.
        // Discard it at the next player update, and before saving/changing cap.
        ClearCappedXP(player, state.enabled, state.cap);
    }

    void OnPlayerSave(Player* player) override
    {
        ClearCappedXP(player, state.enabled, state.cap);
    }
};

class ProgressionCommandScript final : public CommandScript
{
public:
    ProgressionCommandScript() : CommandScript("ProgressionCommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable const commands =
        {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
            { "cap", HandleCap, SEC_GAMEMASTER, Console::Yes }
        };
        static ChatCommandTable const root = { { "progression", commands } };
        return root;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        std::string message = std::string("[Progression] ") + (state.enabled ? "Enabled" : "Disabled")
            + "; progression cap: " + std::to_string(state.cap)
            + "; effective MaxPlayerLevel: " + std::to_string(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            + "; original MaxPlayerLevel: " + std::to_string(state.coreMaximum)
            + ". Expansion unchanged. Live cap overrides reset on config reload/restart.";
        handler->SendSysMessage(message.c_str());
        return true;
    }

    static bool HandleCap(ChatHandler* handler, uint32 level)
    {
        if (!state.enabled)
        {
            handler->SendSysMessage("[Progression] Disabled. Set Progression.Enable = 1 and reload config first.");
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (!Progression::ValidCap(level, state.coreMaximum))
        {
            std::string message = "[Progression] Usage: .progression cap <level>, allowed 1.."
                + std::to_string(std::min(state.coreMaximum, Progression::ClientMaximum)) + ".";
            handler->SendSysMessage(message.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }
        uint32 const previousCap = state.cap;
        state.cap = level;
        ApplyRuntimeMaximum();
        ApplyToOnlinePlayers(true, previousCap);
        std::string message = "[Progression] Cap changed from " + std::to_string(previousCap)
            + " to " + std::to_string(level)
            + ". Save Progression.LevelCap in mod_progression.conf to retain it after reload/restart.";
        handler->SendSysMessage(message.c_str());
        LOG_INFO("module", "mod-progression: GM/console changed cap from {} to {} (runtime only).", previousCap, level);
        return true;
    }
};
}

void AddProgressionScripts()
{
    new ProgressionWorldScript();
    new ProgressionPlayerScript();
    new ProgressionCommandScript();
}

bool ProgressionRuntime::IsEnabled()
{
    return state.enabled;
}

std::uint32_t ProgressionRuntime::GetCap()
{
    return state.cap;
}
