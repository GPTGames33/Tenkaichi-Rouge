# Sagas of the Story Creator

The Story Creator (main menu, "Story Creator", after Custom Character) makes and plays sagas of the player's own:
episodes of scenes on a stage, where the fighters talk in text boxes with voices, camera shots, poses and effects, and
battles with their own rules and events, leading on to the next episode by how the battle went (or by the answer the
player gave to a question).

A saga is one JSON file in the `sagas` folder next to the game (`BT3_SAGAS=<folder>` another one). Copy a file there to
share a saga; the game lists every `*.json` of the folder (not those starting with `_`) on My Sagas. A file that is not
a saga (no `episodes` list of `{ }`) is left out with a message. `_progress.json` in the same folder keeps how far the
player is in each saga, the episodes cleared and the best fight of each (the Story Creator's own file: never the game's
save). A sample saga is built into the game (`port/src/gs/story_sample.h`); "Make a copy to edit" (Triangle on My Sagas)
writes it into the folder.

The editor in the game writes everything below; a file can also be written by hand (UTF-8; unknown keys are kept as they
are; `//` comments are read but not written back). Every key added after the first version is optional: a saga without
them plays as it always did (`format` is still 1).

## In the game

- **My Sagas**: Cross plays a saga from where the player left it, Square edits it, Triangle has more: play from the
  first or any episode, duplicate, copy it to the clipboard as text (to share), reset the progress, delete (the file is
  kept beside as `<file>.deleted`). Each saga shows the episode reached, how many are cleared and the best rank.
  "New saga": empty, from an episode template, or the **What-if generator** (pick two characters: a short saga of a
  meeting with a question, a fight or a truce, a common enemy and an ending). "Paste a saga" reads one from the
  clipboard. "Open the sagas folder" opens it on the PC (Square reads it again).
- **Saga page**: title, author, description; settings (difficulty, tries, what happens when the tries run out); the
  episodes, each with where it leads (`win > 3  lose > again`, `then > end`; `?` when an answer to a question leads on
  too; `!` and a note in the help line when something is wrong); L2 / R2 move an episode up / down, Square has more
  (play it, duplicate, put a new one after it, move), Triangle deletes (after asking). Links by number follow the
  episodes they lead to when episodes move, are copied or deleted (a link to a deleted episode ends the saga instead).
  "+ Add an episode" offers templates: rival duel, boss with phases, team fight, survival, cutscene only. "Ending" is
  the saga's ending scene. "Check the saga" lists links that lead nowhere, episodes never reached, events that wait for
  an event that is not there.
- **Every editing page**: L1 undoes, R1 redoes (the whole saga, up to 80 steps), START saves. Leaving the saga with
  changes not saved asks: save, leave without saving, or stay.
- **Episode page**: stage, music, the teams (Square on a fighter sets that member's own health), the scenes (Square
  previews one), Fight rules, Events during the fight, Lines during the fight (the older, simpler way), where it leads,
  Play this episode.
- **Fight rules page**: how to win and to lose, the clock, the objective, the opponent's strength and exact level, each
  side's health, ki, blast stock, MAX power, starting form and boosts at the start.
- **Events page**: one row per event, "when" and what it does; an **Event page** sets the "when" and lists its
  actions in order; a line or a message of an event is edited on the line page, a cutscene on the scene page (Square
  on it: preview), the others on an action page.
- **Line page**: who speaks, the words, the name, the voice; shown as a text box, a narration card or a title card; a
  transition; camera, move, poses; where the two stand and which way they face; a sound effect and the music; a pause
  before the words; how it goes on; a question with up to three answers; effects. Square on a line of a list: new line
  after it, duplicate, copy, paste (lines copied from any scene).

## The file

```json
{
  "format": 1,
  "title": "Custom Saga: Rivals",
  "author": "who made it",
  "description": "what it is about (shown on My Sagas)",
  "settings": {"difficulty": 0, "lives": 0, "continue": false},
  "episodes": [ { ... }, { ... } ],
  "ending": { scene }
}
```

- `settings.difficulty`: -2..2, every opponent of the saga that many strengths weaker / stronger than its fight says
  (and 5 levels per step for an exact `cpu_level`).
