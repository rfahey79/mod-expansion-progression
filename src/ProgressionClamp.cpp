// SPDX-License-Identifier: GPL-2.0-or-later
#include "ProgressionPolicy.h"
#include "ProgressionRuntime.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "LootItemStorage.h"
#include "LootMgr.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldScript.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct ClampConfig
{
    bool automatic = false;
    bool resetTalents = true;
    bool removeSpells = true;
    bool unequipGear = true;
    bool mailOverflow = true;
    bool starterKit = true;
    uint32 starterCacheEntry = 900100;
    uint32 mailSenderEntry = 34337; // The Postmaster in the stock world DB.
    std::string mailSenderName = "Keepers of the Realm";
    std::string mailSubject = "The Call of an Earlier Age";
    std::string starterSubject = "Equipment for the Earlier Age";
    std::string mailBody = "Adventurer,$B$BThe realm has been drawn back to an earlier age, and some of the equipment you carried belongs to battles yet to come.$B$BThese items have been returned to you for safekeeping. They are yours still, but their time has not yet arrived.$B$BWhen the realm advances, you may reclaim their full use.$B$BUntil then, steel yourself for the trials that lie before you.$B$B-- {sender}";
};

ClampConfig config;
std::set<ObjectGuid::LowType> pendingAutomaticClamps;
std::mutex clampMutex;
std::mutex pendingMutex;

char const* const DefaultMailBody = "Adventurer,$B$BThe realm has been drawn back to an earlier age, and some of the equipment you carried belongs to battles yet to come.$B$BThese items have been returned to you for safekeeping. They are yours still, but their time has not yet arrived.$B$BWhen the realm advances, you may reclaim their full use.$B$BUntil then, steel yourself for the trials that lie before you.$B$B-- {sender}";

struct ClampPreview
{
    uint32 oldLevel = 0;
    uint32 targetLevel = 0;
    std::vector<uint32> spells;
    std::vector<std::string> items;
};

struct RemovedEquipment
{
    uint8 slot;
    ItemTemplate const* itemTemplate;
};

uint32 ClampTarget(Player const* player, uint32 cap)
{
    return player && player->getClass() == CLASS_DEATH_KNIGHT
        ? std::max(cap, Progression::DeathKnightMinimumLevel) : cap;
}

std::string Trim(std::string_view value)
{
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    return first < last ? std::string(first, last) : std::string();
}

bool StartsWithI(std::string_view value, std::string_view prefix)
{
    if (value.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

std::string ReplaceSender(std::string body)
{
    std::string const token = "{sender}";
    std::size_t position = 0;
    while ((position = body.find(token, position)) != std::string::npos)
    {
        body.replace(position, token.size(), config.mailSenderName);
        position += config.mailSenderName.size();
    }
    return body;
}

SpellFamilyNames ClassSpellFamily(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR: return SPELLFAMILY_WARRIOR;
        case CLASS_PALADIN: return SPELLFAMILY_PALADIN;
        case CLASS_HUNTER: return SPELLFAMILY_HUNTER;
        case CLASS_ROGUE: return SPELLFAMILY_ROGUE;
        case CLASS_PRIEST: return SPELLFAMILY_PRIEST;
        case CLASS_DEATH_KNIGHT: return SPELLFAMILY_DEATHKNIGHT;
        case CLASS_SHAMAN: return SPELLFAMILY_SHAMAN;
        case CLASS_MAGE: return SPELLFAMILY_MAGE;
        case CLASS_WARLOCK: return SPELLFAMILY_WARLOCK;
        case CLASS_DRUID: return SPELLFAMILY_DRUID;
        default: return SPELLFAMILY_GENERIC;
    }
}

bool IsHighLevelClassSpell(Player* player, uint32 spellId, uint32 cap)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    return info && !GetTalentSpellPos(spellId) && info->SpellLevel > cap &&
        info->SpellFamilyName == ClassSpellFamily(player->getClass());
}

