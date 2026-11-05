# Sagas of the Story Creator

The Story Creator (main menu, "Story Creator", after Custom Character) makes and plays sagas of the player's own:
episodes of scenes on a stage, where the fighters talk in text boxes with voices, camera shots, poses and effects, and
battles with their own conditions, leading on to the next episode by how the battle went.

A saga is one JSON file in the `sagas` folder next to the game (`BT3_SAGAS=<folder>` another one). Copy a file there to
share a saga; the game lists every `*.json` of the folder (not those starting with `_`) on My Sagas. `_progress.json` in the
same folder keeps how far the player is in each saga. A sample saga is built into the game (`port/src/gs/story_sample.h`);
"Make a copy to edit" (Triangle on My Sagas) writes it into the folder.

The editor in the game writes everything below; a file can also be written by hand (UTF-8; unknown keys are kept as they
are; `//` comments are read but not written back).

## The file

```json
{
  "format": 1,
  "title": "Custom Saga: Rivals",
  "author": "who made it",
  "description": "what it is about (shown on My Sagas)",
  "episodes": [ { ... }, { ... } ]
}
```

## An episode

```json
{
  "title": "A Rival's Return",
  "description": "shown on the title card under the title",
  "stage": 23,
  "music": 8,
  "player": [{"chara": 3, "costume": 0}, {"chara": 32}],
  "enemy": [{"chara": 31}],
  "intro": { scene },
  "battle": { battle },
  "win": { scene },
  "lose": { scene },
  "next_win": "next",
  "next_lose": "retry"
}
```

- `stage`: a stage id of the disc (0 Wasteland - Noon ... 34 Planet - Evening, the order of the stage select), or
  `"added:<name>"` for a stage added from outside the disc (its name on the stage select; `docs/port/adding_stages.md`).
- `music`: the music list's value: 0..19 the disc's songs (0 The Meteor ... 8 Shine ... 19 Evolution), 24 random, an
  added song's value (26 and on, as the music select lists them).
