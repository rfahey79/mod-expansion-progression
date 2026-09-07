-- Lootable container used for per-character progression starter kits.
-- Its contents are generated and persisted by mod-progression when clamping.
DELETE FROM `item_template` WHERE `entry` = 900100;
INSERT INTO `item_template`
    (`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`,
     `Quality`, `Flags`, `FlagsExtra`, `BuyCount`, `BuyPrice`, `SellPrice`,
     `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`,
     `RequiredLevel`, `maxcount`, `stackable`, `ContainerSlots`, `bonding`,
     `description`, `lockid`, `Material`, `ScriptName`, `VerifiedBuild`)
VALUES
    (900100, 15, 0, -1, 'Progression Starter Equipment Cache', 9632,
     2, 4, 0, 1, 0, 0,
     0, -1, -1, 1,
     1, 0, 1, 0, 1,
     'Contains green replacement equipment selected for your class and progression level.',
     0, 1, '', 12340);