- `settings.lives`: 0 as many tries as the player likes; else how many times an episode can be lost and tried again.
  When they run out the saga goes back to its first episode, or (`continue`: true) the episode is tried again anyway.
- `ending`: a scene played when the saga is cleared (after the last episode's win scene), before the credits on the
  result screen. It is played by the fighters of the episode that ended the saga, on its stage; `player`, `enemy`,
  `stage`, `music` in it choose others.

## An episode

```json
{
  "title": "A Rival's Return",
  "description": "shown on the title card under the title",
  "stage": 23,
  "music": 8,
  "player": [{"chara": 3, "costume": 0}, {"chara": 32, "health": 50}],
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
  (its folder in `characters`: a BT4 or custom fighter). `costume`: 0 the first. `health`: that member's own health at
  the start, percent (else the battle's `player_health` / `enemy_health`). The first of each side fights first and
  stands in the scenes; the others come in when the one before is beaten (the game's team rule).
- `intro` is played before the battle, `win` after a won battle, `lose` after a lost one. Without lines a scene is
  skipped (after a fight: the game's own victory pose and line instead).
- `next_win`, `next_lose`: `"next"` (the next episode), `"retry"` (the same again; a loss only), `"end"` (the end of the
  saga), or an episode's number (1 = the first): a loss can lead to an episode of its own (a "what if").
  An episode without a fight goes on by `next_win`. An answer to a question can lead elsewhere (a line's `choices`).

## A battle

```json
{
  "fight": true,
  "win": "ko", "win_value": 50, "survive": 60,
  "fail_member": false, "fail_health": 0,
  "time": 0, "time_up": "health",
  "objective": "", "show_objective": true,
  "cpu": 1, "cpu_level": 20,
  "player_health": 100, "enemy_health": 100,
  "player_ki": 0, "enemy_ki": 0,
  "player_blast": 3, "enemy_blast": 7,
  "player_max": false, "enemy_max": true,
  "player_form": 0, "enemy_form": 1,
  "player_boost": {"attack": 2, "defense": 0, "blast": 0, "ki": 0, "health": 1},
  "enemy_boost": {},
  "events": [ { event }, ... ],
  "lines": [ { line said during the fight }, ... ]
}
```

- `fight`: false makes the episode its opening scene alone.
- `win`: `"ko"` knock out the whole other team; `"survive"` hold out for `survive` seconds (the clock shows HOLD OUT);
  `"health"` bring the opponent's health down to `win_value` percent; `"blast"` the last blow has to be a blast (a
  super or ultimate move: a knock-out by anything else is a loss).
- `fail_member`: true, the fight is lost as soon as one of the player's team is knocked out. `fail_health`: the fight is
  lost when the player's fighter has that little health left (percent; 0 off).
- `time`: a time limit in seconds (0 none); at its end `time_up` decides: `"win"`, `"lose"` or `"health"` (the one with
  more health left wins). Knocking out the other team always wins (except a `"blast"` fight), being knocked out always
  loses. Cutscenes that stop the fight do not count on the clock.
- `objective`: the objective in the player's words (else it is written from the rules); `show_objective`: a banner with
  it over the picture as the fight starts (by default when a rule above other than a plain knock-out is set). The
  result screen shows it too, with the time the fight took, the health left and a rank (S 80% and more, A 60, B 40,
  C 20, D below), kept as the episode's best in `_progress.json`.
- `cpu`: the opponent's strength, 0 easy, 1 normal, 2 hard, 3 very hard, 4 super (the versus mode's levels);
  `cpu_level`: the computer's own scale 0..40 instead, for a finer choice.
- `player_health`, `enemy_health`: health at the start, percent. `player_ki`, `enemy_ki`: ki at the start, percent.
  `player_blast`, `enemy_blast`: blast stock bars at the start (0..7). `player_max`, `enemy_max`: start at MAX power.
  `player_form`, `enemy_form`: transform that many times as the fight starts (0..3, as far as the fighter's forms go).
- `player_boost`, `enemy_boost`: Z-items for the side's fighters by what they do, each a level: `attack`, `defense`,
  `blast` (ki blasts), `ki` 0..3, `health` (extra health bars) 0..2.
- `lines`: said in a box at the bottom of the picture (the speaker's face, name and text; the voice with it) while the
  fight goes on. Each is a line (below) with `when` and `value` as an event's. Each is said once.

## An event

```json
{"when": "enemy_health", "value": 50, "repeat": 0, "do": [ { action }, ... ]}
```

What happens during the fight: when `when` is so, its actions are done one after the other (events that happen together
wait their turn; nothing is judged while a cutscene plays). `repeat`: 0 once; else it happens again each time it is so,
no sooner than that many seconds after the last time.

- `when`: `"start"`; `"enemy_health"` / `"player_health"` (below `value` percent); `"enemy_transform"` /
  `"player_transform"`; `"enemy_switch"` / `"player_switch"` (another team member comes in); `"time"` (`value`
  seconds into the fight); `"time_left"` (`value` seconds left on the clock: a time limit or a hold-out);
  `"player_ki_full"` / `"enemy_ki_full"`; `"player_max"` / `"enemy_max"` (goes into MAX power); `"player_ko"` /
  `"enemy_ko"` (one of the team is knocked out); `"player_combo"` / `"enemy_combo"` (a combo of `value` hits);
  `"player_blast"` / `"enemy_blast"` (`value` blast stock bars); `"after_event"` (right after event number `value` of
  this list, from 1, has done all it does).
- An action is `{"do": kind, "side": "player" | "enemy" (default), "value": n}`:
  - `"say"`: the action is itself a line (below): said in the box, the fight goes on.
  - `"card"`: the action is a line whose text is put across the picture for a few seconds (`"card": "title"` big).
  - `"scene"`: the action is itself a scene (`distance`, `hide_player`, `hide_enemy`, `lines`): the fight stops (as
    soon as both fighters can be interrupted), the scene plays, the fight goes on where it was. Up to 24 in a fight.
  - `"heal"`, `"hurt"` (percent of health; a hurt never knocks out), `"set_health"` (percent), `"ki"` (bars, -5..5),
    `"blast"` (blast stock bars, -7..7), `"max_power"`, `"transform"` (`value` the form, 1..4), `"switch"` (`value` 0
    the next member standing, else member 1..5), `"use_blast"` (0 the first blast, 1 the second, 2 the ultimate),
    `"aura"` (`value` true on / false off): to `side`.
  - `"music"` (a song's value; none: silence), `"sound"` (a sound effect of the battle by number), `"stage"` (the stage
    torn up), `"shake"` (strength 1..50), `"flash"` (0 white, 1 red), `"cpu"` (the opponent's level 0..40 from now on),
    `"time"` (seconds added to the clock, or taken off), `"wait"` (tenths of a second before the next action),
    `"win"`, `"lose"` (the fight ends now).

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
  "card": "box",
  "transition": "none",
  "camera": "auto",
  "move": "push", "move_time": 3,
  "pose": "Point / taunt", "listener_pose": "Stand",
  "distance": 2, "stand": "air", "face": "front", "listener_stand": "ground", "listener_face": "back",
  "sound": 3, "music": 12,
  "effects": ["aura_on", "shake"],
  "delay": 0.5,
  "wait": 0,
  "choices": [{"text": "Fight him", "goto": 2}, {"text": "Run away", "goto": 3}]
}
```

- `speaker`: `"player"` / `"enemy"` (the side's fighter on the stage), `"player2"`..`"player5"` / `"enemy2"`.. (a team
  mate: heard and read, with their face beside the text, while the leader stands on the stage), `"narrator"` (no name
  plate). `as`: another character's face and name for the speaker.
- `text`: what is said (up to 240 letters in the editor; the box types it out, Cross completes it, Cross again goes on).
- `name`: the name on the plate (the character's short name if not given).
- `voice`: one of a character's 100 spoken lines (the files the game plays for entrances and victories): `{"line": n}`
  the speaker's own (lip-synced), `{"chara": c, "line": n}` someone else's, `{"line": "random"}` or `"random"` a random
  one of the speaker's (or of `chara`), another one each time the line is played; `{"story": s, "line": n}` a line
  voiced in Dragon History. The editor lists the lines a character has and plays them (Square).
- `card`: `"box"` the text box; `"narration"` the words alone on a dark band across the picture; `"title"` big words in
  the middle ("Three years later..."). A card has no face and no lip-sync.
- `transition`: `"fade_in"` (the picture comes from black), `"fade_out"` (it goes to black after the line: cards after
  it are read on the black, the next spoken line brings the picture back), `"flash"` (a white flash), `"dip"` (to black
  and back).
- `camera`: `"auto"` (the first line both of them, then the speaker close, over the listener's shoulder, medium, in
  turn), `"close"`, `"medium"`, `"shoulder"`, `"two"` (both, from the side), `"wide"`, `"low"`, `"high"`,
  `"listener"` (on the one listening), `"face"` (very close), `"behind"`, `"profile"`, `"top"`, `"ground"`, `"far"`,
  `"full"` (the whole body), `"keep"` (as the line before left it). Worked out from where the fighters stand and how
  tall they are, on any stage.
- `move`: `"cut"`, `"push"` (closing in), `"pull"` (moving back), `"orbit"`, `"orbit_back"`, `"from_wide"`, `"rise"`,
  `"drop"`, `"slide"`, `"crash"` (a fast zoom), `"pan"` (from the listener); over `move_time` seconds.
- `pose`, `listener_pose`: a pose by name (as the editor lists them: Battle stance, Lean in, Crouch, Arms open, Story
  gesture 1..8, Flying up, Diving, Landing ...) or a motion number of the character's own animation list. `pose_loop`:
  whether it repeats.
- `distance`: from this line on the two stand that far apart (1..3, as a scene's). `stand`, `listener_stand`:
  `"ground"`, `"air"` (floating), `"high"` (high up). `face`, `listener_face`: `"front"` (to the other), `"back"` (back
  turned), `"side"`.
- `sound`: a sound effect of the battle (by number) as the line starts. `music`: a song's value from this line on, or
  `"stop"`.
- `effects`, on the speaker unless said: `"shake"`, `"big_shake"`, `"flash"` (white), `"red_flash"`, `"rumble"` (the
  ground), `"dark"` (the picture dimmed for the line), `"aura_on"`, `"aura_off"`, `"charge_on"`, `"charge_off"`,
  `"burst"` (a ki burst), `"fade_in"` (slowly from black, over the text too), `"fade_out"`, `"hide"`, `"show"` (the
  speaker leaves / appears); `"listener_aura_on"`, `"listener_aura_off"`, `"listener_burst"`, `"listener_hide"`,
  `"listener_show"` on the one listening.
- `delay`: seconds the words wait after the camera and the effects have started.
- `wait`: 0 the line waits for Cross; else it goes on by itself that many seconds after its text is out (and its voice).
- `choices`: the line is a question; the player picks one of up to three answers (up / down, Cross). `goto`: an
  episode's number, `"end"`, `"retry"` (this episode again) or `"next"` (as the episode's own links say). The saga goes
  there when the episode is over: an answer given before the fight counts if the fight is won, one given after it
  (in the win or lose scene) whichever way it went.

## How it is played (for the curious)

An episode is a battle of the story mode (battle mode 1), as Dragon History's are: the game's battle script engine (GSC,
`src/sys/gsc.c`) plays a script made of the episode (`port/src/gs/story_ui.cpp`, `compile`), in place of the script file
the battle would read. The scenes run during the battle's own intro and winner states, with the fighters on the real
stage, the fixed camera of the story scenes, the scripted moves; the Story Creator's own script commands (9001..9007,
`src/battle/port_story_btl.c`) place the fighters, aim the camera, play the voices and type out the text, and a referee
(9004) watches the fight every frame: its rules, its events, the cutscenes that stop it (each an event action of the
script, started through the game's own trigger list). START skips a scene, as in Dragon History.

Testing switches: `BT3_TEST_MODMGR=story` opens the Story Creator; `BT3_TEST_STORY=play:<file|sample>:<episode>[:<scene>]`
plays an episode (or one scene: `intro`, `win`, `lose`, `ending`, `event:<e>:<a>`) at once; `BT3_STORY_AUTO=<frames>`
lines go on by themselves; `BT3_STORY_PICK=<n>` the answer given to every question (from 0); `BT3_STORY_SCRIPT`
presses buttons on the screens; `BT3_STORY_DUMP=<file>` writes the script made. The referee logs each event that
happens and each action it does (`bt3: story: event ...`).
