#!/usr/bin/env python3
"""Custom Character builder: makes a fighter of your own out of the characters on your disc (BT3 USA, SLUS-21678).

    python port/tools/custom_fighter.py build    fighter.json --game DIR          character package in DIR/characters/
    python port/tools/custom_fighter.py replace  fighter.json --game DIR          into gamedata/mods/ over a roster slot
    python port/tools/custom_fighter.py remove   fighter.json --game DIR          undoes `replace`
    python port/tools/custom_fighter.py list | inspect "Goku (End)" [costume] | moves "Goku (End)" | selftest
Options: --iso <BT3 USA disc image> (default: the .iso next to this repository), --game <the game's folder>.

`build` writes a character package (docs/port/character_packages.md): the game gives it a cell of its own on the
roster. It needs a build with character packages (the char-packages branch); the release does not read them yet.
`replace` puts the fighter in the place of an existing character's costume ("slot" in the file), which any build
loads (gamedata/mods/). Forms are only made by `build`.

A fighter file (JSON). Every part is optional except "name"; a character is {"chara": name or id, "costume": 1..16}
(a BT3 character has up to 4, a BT4 one up to 9):
    {
      "name": "Goku Black Rose", "title": "Super Saiyan Rose",
      "base": "Goku (End)",                      AI, roster tables, and whatever is not given (default: look)
      "look": {"chara": "Goku (End)"},           body and hair; or "costumes": [ {...}, {...} ] for several (up to 16)
      "moveset": {"chara": "Vegeta"},            everything the moves below do not change: melee, ki blasts, stats,
                                                 animations, effects, camera, move names (default: look)
      "melee": {"chara": "Broly"},               melee, rush and finishers
      "ki_blasts": {"chara": "Frieza Final Form"},
      "moves": {                                 each move slot from any character's slot of the same kind
        "blast1_1": {"chara": "Goku (End)", "slot": "blast1_2"},
        "blast2_1": {"chara": "Vegeta", "slot": "blast2_2"},
        "blast2_2": {"chara": "Gohan"},          ("slot" defaults to the same slot)
        "ultimate": {"chara": "Super Perfect Cell"}
      },
                                                 a move may also take the look and the camera of other moves:
        "blast2_2": {"chara": "Vegeta", "slot": "blast2_1",
                     "effects": {"chara": "Frieza Final Form", "slot": "ultimate"},   its effects: beam, ball, size
                     "camera": {"chara": "Vegeta", "slot": "blast2_2"}},             how it is filmed
        "ultimate": {"chara": "Vegeta", "effects": ..., "camera": ...,              (camera: all three below)
                     "camera_in": {"chara": "Cell"},                                the opening (an Ultimate's)
                     "camera_loop": {"chara": "Gohan", "slot": "blast2_1"},         from the start to the firing
                     "camera_finish": {"chara": "Goku (End)", "slot": "ultimate"}}  a rush's cinematic finish
                                                 (an entry without "chara": the moveset's own move; see "Mixed
                                                 moves" below for what each takes and when it cannot)
      "names": {"blast2_1": "Galick Kamehameha", "ultimate": "Rose Spirit Bomb"},
      "attack_tint": {"ultimate": {"hue": 280, "strength": 75, "light": 90}},   one colour for a whole move: its
                                                 main colour becomes hue (0..360) / strength (saturation, 0..100) /
                                                 light (brightness, 0..100), its other colours follow; then
      "attack_colours": {"ultimate": [{"from": "#3050ff", "to": "#ff3020", "range": 1.5}]},   colour by colour
                                                 ("blast1_1".."ultimate", "ki_blasts", "charge")
      "voice": {"chara": "Goku (End)"},          spoken lines, bank, lip sync, pre-fight lines (default: base)
      "portrait": {"chara": "Goku (End)"},       the menus' picture and the team picture (default: base)
      "health": 50000, "ki": 20000, "blast": 5,  10000 a bar; 100000 a full ki gauge; stocks 0..7
      "forms": {"up": "my_other_fighter.json"},  left / up / right: transformations (R3 + direction), down: back
                                                 (another fighter file, "disc:<chara>", or none: a direction not
                                                 named leads nowhere; a form not naming down goes back to its owner)
      "form_costs": {"up": 2},                   blast stocks a transformation costs
      "form_kinds": {"up": "power_up"},          power_up (the burst), quick (the burst, no waiting), big (a long
                                                 powering up), instant (no burst); not said: the moveset's table
      "form_bursts": {"up": "#a040ff"},          the transformation's burst in that colour (not said: the game's)
      "form_default": "up",                      the transformation of R3 alone (not said: the moveset's table)
      "slot": {"chara": "Goku (End)", "costume": 1}   `replace` only: whose place it takes
      "paint": {"1:5": {"w": 128, "h": 128, "sig": "1a2b3c4d", "png": "<base64>"}}   painted pixels of a texture:
                                                 "<costume>:<texture>" of the built model, an RGBA PNG of its size
                                                 (alpha 255: painted that colour), after the colour changes
    }

What lives where (decompilation: CUSTOM_CHARACTER_RESEARCH.md; numbers are game entries, BObjFile_GetEntry: entry
n of a pack is part n - 1):
- character c: model packs 0x590 + c*10 + costume (+4 damaged), animations +8 (motion N = entry N + 1), effects +9;
  voice bank 0xC7B + c (English) / 0xBDA + c (Japanese); spoken lines 0xCC32 / 0x8D4E + c*100 + line (100 each,
  second archive); portrait 0x2F9 + c; team picture 8 + c*2 + side.
- model pack entries: 0x12 general, 0x13 melee, 0x14 ki blasts, 0x15 movement, 0x16 pre/post-fight talk table,
  0x17 hair / cloth chains, 0x18 Blast 2 + Ultimate (arrays of 3), 0x19 Blast 1 (arrays of 2), 0x1B AI,
  0x1C camera cuts (0x24 each: 0..2 the starts of slots 2..4, 3 the Ultimate's intro), 0x2D..0x34 the move list
  text in 8 languages, 0x35.. lip sync (0x35 + line, + 0x64 for the other language).
- a move slot: its elements of 0x18 / 0x19, its motions in +8 (Blast 1: 0x103 + slot; Blast 2 / Ultimate: 0x22 from
  0x105 + (slot - 2) * 0x22, the Ultimate also 0x16B), its effect pack (+9 entry slot + 1), its camera cut, and its
  block of the move list text (the n-th '*' line and the lines after it).

Mixed moves (decompilation: src/battle/btl_act_f.c, btl_tech_b.c, eft_h.c .. eft_l_b.c, btl_char_cam_*.c, eft_a.c):
- The motions reach the effects only through event bits (0x200 start, 0x400 fire, 0x800 end, ...) and body nodes,
  never by an index into the effect pack: another move's effect pack plays where the motions raise those events.
  But the effect module is more than a look: for a projectile it carries the hitbox and ends the fighter's charge
  and firing loops. So "effects" takes, as one unit, move B's effect pack (+9 entry slot + 1; for a Blast 2 /
  Ultimate keeping A's entry 3, the rush scene's placement of the fighters, which belongs to A's motions) and B's
  fields of 0x18 / 0x19 that pick and shape the effect (EFFECT_SUPER / EFFECT_SKILL: the effect type 0x138, its
  flags but the camera shake bits, size, speed of a shot, pieces, impacts, streaks, tint); A keeps its motions,
  action type 0x13E, damage, timing, reactions, ki, camera. It is done only where the game itself pairs A's action
  type with B's effect type (FIT_SUPER, from every disc character) and both are rushes or both are not (0x13B 5);
  for a Blast 1, the same effect type and kind. Where they do not fit, A keeps its own effects in B's colours (a
  note says so): a projectile's effect cannot drive a rush and the other way round.
- Cameras: 0x1C holds 10 orbit cuts of 0x24; cut slot - 2 is a technique's opening, which runs until it fires
  ("camera", "camera_loop": also its camera shake bits, 0x18 +0 0x1C000000), cut 3 the Ultimate's intro (motion
  0x16B; "camera_in": from an Ultimate). The middle of a beam is the battle's camera, with no data of its own. A
  rush that connects plays a keyframed camera of its own: effects entry 6 (the general pack), its entry 8, entry
  slot - 1, with the times of 0x18 +0x22C / +0x238 ("camera_finish"); only a rush has one (effect types 2 and 9:
  EftShotTech / EftRushShot), so a beam keeps the battle's camera whatever is chosen.
Standard library only.
"""
import argparse, json, pathlib, re, struct, sys

ALIGN = 64
COSTUME_MAX = 16  # the costumes a package may have (the game: PORT_COSTUME_MAX, include/port_chara.h); the disc has 4
FIRST_CHARA_FILE = 0x590
PT2_FIRST_ID = 0xD48
VOICE_BANK_US, VOICE_BANK_JP = 0xC7B, 0xBDA
VOICE_LINE_US, VOICE_LINE_JP = 0xCC32, 0x8D4E
PORTRAIT_FILE, MEMBER_FILE = 0x2F9, 8

E_GENERAL, E_MELEE, E_KIBLAST, E_MOVE, E_TALK, E_CHAINS, E_SUPER, E_SKILL, E_AI, E_CUTS = (
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1B, 0x1C)
MOVESET_ENTRIES = (E_GENERAL, E_MELEE, E_KIBLAST, E_MOVE, 0x1A, E_SUPER, E_SKILL, E_AI, E_CUTS)
TEXT_ENTRIES = range(0x2D, 0x35)
LIP_ENTRIES = range(0x35, 0x35 + 0xC8)
CUT_SIZE = 0x24

SLOTS = {"blast1_1": 0, "blast1_2": 1, "blast2_1": 2, "blast2_2": 3, "ultimate": 4}
SLOT_KIND = {0: "blast1", 1: "blast1", 2: "blast2", 3: "blast2", 4: "ultimate"}

# Element layout of the two structures of arrays: (offset, element size, rows); element i of row r of a field is at
# offset + (r * n + i) * size, n = 3 for 0x18, 2 for 0x19. From the readers in btl_tech_b.c and eft_h.c.
SUPER_LAYOUT = (
    [(0x000, 4, 1), (0x00C, 4, 1), (0x018, 2, 1), (0x01E, 2, 1)]
    + [(o, 4, 1) for o in range(0x024, 0x090, 0xC)]
    + [(o, 1, 1) for o in range(0x090, 0x0A8, 3)]
    + [(0x0A8, 1, 6), (0x0BA, 1, 8), (0x0D4, 4, 8), (0x134, 1, 1), (0x138, 1, 1)]
    + [(o, 1, 1) for o in range(0x13B, 0x153, 3)]
    + [(0x153, 1, 2)]
    + [(o, 1, 1) for o in range(0x159, 0x16B, 3)]
    + [(o, 4, 1) for o in range(0x16C, 0x208, 0xC)]
    + [(0x208, 2, 1), (0x20E, 2, 1), (0x214, 4, 1)]
    + [(o, 1, 1) for o in range(0x220, 0x22C, 3)]
    + [(0x22C, 4, 1), (0x238, 4, 1)])
SKILL_LAYOUT = (
    [(0x00, 4, 1), (0x08, 4, 1), (0x10, 2, 1), (0x14, 2, 1)]
    + [(o, 4, 1) for o in range(0x18, 0x38, 8)]
    + [(o, 1, 1) for o in range(0x38, 0x48, 2)]
    + [(0x48, 1, 6), (0x54, 1, 6)]
    + [(o, 1, 1) for o in range(0x60, 0x74, 2)]
    + [(o, 4, 1) for o in range(0x74, 0x94, 8)]
    + [(o, 1, 1) for o in range(0x94, 0xA0, 2)]
    + [(o, 4, 1) for o in range(0xA0, 0xC8, 8)])

# ---- mixed moves (see "Mixed moves" in the notes above)
# The fields of 0x18 that pick and shape a technique's effect (EftShot_BuildParam, eft_h.c): taken with its effect
# pack. (The flags +0 too, but for the camera shake bits; the technique id +0x18 only where an effect module or the
# other move's one reads it.)
EFFECT_SUPER = ([(0x030, 4, 1), (0x03C, 4, 1), (0x048, 4, 1), (0x06C, 4, 1), (0x078, 4, 1), (0x084, 4, 1)]
                + [(o, 1, 1) for o in range(0x090, 0x0A8, 3)]
                + [(0x0A8, 1, 6), (0x0BA, 1, 8), (0x0D4, 4, 8), (0x134, 1, 1), (0x138, 1, 1),
                   (0x15C, 1, 1), (0x15F, 1, 1), (0x162, 1, 1), (0x214, 4, 1)])
EFFECT_SKILL = ([(0x00, 4, 1), (0x30, 4, 1)] + [(o, 1, 1) for o in range(0x38, 0x48, 2)]
                + [(0x48, 1, 6), (0x60, 1, 1), (0x6E, 1, 1), (0x70, 1, 1)])