std::vector<uint32> FindHighLevelClassSpells(Player* player, uint32 cap)
{
    std::vector<uint32> result;
    for (auto const& entry : player->GetSpellMap())
        if (entry.second->State != PLAYERSPELL_REMOVED && IsHighLevelClassSpell(player, entry.first, cap))
            result.push_back(entry.first);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<uint8> FindHighLevelEquipment(Player* player, uint32 cap)
{
    std::vector<uint8> result;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (ItemTemplate const* itemTemplate = item->GetTemplate())
                if (itemTemplate->RequiredLevel > cap)
                    result.push_back(slot);
    return result;
}

uint32 MaximumStarterItemLevel(uint32 target)
{
    if (target <= 60)
        return target + 10;
    if (target <= 70)
        return 115;
    return 187;
}

int32 ClassStatWeight(uint8 playerClass, uint32 stat)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR:
        case CLASS_DEATH_KNIGHT:
            if (stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_ATTACK_POWER) return 5;
            if (stat == ITEM_MOD_STAMINA || stat == ITEM_MOD_DEFENSE_SKILL_RATING) return 2;
            return stat == ITEM_MOD_AGILITY ? 1 : 0;
        case CLASS_PALADIN:
            if (stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_ATTACK_POWER) return 4;
            if (stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPELL_POWER) return 3;
            return stat == ITEM_MOD_STAMINA ? 2 : 0;
        case CLASS_HUNTER:
            if (stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_RANGED_ATTACK_POWER) return 5;
            if (stat == ITEM_MOD_ATTACK_POWER) return 3;
            return stat == ITEM_MOD_STAMINA ? 2 : 0;
        case CLASS_ROGUE:
            if (stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_ATTACK_POWER) return 5;
            return stat == ITEM_MOD_STAMINA ? 2 : 0;
        case CLASS_PRIEST:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            if (stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPELL_POWER) return 5;
            if (stat == ITEM_MOD_SPIRIT || stat == ITEM_MOD_MANA_REGENERATION) return 3;
            return stat == ITEM_MOD_STAMINA ? 2 : 0;
        case CLASS_SHAMAN:
        case CLASS_DRUID:
            if (stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_STRENGTH ||
                stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPELL_POWER) return 4;
            if (stat == ITEM_MOD_ATTACK_POWER || stat == ITEM_MOD_MANA_REGENERATION) return 3;
            return stat == ITEM_MOD_STAMINA || stat == ITEM_MOD_SPIRIT ? 2 : 0;
        default:
            return 0;
    }
}

int64 StarterItemScore(Player* player, ItemTemplate const& itemTemplate)
{
    int64 score = static_cast<int64>(itemTemplate.RequiredLevel) * 100000
        + static_cast<int64>(itemTemplate.ItemLevel) * 100;
    for (uint32 index = 0; index < itemTemplate.StatsCount && index < MAX_ITEM_PROTO_STATS; ++index)
        score += static_cast<int64>(ClassStatWeight(player->getClass(), itemTemplate.ItemStat[index].ItemStatType))
            * std::max(itemTemplate.ItemStat[index].ItemStatValue, 0);
    return score;
}

bool IsArmorBodySlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
        case EQUIPMENT_SLOT_SHOULDERS:
        case EQUIPMENT_SLOT_CHEST:
        case EQUIPMENT_SLOT_WAIST:
        case EQUIPMENT_SLOT_LEGS:
        case EQUIPMENT_SLOT_FEET:
        case EQUIPMENT_SLOT_WRISTS:
        case EQUIPMENT_SLOT_HANDS:
            return true;
        default:
            return false;
    }
}

uint32 ClassArmorSubclass(uint8 playerClass, uint32 target)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
            return target >= 40 ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
        case CLASS_DEATH_KNIGHT:
            return ITEM_SUBCLASS_ARMOR_PLATE;
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            return target >= 40 ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
        case CLASS_ROGUE:
        case CLASS_DRUID:
            return ITEM_SUBCLASS_ARMOR_LEATHER;
        default:
            return ITEM_SUBCLASS_ARMOR_CLOTH;
    }
}

bool SameEquipmentKind(Player* player, RemovedEquipment const& removed,
    ItemTemplate const& candidate, uint32 target)
{
    if (removed.itemTemplate->Class != candidate.Class)
        return false;
    if (candidate.Class == ITEM_CLASS_ARMOR && IsArmorBodySlot(removed.slot))
        return candidate.SubClass == ClassArmorSubclass(player->getClass(), target);
    // Preserve weapon choice (staff, bow, sword, and so on). For jewelry,
    // shields, cloaks and relics, slot and usability checks choose the result.
    return candidate.Class != ITEM_CLASS_WEAPON || removed.itemTemplate->SubClass == candidate.SubClass;
}

