# Corrupt Patrol

A game mode of its own, on the main menu after the Story Creator ("Corrupt Patrol"). Hovering it plays
`menu/hover_corrupt_patrol.mp3` next to the game (any mp3 / wav / flac of that name).

## The story

A purple power is amplifying the strongest fighters and turning them evil. The player makes a warrior, joins the
Corrupt Patrol under the Supreme Kai, and frees them one by one:

| Chapter | Corrupted | Where |
|---|---|---|
| 1 | Piccolo | the Wasteland |
| 2 | Vegeta | Planet Namek |
| 3 | Gohan | the Cell Games Arena |
| 4 | Frieza | Dying Namek |
| 5 | Cell | the Cell Games Arena |
| 6 | Majin Buu | the World Tournament Stage |
| 7 | Goku | Supreme Kai's World |
| 8 | Janemba, the source | Hell |

Each chapter has a scene before the fight, the fight, and a scene after (a won and a lost one), played on the stage
with the Story Creator's scene engine. The corrupted fight with every stat raised (Z items) and a stronger AI, more so
later in the story.

## Masters

A freed fighter becomes a master at the Training Grounds. Each has five lessons, battles that get harder each time. Each
win teaches one of the master's own techniques, in the order of their slots: two Blast 1 skills, two Blast 2 techniques,
the Ultimate. The fifth lesson also unlocks their fighting style and outfit. Lessons already learnt can be fought again
for experience.

## The headquarters (Kami's Lookout)

- **Main Quest** (Supreme Kai): the next chapter, and the briefing for each.
- **Training Grounds**: the masters.
- **Hercule's Shop**: accessories, the game's own Z items (Attack / Defense / Ki Blast / Ki +5 / +10 / +15, extra health
  bars, ...), unlocked by level. Three can be worn.
- **Bounty Board** (Krillin): repeatable fights with a random corrupted target of the player's strength, for zeni and
  experience.
- **Wardrobe** (Videl): the outfits unlocked.
- **Patrol Menu** (START): Stats, Accessories, Techniques, Customize, Quest Log, Save, Return to Title.

## Progress

- Levels 1..50; experience from every battle (a little even from a loss). Attack, defense, ki blast and ki rise with the
  level (hidden Z items in each battle), each at its own levels.
- The warrior starts with three health bars and three blast stocks; Full Power Energy Wave, Full Power Energy Blast
  Volley, and Super Explosive Wave as the Ultimate.
- Zeni from every won battle; spent at the shop.

## The save

`patrol/save.json` next to the game (`BT3_PATROL=<folder>` another one): JSON, written after every battle, on Save, and
when leaving. The game's own save is never touched. Delete the file (or New Game) to start over.

## The warrior's look

Until the warrior has a look of their own (the Custom Character screen's Patrol mode), they fight as their race's
stand-in: Bardock (Saiyan), Nail (Namekian), Frieza 1st Form (Arcosian), Tien (Human), Majuub (Majin), Cell Jr
(Bio-Android). With a package, the save's `fighter` names it (the package's name, as `added:<name>` in a saga).

## For testing

- `BT3_TEST_MODMGR=patrol`: the mode at once; `=menupatrol`: the main menu on its item.
- `BT3_PATROL_SCRIPT="<frame>:<button>,..."`: buttons pressed on the screens (up down left right ok back sq tri l1 r1
  start), frames counted from the screens' opening.
- `BT3_PATROL_CONTINUE=1`: straight to the headquarters when there is a save.
- `BT3_PATROL_DUMP=1`: the last battle's saga written to `patrol/_last_battle.json`.
- `BT3_PATROL_HP=<percent>`, `BT3_PATROL_FOE_HP=<percent>`: the player's / the foe's starting health.

## How it is made

- `src/menu/menu_a.c`: main menu item 15; the Duel mode runs the mode with `gPortDuelRole` 5 (`src/menu/menu_g_b.c`).
- `src/menu/port_patrol.c`: the game's half (backdrop, model, sounds, the battle's setup), and
  `PortPatrol_AfterBattle`, which `src/menu/menu_a_b.c` calls after any battle.
- `port/src/gs/patrol_ui.cpp`: the screens, the content (races, masters, shop, bounties), the save, and the battles: each
  is a saga of one episode, made into its script by the Story Creator's compiler (`PortStoryUi_PrepareDoc`,
  `port/src/gs/story_ui.cpp`), which takes `cpu_level` (the AI's own scale, 0..40) and `player_items` / `enemy_items`
  (up to eight Z item ids per side) in a battle besides the documented keys (`docs/port/sagas.md`).
