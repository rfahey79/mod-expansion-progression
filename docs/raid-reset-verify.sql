-- Read-only. Select your characters database before running these queries.
SELECT mapid, difficulty, resettime, FROM_UNIXTIME(resettime) AS next_reset
FROM instance_reset
ORDER BY resettime, mapid, difficulty;

-- MC instance 30: compare stored per-instance reset (normally 0 for raids)
-- with the effective global timestamp. Preserve permanent and extended flags.
SELECT ci.guid, i.map, i.id AS instance, ci.permanent, ci.extended, i.difficulty,
       i.resettime AS stored_instance_reset, i.completedEncounters,
       ir.resettime AS global_reset, FROM_UNIXTIME(ir.resettime) AS next_reset,
       ir.resettime - UNIX_TIMESTAMP() AS seconds_remaining
FROM instance AS i
JOIN character_instance AS ci ON ci.instance = i.id
LEFT JOIN instance_reset AS ir ON ir.mapid = i.map AND ir.difficulty = i.difficulty
WHERE i.map = 409 AND i.id = 30;

-- Classic raid comparison. All returned rows should share the ZG timestamp.
SELECT mapid, difficulty, resettime
FROM instance_reset
WHERE mapid IN (249, 309, 409, 469, 509, 531)
ORDER BY mapid, difficulty;