SHAKE_BITS = 0x1C000000  # 0x18 +0: shake at the fire event / every frame of the firing / every camera near
# The effect types (0x138) the disc gives each action type (0x13E): 0 / 7 beam, 1 two-stage, 2 charge, 3 repeat,
# 4 / 5 homing rush, 6 straight rush, 7 throw, 8 flying, 9 warp beam, 10 strike (BtlAct_GetTechniqueAction).
FIT_SUPER = {0: {0, 1, 3, 5, 6}, 1: {4}, 2: {0}, 3: {1}, 4: {2}, 5: {2}, 6: {2}, 7: {8, 9}, 8: {7}, 9: {0}, 10: {2}}
ACTION_KIND = {0: "blast", 1: "two-stage blast", 2: "charged blast", 3: "rapid fire", 4: "rush", 5: "rush",
               6: "rush", 7: "throw", 8: "flying attack", 9: "warp blast", 10: "strike"}
EFFECT_IDS = {0x158, 0x202, 0x19A, 0x26E, 0x26F, 0x1C6, 0x165, 0x166, 0x157, 0x193, 0x194, 0x188, 0x1A3, 0x14B,
              0x2B4, 0x27C, 0x162, 0x167, 0x168, 0x1F3, 0x282, 0x2A8, 0x19D, 0x290}  # read by effect modules
BEHAVIOUR_IDS = {0x268, 0x2CD, 0x2D5, 0x27F, 0x290}  # special damage, charge time, throw: the move's own
FINISH_ENTRY = 8  # effects entry 6 (the general pack): entry 8 holds the rush cameras, entry slot - 1 each


def super_info(data, i):
    """(flags, technique id, effect type, rush kind 0x13B, action type 0x13E) of element i of an entry 0x18."""
    return (struct.unpack_from("<I", data, i * 4)[0], struct.unpack_from("<h", data, 0x18 + i * 2)[0],
            struct.unpack_from("b", data, 0x138 + i)[0], data[0x13B + i], struct.unpack_from("b", data, 0x13E + i)[0])


def skill_info(data, i):
    """(effect type, kind 0x62) of element i of an entry 0x19."""
    return struct.unpack_from("b", data, 0x60 + i)[0], data[0x62 + i]


def try_split(data):
    """The parts of a pack as the game reads them (its offsets, whatever their alignment: a BT4 pack's are 16-byte
    aligned), None where it is not a pack."""
    if not data or len(data) < 8:
        return None
    n = struct.unpack_from("<I", data)[0]
    if n == 0 or n > 4096 or 4 + (n + 1) * 4 > len(data):
        return None
    offs = struct.unpack_from(f"<{n + 1}I", data, 4)
    if offs[0] < 4 + (n + 1) * 4 or list(offs) != sorted(offs) or offs[-1] > len(data):
        return None
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def has_effect_set(slot_pack):
    """Whether a technique's effect pack holds an emitter set with parts (entry 4: the header)."""
    parts = try_split(slot_pack)
    if not parts or len(parts) < 4 or len(parts[3]) < 0x40:
        return False
    h = parts[3]
    groups = [h[0x40 + g * 0x20:0x43 + g * 0x20] for g in range(h[4]) if 0x43 + g * 0x20 <= len(h)]
    return any(k and n for k, n, _ in groups)


def effects_fit(a_data, a_slot, b_data, b_slot, b_pack, a_name="it", b_name="that one"):
    """Whether move b's effects can play move a's (entries 0x18 / 0x19 of the two, the slots, b's effect pack):
    (True, "") or (False, why)."""
    if a_slot < 2:
        ta, ka = skill_info(a_data, a_slot)
        tb, kb = skill_info(b_data, b_slot)
        if ta < 0 or tb < 0:
            return False, f"{a_name if ta < 0 else b_name} has no effect to trade"
        if (ta, ka) != (tb, kb):
            return False, f"{b_name}'s effect is of another kind than {a_name}'s"
        return True, ""
    _, ida, _, suba, act = super_info(a_data, a_slot - 2)
    _, idb, tb, subb, actb = super_info(b_data, b_slot - 2)
    if tb not in FIT_SUPER.get(act, ()):
        return False, (f"{a_name} is a {ACTION_KIND.get(act, 'technique')} and {b_name} a "
                       f"{ACTION_KIND.get(actb, 'technique')}: its effects cannot drive it")
    if (suba == 5) != (subb == 5):
        return False, f"{a_name} and {b_name} are not both rushes"
    if ida in BEHAVIOUR_IDS:
        return False, f"{a_name} works in a way of its own"
    if tb != 2 and not has_effect_set(b_pack):
        return False, f"{b_name} has no effects of its own"
    return True, ""


def is_rush_effect(data, slot):
    """Whether a Blast 2 / Ultimate's effect module plays a camera of its own when it connects (types 2 and 9)."""
    return slot >= 2 and super_info(data, slot - 2)[2] in (2, 9)


def finish_camera(eff, slot):
    """A technique's rush camera (bytes, b"" none) in an effects pack (Pack)."""
    gen = try_split(eff[6]) if slot >= 2 else None
    if not gen or len(gen) < FINISH_ENTRY:
        return b""
    cams = try_split(gen[FINISH_ENTRY - 1])
    return cams[slot - 2] if cams and len(cams) > slot - 2 else b""


def set_finish_camera(eff, slot, cam):
    """Puts a rush camera into effects pack `eff` (Pack) for slot `slot`; False when its pack has no place for it."""
    gen = try_split(eff[6])
    if not gen or len(gen) < FINISH_ENTRY:
        return False
    cams = try_split(gen[FINISH_ENTRY - 1])
    if not cams or len(cams) <= slot - 2:
        return False
    cams[slot - 2] = cam
    gen[FINISH_ENTRY - 1] = join_pack(cams)
    eff[6] = join_pack(gen)
    return True

# Motions (ids into the animation pack) of the groups.
MELEE_MOTIONS = (list(range(0x37, 0x41)) + list(range(0x4C, 0x5C)) + list(range(0x5E, 0x6B)) + [0x6D, 0x70, 0x93]
                 + [m + k for m in (0x41, 0x44, 0x47, 0x90, 0x56, 0x59, 0x199) for k in range(3)])
KIBLAST_MOTIONS = list(range(0x71, 0x90))

NAMES = [
    "Goku (Early)", "Goku (Med)", "Goku (Med) Super Saiyan", "Goku (End)", "Goku (End) Super Saiyan", "Goku (End) Super Saiyan 2",
    "Goku (End) Super Saiyan 3", "Goku (GT)", "Goku (GT) Super Saiyan", "Goku (GT) Super Saiyan 3", "Goku (GT) Super Saiyan 4", "Kid Goku",
    "Great Ape", "Kid Gohan", "Teen Gohan", "Teen Gohan Super Saiyan", "Teen Gohan Super Saiyan 2", "Gohan",
    "Gohan Super Saiyan", "Gohan Super Saiyan 2", "Great Saiyaman", "Ultimate Gohan", "Piccolo (Early)", "Piccolo (End)",
    "Nail", "Krillin", "Yamcha", "Tien", "Chiaotzu", "Vegeta (Scouter)",
    "Vegeta (Scouter) Great Ape", "Vegeta", "Vegeta Super Saiyan", "Super Vegeta", "Vegeta (2nd Form)", "Vegeta (2nd Form) Super Saiyan",
    "Vegeta (2nd Form) Super Saiyan 2", "Majin Vegeta", "Vegeta (2nd Form) Super Saiyan 4", "Trunks (Sword)", "Trunks (Sword) Super Saiyan", "Trunks",
    "Trunks Super Saiyan", "Super Trunks", "Kid Trunks", "Kid Trunks Super Saiyan", "Goten", "Goten Super Saiyan",
    "Gotenks", "Gotenks Super Saiyan", "Gotenks Super Saiyan 3", "Vegito", "Super Vegito", "Super Gogeta",
    "Gogeta Super Saiyan 4", "Mr Satan", "Videl", "Great Saiyaman 2", "Supreme Kai", "Kibito Kai",
    "Yajirobe", "Paikuhan", "Tapion", "Pan", "Uub", "Majuub",
    "Master Roshi", "Master Roshi Full Power", "Grandpa Gohan", "Namu", "Hatchan", "King Vegeta",
    "King Vegeta Great Ape", "Bardock", "Bardock Great Ape", "Celipa", "Celipa Great Ape", "Raditz",
    "Raditz Great Ape", "Saibamen", "Nappa", "Nappa Great Ape", "Zarbon", "Zarbon Transformed",
    "Dodoria", "Cui", "Ginyu", "Recoome", "Burter", "Jeice",
    "Guldo", "Frieza 1st Form", "Frieza 2nd Form", "Frieza 3rd Form", "Frieza Final Form", "Frieza 100% Power",
    "Mecha Frieza", "King Cold", "Appule", "Frieza Soldier", "Android 16", "Android 17",
    "Android 18", "Android 19", "Android 2", "Cell 1st Form", "Cell 2nd Form", "Perfect Cell",
    "Super Perfect Cell", "Cell Jr", "Babidi", "Dabura", "Majin Buu", "Evil Buu",
    "Super Buu", "Super Buu Gotenks Absorbed", "Super Buu Gohan Absorbed", "Kid Buu", "Garlic Jr", "Super Garlic Jr",
    "Wheelo", "Turles", "Turles Great Ape", "Slug", "Slug Giant", "Sauza",
    "Cooler", "Cooler Final Form", "Metal Cooler", "Android 13", "Android 13 Fusion", "Broly",
    "Broly Super Saiyan", "Broly Legendary Super Saiyan", "Zangya", "Bojack", "Bojack 100% Power", "Janemba",
    "Super Janemba", "Hildegarn", "Baby Vegeta", "Super Baby 1", "Super Baby 2", "Super Baby Great Ape",
    "Super 17", "Nuova Shenron", "Shin Shenron", "Omega Shenron", "Taopaipai", "Cyborg Tao",
    "General Blue", "Akuman", "Pilaf Machine", "Pilaf Machine Fusion", "Tambourine", "Demon King Piccolo",
    "Arale", "Chichi", "Spopovich", "Future Gohan", "Future Gohan Super Saiyan",
]


class Fail(Exception):
    pass


# BT4 characters (the Team BT4 mod's disc, read through port/tools/bt4) have ids from BT4_BASE: BT4_BASE + the BT4
# character id. They can be any donor (look, moves, voice, portrait); the base (AI, roster tables) is a BT3 one.
BT4_BASE = 1000
_bt4 = {"disc": None, "names": None, "iso": None}


def bt4_iso():
    if _bt4["iso"] is None:
        # next to the repository (the folder above it; from a git worktree inside it, further up)
        name_ = "SLUS_219.78.DBZBT4B14REV2ENG.iso"
        here = pathlib.Path(__file__).resolve()
        found = [p / name_ for p in here.parents[3:] if (p / name_).is_file()]
        _bt4["iso"] = str(found[0] if found else here.parents[3] / name_)
    return _bt4["iso"]


def bt4():
    """The BT4 disc (BT4Chars of port/tools/bt4/bt4_to_bt3.py), opened when first needed."""
    if _bt4["disc"] is None:
        here = pathlib.Path(__file__).resolve().parent / "bt4"
        if str(here) not in sys.path:
            sys.path.insert(0, str(here))
        if not pathlib.Path(bt4_iso()).is_file():
            raise Fail(f"no BT4 disc image at {bt4_iso()}")
        import bt4_disc, bt4_to_bt3
        _bt4["disc"] = bt4_to_bt3.BT4Chars(bt4_disc.BT4Disc(bt4_iso()))
        _bt4["cv"] = bt4_to_bt3
    return _bt4["disc"]


def bt4_names():
    """[(BT4 id, "BT4: name (form)", costumes)] of the BT4 characters that have a model."""
    if _bt4["names"] is None:
        d = bt4()
        out = []
        for c, n in enumerate(d.ch.costumes):
            label = d.ch.display_name(c).strip()
            if n and label:
                out.append((c, "BT4: " + label, n))
        _bt4["names"] = out
    return _bt4["names"]


def portrait_module():
    here = pathlib.Path(__file__).resolve().parent / "creator"
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    import portrait
    return portrait


# ------------------------------------------------------------------------------------------------- colours
# A model's textures (entry 0xC) are 4- or 8-bit with palettes (CLUTs); a colour change rewrites the palette
# entries near one colour of one texture, keeping each entry's shading (its brightness relative to the colour).
def hex_rgb(s):
    s = s.strip().lstrip("#")
    if not re.fullmatch(r"[0-9a-fA-F]{6}", s):
        raise ValueError(s)
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def _hsv(rgb):
    import colorsys
    return colorsys.rgb_to_hsv(*(v / 255 for v in rgb))


