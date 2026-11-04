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

A fighter file (JSON). Every part is optional except "name"; a character is {"chara": name or id, "costume": 1..4}:
    {
      "name": "Goku Black Rose", "title": "Super Saiyan Rose",
      "base": "Goku (End)",                      AI, roster tables, and whatever is not given (default: look)
      "look": {"chara": "Goku (End)"},           body and hair; or "costumes": [ {...}, {...} ] for several
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
      "names": {"blast2_1": "Galick Kamehameha", "ultimate": "Rose Spirit Bomb"},
      "voice": {"chara": "Goku (End)"},          spoken lines, bank, lip sync, pre-fight lines (default: base)
      "portrait": {"chara": "Goku (End)"},       the menus' picture and the team picture (default: base)
      "health": 50000, "ki": 20000, "blast": 5,  10000 a bar; 100000 a full ki gauge; stocks 0..7
      "forms": {"up": "my_other_fighter.json"},  left / up / right: transformations (R3 + direction), down: back
      "form_costs": {"up": 2},                   blast stocks a transformation costs
      "slot": {"chara": "Goku (End)", "costume": 1}   `replace` only: whose place it takes
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
Standard library only.
"""
import argparse, json, pathlib, re, struct, sys

ALIGN = 64
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
    if isinstance(name, int):
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


def ref(obj, default=None):
    """{"chara": ..., "costume": 1..4} -> (id, costume index)."""
    if obj is None:
        return default
    if isinstance(obj, (str, int)):
        obj = {"chara": obj}
    costume = int(obj.get("costume", 1))
    if not 1 <= costume <= 4:
        raise Fail(f"costume {costume} out of range (1..4)")
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

    def has_costume(self, c, costume):
        return split_pack(self.raw(chara_file(c, costume)), 252) is not None

    def model(self, c, costume, damaged=False):
        if not self.has_costume(c, costume):
            raise Fail(f"{NAMES[c]} has no costume {costume + 1}")
        data = self.raw(chara_file(c, costume + (4 if damaged else 0)))
        if split_pack(data, 252) is None:  # no damaged model: the costume's own
            data = self.raw(chara_file(c, costume))
        return Pack(data, 252)

    def anims(self, c):
        return Pack(self.raw(chara_file(c, 8)))

    def effects(self, c):
        return Pack(self.raw(chara_file(c, 9)))


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
    _, blocks, _ = text_blocks(pack[0x2D])
    return [re.sub(r"^..(?:&\d\d\d)?\*\d\d\d", "", b[0]) for b in blocks]


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
        if not 1 <= len(self.costumes) <= 4:
            raise Fail("1 to 4 costumes")
        self.look = self.costumes[0]
        self.base = ref(spec.get("base"), self.look)
        self.moveset = ref(spec.get("moveset"), self.look)
        self.voice = ref(spec.get("voice"), self.base)
        self.portrait = ref(spec.get("portrait"), self.base)
        self.melee = ref(spec.get("melee"))
        self.kiblasts = ref(spec.get("ki_blasts"))
        self.moves = {}
        for key, v in spec.get("moves", {}).items():
            if key not in SLOTS:
                raise Fail(f"unknown move slot {key!r}: one of {', '.join(SLOTS)}")
            d = SLOTS[key]
            s = SLOTS[v.get("slot", key)] if isinstance(v, dict) else d
            if SLOT_KIND[s] != SLOT_KIND[d]:
                raise Fail(f"{key} can only take a move of the same kind ({SLOT_KIND[d]}), not {v.get('slot')}")
            self.moves[d] = (ref(v), s)
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
        ms = d.model(*self.moveset, damaged)
        for e in MOVESET_ENTRIES + tuple(TEXT_ENTRIES):
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
        for slot, name in self.names.items():
            for e in TEXT_ENTRIES:
                if m[e]:
                    h, blocks, t = text_blocks(m[e])
                    if slot < len(blocks):
                        blocks[slot] = rename_block(blocks[slot], name)
                        m[e] = join_text(h, blocks, t)
        if self.voice[0] != costume[0]:
            v = d.model(self.voice[0], 0, damaged)
            m[E_TALK] = v[E_TALK]
            for e in LIP_ENTRIES:
                m[e] = v[e]
        return m

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
        return e

    def changes_motions(self):
        return bool(self.moveset[0] != self.look[0] or self.moves or self.melee or self.kiblasts)

    def describe(self):
        def nm(r):
            return NAMES[r[0]] + (f" (costume {r[1] + 1})" if r[1] else "")
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
            lines.append(f"  {key:9s} {self.names.get(slot, own)}  <- {NAMES[who[0]]}'s {own} ({skey})")
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
    v = f.voice[0]
    (out / f"{stem}_voice_us.pak").write_bytes(disc.raw(VOICE_BANK_US + v))
    (out / f"{stem}_voice_jp.pak").write_bytes(disc.raw(VOICE_BANK_JP + v))
    for lang, first in (("voice_us", VOICE_LINE_US), ("voice_jp", VOICE_LINE_JP)):
        (out / lang).mkdir(exist_ok=True)
        for line in range(100):
            data = disc.raw(first + v * 100 + line)
            if len(data) >= 16:
                (out / lang / f"{line:03d}.adx").write_bytes(data)
    p = f.portrait[0]
    (out / "portrait.bin").write_bytes(disc.raw(PORTRAIT_FILE + p))
    for side in (0, 1):
        (out / f"member_{side + 1}p.bin").write_bytes(disc.raw(MEMBER_FILE + p * 2 + side))
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
    for direction, target in spec.get("forms", {}).items():
        if direction not in ("left", "up", "right", "down"):
            raise Fail(f"forms: {direction!r} is not left, up, right or down")
        if target in (None, "none"):
            manifest.append(f"{direction}=none")
        elif isinstance(target, str) and target.startswith("disc:"):
            manifest.append(f"{direction}=disc:{chara_id(target[5:])}")
        else:
            manifest.append(f"{direction}={write_package(disc, spec_path.parent / target, chars_dir, done)}")
    for direction, cost in spec.get("form_costs", {}).items():
        manifest.append(f"{direction}_cost={int(cost)}")
    (out / "character.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    print(f.describe())
    print(f"  -> characters/{folder}/")
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
    out = {}
    for damaged in (False, True):
        out[("pzs3us1", chara_file(*slot) + (4 if damaged else 0) - 1)] = f.model(f.look, damaged).bytes()
    if f.changes_motions() or f.moveset[0] != slot[0]:
        out[("pzs3us1", chara_file(slot[0], 8) - 1)] = f.anims().bytes()
        out[("pzs3us1", chara_file(slot[0], 9) - 1)] = f.effects().bytes()
    if f.voice[0] != slot[0]:
        v = f.voice[0]
        for bank in (VOICE_BANK_US, VOICE_BANK_JP):
            out[("pzs3us1", bank + slot[0] - 1)] = disc.raw(bank + v)
        for first in (VOICE_LINE_US, VOICE_LINE_JP):
            for line in range(100):
                out[("pzs3us2", first + slot[0] * 100 + line - PT2_FIRST_ID)] = disc.raw(first + v * 100 + line)
    return f, slot, out


def mods_names(slot):
    names = [("pzs3us1", chara_file(slot[0], k) - 1) for k in (slot[1], slot[1] + 4, 8, 9)]
    names += [("pzs3us1", b + slot[0] - 1) for b in (VOICE_BANK_US, VOICE_BANK_JP)]
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
    print(f"  -> in place of {NAMES[slot[0]]}, costume {slot[1] + 1} ({len(files)} files in gamedata/mods/)")
    if f.changes_motions():
        print(f"  note: {NAMES[slot[0]]}'s other costumes share these animations and effects while this is installed")


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
    print(NAMES[c])
    for key, slot in SLOTS.items():
        print(f"  {key:9s} {names[slot] if slot < len(names) else '-'}")


def cmd_inspect(args):
    disc = Disc(open_source(args))
    c = chara_id(args.chara)
    m = disc.model(c, args.costume - 1)
    print(f"{NAMES[c]}, costume {args.costume}: file id {chara_file(c, args.costume - 1):#x}")
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
