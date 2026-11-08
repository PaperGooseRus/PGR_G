# RPG shared crafting world

<!-- RPG_STATE_V1 -->

This is a single authoritative snapshot of all mutable game entities.

## Meta

| key | value |
| --- | --- |
| seed | 13623382633559983636 |
| day | 1 |
| next_id | 100 |

## Actors

| id | name | npc | facing | jumps | prayers | prayed_day |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Player | 0 | north | 0 | 0 | 0 |
| 2 | AlchemistNPC | 1 | north | 0 | 0 | 0 |
| 3 | BlacksmithNPC | 1 | north | 0 | 0 | 0 |
| 4 | CarpenterNPC | 1 | north | 0 | 0 | 0 |
| 5 | JewelerNPC | 1 | north | 0 | 0 | 0 |
| 6 | ForesterNPC | 1 | north | 0 | 0 | 0 |
| 7 | MinerNPC | 1 | north | 0 | 0 | 0 |
| 8 | HerbalistNPC | 1 | north | 0 | 0 | 0 |
| 9 | CookNPC | 1 | north | 0 | 0 | 0 |
| 10 | TailorNPC | 1 | north | 0 | 0 | 0 |
| 11 | EngineerNPC | 1 | north | 0 | 0 | 0 |
| 12 | MysticNPC | 1 | north | 0 | 0 | 0 |

## Skills

| actor | profession | xp |
| --- | --- | --- |
| 1 | alchemist | 0 |
| 2 | alchemist | 2420 |
| 3 | blacksmith | 1620 |
| 4 | carpenter | 1620 |
| 5 | jeweler | 2420 |
| 6 | forester | 720 |
| 7 | miner | 720 |
| 8 | herbalist | 980 |
| 9 | cook | 980 |
| 10 | tailor | 980 |
| 11 | engineer | 980 |
| 12 | mystic | 980 |

## Lots

| id | owner | kind | quantity | element | form | quality | source1 | source2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

## Items

| id | owner | kind | component1 | component2 | decoration_lot | attunement_lot | quality | hardness | comfort | resonance | decorated | attuned | anomaly |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

## Nodes

| id | kind | remaining | element |
| --- | --- | --- | --- |
| 1 | ore-vein | 40 | 5 |
| 2 | forest | 40 | 0 |
| 3 | gem-vein | 12 | 5 |
| 4 | herb-patch | 24 | 0 |
| 5 | fiber-patch | 24 | 0 |
| 6 | primal-spring | 64 | 1 |

## Orders

| id | customer | worker | operation | input1 | input2 | status | output | output_item |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

## Discoveries

| id | actor | item | effect | day |
| --- | --- | --- | --- | --- |

## Knowledge

| actor | effect | observations |
| --- | --- | --- |

## Events

| id | actor | verb | input1 | input2 | output | success | day |
| --- | --- | --- | --- | --- | --- | --- | --- |