def colour_near(rgb, ref, rng=1.0):
    """Whether palette colour rgb is a shade of colour ref: grey shades by brightness, colours by hue and saturation."""
    h, s, v = _hsv(rgb)
    H, S, V = _hsv(ref)
    if S < 0.15 or s < 0.15:
        return s < 0.22 * rng + 0.05 and S < 0.22 * rng + 0.05 and abs(v - V) < 0.28 * rng
    dh = min(abs(h - H), 1 - abs(h - H))
    return dh < 0.055 * rng and abs(s - S) < 0.4 * rng and abs(v - V) < 0.55 * rng


def colour_shift(rgb, ref, to):
    import colorsys
    h, s, v = _hsv(rgb)
    H, S, V = _hsv(ref)
    h2, s2, v2 = _hsv(to)
    s_new = min(1.0, s2 * (s / S if S > 0.05 else 1.0))
    v_new = min(1.0, v2 * (v / V if V > 0.02 else 1.0))
    r, g, b = colorsys.hsv_to_rgb(h2, s_new, v_new)
    return bytes((round(r * 255), round(g * 255), round(b * 255)))


def texture_cluts(t):
    """(texture index, data offset, entries) of every texture's palette upload in a texture file."""
    count, eo = struct.unpack_from("<II", t, 0)
    out = []
    for i in range(min(count, 255)):
        o = eo * 4 + i * 0x40
        if o + 0x40 > len(t):
            break
        clut = struct.unpack_from("<I", t, o + 4)[0]
        if not clut:
            continue
        p, end, w = clut * 4, min(len(t), clut * 4 + 0x100), None
        while p + 16 <= end:  # the upload packet: A+D registers (TRXREG: width, height), then the IMAGE data
            lo, hi = struct.unpack_from("<QQ", t, p)
            nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) & 15
            if lo == 0 and hi == 0:
                p += 16
                continue
            if flg == 0 and hi == 0xE and nreg == 1:
                for k in range(nloop):
                    val, reg = struct.unpack_from("<QQ", t, p + 16 + k * 16)
                    if reg & 0xFF == 0x52:
                        w, h = val & 0xFFF, (val >> 32) & 0xFFF
                p += 16 + nloop * 16
                continue
            if flg == 2 and w:
                out.append((i, p + 16, min(w * h, nloop * 4)))
                break
            p += 16
    return out


def recolour(texfile, changes):
    """The texture file with its palettes changed: changes [(texture index, or None for every texture, from, to,
    range)]."""
    t = bytearray(texfile)
    for index, off, n in texture_cluts(t):
        for tex, ref, to, rng in changes:
            if tex is not None and tex != index:
                continue
            for k in range(n):
                a = off + k * 4
                if colour_near(bytes(t[a:a + 3]), ref, rng):
                    t[a:a + 3] = colour_shift(bytes(t[a:a + 3]), ref, to)
    return bytes(t)


E_EYES, E_FACE_MOUTH = 0xD, (0xE, 0xF, 0x10, 0x11)


def face_texture(m):
    """The face texture's index (model header +0x4C) of a model pack, None if it has no model."""
    model = m[3]
    return model[0x4C] if model and model[:4] == b"pmdl" and len(model) > 0x4C else None


def texture_map(src, dst, src_face, dst_face):
    """{texture index of texture file src: the index of the same texture in texture file dst}: the one with the same
    pixels (and palette, where several have them), else the same palette; the face texture is the face texture. A
    costume's damaged model often has its textures in another order (36 of the disc's 349: Piccolo's face is 5,
    damaged 6), so a colour change named by the costume's texture is put on the damaged model's through this."""
    def keys(t):
        count, eo = struct.unpack_from("<II", t, 0)
        out = {}
        for i in range(min(count, 255)):
            o = eo * 4 + i * 0x40
            if o + 0x40 > len(t):
                break
            pix, clut, psize, csize = struct.unpack_from("<IIii", t, o)
            out[i] = (bytes(t[pix * 4:pix * 4 + max(psize, 0)]) if pix else b"",
                      bytes(t[clut * 4:clut * 4 + max(csize, 0)]) if clut else b"")
        return out
    a, b = keys(src), keys(dst)
    out = {}
    for i, (pix, clut) in a.items():
        same = [j for j, (p, _) in b.items() if p == pix and pix]
        pick = next((j for j in same if b[j][1] == clut), same[0] if same else None)
        if pick is None:
            pick = next((j for j, (_, c) in b.items() if c == clut and clut), None)
        if pick is not None:
            out[i] = pick
    if src_face is not None and dst_face is not None:
        out[src_face] = dst_face
    return out


def recolour_model(m, changes):
    """A model pack (Pack) recoloured: its textures (entry 0xC), and the face's other pictures with the changes of
    the face texture. The game does not draw the face texture (model header +0x4C) itself all the time: while the
    eyes blink it uploads one of the eye frames of entry 0xD in its place, and for mouth shapes 5..8 one of the
    mouth pictures of entries 0xE..0x11 (BtlObj_GetFaceTexture, src/battle/bobj_a.c; ObjDraw_UploadTextures,
    src/sys/gfxm_d_c.c), each a texture file with a palette of its own: changed in 0xC alone, the face shows its
    old colours whenever it blinks or talks."""
    if not changes:
        return m
    m[0xC] = recolour(m[0xC], changes)
    model = m[3]
    if model and model[:4] == b"pmdl" and len(model) > 0x4C:
        face = [(None, ref, to, rng) for tex, ref, to, rng in changes if tex == model[0x4C]]
        for e in (E_EYES,) + E_FACE_MOUTH:
            if face and m[e] and len(m[e]) >= 8:
                m[e] = recolour(m[e], face)
    return m


