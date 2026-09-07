-- AzerothCore only reconstructs persisted container contents when the container
-- has an item_loot_template. This quest-only self entry acts as the required
-- template anchor but is never visible because no quest requests the cache item.
DELETE FROM `item_loot_template` WHERE `Entry` = 900100 AND `Item` = 900100;
INSERT INTO `item_loot_template`
    (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`,
     `GroupId`, `MinCount`, `MaxCount`, `Comment`)
VALUES
    (900100, 900100, 0, 100, 1, 1, 0, 1, 1,
     'Progression starter cache persistence anchor - hidden quest-only entry');
