# Corrupt Patrol

A game mode of its own, on the main menu after the Story Creator ("Corrupt Patrol"), in the manner of Xenoverse.
Hovering it plays `menu/hover_corrupt_patrol.mp3` next to the game (any mp3 / wav / flac of that name).

## The story

In Hell, the Soul Cleansing Machine has been sabotaged: the evil it scrubs from every soul pools into the **Malice**,
a violet storm of ki that slips into the living world and turns the strongest, proudest fighters into the
**Corrupted**. The Supreme Kai founds the Corrupt Patrol at Kami's Lookout; the player's warrior is its first recruit,
someone the Malice can't seem to grip. Twelve chapters (Piccolo, Vegeta, Gohan, Frieza, Cell, the Buus, Goku, the one
behind it all, and what comes after him), 63 side quests from people all over the world, and every one of the game's
161 characters to win over as a master.

## Places

Twenty places, each a battle stage to walk about in (no lock-on, no attack, no HUD): Kami's Lookout (the main story,
Videl's wardrobe, the Training Grounds), the Time Chamber, Kame House (Krillin's bounties, Master Roshi), the World
Tournament Stage (Mr Satan's item shop, Uub), Mount Paozu, the Wasteland, Rocky Area, Mountain Road, the Islands, City
Ruins, the Desert, the Glacier, Penguin Village, Muscle Tower, King's Castle, the Cell Games Arena, Planet Namek, Outer
Space, the Supreme Kai's World and Hell. The large ones have their people spread far apart. Masters stand at their
own homes once won over.

- Walking about: Circle beside someone talks to them; Triangle opens the **world map** (a hologram of the world, the
  other worlds beside it; the stick or d-pad moves to the nearest place that way, L1 / R1 the next, Cross flies there,
  Circle / Triangle closes it); START opens the Patrol Menu. The list at the left shows who is there and how far.
- Flying: the warrior powers up, bursts up into the sky and is gone, a camera on the ground watching; the map shows
  the flight; at the other place they dive down out of the sky, brake, land in a crouch, the camera comes round behind
  them and the place's name comes up (black bars meanwhile). The fighters' own motions (fast ascend 0x2E / 0x2F, fast
  descend 0x32 / 0x33, landing 0x2D, ki charge 0x34 / 0x35); at the end the scripted motion is stopped
  (BtlFacade_StopCharMotion), or the player's fighter would stay in it, taking no buttons.
- The menus are drawn over the stage of the place the player is at, the person talked to standing on it.

## The warrior

New Game: the six races stand in a row on the Lookout; left / right turns the row, the one in front is chosen (its
description and traits below it). Then the name, then the Custom Character screen in its Patrol mode: the race's
stand-in body, every grid showing only fighters of the race (Saiyans and humans share theirs: they look the same) and
the masters whose training is complete. START is Done: the warrior is saved and packaged with the mode's techniques
and stats (three health bars, three blast stocks; Full Power Energy Wave, Full Power Energy Blast Volley, Super
Explosive Wave). A name one of the player's own warriors already has gets " (Patrol)" after it.

## Masters

Every disc character: unlocked by clearing a chapter or a side quest (each unlocks its own). Each has five lessons,
battles against them that get harder; each win teaches one of their techniques (two Blast 1, two Blast 2, the
Ultimate); the fifth also their fighting style and outfit. Techniques: Cross sets one in its slot, Triangle trains them
into the warrior (the package made again). The Training Grounds (the Lookout, the Time Chamber) list them all.

## Items

The item shop is the game's own (Evolution Z's shop, with its guides and voices), run with the mode's zeni and items:
the player's own save is set aside while it is open and put back after it (never written meanwhile). Its stock grows
with the warrior's level (the shop's levels 0..5 at levels 1, 9, 17, 25, 33, 41). The items' names and descriptions
are read while it is open (kept in `patrol/items.json`). The Patrol Menu's Items: up to four worn, within the item
slots the warrior has (2, one more every 7 levels, 8 at most).

## Progress

Levels 1..60; experience and zeni from every battle (a little from a loss). Attack, defense, ki blast and ki rise
with the level (hidden Z items in each battle). Missions have a recommended level; the Corrupted fight with every stat
raised and a stronger AI the further the story is.

## The save

`patrol/save.json` next to the game (`BT3_PATROL=<folder>` another one), format 2: JSON, written after every battle,
on Save, after the shop and when leaving. The game's own save is never touched. A first-story save (format 1) keeps
its warrior, level, zeni and items and starts the new story.

## The content

The writing is plain spoken dialogue, in each character's voice as in the show (no prose, no running jokes). The places, the story, the quests and the masters' lines are data: `port/assets/patrol/` (`locations.json`,
`story.json`, `quests_b.json`, `quests_c.json`), made one document by `port/tools/patrol_content.py` (which checks it:
every quest giver at their place, every master unlocked exactly once) into `content.json` and
`port/src/gs/patrol_content.inc`, built into the game. A `patrol/content.json` next to the game is read in its place.

## The corrupted

The corrupted have a violet aura that never goes out, a violet power-up glow, and a violet pulse over their colours
(port/src/port_corrupt.c; src/battle/eft_n.c, eft_q.c, src/sys/gfxm_d_c.c). `BT3_CORRUPT=<side mask>` shows it in any
battle (1 the left side, 2 the right).

## For testing

- `BT3_TEST_MODMGR=patrol`: the mode at once; `=menupatrol`: the main menu on its item.
- `BT3_PATROL_SCRIPT="<frame>:<button>,..."`: buttons pressed on the screens (up down left right ok back sq tri l1 r1
  start), frames counted from the screens' opening.
- `BT3_PATROL_CONTINUE=1`: straight to the place when there is a save.
- `BT3_PATROL_DUMP=1`: the last battle's saga written to `patrol/_last_battle.json`.
- `BT3_PATROL_NOHUB=1`: the list of the people there instead of walking; `BT3_PATROL_NOCREATOR=1`: a new warrior without
  the creator (the stand-in).
- `BT3_HUB_LOG=1`: where the player is in a place, every 30 frames (and BT3_PAD's `sup sdown sleft sright`, the left
  stick, with `+n` to hold n frames).
- `BT3_PATROL_HP=<percent>`, `BT3_PATROL_FOE_HP=<percent>`: the player's / the foe's starting health.

## How it is made

- `src/menu/menu_a.c`: main menu item 15; the Duel mode runs the mode with `gPortDuelRole` 5 (`src/menu/menu_g_b.c`).
- `src/menu/port_patrol.c`: the game's half (the stage behind the screens, the model and the row of races
  (`Mm_ViewSetLine`, src/menu/port_modmgr.c), sounds, the battle's setup, the shop), and `PortPatrol_AfterBattle`, which
  `src/menu/menu_a_b.c` calls after any battle.
- `port/src/gs/patrol_ui.cpp`: the screens, the save, the battles: each is a saga of one episode, made into its script
  by the Story Creator's compiler (`PortStoryUi_PrepareDoc`, `port/src/gs/story_ui.cpp`), which takes `cpu_level` and
  `player_items` / `enemy_items` besides the documented keys (`docs/port/sagas.md`), and `hub` for a place.
- `src/battle/port_story_btl.c`, command 9007: a place's battle (its people, the landing, the map's buttons, the takeoff;
  `gPortHubFreeze` keeps the player's buttons from the fighter meanwhile: src/battle/btl_input.c).