- `player`, `enemy`: one to five fighters each. `chara` is a character id of the disc (0..160, the order of the character
  table: 3 Goku (End), 31 Vegeta, 133 Broly Legendary Super Saiyan ...) or `"added:<folder>"` for an added character
  (its folder in `characters`: a BT4 or custom fighter). `costume`: 0 the first. The first of each side fights first and
  stands in the scenes; the others come in when the one before is beaten (the game's team rule).
- `intro` is played before the battle, `win` after a won battle, `lose` after a lost one. Without lines a scene is
  skipped (after a fight: the game's own victory pose and line instead).
- `next_win`, `next_lose`: `"next"` (the next episode), `"retry"` (the same again; a loss only), `"end"` (the end of the
  saga), or an episode's number (1 = the first): a loss can lead to an episode of its own (a "what if").
  An episode without a fight goes on by `next_win`.

## A battle

```json
{
  "fight": true,
  "win": "ko",
  "survive": 60,
  "time": 0,
  "time_up": "health",
  "player_health": 100, "enemy_health": 100,
  "player_ki": 0, "enemy_ki": 0,
  "cpu": 1,
  "lines": [ { line said during the fight }, ... ]
}
```

- `fight`: false makes the episode its opening scene alone.
- `win`: `"ko"` knock out the whole other team; `"survive"` hold out for `survive` seconds (the clock shows HOLD OUT).
- `time`: a time limit in seconds (0 none); at its end `time_up` decides: `"win"`, `"lose"` or `"health"` (the one with
  more health left wins). Knocking out the other team always wins, being knocked out always loses.
- `player_health`, `enemy_health`: health at the start, percent. `player_ki`, `enemy_ki`: ki at the start, percent.
- `cpu`: the opponent's strength, 0 easy, 1 normal, 2 hard, 3 very hard, 4 super (the versus mode's levels).
- `lines`: said in a box at the bottom of the picture (the speaker's face, name and text; the voice with it) while the
  fight goes on. Each is a line (below) with `when` and `value`: `"start"`, `"enemy_health"` / `"player_health"` (the
  fighter's health drops to `value` percent), `"enemy_transform"` / `"player_transform"`, `"enemy_switch"` /
  `"player_switch"` (another team member comes in), `"time"` (`value` seconds into the fight). Each is said once.

## A scene

```json
{
  "distance": 2,
  "hide_player": false,
  "hide_enemy": false,
  "lines": [ { line }, ... ]
}
```

- `distance`: how far apart the two leaders stand, in the middle of the stage: 0 where the stage starts a fight, 1
  close, 2 talking distance, 3 far apart (measured in the height of the taller one, so a giant stands further off).
- `hide_player`, `hide_enemy`: that side's leader is not on the stage at the start (a line with the effect `show`
  brings them in).

## A line

```json
{
  "speaker": "enemy",
  "text": "Kakarot! I've waited a long time for this.",
  "name": "Prince Vegeta",
  "voice": {"chara": 31, "line": 0},
  "camera": "auto",
  "move": "push", "move_time": 3,
  "pose": "Point / taunt", "listener_pose": "Stand",
  "effects": ["aura_on", "shake"],
  "wait": 0
}
```

- `speaker`: `"player"` / `"enemy"` (the side's fighter on the stage), `"player2"`..`"player5"` / `"enemy2"`.. (a team
  mate: heard and read, with their face beside the text, while the leader stands on the stage), `"narrator"` (no name
  plate). `as`: another character's face and name for the speaker.
- `text`: what is said (up to 240 letters in the editor; the box types it out, Cross completes it, Cross again goes on).
- `name`: the name on the plate (the character's short name if not given).
- `voice`: one of a character's 100 spoken lines (the files the game plays for entrances and victories): `{"line": n}`
  the speaker's own (lip-synced), `{"chara": c, "line": n}` someone else's, `{"line": "random"}` or `"random"` a random
  one of the speaker's (or of `chara`) each time. The editor lists the lines a character has and plays them (Square).
- `camera`: `"auto"` (the first line both of them, then the speaker close, over the listener's shoulder, medium, in
  turn), `"close"`, `"medium"`, `"shoulder"`, `"two"` (both, from the side), `"wide"`, `"low"`, `"high"`,
  `"listener"` (on the one listening), `"keep"` (as the line before left it). Worked out from where the fighters stand
  and how tall they are, on any stage.
- `move`: `"cut"`, `"push"` (closing in), `"pull"` (moving back), `"orbit"`, `"from_wide"`, `"rise"`; over `move_time`
  seconds.
- `pose`, `listener_pose`: a pose by name (as the editor lists them: Stand, Fighting stance, Talk (calm), Point / taunt,
  Shout, Power up, Laugh ...) or a motion number of the character's own animation list. `pose_loop`: whether it repeats.
- `effects`: `"shake"`, `"big_shake"`, `"flash"` (white), `"aura_on"`, `"aura_off"`, `"charge_on"`, `"charge_off"`,
  `"burst"` (a ki burst), `"fade_in"` (from black), `"hide"`, `"show"` (the speaker leaves / appears); on the speaker.
- `wait`: 0 the line waits for Cross; else it goes on by itself that many seconds after its text is out (and its voice).

## How it is played (for the curious)

An episode is a battle of the story mode (battle mode 1), as Dragon History's are: the game's battle script engine (GSC,
`src/sys/gsc.c`) plays a script made of the episode (`port/src/gs/story_ui.cpp`, `compile`), in place of the script file
the battle would read. The scenes run during the battle's own intro and winner states, with the fighters on the real
stage, the fixed camera of the story scenes, the scripted moves; the Story Creator's own script commands (9001..9006,
`src/battle/port_story_btl.c`) place the fighters, aim the camera, play the voices and type out the text. START skips a
scene, as in Dragon History. `BT3_STORY_DUMP=<file>` writes the script made (testing).