uint32 FindGreenReplacement(Player* player, RemovedEquipment const& removed, uint32 target,
    std::set<uint32> const& alreadySelected)
{
    uint32 bestEntry = 0;
    int64 bestScore = -1;
    uint32 const minimumRequiredLevel = target > 10 ? target - 10 : 1;
    uint32 const maximumItemLevel = MaximumStarterItemLevel(target);

    for (auto const& [entry, itemTemplate] : *sObjectMgr->GetItemTemplateStore())
    {
        if (itemTemplate.Quality != ITEM_QUALITY_UNCOMMON || itemTemplate.RequiredLevel < minimumRequiredLevel ||
            itemTemplate.RequiredLevel > target || itemTemplate.ItemLevel > maximumItemLevel ||
            itemTemplate.StartQuest || itemTemplate.Area || itemTemplate.Map ||
            itemTemplate.Bonding == BIND_QUEST_ITEM || itemTemplate.Bonding == BIND_QUEST_ITEM1 ||
            itemTemplate.MaxCount == 1 || itemTemplate.HasFlag(ITEM_FLAG_CONJURED) ||
            itemTemplate.HasFlag(ITEM_FLAG_HAS_LOOT) || alreadySelected.count(entry) ||
            !SameEquipmentKind(player, removed, itemTemplate, target) ||
            player->FindEquipSlot(&itemTemplate, removed.slot, false) != removed.slot ||
            player->CanUseItem(&itemTemplate) != EQUIP_ERR_OK)
            continue;

        int64 const score = StarterItemScore(player, itemTemplate);
        if (score > bestScore || (score == bestScore && entry < bestEntry))
        {
            bestEntry = entry;
            bestScore = score;
        }
    }
    return bestEntry;
}

std::vector<uint32> BuildStarterKit(Player* player, std::vector<RemovedEquipment> const& removed, uint32 target)
{
    std::vector<uint32> result;
    std::set<uint32> selected;
    for (RemovedEquipment const& equipment : removed)
    {
        uint32 const replacement = FindGreenReplacement(player, equipment, target, selected);
        if (replacement)
        {
            result.push_back(replacement);
            selected.insert(replacement);
        }
    }
    return result;
}

ClampPreview BuildPreview(Player* player, uint32 cap)
{
    ClampPreview preview;
    preview.oldLevel = player->GetLevel();
    uint32 const target = ClampTarget(player, cap);
    preview.targetLevel = std::min(preview.oldLevel, target);
    if (config.removeSpells)
        preview.spells = FindHighLevelClassSpells(player, target);
    if (config.unequipGear)
        for (uint8 slot : FindHighLevelEquipment(player, target))
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                preview.items.push_back(item->GetTemplate()->Name1);
    return preview;
}

void SendPreview(ChatHandler* handler, Player* player, ClampPreview const& preview)
{
    std::ostringstream message;
    message << "[Progression] Clamp preview for " << player->GetName() << ": level "
        << preview.oldLevel << " -> " << preview.targetLevel << "; talents "
        << (config.resetTalents ? "will be reset" : "unchanged") << "; high-level class spells: "
        << preview.spells.size() << "; equipped over-level items: " << preview.items.size() << ".";
    handler->SendSysMessage(message.str().c_str());
    for (std::string const& item : preview.items)
        handler->SendSysMessage(("  unequip: " + item).c_str());
}

void ResetAllTalentSpecs(Player* player)
{
    uint8 const originalSpec = player->GetActiveSpec();
    if (player->GetSpecsCount() > 1)
    {
        uint8 const otherSpec = originalSpec == 0 ? 1 : 0;
        player->ActivateSpec(otherSpec);
        player->resetTalents(true);
        player->ActivateSpec(originalSpec);
    }
    player->resetTalents(true);
    player->SendTalentsInfoData(false);
    Pet* pet = player->GetPet();
    Pet::resetTalentsForAllPetsOf(player, pet);
    if (pet)
        player->SendTalentsInfoData(true);
}