def texture_colours(model_pack, limit=8):
    """For the creator: [(texture index, [(#rrggbb, share of the texture's pixels)])] of a model: the main colours
    of each texture (shades grouped, most used first), as they look in the picture."""
    here = pathlib.Path(__file__).resolve().parent / "creator"
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    import model as mdl
    out = []
    for tex in mdl.decode_texfile(model_pack[0xC]):
        px = tex.get("rgba")
        if not px:
            continue
        groups = []  # [reference rgb, count, sum r, sum g, sum b]
        n = len(px) // 4
        step = max(1, n // 4096)
        for k in range(0, n, step):
            e = px[k * 4:k * 4 + 4]
            if e[3] < 128:
                continue
            rgb = bytes(e[:3])
            for g in groups:
                if colour_near(rgb, g[0]):
                    g[1] += 1
                    g[2] += e[0]; g[3] += e[1]; g[4] += e[2]
                    break
            else:
                groups.append([rgb, 1, e[0], e[1], e[2]])
        groups.sort(key=lambda g: -g[1])
        total = sum(g[1] for g in groups) or 1
        cols = []
        for g in groups[:limit]:
            if g[1] / total < 0.02:
                continue
            cols.append(("#%02x%02x%02x" % (g[2] // g[1], g[3] // g[1], g[4] // g[1]), round(g[1] / total, 3)))
        out.append((tex["index"], cols))
    return out


# ------------------------------------------------------------------------------------------------- paint
# The texture painter (the game's Custom Character screen, port/src/gs/creator_ui.cpp): "paint" in the spec is, per
# texture of a costume of the built model, the pixels painted over it (an RGBA PNG: alpha 255 = painted). Applied
# after the colour changes, into the texture's own palette (4 bits: 16 colours, 8 bits: 256): a new colour takes a
# palette entry no pixel uses any more, or - when there is none - the two closest colours are merged (the painted
# ones kept as they are where they can be). A painted pixel keeps its alpha. The same pixels are painted on every
# texture that is the same picture where it is (the face's eye frames and mouth textures, entries 0xD..0x11, which
# the game puts in its place while it blinks and talks, and the damaged costume's copy of the texture).
PAINT_ENTRIES = (0xC, 0xD, 0xE, 0xF, 0x10, 0x11)
PAINT_SHARE = 0.5  # another texture is "the same picture" when this much of its opaque pixels are the painted one's


def model_module():
    here = pathlib.Path(__file__).resolve().parent / "creator"
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    import model as mdl
    return mdl


def tex_layout(t, i):
    """Texture i of texture file t as the painter writes it: {w, h, bits, cells, pal, clut, sig}: cells[p] = (byte
    offset, nibble shift or -1 for a byte) of pixel p's palette index in t (None: not uploaded), pal[k] = the file
    offset of palette entry k (None: none); sig = the crc of its pixels' upload (the same whatever its colours).
    None when it is not a palettised texture."""
    import zlib
    mdl = model_module()
    count, ent = struct.unpack_from("<II", t, 0)
    if i < 0 or i >= count or ent * 4 + (i + 1) * 0x40 > len(t):
        return None
    o = ent * 4 + i * 0x40
    pix, clut, _, _, _, _, pblt, _, _, _ = struct.unpack_from("<IIiiiiIIii", t, o)
    tex0 = struct.unpack_from("<Q", t, o + 0x30)[0]
    psm, tbw = (tex0 >> 20) & 0x3F, (tex0 >> 14) & 0x3F
    tw, th, csa = 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15), (tex0 >> 56) & 0x1F
    if psm not in (0x13, 0x14) or not clut or (pblt >> 8) & 0x3F:
        return None
    try:
        uw, uh, dofs, _ = mdl.parse_gs_upload(t, pix * 4)
        cw, ch, cofs, _ = mdl.parse_gs_upload(t, clut * 4)
    except mdl.ModelError:
        return None
    if not uw or dofs + uw * uh * 4 > len(t):
        return None
    dbw = pblt & 0x3F
    where = {}
    for y in range(uh):
        for x in range(uw):
            where[mdl.addr32(dbw, x, y)] = dofs + (y * uw + x) * 4
    cells = []
    for y in range(th):
        for x in range(tw):
            if psm == 0x14:
                a = mdl.addr4(tbw, x, y)
                b = a >> 1
                w = where.get(b >> 2)
                cells.append(None if w is None else (w + (b & 3), 4 if a & 1 else 0))
            else:
                a = mdl.addr8(tbw, x, y)
                w = where.get(a >> 2)
                cells.append(None if w is None else (w + (a & 3), -1))
    n = cw * ch
    if psm == 0x13:
        pal = [cofs + mdl.csm1(k) * 4 if mdl.csm1(k) < n else None for k in range(256)]
    else:
        pal = [cofs + (k + csa * 16) * 4 if k + csa * 16 < n else None for k in range(16)]
    return {"w": tw, "h": th, "bits": 4 if psm == 0x14 else 8, "cells": cells, "pal": pal, "clut": clut,
            "sig": "%08x" % (zlib.crc32(bytes(t[dofs:dofs + uw * uh * 4])) & 0xFFFFFFFF)}


def tex_indices(t, lay):
    out = []
    for c in lay["cells"]:
        if c is None:
            out.append(0)
        elif c[1] < 0:
            out.append(t[c[0]])
        else:
            out.append((t[c[0]] >> c[1]) & 15)
    return out


def tex_rgba(t, lay, idx=None):
    """A texture's pixels as RGBA (alpha 0..255), from its layout."""
    idx = tex_indices(t, lay) if idx is None else idx
    pal = [bytes(t[o:o + 3]) + bytes((min(255, t[o + 3] * 2),)) if o is not None else b"\0\0\0\xff" for o in lay["pal"]]
    return b"".join(pal[k] for k in idx)


def tex_sigs(t):
    """{texture index: sig} of a texture file's palettised textures (cheap: no pixel layout)."""
    import zlib
    mdl = model_module()
    out = {}
    count, ent = struct.unpack_from("<II", t, 0)
    for i in range(min(count, 256)):
        o = ent * 4 + i * 0x40
        if o + 0x40 > len(t):
            break
        pix, clut = struct.unpack_from("<II", t, o)
        tex0 = struct.unpack_from("<Q", t, o + 0x30)[0]
        if (tex0 >> 20) & 0x3F not in (0x13, 0x14) or not clut:
            continue
        try:
            uw, uh, dofs, _ = mdl.parse_gs_upload(t, pix * 4)
        except mdl.ModelError:
            continue
        out[i] = "%08x" % (zlib.crc32(bytes(t[dofs:dofs + uw * uh * 4])) & 0xFFFFFFFF)
    return out


def _cdist(a, b):
    da = (a[3] - b[3]) * 4
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2 + da * da


def paint_texture(t, lay, painted, locked=()):
    """Paints pixels {p: (r, g, b)} of the texture `lay` of texture file t (a bytearray, changed in place), into its
    palette. `locked`: palette entries (file offsets) other textures use too (never changed). -> the palette entries
    merged to make room (0: the colours fitted)."""
    idx = tex_indices(t, lay)
    pal_offs = lay["pal"]
    n = len(pal_offs)
    raw = [tuple(t[o:o + 4]) if o is not None else None for o in pal_offs]
    cells = lay["cells"]
    want = {}  # pixel -> raw rgba it is to be
    for p, rgb in painted.items():
        if 0 <= p < len(cells) and cells[p] is not None and raw[idx[p]] is not None:
            want[p] = (rgb[0], rgb[1], rgb[2], raw[idx[p]][3])
    if not want:
        return 0
    used = [0] * n
    for p in range(len(idx)):
        if p not in want and cells[p] is not None:
            used[idx[p]] += 1
    # clusters of colours: [colour, weight, original entries, painted colours, locked]
    clusters = []
    for k in range(n):
        if raw[k] is None:
            continue
        lk = pal_offs[k] in locked
        if used[k] or lk:
            clusters.append([raw[k], used[k], {k}, set(), lk])
    counts = {}
    for c in want.values():
        counts[c] = counts.get(c, 0) + 1
    for c, w in counts.items():
        for cl in clusters:
            if cl[0] == c:
                cl[1] += w * 4
                cl[3].add(c)
                break
        else:
            clusters.append([c, w * 4, set(), {c}, False])
    slots = sum(1 for o in pal_offs if o is not None)
    merged = 0
    while len(clusters) > slots:
        best = None
        for i in range(len(clusters)):
            a = clusters[i]
            for j in range(i + 1, len(clusters)):
                b = clusters[j]
                if a[4] and b[4]:
                    continue
                if a[0][3] != b[0][3] and (a[0][3] == 0 or b[0][3] == 0):  # (clear and seen stay apart)
                    cost = 1e18
                else:
                    wa, wb = max(a[1], 1), max(b[1], 1)
                    cost = wa * wb / (wa + wb) * _cdist(a[0], b[0])
                if best is None or cost < best[0]:
                    best = (cost, i, j)
        if best is None:
            break
        _, i, j = best
        a, b = clusters[i], clusters[j]
        if a[4] or b[4]:
            col = a[0] if a[4] else b[0]
        else:
            wa, wb = max(a[1], 1), max(b[1], 1)
            col = tuple(round((a[0][k] * wa + b[0][k] * wb) / (wa + wb)) for k in range(4))
        clusters[i] = [col, a[1] + b[1], a[2] | b[2], a[3] | b[3], a[4] or b[4]]
        del clusters[j]
        merged += 1
    # a palette entry per cluster: the entry it had where it can, else a free one
    taken, slot_of = set(), [None] * len(clusters)
    for ci, cl in enumerate(clusters):
        own = sorted(k for k in cl[2] if ((pal_offs[k] in locked) if cl[4] else raw[k] == cl[0]))
        for k in own:
            if k not in taken:
                slot_of[ci] = k
                taken.add(k)
                break
    for ci, cl in enumerate(clusters):
        if slot_of[ci] is None:
            for k in sorted(cl[2]) + list(range(n)):
                if k not in taken and pal_offs[k] is not None and pal_offs[k] not in locked:
                    slot_of[ci] = k
                    taken.add(k)
                    break
    old_to, col_to = {}, {}
    for ci, cl in enumerate(clusters):
        k = slot_of[ci]
        if k is None:
            continue
        if not cl[4] and raw[k] != cl[0]:
            t[pal_offs[k]:pal_offs[k] + 4] = bytes(cl[0])
        for o in cl[2]:
            old_to[o] = k
        for c in cl[3]:
            col_to[c] = k
    for p in range(len(idx)):
        c = cells[p]
        if c is None:
            continue
        k = col_to.get(want[p]) if p in want else old_to.get(idx[p], idx[p])
        if k is None or k == idx[p]:
            continue
        if c[1] < 0:
            t[c[0]] = k
        else:
            t[c[0]] = (t[c[0]] & ~(15 << c[1]) & 0xFF) | (k << c[1])
    return merged


def paint_entries(spec):
    """The spec's "paint" as [(costume index, texture index, w, h, sig, {pixel: (r, g, b)})]."""
    import base64
    out = []
    for key, v in (spec.get("paint") or {}).items():
        m = re.fullmatch(r"(\d+):(\d+)", str(key))
        if not m or not isinstance(v, dict) or not v.get("png"):
            raise Fail(f"paint: {key!r} should be \"<costume>:<texture>\": {{w, h, png}}")
        P = portrait_module()
        try:
            w, h, px = P.png_read(base64.b64decode(v["png"].split(",", 1)[-1]))
        except Exception as e:
            raise Fail(f"paint {key}: not a PNG ({e})")
        painted = {p: (px[p * 4], px[p * 4 + 1], px[p * 4 + 2]) for p in range(w * h) if px[p * 4 + 3] >= 128}
        if painted:
            out.append((int(m.group(1)) - 1, int(m.group(2)), w, h, v.get("sig"), painted))
    return out


def node_keys(opts):
    """Workshop options with their per-node keys as numbers (JSON keeps them as text)."""
    if not opts or "nodes" not in opts:
        return opts
    out = dict(opts)
    out["nodes"] = {(int(k, 0) if isinstance(k, str) else k): v for k, v in opts["nodes"].items()}
    return out


def fx_module():
    here = pathlib.Path(__file__).resolve().parent / "creator"
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    import fxcolour
    return fxcolour


def mix_module():
    here = pathlib.Path(__file__).resolve().parent / "creator"
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    import mix
    return mix


def is_bt4(c):
    return c >= BT4_BASE


def name(c):
    if is_bt4(c):
        for i, label, _ in bt4_names():
            if i == c - BT4_BASE:
                return label
        return f"BT4: character {c - BT4_BASE}"
    return NAMES[c]


# --------------------------------------------------------------------------------------------- reading game files
class IsoDisc:
    """The two archives of the disc image (ISO 9660), by file id."""

    def __init__(self, iso):
        self.f = open(iso, "rb")
        root = self._rd(16 * 2048, 2048)[156:190]
        lba, size = struct.unpack("<II", root[2:6] + root[10:14])
        self.afs = [self._open_afs(lba, size, name) for name in ("PZS3US1.AFS", "PZS3US2.AFS")]

    def _rd(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _find(self, lba, size, parts):
        d = self._rd(lba * 2048, size)
        i = 0
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            e = d[i:i + n]
            if e[33:33 + e[32]].decode("latin1").split(";")[0].upper() == parts[0]:
                elba, esz = struct.unpack("<II", e[2:6] + e[10:14])
                return (elba, esz) if len(parts) == 1 else self._find(elba, esz, parts[1:])
            i += n
        raise Fail(f"{'/'.join(parts)} not found in the disc image: is it the USA release of BT3 (SLUS-21678)?")

    def _open_afs(self, lba, size, name):
        alba, _ = self._find(lba, size, ["DATA", name])
        base = alba * 2048
        if self._rd(base, 4) != b"AFS\0":
            raise Fail(f"{name} is not an AFS archive")
        n = struct.unpack("<I", self._rd(base + 4, 4))[0]
        table = self._rd(base + 8, n * 8)
        return base, [struct.unpack_from("<II", table, i * 8) for i in range(n)]

    def read_id(self, fid):
        base, entries = self.afs[0] if fid < PT2_FIRST_ID else self.afs[1]
        off, size = entries[fid - 1 if fid < PT2_FIRST_ID else fid - PT2_FIRST_ID]
        return self._rd(base + off, size)


class GameData:
    """The setup's unpacked copy (gamedata/pzs3us1, pzs3us2): the originals, never the mods/ copies."""

    def __init__(self, game):
        self.dir = pathlib.Path(game) / "gamedata"

    def read_id(self, fid):
        part, index = ("pzs3us1", fid - 1) if fid < PT2_FIRST_ID else ("pzs3us2", fid - PT2_FIRST_ID)
        return (self.dir / part / f"{index:05d}.bin").read_bytes()


def open_source(args):
    if getattr(args, "iso", None):
        return IsoDisc(args.iso)
    if getattr(args, "game", None) and (pathlib.Path(args.game) / "gamedata" / "pzs3us2").is_dir():
        return GameData(args.game)
    default = pathlib.Path(__file__).resolve().parents[3] / "Dragon Ball Z - Budokai Tenkaichi 3 (USA) (En,Ja).iso"
    if default.is_file():
        return IsoDisc(default)
    raise Fail("no game data: pass --iso <BT3 USA disc image> or --game <folder with gamedata/>")


# -------------------------------------------------------------------------------------------------------- packs
def header_size(n):
    return -(-(4 + (n + 1) * 4) // ALIGN) * ALIGN


def split_pack(data, count=None):
    """The parts of a pack (count, count + 1 byte offsets, 64-byte aligned parts), with their padding; None for a
    placeholder (a costume slot the character lacks)."""
    if len(data) < 8:
        return None
    n = struct.unpack_from("<I", data)[0]
    if count is not None and n != count:
        return None
    if n == 0 or header_size(n) > len(data):
        return None
    offs = struct.unpack_from(f"<{n + 1}I", data, 4)
    if offs[0] != header_size(n) or offs[-1] != len(data) or list(offs) != sorted(offs):
        raise Fail("unexpected pack layout")
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def join_pack(parts):
    parts = [p + b"\0" * (-len(p) % ALIGN) for p in parts]
    pos, offs = header_size(len(parts)), []
    for p in parts:
        offs.append(pos)
        pos += len(p)
    offs.append(pos)
    head = struct.pack(f"<I{len(parts) + 1}I", len(parts), *offs)
    return head + b"\0" * (header_size(len(parts)) - len(head)) + b"".join(parts)


class Pack:
    """A pack whose parts are numbered as the game numbers entries (from 1)."""

    def __init__(self, data, count=None):
        parts = split_pack(data, count)
        if parts is None:
            raise Fail("not a pack")
        self.parts = list(parts)

    def __getitem__(self, entry):
        return self.parts[entry - 1]

    def __setitem__(self, entry, value):
        self.parts[entry - 1] = value

    def motion(self, m):  # an animation pack: motion m is entry m + 1
        return self[m + 1]

    def set_motion(self, m, value):
        self[m + 1] = value

    def bytes(self):
        return join_pack(self.parts)


# -------------------------------------------------------------------------------------------------- characters
def chara_id(name):
    if isinstance(name, str) and name.lower().startswith("bt4:"):
        rest = name[4:].strip()
        if re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)", rest):
            return BT4_BASE + int(rest, 0)
        for i, label, _ in bt4_names():
            if label.lower() == name.lower() or label[5:].lower() == rest.lower():
                return BT4_BASE + i
        close = [label for _, label, _ in bt4_names() if rest.lower() in label.lower()]
        raise Fail(f"unknown BT4 character {name!r}" + (f"; did you mean: {', '.join(close[:6])}" if close else ""))
    if isinstance(name, int):
        if name >= BT4_BASE:
            return name
        c = name
    elif isinstance(name, str) and re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)", name.strip()):
        c = int(name, 0)
    else:
        low = {n.lower(): i for i, n in enumerate(NAMES)}
        if name.lower() not in low:
            close = [n for n in NAMES if name.lower() in n.lower()]
            raise Fail(f"unknown character {name!r}" + (f"; did you mean: {', '.join(close[:6])}" if close else ""))
        c = low[name.lower()]
    if not 0 <= c < len(NAMES):
        raise Fail(f"character id {c} out of range (0..{len(NAMES) - 1})")
    return c


def disc_link(c):
    """A link of character.txt to roster character c: disc:<id> for a BT3 one; for a BT4 one (BT4_BASE + its id), the
    BT3 character both games have (port/tools/bt4/bt3_to_bt4_chara.json), else the folder
    port/tools/bt4/import_bt4_roster.py makes for it ("<name> <form> (BT4)"; the game reads disc: ids of BT3 only)."""
    if not is_bt4(c):
        return f"disc:{c}"
    k = c - BT4_BASE
    here = pathlib.Path(__file__).resolve().parent / "bt4"
    for p in (here / "bt3_to_bt4_chara.json", here.parents[3] / "research" / "bt3_to_bt4_chara.json"):
        if p.is_file():
            pair = {int(v): int(b) for b, v in json.loads(p.read_text(encoding="utf-8")).items()}
            if k in pair and 0 <= pair[k] < len(NAMES):
                return f"disc:{pair[k]}"
            break
    import unicodedata
    nm, form = bt4().ch.names()[k]
    nm = (nm or name(c)[5:]).strip()
    form = (form or "").strip()
    plain = unicodedata.normalize("NFKD", f"{nm}{' ' + form if form else ''} (BT4)").encode("ascii", "ignore").decode()
    return folder_name(plain)


def ref(obj, default=None):
    """{"chara": ..., "costume": 1..16} -> (id, costume index). (Whether the character has it: Disc.has_costume.)"""
    if obj is None:
        return default
    if isinstance(obj, (str, int)):
        obj = {"chara": obj}
    costume = int(obj.get("costume", 1))
    if not 1 <= costume <= COSTUME_MAX:
        raise Fail(f"costume {costume} out of range (1..{COSTUME_MAX})")
    return chara_id(obj["chara"]), costume - 1


def chara_file(c, k):
    return FIRST_CHARA_FILE + c * 10 + k


class Disc:
    """Cached reads of a character's files."""

    def __init__(self, src):
        self.src = src
        self.cache = {}

    def raw(self, fid):
        if fid not in self.cache:
            self.cache[fid] = self.src.read_id(fid)
        return self.cache[fid]

    def _bt4(self, key, make):
        if key not in self.cache:
            try:
                self.cache[key] = make()
            except (ValueError, IndexError) as e:
                raise Fail(str(e))
        return self.cache[key]

    def has_costume(self, c, costume):
        if is_bt4(c):
            return 0 <= costume < bt4().ch.costumes[c - BT4_BASE]
        return 0 <= costume < 4 and split_pack(self.raw(chara_file(c, costume)), 252) is not None

    def costume_count(self, c):
        """How many costumes character c has (BT3: the disc's model slots that hold one, up to 4; BT4: its table's)."""
        if is_bt4(c):
            return bt4().ch.costumes[c - BT4_BASE]
        n = 0
        while n < 4 and self.has_costume(c, n):
            n += 1
        return n

    def model(self, c, costume, damaged=False):
        if not self.has_costume(c, costume):
            raise Fail(f"{name(c)} has no costume {costume + 1}")
        if is_bt4(c):
            cv = _bt4["cv"]
            try:
                data = self._bt4(("bt4m", c, costume, damaged), lambda: cv.bt3_model_pack(bt4(), c - BT4_BASE, costume, damaged))
            except Fail:
                data = self._bt4(("bt4m", c, costume, False), lambda: cv.bt3_model_pack(bt4(), c - BT4_BASE, costume, False))
            return Pack(data, 252)
        data = self.raw(chara_file(c, costume + (4 if damaged else 0)))
        if split_pack(data, 252) is None:  # no damaged model: the costume's own
            data = self.raw(chara_file(c, costume))
        return Pack(data, 252)

    def anims(self, c):
        if is_bt4(c):
            bt4()
            return Pack(self._bt4(("bt4a", c), lambda: _bt4["cv"].bt3_anim_pack(bt4(), c - BT4_BASE)))
        return Pack(self.raw(chara_file(c, 8)))

    def effects(self, c):
        if is_bt4(c):
            bt4()
            return Pack(self._bt4(("bt4e", c), lambda: _bt4["cv"].bt3_effect_pack(bt4(), c - BT4_BASE)))
        return Pack(self.raw(chara_file(c, 9)))

    def voice_files(self, c):
        """(bank_us, bank_jp, [line_us 0..99], [line_jp 0..99]) of character c; a missing line is b""."""
        if is_bt4(c):
            v = self._bt4(("bt4v", c), lambda: _bt4["cv"].bt3_voice_files(bt4(), c - BT4_BASE, 0))
            return (v.get(VOICE_BANK_US, b""), v.get(VOICE_BANK_JP, b""),
                    [v.get(VOICE_LINE_US + k, b"") for k in range(100)], [v.get(VOICE_LINE_JP + k, b"") for k in range(100)])
        return (self.raw(VOICE_BANK_US + c), self.raw(VOICE_BANK_JP + c),
                [self.raw(VOICE_LINE_US + c * 100 + k) for k in range(100)],
                [self.raw(VOICE_LINE_JP + c * 100 + k) for k in range(100)])

    def portrait(self, c):
        if is_bt4(c):
            bt4()
            return self._bt4(("bt4p", c), lambda: _bt4["cv"].bt3_portrait(bt4(), c - BT4_BASE))
        return self.raw(PORTRAIT_FILE + c)

    def chip(self, c):
        """The character select's chip (menu archive 0x1C1, screen 2, section 45, entry id + 1): BT3's, or for a
        BT4 character the BT4 disc's own (the same format)."""
        try:
            here = pathlib.Path(__file__).resolve().parent
            if str(here) not in sys.path:
                sys.path.insert(0, str(here))
            import import_chara as ic
            if is_bt4(c):
                arc = bt4().raw(0x1C1)
                res = ic.bpe(ic.section(arc, ic.table(arc), 2))
                chips = ic.section(res, ic.table(res), 45)
                e = ic.section(chips, ic.table(chips), c - BT4_BASE + 1)
                return e if len(e) == 5504 else None
            arc = self.raw(0x1C1)
            res = ic.bpe(ic.section(arc, ic.table(arc), 2))
            chips = ic.section(res, ic.table(res), 45)
            return ic.section(chips, ic.table(chips), c + 1)
        except Exception:
            return None

    def member(self, c, side):
        """The fight's team picture (64 x 32): BT3's, or the BT4 disc's own for a BT4 character."""
        if is_bt4(c):
            d = bt4()
            m = d.raw(d.ch.file_id("tex2", c - BT4_BASE, side))
            return m if 16 <= len(m) <= 0x1000 else None
        return self.raw(MEMBER_FILE + c * 2 + side)


# ------------------------------------------------------------------------------------------------ move list text
def text_blocks(raw):
    """A move list script (UTF-16, game entry 0x2D..0x34) -> (head, [block per move], tail). A move's block is its
    '*' line and the lines after it up to the next '*' line, or the '@' line and what follows, which ends the list.
    Joined again with join_text it gives back the same text."""
    t = raw.decode("utf-16le", errors="surrogatepass")
    lines = t.split("\n")
    head, blocks, tail, cur = [], [], [], None
    for i, ln in enumerate(lines):
        body = ln[2:] if len(ln) >= 2 else ""
        if body.startswith("&"):  # an unlock condition: '&' and three digits
            body = body[4:]
        if body.startswith("*"):
            cur = [ln]
            blocks.append(cur)
        elif "@" in ln[:3]:
            tail = lines[i:]
            break
        elif cur is None:
            head.append(ln)
        else:
            cur.append(ln)
    return head, blocks, tail


def join_text(head, blocks, tail):
    t = "\n".join(head + [ln for b in blocks for ln in b] + tail)
    data = t.encode("utf-16le", errors="surrogatepass")
    return data + b"\0" * (-len(data) % ALIGN)


def rename_block(block, name):
    """The '*' line: two prefix characters, an optional unlock condition ('&' and three digits), '*', three digits
    (the cost shown), then the name."""
    m = re.match(r"^(..(?:&\d\d\d)?\*\d\d\d)", block[0])
    if m is None:
        raise Fail(f"unexpected move line {block[0]!r}")
    return [m.group(1) + name] + block[1:]


def move_names(pack):
    """The five move names in English (entry 0x2D)."""
    return text_move_names(pack[0x2D])


def text_move_names(raw):
    _, blocks, _ = text_blocks(raw)
    return [re.sub(r"^..(?:&\d\d\d)?\*\d\d\d", "", b[0]) for b in blocks]


def bt4_move_names(c):
    """A BT4 character's move names, from its move list file (no model to convert)."""
    d = bt4()
    raw = d.raw(d.ch.file_id("movelist", c - BT4_BASE))
    return text_move_names(raw.rstrip(b"\0") if len(raw.rstrip(b"\0")) % 2 == 0 else raw.rstrip(b"\0") + b"\0")


# --------------------------------------------------------------------------------------------------- the builder
def copy_slot_data(dst, src, layout, n, i_dst, i_src):
    out = bytearray(dst)
    for off, size, rows in layout:
        for r in range(rows):
            a, b = off + (r * n + i_dst) * size, off + (r * n + i_src) * size
            out[a:a + size] = src[b:b + size]
    return bytes(out)


def slot_motions(slot):
    if slot < 2:
        return [0x103 + slot]
    first = 0x105 + (slot - 2) * 0x22
    return list(range(first, first + 0x22)) + ([0x16B] if slot == 4 else [])


class Fighter:
    def __init__(self, disc, spec, path):
        self.disc, self.spec, self.path = disc, spec, path
        if not spec.get("name"):
            raise Fail(f"{path}: the fighter needs a \"name\"")
        if "costumes" in spec:
            self.costumes = [ref(c) for c in spec["costumes"]]
        else:
            self.costumes = [ref(spec.get("look"), ref(spec.get("base"), (0, 0)))]
        if not 1 <= len(self.costumes) <= COSTUME_MAX:
            raise Fail(f"1 to {COSTUME_MAX} costumes")
        self.look = self.costumes[0]
        self.base = ref(spec.get("base"), self.look if not is_bt4(self.look[0]) else (3, 0))
        if is_bt4(self.base[0]):
            raise Fail("the base (AI and roster tables) must be a BT3 character: pick the one that fights most alike")
        self.moveset = ref(spec.get("moveset"), self.look)
        self.voice = ref(spec.get("voice"), self.base)
        self.portrait = ref(spec.get("portrait"), self.base)
        self.melee = ref(spec.get("melee"))
        self.kiblasts = ref(spec.get("ki_blasts"))
        order = ("head", "hair", "torso", "tail", "arms", "hands", "legs", "feet")  # later ones win where they overlap
        parts = spec.get("parts") or {}
        for k in parts:
            if k not in order:
                raise Fail(f"unknown piece {k!r}: one of {', '.join(order)}")
        # a piece may be one donor or a list of them, layered in order (two hairs: both sets of locks)
        # ({"chara": ..., "add": true}: that donor's hair locks / capes are added on top, the model's own kept)
        self.parts = [(k + ("+" if isinstance(v, dict) and v.get("add") else ""), ref(v),
                       (v.get("adjust") if isinstance(v, dict) else None) or {})
                      for k in order if parts.get(k)
                      for v in (parts[k] if isinstance(parts[k], list) else [parts[k]]) if v]
        self.body = node_keys(spec.get("body") or {})
        # attack colours: {"blast1_1".."ultimate" | "ki_blasts" | "charge": [{"from", "to", "range"}]}
        self.fx = {k: v for k, v in (spec.get("attack_colours") or {}).items() if v}
        for k in self.fx:
            if k not in SLOTS and k not in ("ki_blasts", "charge"):
                raise Fail(f"attack_colours: unknown {k!r}")
        self.parts = [(p_, w, node_keys(o)) for p_, w, o in self.parts]
        self.notes, self.mix_notes = [], []
        self.colours = []
        for c in spec.get("colours", []):
            try:
                self.colours.append((int(c["texture"]), hex_rgb(c["from"]), hex_rgb(c["to"]), float(c.get("range", 1.0))))
            except (KeyError, ValueError, TypeError):
                raise Fail(f"a colour change needs texture, from and to (#rrggbb): {c!r}")
        self.paint = paint_entries(spec)
        self._paint_ref = {}  # look -> the texture file the paint was made on (undamaged, before the colours)
        self.moves = {}
        self.move_mix = {}  # slot -> {"effects" | "camera" | "camera_in" | ...: (character, slot)}
        for key, v in spec.get("moves", {}).items():
            if key not in SLOTS:
                raise Fail(f"unknown move slot {key!r}: one of {', '.join(SLOTS)}")
            d = SLOTS[key]
            if isinstance(v, dict) and v.get("slot", key) not in SLOTS:
                raise Fail(f"{key}: unknown slot {v.get('slot')!r}")
            s = SLOTS[v.get("slot", key)] if isinstance(v, dict) else d
            if SLOT_KIND[s] != SLOT_KIND[d]:
                raise Fail(f"{key} can only take a move of the same kind ({SLOT_KIND[d]}), not {v.get('slot')}")
            if isinstance(v, dict) and "chara" not in v:  # (only effects or a camera for the moveset's own move)
                self.moves[d] = (self.moveset, s)
            else:
                self.moves[d] = (ref(v), s)
            mix = {}
            for part in ("effects", "camera", "camera_in", "camera_loop", "camera_finish"):
                w = v.get(part) if isinstance(v, dict) else None
                if not w:
                    continue
                wkey = (w.get("slot") if isinstance(w, dict) else None) or ("ultimate" if part == "camera_in" else key)
                if wkey not in SLOTS:
                    raise Fail(f"{key} {part}: unknown slot {wkey!r}")
                ws = SLOTS[wkey]
                if part == "effects" and (ws < 2) != (d < 2):
                    raise Fail(f"{key}: the effects of a Blast 1 go with a Blast 1, of a Blast 2 or Ultimate with those")
                if part != "effects" and (d < 2 or ws < 2):
                    raise Fail(f"{key} {part}: a Blast 1 has no camera of its own (only Blast 2 and Ultimates have)")
                if part == "camera_in" and (d != 4 or ws != 4):
                    raise Fail(f"{key}: camera_in is the opening of an Ultimate, from another Ultimate")
                mix[part] = (ref(w), ws)
            if mix:
                self.move_mix[d] = mix
        self.tint = {}
        for key, v in (spec.get("attack_tint") or {}).items():
            if key not in SLOTS:
                raise Fail(f"attack_tint: unknown {key!r}")
            try:
                self.tint[SLOTS[key]] = (float(v["hue"]) % 360, min(100.0, max(0.0, float(v["strength"]))),
                                         min(100.0, max(0.0, float(v["light"]))))
            except (KeyError, TypeError, ValueError):
                raise Fail(f"attack_tint {key}: needs hue (0..360), strength and light (0..100)")
        self._fit = {}
        self.names = {}
        for key, v in spec.get("names", {}).items():
            if key not in SLOTS:
                raise Fail(f"unknown move slot {key!r} in names")
            if not v or len(v) > 40 or "\n" in v:
                raise Fail(f"move name {v!r}: 1 to 40 characters on one line")
            self.names[SLOTS[key]] = v

    def model(self, costume, damaged):
        """One model (a costume, or its damaged version) with the fighter's moves, names and voice."""
        d = self.disc
        m = d.model(*costume, damaged)
        if self.parts or self.body:  # pieces of other characters' models, workshop (port/tools/creator/mix.py)
            m = self.mixed(m, damaged)
        pre = self.pre_colours = {e: m[e] for e in PAINT_ENTRIES}  # (the textures before the colour changes)
        if self.colours:
            # (the colour changes name the textures of the costume's model, as the creator shows it; the damaged
            # model gets them on its own textures that are the same)
            plain = getattr(self, "_plain", None)
            if plain is None:
                plain = self._plain = {}
            changes = self.colours
            if not damaged:
                plain[tuple(costume)] = (m[0xC], face_texture(m))
            else:
                if tuple(costume) not in plain:
                    p = d.model(*costume, False)
                    if self.parts or self.body:
                        p = self.mixed(p, False)
                    plain[tuple(costume)] = (p[0xC], face_texture(p))
                tex, face = plain[tuple(costume)]
                tmap = texture_map(tex, m[0xC], face, face_texture(m))
                changes = [(tmap[t], ref, to, rng) for t, ref, to, rng in changes if t in tmap]
            m = recolour_model(m, changes)
        if self.paint:
            self.painted(m, costume, damaged, pre)
        ms = d.model(*self.moveset, damaged)
        for e in MOVESET_ENTRIES + tuple(TEXT_ENTRIES):
            m[e] = ms[e]
        for e in (2, 0x17):  # (a model without entry 2 or 0x17, as Team BT4's joke costumes: every fight reads
            if not m[e] and ms[e]:  # them, BtlMove_ClampToStage, BtlChar_UpdateLook: the moveset's own)
                m[e] = ms[e]
        if self.melee:
            m[E_MELEE] = d.model(*self.melee, damaged)[E_MELEE]
        if self.kiblasts:
            m[E_KIBLAST] = d.model(*self.kiblasts, damaged)[E_KIBLAST]
        for dst, (who, src_slot) in self.moves.items():
            donor = d.model(*who, damaged)
            if dst >= 2:
                m[E_SUPER] = copy_slot_data(m[E_SUPER], donor[E_SUPER], SUPER_LAYOUT, 3, dst - 2, src_slot - 2)
                cuts = bytearray(m[E_CUTS])
                for a, b in [(dst - 2, src_slot - 2)] + ([(3, 3)] if dst == 4 else []):
                    src_cut = donor[E_CUTS][b * CUT_SIZE:(b + 1) * CUT_SIZE]
                    if len(src_cut) == CUT_SIZE and (a + 1) * CUT_SIZE <= len(cuts):
                        cuts[a * CUT_SIZE:(a + 1) * CUT_SIZE] = src_cut
                m[E_CUTS] = bytes(cuts)
            else:
                m[E_SKILL] = copy_slot_data(m[E_SKILL], donor[E_SKILL], SKILL_LAYOUT, 2, dst, src_slot)
            for e in TEXT_ENTRIES:
                if m[e] and donor[e]:
                    h, blocks, t = text_blocks(m[e])
                    dblocks = text_blocks(donor[e])[1]
                    if dst < len(blocks) and src_slot < len(dblocks):
                        blocks[dst] = dblocks[src_slot]
                        m[e] = join_text(h, blocks, t)
        for dst, mix in self.move_mix.items():
            self.mix_slot(m, dst, mix, damaged)
        for slot, name in self.names.items():
            for e in TEXT_ENTRIES:
                if m[e]:
                    h, blocks, t = text_blocks(m[e])
                    if slot < len(blocks):
                        blocks[slot] = rename_block(blocks[slot], name)
                        m[e] = join_text(h, blocks, t)
        if self.voice[0] != costume[0] or is_bt4(self.voice[0]) != is_bt4(costume[0]):
            v = d.model(self.voice[0], 0, damaged)
            m[E_TALK] = v[E_TALK]
            for e in LIP_ENTRIES:
                m[e] = v[e]
        return m

    def move_label(self, who, slot):
        try:
            return move_names(self.disc.model(*who))[slot]
        except (Fail, IndexError):
            return f"{name(who[0])}'s move"

    def effects_ok(self, dst):
        """Whether slot dst takes the effects its spec names (else it keeps its own, in their colours: a note)."""
        if dst not in self._fit:
            who, s = self.moves[dst]
            bw, bs = self.move_mix[dst]["effects"]
            e = E_SUPER if dst >= 2 else E_SKILL
            an, bn = self.move_label(who, s), self.move_label(bw, bs)
            ok, why = effects_fit(self.disc.model(*who)[e], s, self.disc.model(*bw)[e], bs,
                                  self.disc.effects(bw[0])[bs + 1], an, bn)
            if not ok:
                self.note(f"{an} keeps its own effects, in {bn}'s colours: {why}")
            self._fit[dst] = ok
        return self._fit[dst]

    def finish_of(self, dst):
        """(character, slot) whose rush camera slot dst plays when it connects, None: its own."""
        key = ("finish", dst)
        if key not in self._fit:
            mix = self.move_mix.get(dst, {})
            src = mix.get("camera_finish") or mix.get("camera")
            out = None
            if src and dst >= 2:
                said = "camera_finish" in mix
                w0, s0 = mix["effects"] if "effects" in mix and self.effects_ok(dst) else self.moves[dst]
                mine = self.move_label(*self.moves[dst])
                if not is_rush_effect(self.disc.model(*w0)[E_SUPER], s0):
                    if said:
                        self.note(f"{mine} is not a rush: its end is filmed by the battle's camera, not "
                                  f"{self.move_label(*src)}'s")
                elif not finish_camera(self.disc.effects(src[0][0]), src[1]):
                    if said:
                        self.note(f"{self.move_label(*src)} has no finish camera: {mine} keeps its own")
                else:
                    out = src
            self._fit[key] = out
        return self._fit[key]

    def mix_slot(self, m, dst, mix, damaged):
        """Slot dst of model m (its move already copied in) with the effects and cameras of other moves."""
        d = self.disc
        if "effects" in mix and self.effects_ok(dst):
            bw, bs = mix["effects"]
            b = d.model(*bw, damaged)
            if dst >= 2:
                i, j = dst - 2, bs - 2
                out = bytearray(copy_slot_data(m[E_SUPER], b[E_SUPER], EFFECT_SUPER, 3, i, j))
                fa, ida = super_info(m[E_SUPER], i)[:2]
                fb, idb = super_info(b[E_SUPER], j)[:2]
                struct.pack_into("<I", out, i * 4, (fb & ~SHAKE_BITS) | (fa & SHAKE_BITS))
                if idb in EFFECT_IDS or ida in EFFECT_IDS:
                    struct.pack_into("<h", out, 0x18 + i * 2, idb)
                m[E_SUPER] = bytes(out)
            else:
                m[E_SKILL] = copy_slot_data(m[E_SKILL], b[E_SKILL], EFFECT_SKILL, 2, dst, bs)
        if dst < 2:
            return
        i = dst - 2
        cuts, data = bytearray(m[E_CUTS]), bytearray(m[E_SUPER])

        def put_cut(src, k_src, k_dst):
            c = src[E_CUTS][k_src * CUT_SIZE:(k_src + 1) * CUT_SIZE]
            if len(c) == CUT_SIZE and (k_dst + 1) * CUT_SIZE <= len(cuts):
                cuts[k_dst * CUT_SIZE:(k_dst + 1) * CUT_SIZE] = c

        def opening(w, ws):  # its cut until it fires, and its camera shakes
            src = d.model(*w, damaged)
            put_cut(src, ws - 2, i)
            f = struct.unpack_from("<I", data, i * 4)[0]
            g = super_info(src[E_SUPER], ws - 2)[0]
            struct.pack_into("<I", data, i * 4, (f & ~SHAKE_BITS) | (g & SHAKE_BITS))

        if "camera" in mix:
            w, ws = mix["camera"]
            opening(w, ws)
            if dst == 4 and ws == 4:
                put_cut(d.model(*w, damaged), 3, 3)
        if "camera_loop" in mix:
            opening(*mix["camera_loop"])
        if "camera_in" in mix:
            put_cut(d.model(*mix["camera_in"][0], damaged), 3, 3)
        fin = self.finish_of(dst)
        if fin:  # (the times of the rush finish's cut 9 go with its camera)
            src = d.model(*fin[0], damaged)[E_SUPER]
            for off in (0x22C, 0x238):
                data[off + i * 4:off + i * 4 + 4] = src[off + (fin[1] - 2) * 4:off + (fin[1] - 2) * 4 + 4]
        m[E_CUTS], m[E_SUPER] = bytes(cuts), bytes(data)

    def mixed(self, m, damaged):
        """The look with its pieces and workshop changes. A layer that cannot be put on is left out (with a
        note in self.notes), so a warrior can always be built."""
        mix = mix_module()
        d = self.disc
        sel = []
        for piece, who, opts in self.parts:
            try:
                sel.append((d.model(*who, damaged).bytes(), piece, opts))
            except Fail as e:
                self.note(f"{piece} from {name(who[0])} left out: {e}")
        while True:
            kw = {}
            if self.body:
                kw["adjust"] = self.body
            try:
                try:
                    out = mix.mix(m.bytes(), sel, report=self.mix_notes, **kw)
                except (TypeError, ValueError):  # (a mixer without opts / adjust: the plain selections)
                    out = mix.mix(m.bytes(), [(b, p) for b, p, _ in sel], report=self.mix_notes)
                return Pack(out, 252)
            except mix.MixError as e:
                if not sel:
                    self.note(f"the body's workshop changes left out: {e}")
                    return m
                dropped = sel.pop()
                self.note(f"one {dropped[1].rstrip('+')} layer left out: {e}")

    def painted(self, m, costume, damaged, pre):
        """The paint of the costumes with this look, on the model m (after its colour changes; pre: its texture
        files before them). Painted on the texture it was made on - found by its pixels' sig, wherever the build
        put it - and, where they are the same picture, on the face's eye frames and mouth textures and the damaged
        model's textures."""
        todo = [pe for pe in self.paint if pe[0] < len(self.costumes) and self.costumes[pe[0]] == costume]
        if not todo:
            return
        if damaged:
            ref = self._paint_ref.get(costume)
            if ref is None:
                u = self.disc.model(*costume, False)
                if self.parts or self.body:
                    u = self.mixed(u, False)
                ref = self._paint_ref[costume] = u[0xC]
        else:
            ref = self._paint_ref[costume] = pre[0xC]
        data = {e: bytearray(m[e]) for e in PAINT_ENTRIES if m[e] and pre.get(e)}
        lays, pics = {}, {}

        def layout(e, j):
            if (e, j) not in lays:
                lays[(e, j)] = tex_layout(pre[e], j)
            return lays[(e, j)]

        def dims(t, j):
            count, ent = struct.unpack_from("<II", t, 0)
            if j >= count or ent * 4 + (j + 1) * 0x40 > len(t):
                return None
            tex0 = struct.unpack_from("<Q", t, ent * 4 + j * 0x40 + 0x30)[0]
            return 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15)
        sigs = tex_sigs(ref)
        for ci, ti, w, h, sig, painted in todo:
            target = ti if sig is None or sigs.get(ti) == sig else next((k for k, s in sorted(sigs.items()) if s == sig), None)
            rl = tex_layout(ref, target) if target is not None else None
            if rl is None or (rl["w"], rl["h"]) != (w, h):
                self.note(f"the paint of texture {ti} left out: the build's textures changed since it was painted")
                continue
            ref_px = tex_rgba(ref, rl)
            opaque = [ref_px[p * 4 + 3] >= 128 for p in range(w * h)]
            n_opaque = max(1, sum(opaque))
            for e in data:
                count = struct.unpack_from("<I", pre[e], 0)[0]
                for j in range(min(count, 256)):
                    if dims(pre[e], j) != (w, h):
                        continue
                    lay = layout(e, j)
                    if lay is None:
                        continue
                    mask = None
                    if damaged or e != 0xC or j != target:
                        if (e, j) not in pics:
                            pics[(e, j)] = tex_rgba(pre[e], lay)
                        px = pics[(e, j)]
                        mask = [px[p * 4:p * 4 + 4] == ref_px[p * 4:p * 4 + 4] for p in range(w * h)]
                        if sum(1 for p in range(w * h) if mask[p] and opaque[p]) / n_opaque < PAINT_SHARE:
                            continue
                    sub = painted if mask is None else {p: c for p, c in painted.items() if mask[p]}
                    locked = set()
                    ent = struct.unpack_from("<I", pre[e], 4)[0]
                    for j2 in range(min(count, 256)):  # (a palette other textures use too: its entries stay)
                        if j2 != j and struct.unpack_from("<I", pre[e], ent * 4 + j2 * 0x40 + 4)[0] == lay["clut"]:
                            l2 = layout(e, j2)
                            if l2 is not None:
                                locked |= {l2["pal"][k] for k in set(tex_indices(data[e], l2))}
                    paint_texture(data[e], lay, sub, locked)
        for e, t in data.items():
            m[e] = bytes(t)

    def note(self, text):
        if text not in self.notes:
            self.notes.append(text)

    def anims(self):
        a = self.disc.anims(self.moveset[0])
        for who, motions in ((self.melee, MELEE_MOTIONS), (self.kiblasts, KIBLAST_MOTIONS)):
            if who:
                src = self.disc.anims(who[0])
                for mo in motions:
                    a.set_motion(mo, src.motion(mo))
        for dst, (who, s) in self.moves.items():
            src = self.disc.anims(who[0])
            for md, msrc in zip(slot_motions(dst), slot_motions(s)):
                a.set_motion(md, src.motion(msrc))
        return a

    def effects(self):
        e = self.disc.effects(self.moveset[0])
        for dst, (who, s) in self.moves.items():
            e[dst + 1] = self.disc.effects(who[0])[s + 1]
        like = []  # (slot, effects pack, its slot): the slot keeps its own effects in the colours of that one
        for dst, mix in sorted(self.move_mix.items()):
            if "effects" in mix:
                bw, bs = mix["effects"]
                src = self.disc.effects(bw[0])
                if not self.effects_ok(dst):
                    like.append((dst, src, bs))
                else:
                    parts, own = try_split(src[bs + 1]), try_split(e[dst + 1])
                    if dst >= 2 and parts and own and len(parts) >= 3 and len(own) >= 3:
                        parts[2] = own[2]  # (the rush scene's placements belong to the motions)
                        e[dst + 1] = join_pack(parts)
                    else:
                        e[dst + 1] = src[bs + 1]
            fin = self.finish_of(dst)
            if fin and not set_finish_camera(e, dst, finish_camera(self.disc.effects(fin[0][0]), fin[1])):
                self.note(f"{self.move_label(*self.moves[dst])}: no place for {self.move_label(*fin)}'s finish camera")
        if like or self.tint:
            fx = fx_module()
            data = e.bytes()
            for dst, src, bs in like:
                data = fx.recolour_attack(data, dst, fx.match_changes(data, dst, src.bytes(), bs))
            for slot, (h, s, l) in sorted(self.tint.items()):
                data = fx.recolour_attack(data, slot, fx.tint_changes(fx.attack_colours(data, slot), h, s, l))
            e = Pack(data)
        if self.fx:
            fx = fx_module()
            data = e.bytes()
            for k, changes in self.fx.items():
                if k in SLOTS:
                    data = fx.recolour_attack(data, SLOTS[k], changes)
                elif k == "ki_blasts":
                    data = fx.recolour_kiblast(data, changes)
                else:
                    data = fx.recolour_aura(data, changes)
            e = Pack(data)
        return e

    def changes_motions(self):
        return bool(self.moveset[0] != self.look[0] or self.moves or self.melee or self.kiblasts or self.tint)

    def describe(self):
        def nm(r):
            return name(r[0]) + (f" (costume {r[1] + 1})" if r[1] else "")
        lines = [f"{self.spec['name']}" + (f" - {self.spec['title']}" if self.spec.get("title") else ""),
                 f"  look      {', '.join(nm(c) for c in self.costumes)}",
                 f"  base      {nm(self.base)} (AI and roster tables)",
                 f"  moveset   {nm(self.moveset)}"]
        if self.melee:
            lines.append(f"  melee     {nm(self.melee)}")
        if self.kiblasts:
            lines.append(f"  ki blasts {nm(self.kiblasts)}")
        for key, slot in SLOTS.items():
            who, s = self.moves.get(slot, (self.moveset, slot))
            try:
                own = move_names(self.disc.model(*who))[s]
            except (Fail, IndexError):
                own = "?"
            skey = [k for k, v in SLOTS.items() if v == s][0]
            lines.append(f"  {key:9s} {self.names.get(slot, own)}  <- {name(who[0])}'s {own} ({skey})")
            mix = self.move_mix.get(slot, {})
            took = [f"{part.replace('_', ' ')} of {name(w[0])}'s {self.move_label(w, ws)}"
                    + (" (does not fit: its own, in those colours)" if part == "effects" and not self.effects_ok(slot) else "")
                    for part, (w, ws) in mix.items()]
            if took:
                lines.append("            " + "; ".join(took))
        lines.append(f"  voice     {nm(self.voice)}")
        return "\n".join(lines)


# -------------------------------------------------------------------------------------------------- commands
def load_spec(path):
    p = pathlib.Path(path)
    try:
        return json.loads(p.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as e:
        raise Fail(f"{p}: {e}")


def folder_name(name):
    s = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "", name).strip().rstrip(".")
    return s[:63] or "Custom Fighter"


BUILD_NOTES = []  # what the last builds had to change or leave out (shown by the creator)

# A transformation's kind: (sequence +0xA0 = `<dir>_type`, burst +0xA4 = `<dir>_effect`) of the parameter block
# (BtlAct_QueueTransform, btl_act_j.c; the actions in btl_act_h_b.c; the bursts EftTransform_*, eft_ab_b.c). Those
# that need no partner, no moon and no animations of their own: action 0xEC the power-up (lead-in, waiting loop,
# closing), 0xEE the same without the wait, 0xF0 a longer one, 0xED a change without a burst; burst 2 the standard.
FORM_KINDS = {"power_up": (0, 2), "quick": (2, 2), "big": (4, 2), "instant": (1, 0)}


def form_lines(spec):
    """character.txt lines of the spec's form_kinds, form_bursts and form_default."""
    out = []
    for direction, kind in (spec.get("form_kinds") or {}).items():
        if direction not in ("left", "up", "right", "down"):
            raise Fail(f"form_kinds: {direction!r} is not left, up, right or down")
        if kind not in FORM_KINDS:
            raise Fail(f"form_kinds {direction}: {kind!r} is not one of {', '.join(FORM_KINDS)}")
        seq, burst = FORM_KINDS[kind]
        out += [f"{direction}_type={seq}", f"{direction}_effect={burst}"]
    for direction, rgb in (spec.get("form_bursts") or {}).items():
        if direction not in ("left", "up", "right", "down"):
            raise Fail(f"form_bursts: {direction!r} is not left, up, right or down")
        try:
            out.append(f"{direction}_burst=%02x%02x%02x" % hex_rgb(rgb))
        except (ValueError, AttributeError):
            raise Fail(f"form_bursts {direction}: {rgb!r} is not a colour (#rrggbb)")
    d = spec.get("form_default")
    if d:
        if d not in ("left", "up", "right"):
            raise Fail(f"form_default: {d!r} is not left, up or right")
        out.append(f"default={d}")
    return out


def write_package(disc, spec_path, chars_dir, done):
    spec_path = pathlib.Path(spec_path).resolve()
    if spec_path in done:
        return done[spec_path]
    spec = load_spec(spec_path)
    f = Fighter(disc, spec, spec_path)
    folder = folder_name(spec["name"])
    done[spec_path] = folder
    out = chars_dir / folder
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.pak"):
        old.unlink()
    stem = re.sub(r"[^A-Za-z0-9]+", "_", spec["name"]).strip("_")[:24] or "fighter"
    for i, costume in enumerate(f.costumes):
        (out / f"{stem}_{i + 1}p.pak").write_bytes(f.model(costume, False).bytes())
        (out / f"{stem}_{i + 1}p_dmg.pak").write_bytes(f.model(costume, True).bytes())
    (out / f"{stem}_anm.pak").write_bytes(f.anims().bytes())
    (out / f"{stem}_eff.pak").write_bytes(f.effects().bytes())
    bank_us, bank_jp, lines_us, lines_jp = disc.voice_files(f.voice[0])
    (out / f"{stem}_voice_us.pak").write_bytes(bank_us)
    (out / f"{stem}_voice_jp.pak").write_bytes(bank_jp)
    for lang, lines in (("voice_us", lines_us), ("voice_jp", lines_jp)):
        (out / lang).mkdir(exist_ok=True)
        for line, data in enumerate(lines):
            if len(data) >= 16:
                (out / lang / f"{line:03d}.adx").write_bytes(data)
    p = f.portrait[0]
    shot = spec.get("portrait_image")
    for old in ("chip.bin",):
        if (out / old).exists():
            (out / old).unlink()
    if shot and (spec_path.parent / shot).is_file():
        # a picture of the creator's 3D view: the portrait, the roster chip and the team pictures made from it
        P = portrait_module()
        png = (spec_path.parent / shot).read_bytes()
        stored = P.portrait_rgba(png)
        b = f.base[0]
        (out / "portrait.bin").write_bytes(P.portrait_bin(None, disc.portrait(b), stored=stored))
        chip = disc.chip(b)
        if chip:
            (out / "chip.bin").write_bytes(P.chip_bin(None, chip, stored=stored))
        m1, m2 = P.member_bins(None, disc.member(b, 0), disc.member(b, 1), stored=stored)
        (out / "member_1p.bin").write_bytes(m1)
        (out / "member_2p.bin").write_bytes(m2)
    else:
        (out / "portrait.bin").write_bytes(disc.portrait(p))
        for side in (0, 1):
            m = disc.member(p, side) or disc.member(f.base[0], side)
            (out / f"member_{side + 1}p.bin").write_bytes(m)
        if p != f.base[0]:  # the roster chip of the same character as the portrait
            chip = disc.chip(p)
            if chip:
                (out / "chip.bin").write_bytes(chip)
    manifest = ["# made by port/tools/custom_fighter.py from " + spec_path.name, "format=1",
                f"name={spec['name'][:63]}", f"form={spec.get('title', '')[:95]}", f"base={f.base[0]}",
                "author=Custom Character", "about=" + " / ".join(
                    ln.strip() for ln in f.describe().splitlines()[1:4])[:199]]
    for key, lo, hi in (("health", 10000, 150000), ("ki", 0, 100000), ("blast", 0, 7)):
        if key in spec:
            val = int(spec[key])
            if not lo <= val <= hi:
                raise Fail(f"{key} must be {lo}..{hi}")
            manifest.append(f"{key}={val}")
    # The four transformation inputs, every one written: a direction the fighter file does not name leads nowhere.
    # (Left out, the game would take the number of the model's own table: the transformations of whoever the
    # moves come from, Super Vegeta's "back" to Vegeta for a warrior made of Super Vegeta, or for a BT4 donor a
    # BT4 number that is another character here.)
    forms = spec.get("forms") or {}
    for direction in forms:
        if direction not in ("left", "up", "right", "down"):
            raise Fail(f"forms: {direction!r} is not left, up, right or down")
    ahead = []  # the warriors of this file's own that left / up / right lead to
    for direction in ("left", "up", "right", "down"):
        target = forms.get(direction)
        if target in (None, "none", ""):
            manifest.append(f"{direction}=none")
        elif isinstance(target, str) and target.startswith("disc:"):
            manifest.append(f"{direction}={disc_link(chara_id(target[5:]))}")
        else:
            linked = write_package(disc, spec_path.parent / target, chars_dir, done)
            manifest.append(f"{direction}={linked}")
            if direction != "down" and linked != folder:
                ahead.append(linked)
    for direction, cost in spec.get("form_costs", {}).items():
        manifest.append(f"{direction}_cost={int(cost)}")
    manifest += form_lines(spec)
    (out / "character.txt").write_bytes(("\n".join(manifest) + "\n").encode("utf-8"))
    # A form of this warrior whose own file names no way back goes back to this warrior (R3 + Down). (The game also
    # remembers who a fighter transformed from, whatever the files say: src/battle/btl_char_mgr.c.)
    for linked in ahead:
        txt = chars_dir / linked / "character.txt"
        if txt.is_file():
            t = txt.read_bytes().decode("utf-8")
            if re.search(r"^down=none$", t, re.M):
                txt.write_bytes(re.sub(r"^down=none$", lambda _m: f"down={folder}", t, count=1, flags=re.M).encode("utf-8"))
    for n in f.notes + f.mix_notes:
        BUILD_NOTES.append(f"{spec['name']}: {n}")
    print(f.describe())
    print(f"  -> characters/{folder}/")
    return folder


def write_extra_costumes(disc, chara, looks, chars_dir, folder, first=None, author="Custom Character"):
    """A package that gives disc character `chara` (a BT3 id) more costumes than the disc has room for: a folder
    with `replaces=<chara>` and only the model files of costumes first + 1 .. (`first`: default the costumes the disc
    has, so the new ones come after them). `looks`: [(id, costume index)] of the models to wear, each with the disc
    character's own moves, names and voice (its animations and effects are the disc's, which they must fit). The
    game: a package that replaces a disc character adds the costumes it has after the disc's (plat_charas.c)."""
    if is_bt4(chara) or not 0 <= chara < len(NAMES):
        raise Fail("extra costumes are for a character of the BT3 disc")
    have = disc.costume_count(chara) if first is None else first
    if not looks or have + len(looks) > COSTUME_MAX:
        raise Fail(f"{NAMES[chara]}: 1 to {COSTUME_MAX - have} extra costumes")
    spec = {"name": NAMES[chara], "costumes": [{"chara": c, "costume": k + 1} for c, k in looks],
            "moveset": {"chara": chara}, "base": {"chara": chara}, "voice": {"chara": chara}}
    f = Fighter(disc, spec, "extra costumes")
    out = chars_dir / folder
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.pak"):
        old.unlink()
    stem = re.sub(r"[^A-Za-z0-9]+", "_", NAMES[chara]).strip("_")[:24] or "fighter"
    for i, costume in enumerate(f.costumes):
        n = have + i + 1
        (out / f"{stem}_{n}p.pak").write_bytes(f.model(costume, False).bytes())
        (out / f"{stem}_{n}p_dmg.pak").write_bytes(f.model(costume, True).bytes())
    manifest = [f"# made by port/tools/custom_fighter.py: costumes {have + 1}..{have + len(looks)} of {NAMES[chara]}",
                "format=1", f"replaces={chara}", f"author={author}",
                "about=" + (f"More costumes for {NAMES[chara]}: " + ", ".join(
                    f"{name(c)} {k + 1}" for c, k in looks))[:199]]
    (out / "character.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    return folder


def game_dir(game):
    if not game:
        raise Fail("--game <the game's folder> is needed")
    g = pathlib.Path(game)
    if not (g / "Tenkaichi3Decomp.exe").exists() and not (g / "gamedata").exists():
        raise Fail(f"{g} does not look like the game's folder (no Tenkaichi3Decomp.exe or gamedata/)")
    return g


def cmd_build(args):
    write_package(Disc(open_source(args)), args.fighter, game_dir(args.game) / "characters", {})


def replace_files(disc, spec):
    """{(archive folder, index): bytes} for `replace`."""
    f = Fighter(disc, spec, "fighter")
    if "slot" not in spec:
        raise Fail('replace needs "slot": {"chara": ..., "costume": ...} (whose place the fighter takes)')
    slot = ref(spec["slot"])
    if is_bt4(slot[0]):
        raise Fail("the place a fighter takes in this game is a BT3 character (for BT4, use the BT4 install)")
    if slot[1] >= 4:
        raise Fail("the place a fighter takes is one of the disc's four costumes (more: `build` a package)")
    out = {}
    for damaged in (False, True):
        out[("pzs3us1", chara_file(*slot) + (4 if damaged else 0) - 1)] = f.model(f.look, damaged).bytes()
    if f.changes_motions() or f.moveset[0] != slot[0]:
        out[("pzs3us1", chara_file(slot[0], 8) - 1)] = f.anims().bytes()
        out[("pzs3us1", chara_file(slot[0], 9) - 1)] = f.effects().bytes()
    p = f.portrait[0] if "portrait" in spec else f.look[0]
    if p != slot[0]:  # the menus' picture and the team picture of the fight
        out[("pzs3us1", PORTRAIT_FILE + slot[0] - 1)] = disc.portrait(p)
        for side in (0, 1):
            m = disc.member(p, side)
            if m is not None:
                out[("pzs3us1", MEMBER_FILE + slot[0] * 2 + side - 1)] = m
    if f.voice[0] != slot[0]:
        bank_us, bank_jp, lines_us, lines_jp = disc.voice_files(f.voice[0])
        out[("pzs3us1", VOICE_BANK_US + slot[0] - 1)] = bank_us
        out[("pzs3us1", VOICE_BANK_JP + slot[0] - 1)] = bank_jp
        for first, lines in ((VOICE_LINE_US, lines_us), (VOICE_LINE_JP, lines_jp)):
            for line, data in enumerate(lines):
                if len(data) >= 16:
                    out[("pzs3us2", first + slot[0] * 100 + line - PT2_FIRST_ID)] = data
    return f, slot, out


def bt4_stage(disc, spec, folder):
    """The fighter's files in BT3 formats, as port/tools/bt4/bt4_patch.py takes them (--model, --damaged, --anim,
    --effect, --portrait, --voice): -> the arguments for it."""
    f = Fighter(disc, spec, "fighter")
    folder = pathlib.Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "model.pak").write_bytes(f.model(f.look, False).bytes())
    (folder / "damaged.pak").write_bytes(f.model(f.look, True).bytes())
    (folder / "anim.pak").write_bytes(f.anims().bytes())
    (folder / "effect.pak").write_bytes(f.effects().bytes())
    shot = spec.get("portrait_image")
    shot = pathlib.Path(spec.get("_dir", ".")) / shot if shot else None
    if shot is not None and shot.is_file():  # the creator's screenshot portrait
        b = f.base[0]
        (folder / "portrait.bin").write_bytes(portrait_module().portrait_bin(shot.read_bytes(), disc.portrait(b)))
    else:
        (folder / "portrait.bin").write_bytes(disc.portrait(f.portrait[0] if "portrait" in spec else f.look[0]))
    bank_us, bank_jp, lines_us, lines_jp = disc.voice_files(f.voice[0])
    v = folder / "voice"
    (v / "us").mkdir(parents=True, exist_ok=True)
    (v / "jp").mkdir(parents=True, exist_ok=True)
    (v / "bank_us.bin").write_bytes(bank_us)
    (v / "bank_jp.bin").write_bytes(bank_jp)
    for lang, lines in (("us", lines_us), ("jp", lines_jp)):
        for line, data in enumerate(lines):
            if len(data) >= 16:
                (v / lang / f"{line:02d}.adx").write_bytes(data)
    return ["--model", str(folder / "model.pak"), "--damaged", str(folder / "damaged.pak"),
            "--anim", str(folder / "anim.pak"), "--effect", str(folder / "effect.pak"),
            "--portrait", str(folder / "portrait.bin"), "--voice", str(v)]


def bt4_install(disc, fighters, work, log=print):
    """Puts fighters [(spec, (BT4 id, costume index))] into a fresh copy of the BT4 disc
    (SLUS_219.78.DBZBT4B14REV2ENG.custom.iso next to the original): each one in place of that BT4 character's
    costume. The copy is made again from the original each time, so it holds exactly these fighters."""
    import subprocess
    patch = pathlib.Path(__file__).resolve().parent / "bt4" / "bt4_patch.py"
    if not fighters:
        raise Fail("no fighter to put into BT4")
    for n, (spec, (c, costume)) in enumerate(fighters):
        if not is_bt4(c):
            raise Fail(f"{spec.get('name')}: the BT4 place must be a BT4 character")
        log(f"{spec.get('name')}: building its files")
        args = bt4_stage(disc, spec, pathlib.Path(work) / f"bt4_{n}")
        cmd = [sys.executable, str(patch), "--iso", bt4_iso(), "--chara", str(c - BT4_BASE),
               "--costume", str(costume + 1)] + args + (["--fresh"] if n == 0 else [])
        log(f"{spec.get('name')}: writing into the BT4 disc copy (in place of {name(c)}, costume {costume + 1})")
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if r.returncode != 0:
            raise Fail(f"the BT4 patcher stopped:\n{(r.stdout + r.stderr)[-1500:]}")
        log(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "written")
    out = pathlib.Path(bt4_iso()).with_name("SLUS_219.78.DBZBT4B14REV2ENG.custom.iso")
    log(f"done: {out}")
    return out


def mods_names(slot):
    names = [("pzs3us1", chara_file(slot[0], k) - 1) for k in (slot[1], slot[1] + 4, 8, 9)]
    names += [("pzs3us1", b + slot[0] - 1) for b in (VOICE_BANK_US, VOICE_BANK_JP)]
    names += [("pzs3us1", PORTRAIT_FILE + slot[0] - 1)]
    names += [("pzs3us1", MEMBER_FILE + slot[0] * 2 + side - 1) for side in (0, 1)]
    names += [("pzs3us2", first + slot[0] * 100 + line - PT2_FIRST_ID)
              for first in (VOICE_LINE_US, VOICE_LINE_JP) for line in range(100)]
    return names


def cmd_replace(args):
    spec = load_spec(args.fighter)
    f, slot, files = replace_files(Disc(open_source(args)), spec)
    mods = game_dir(args.game) / "gamedata" / "mods"
    for part, index in mods_names(slot):  # what an earlier version of this fighter left
        old = mods / part / f"{index:05d}.bin"
        if (part, index) not in files and old.exists():
            old.unlink()
    for (part, index), data in sorted(files.items()):
        (mods / part).mkdir(parents=True, exist_ok=True)
        (mods / part / f"{index:05d}.bin").write_bytes(data)
    print(f.describe())
    print(f"  -> in place of {name(slot[0])}, costume {slot[1] + 1} ({len(files)} files in gamedata/mods/)")
    if f.changes_motions():
        print(f"  note: {name(slot[0])}'s other costumes share these animations and effects while this is installed")


def cmd_remove(args):
    slot = ref(load_spec(args.fighter)["slot"])
    mods = game_dir(args.game) / "gamedata" / "mods"
    n = 0
    for part, index in mods_names(slot):
        p = mods / part / f"{index:05d}.bin"
        if p.exists():
            p.unlink()
            n += 1
    print(f"removed {n} files from gamedata/mods/")


def cmd_list(args):
    disc = Disc(open_source(args))
    for c, name in enumerate(NAMES):
        n = sum(disc.has_costume(c, k) for k in range(4))
        print(f"{c:3d}  {name:34s} {n} costume{'s' if n != 1 else ''}")


def cmd_moves(args):
    disc = Disc(open_source(args))
    c = chara_id(args.chara)
    names = move_names(disc.model(c, 0))
    print(name(c))
    for key, slot in SLOTS.items():
        print(f"  {key:9s} {names[slot] if slot < len(names) else '-'}")


def cmd_inspect(args):
    disc = Disc(open_source(args))
    c = chara_id(args.chara)
    m = disc.model(c, args.costume - 1)
    print(f"{name(c)}, costume {args.costume}: file id {chara_file(c, args.costume - 1):#x}")
    for e, p in enumerate(m.parts, 1):
        if p:
            tag = p[:4].decode("latin1") if p[:4].isascii() and p[:4].isalpha() else p[:8].hex()
            print(f"  {e:#05x} {len(p):9,d}  {tag}")


def cmd_selftest(args):
    disc = Disc(open_source(args))
    n = 0
    for c in range(len(NAMES)):
        for k in range(10):
            data = disc.raw(chara_file(c, k))
            parts = split_pack(data)
            if parts is not None:
                if join_pack(parts) != data:
                    raise Fail(f"{NAMES[c]} file {k}: rebuilt pack differs")
                n += 1
        for k in range(4):
            if disc.has_costume(c, k):
                m = disc.model(c, k)
                for e in TEXT_ENTRIES:
                    if m[e]:
                        h, b, t = text_blocks(m[e])
                        if join_text(h, b, t).rstrip(b"\0") != m[e].rstrip(b"\0"):
                            raise Fail(f"{NAMES[c]} move text {e:#x}: rebuilt text differs")
                        if len(b) != 5 and e == 0x2D:
                            print(f"  note: {NAMES[c]} costume {k + 1} has {len(b)} moves in its list")
    print(f"ok: {n} packs and every move list rebuilt byte for byte")


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--iso", help="the BT3 USA disc image")
    common.add_argument("--game", help="the game's folder (Tenkaichi3Decomp.exe, gamedata/)")
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("build", "replace", "remove"):
        sub.add_parser(name, parents=[common]).add_argument("fighter")
    sub.add_parser("list", parents=[common])
    sub.add_parser("moves", parents=[common]).add_argument("chara")
    p = sub.add_parser("inspect", parents=[common])
    p.add_argument("chara")
    p.add_argument("costume", nargs="?", type=int, default=1)
    sub.add_parser("selftest", parents=[common])
    args = ap.parse_args()
    try:
        {"build": cmd_build, "replace": cmd_replace, "remove": cmd_remove, "list": cmd_list, "moves": cmd_moves,
         "inspect": cmd_inspect, "selftest": cmd_selftest}[args.cmd](args)
    except Fail as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