std::size_t RemoveHighLevelClassSpells(Player* player, uint32 cap)
{
    std::vector<uint32> const spells = FindHighLevelClassSpells(player, cap);
    std::map<uint32, uint32> chains;
    for (uint32 spellId : spells)
    {
        uint32 const first = sSpellMgr->GetFirstSpellInChain(spellId);
        uint32& fallback = chains[first];
        for (auto const& entry : player->GetSpellMap())
        {
            if (entry.second->State == PLAYERSPELL_REMOVED)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(entry.first);
            if (info && sSpellMgr->GetFirstSpellInChain(entry.first) == first &&
                info->SpellLevel <= cap && (!fallback || info->GetRank() > sSpellMgr->GetSpellInfo(fallback)->GetRank()))
                fallback = entry.first;
        }
    }

    for (auto const& chain : chains)
    {
        player->removeSpell(chain.first, SPEC_MASK_ALL, false);
        if (chain.second)
            player->learnSpell(chain.second);
    }
    return spells.size();
}

std::size_t StoreOrMailHighLevelEquipment(Player* player, uint32 cap, std::size_t& mailed,
    std::size_t& skipped, std::size_t& starterItems)
{
    std::vector<Item*> mailItems;
    std::vector<RemovedEquipment> removedEquipment;
    std::size_t moved = 0;
    for (uint8 slot : FindHighLevelEquipment(player, cap))
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        ItemTemplate const* itemTemplate = item->GetTemplate();

        ItemPosCountVec destination;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, destination, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(destination, item, true);
            removedEquipment.push_back({ slot, itemTemplate });
            ++moved;
            continue;
        }

        if (!config.mailOverflow)
        {
            ++skipped;
            continue;
        }

        player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
        mailItems.push_back(item);
        removedEquipment.push_back({ slot, itemTemplate });
        ++mailed;
    }

    std::vector<uint32> replacements;
    if (config.starterKit && !removedEquipment.empty())
        replacements = BuildStarterKit(player, removedEquipment, cap);
    starterItems = replacements.size();

    if (!mailItems.empty() || !replacements.empty())
    {
        CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
        for (Item* item : mailItems)
        {
            item->DeleteFromInventoryDB(transaction);
            if (item->GetState() == ITEM_UNCHANGED)
                item->FSetState(ITEM_CHANGED);
            item->SetOwnerGUID(player->GetGUID());
            item->SaveToDB(transaction);
        }

        for (std::size_t offset = 0; offset < mailItems.size(); offset += MAX_MAIL_ITEMS)
        {
            MailDraft draft(config.mailSubject, ReplaceSender(config.mailBody));
            std::size_t const end = std::min(offset + static_cast<std::size_t>(MAX_MAIL_ITEMS), mailItems.size());
            for (std::size_t index = offset; index < end; ++index)
                draft.AddItem(mailItems[index]);
            draft.SendMailTo(transaction, MailReceiver(player),
                MailSender(MAIL_CREATURE, config.mailSenderEntry, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_HAS_BODY);
        }

        for (std::size_t offset = 0; offset < replacements.size(); offset += MAX_NR_LOOT_ITEMS)
        {
            Item* cache = Item::CreateItem(config.starterCacheEntry, 1, player);
            if (!cache)
            {
                LOG_ERROR("module", "mod-progression: starter cache item {} is missing; no replacement cache was mailed to {}.",
                    config.starterCacheEntry, player->GetName());
                starterItems = 0;
                break;
            }

            cache->SetOwnerGUID(player->GetGUID());
            cache->loot.containerGUID = cache->GetGUID();
            cache->loot.lootOwnerGUID = player->GetGUID();
            cache->loot.sourceWorldObjectGUID = cache->GetGUID();
            std::size_t const end = std::min(offset + static_cast<std::size_t>(MAX_NR_LOOT_ITEMS), replacements.size());
            for (std::size_t index = offset; index < end; ++index)
                cache->loot.AddItem(LootStoreItem(replacements[index], 0, 100.0f, false,
                    LOOT_MODE_DEFAULT, 0, 1, 1));
            cache->m_lootGenerated = true;
            sLootItemStorage->AddNewStoredLoot(&cache->loot, player);
            cache->SaveToDB(transaction);

            MailDraft starterDraft(config.starterSubject,
                "Your higher-level equipment has been set aside. Open this cache for green replacement equipment selected for your class and current progression level.");
            starterDraft.AddItem(cache);
            starterDraft.SendMailTo(transaction, MailReceiver(player),
                MailSender(MAIL_CREATURE, config.mailSenderEntry, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_HAS_BODY);
        }
        player->SaveInventoryAndGoldToDB(transaction);
        CharacterDatabase.CommitTransaction(transaction);
    }
    return moved;
}

bool ClampPlayer(Player* player, uint32 cap, ChatHandler* feedback)
{
    uint32 const target = ClampTarget(player, cap);
    if (!player || player->GetLevel() <= target)
    {
        if (feedback && player)
            feedback->SendSysMessage(("[Progression] " + player->GetName() + " is already at or below its safe clamp level " + std::to_string(target) + ".").c_str());
        return false;
    }

    if (!player->GetSession())
    {
        if (feedback)
        {
            feedback->SendSysMessage(("[Progression] Cannot clamp " + player->GetName() + " without an active player session.").c_str());
            feedback->SetSentErrorMessage(true);
        }
        LOG_ERROR("module", "mod-progression: skipped clamp for {} because it has no active session.", player->GetName());
        return false;
    }

    std::lock_guard<std::mutex> clampLock(clampMutex);

    uint32 const oldLevel = player->GetLevel();
    player->GiveLevel(static_cast<uint8>(target));
    if (player->GetLevel() != target)
    {
        if (feedback)
        {
            feedback->SendSysMessage(("[Progression] Could not lower " + player->GetName() + " to level " + std::to_string(target) + "; no normalization changes were made.").c_str());
            feedback->SetSentErrorMessage(true);
        }
        return false;
    }
    player->SetUInt32Value(PLAYER_XP, 0);

    if (config.resetTalents)
        ResetAllTalentSpecs(player);
    std::size_t const removedSpells = config.removeSpells ? RemoveHighLevelClassSpells(player, target) : 0;
    std::size_t mailed = 0;
    std::size_t skipped = 0;
    std::size_t starterItems = 0;
    std::size_t const bagged = config.unequipGear
        ? StoreOrMailHighLevelEquipment(player, target, mailed, skipped, starterItems) : 0;

    player->UpdateAllStats();
    player->SetFullHealth();
    player->SaveToDB(false, false);

    std::ostringstream message;
    message << "[Progression] Clamped " << player->GetName() << " from level " << oldLevel << " to " << target
        << "; removed " << removedSpells << " high-level class spells; moved " << bagged
        << " equipped items to bags; mailed " << mailed << "; starter replacements: " << starterItems << ".";
    if (skipped)
        message << " " << skipped << " items stayed equipped because bags were full and overflow mail is disabled.";
    if (feedback)
        feedback->SendSysMessage(message.str().c_str());
    if (player->GetSession() && (!feedback || feedback->GetSession() != player->GetSession()))
        ChatHandler(player->GetSession()).SendSysMessage(message.str().c_str());
    if (config.resetTalents && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(
            "[Progression] Your talents and pet talents have been reset. Please spend your talent points again.");
    LOG_INFO("module", "{}", message.str());
    return true;
}

Player* FindOnlineTarget(ChatHandler* handler, std::string name)
{
    if (name.empty())
        return handler->getSelectedPlayerOrSelf();
    if (!normalizePlayerName(name))
        return nullptr;
    return ObjectAccessor::FindPlayerByName(name, false);
}

class ProgressionClampWorldScript final : public WorldScript
{
public:
    ProgressionClampWorldScript() : WorldScript("ProgressionClampWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool) override
    {
        std::lock_guard<std::mutex> clampLock(clampMutex);
        config.automatic = sConfigMgr->GetOption<bool>("Progression.ClampExistingCharacters", false);
        config.resetTalents = sConfigMgr->GetOption<bool>("Progression.Clamp.ResetTalents", true);
        config.removeSpells = sConfigMgr->GetOption<bool>("Progression.Clamp.RemoveHighLevelSpells", true);
        config.unequipGear = sConfigMgr->GetOption<bool>("Progression.Clamp.UnequipHighLevelGear", true);
        config.mailOverflow = sConfigMgr->GetOption<bool>("Progression.Clamp.MailOverflowGear", true);
        config.starterKit = sConfigMgr->GetOption<bool>("Progression.Clamp.StarterKit", true);
        config.starterCacheEntry = sConfigMgr->GetOption<uint32>("Progression.Clamp.StarterCacheEntry", 900100);
        config.mailSenderEntry = sConfigMgr->GetOption<uint32>("Progression.Mail.SenderEntry", 34337);
        config.mailSenderName = sConfigMgr->GetOption<std::string>("Progression.Mail.SenderName", "Keepers of the Realm");
        config.mailSubject = sConfigMgr->GetOption<std::string>("Progression.Mail.Subject", "The Call of an Earlier Age");
        config.starterSubject = sConfigMgr->GetOption<std::string>("Progression.Mail.StarterSubject", "Equipment for the Earlier Age");
        config.mailBody = sConfigMgr->GetOption<std::string>("Progression.Mail.Body", DefaultMailBody);
    }
};

class ProgressionClampPlayerScript final : public PlayerScript
{
public:
    ProgressionClampPlayerScript() : PlayerScript("ProgressionClampPlayerScript", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerLogin(Player* player) override
    {
        bool automatic;
        {
            std::lock_guard<std::mutex> clampLock(clampMutex);
            automatic = config.automatic;
        }
        if (automatic && ProgressionRuntime::IsEnabled() && player->GetLevel() > ClampTarget(player, ProgressionRuntime::GetCap()))
        {
            std::lock_guard<std::mutex> pendingLock(pendingMutex);
            pendingAutomaticClamps.insert(player->GetGUID().GetCounter());
        }
    }

    void OnPlayerUpdate(Player* player, uint32) override
    {
        {
            std::lock_guard<std::mutex> pendingLock(pendingMutex);
            auto const found = pendingAutomaticClamps.find(player->GetGUID().GetCounter());
            if (found == pendingAutomaticClamps.end())
                return;
            pendingAutomaticClamps.erase(found);
        }
        bool automatic;
        {
            std::lock_guard<std::mutex> clampLock(clampMutex);
            automatic = config.automatic;
        }
        if (automatic && ProgressionRuntime::IsEnabled() && player->GetLevel() > ClampTarget(player, ProgressionRuntime::GetCap()))
            ClampPlayer(player, ProgressionRuntime::GetCap(), nullptr);
    }
};

class ProgressionClampCommandScript final : public CommandScript
{
public:
    ProgressionClampCommandScript() : CommandScript("ProgressionClampCommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable const commands =
        {
            { "clamp", HandleClamp, SEC_GAMEMASTER, Console::Yes }
        };
        static ChatCommandTable const root = { { "progression", commands } };
        return root;
    }

    static bool HandleClamp(ChatHandler* handler, Acore::ChatCommands::Tail arguments)
    {
        if (!ProgressionRuntime::IsEnabled())
        {
            handler->SendSysMessage("[Progression] Disabled. Enable progression before clamping characters.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string input = Trim(arguments);
        bool preview = false;
        if (StartsWithI(input, "preview "))
        {
            preview = true;
            input = Trim(std::string_view(input).substr(8));
        }

        if (!preview && StartsWithI(input, "all") && Trim(std::string_view(input).substr(3)).empty())
        {
            std::vector<ObjectGuid> players;
            {
                std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
                for (auto const& entry : ObjectAccessor::GetPlayers())
                    players.push_back(entry.first);
            }
            std::size_t clamped = 0;
            for (ObjectGuid const& guid : players)
                if (Player* player = ObjectAccessor::FindPlayer(guid))
                    if (player->GetLevel() > ClampTarget(player, ProgressionRuntime::GetCap()) && ClampPlayer(player, ProgressionRuntime::GetCap(), nullptr))
                        ++clamped;
            handler->SendSysMessage(("[Progression] Clamped " + std::to_string(clamped) + " online characters/bots. Offline characters are handled at login when Progression.ClampExistingCharacters = 1.").c_str());
            return true;
        }

        Player* player = FindOnlineTarget(handler, input);
        if (!player)
        {
            handler->SendSysMessage("[Progression] Usage: .progression clamp [preview] <online-character-name>, or .progression clamp all");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (preview)
        {
            std::lock_guard<std::mutex> clampLock(clampMutex);
            SendPreview(handler, player, BuildPreview(player, ProgressionRuntime::GetCap()));
            return true;
        }
        ClampPlayer(player, ProgressionRuntime::GetCap(), handler);
        return true;
    }
};
}

void AddProgressionClampScripts()
{
    new ProgressionClampWorldScript();
    new ProgressionClampPlayerScript();
    new ProgressionClampCommandScript();
}
