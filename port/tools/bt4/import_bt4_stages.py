#!/usr/bin/env python3
"""Every stage of the Team BT4 mod that BT3 does not have (its new maps and its changed versions of BT3's), as a
stage package each (docs/port/adding_stages.md, "Stage packages"): a folder with stage.txt and the stage's files,
for the `stages` folder next to the game.

    python port/tools/bt4/import_bt4_stages.py --out "<folder>" [--iso <BT4 disc image>] [--only 0x1f,0x2a] [--list]

Each package has, from the BT4 disc:
  model.unk       the one-screen model (BT4 file 0x5F3 + stage: geometry, collision, textures, sky, lights, the
                  stage's objects and their broken states, effects, paths)
  split.unk       the split-screen model (0x656 + stage), where it is not the same file
  bank.bin        the stage's sound bank (0x590 + stage)
  thumb.bin       the stage select's picture (0x6B9 + stage)
  icon.bin        the select reel's small picture (menu archive 0x1C1, section 2, its section 46, entry stage + 1)
  name.rgba       the select's name picture (the same pack's section 6: four 512 x 64 names to a sheet), RGBA
  transition.bin  the scene shown while it is destroyed (0x1BB + the kind byte of its model), where BT3's would differ
  stage.txt       name, which stage it turns into when destroyed (destroyed=: another package, or disc:<BT3 id>),
                  placement= (the BT3 stage whose places the technique scenes take: BT4 takes stage % 35) and
                  rules= (the stage id BT4's fight rules see: its own id where that is below 0x24)

What is left out: the 31 stages of BT4's select that are BT3's own (the same model but for a byte or two of its
settings), and the BT4 models its select does not list (0x24..0x26, 0x35, 0x36, 0x45, 0x4A, 0x4D). BT4 keeps the
ambient sound loops of its stages where BT3 has them (file 0x916 + kind, the same 35 streams as BT3's 0x10BA4 +
kind), so no package needs one. Its music list is another matter (songs, not stages).

How BT4 stores its stages (from its ELF, which is BT3's patched): 99 stage slots, file ids 0x590 (bank), 0x5F3
(model), 0x656 (split model), 0x6B9 (select picture) + stage; BtlStage_GetChangeTarget returns the signed byte at
+0xD of the model's environment record (BT3: the kind at +0xC, 1 -> 0xF, 2 -> 3); the destruction scene is file
0x1BB + the byte at +0xC (BT3: 0x1BF for stage 3, else 0x1C0); BtlSuper_GetStagePlacement takes stage % 35 (a code
cave in BIN/DBZP.BIN). The select's list (section 39 of the same pack) has 66 entries.
"""
import argparse, pathlib, re, shutil, struct, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import bt4_disc  # noqa: E402

ISO_NAME = "SLUS_219.78.DBZBT4B14REV2ENG.iso"
F_BANK, F_MODEL, F_SPLIT, F_THUMB, F_TRANSITION = 0x590, 0x5F3, 0x656, 0x6B9, 0x1BB

# BT4's stage names by stage id, as its select's name pictures say them (section 6 of the select's pack; read off
# the pictures, which are the only place the disc names them). None: no picture.
NAMES = {
    0x00: "Wasteland - Evening", 0x01: "Rocky Area - Noon", 0x02: "Planet Namek", 0x03: "Tournament of Power - Final",
    0x04: "World Tournament - Noon", 0x05: "Kami's Lookout - Noon", 0x06: "Cell Game Arena - Ruins",
    0x07: "Supreme Kai's World", 0x08: "Hyperbolic Time Chamber", 0x09: "City Ruins - Noon", 0x0A: "Mountain Road - Noon",
    0x0B: "Archipelago - Evening", 0x0C: "Kame House - Noon", 0x0D: "Glacier", 0x0E: "Ice Continent", 0x0F: "Dying Planet",
    0x10: "Outer Space - Earth", 0x11: "Tournament of Power - Climax", 0x12: "Hell", 0x13: "Desert - Noon",
    0x14: "King's Castle", 0x15: "Penguin Village - Noon", 0x16: "Mount Paozu", 0x17: "Plains - Noon", 0x18: "South City",
    0x19: "Baba's Palace - Night", 0x1A: "Rocky Area - Night", 0x1B: "World Tournament - Evening",
    0x1C: "Cell Games Arena - Noon", 0x1D: "City Ruins - Evening", 0x1E: "City Ruins - Future", 0x1F: "Tournament of Power",
    0x20: "Champa's Ring", 0x21: "Islands", 0x22: "Planet - Evening", 0x23: "Random", 0x27: "Otherworld Tournament",
    0x28: "Kami's Lookout - Night", 0x29: "Cell Games Arena - Evening", 0x2A: "Beerus' Planet", 0x2C: "West City",
    0x2D: "Mountain Road - Night", 0x2E: "Archipelago - Noon", 0x2F: "Kame House - Night", 0x30: "Planet - Night",
    0x32: "Ruined Earth", 0x33: "Outer Space - Planet Vegeta", 0x34: "Penguin Village - Night", 0x38: "Muscle Tower",
    0x39: "Planet Tuffle", 0x3A: "Plains - Night", 0x3B: "Wasteland - Night", 0x3C: "Rocky Area - Evening",
    0x3D: "Baba's Palace - Noon", 0x41: "City Ruins - Night", 0x42: "Desert - Evening", 0x43: "Desert - Night",
    0x44: "Mountain Road - Evening", 0x46: "Wasteland - Noon", 0x48: "Agonizing Planet Namek", 0x49: "Dying Planet Namek",
    0x4B: "Red Ribbon Base Yard - Noon", 0x4C: "Red Ribbon Base - Ruins", 0x4E: "North City - Ruins",
    0x4F: "Red Ribbon Base Yard - Storm", 0x50: "Open Field",
}

# The BT4 stages that are BT3's own, BT4 id -> BT3 id: the same model file but for the environment record's two
# bytes (the change target BT4 keeps there) and a few bytes elsewhere (at most 81 of 6 MB); research note in
# docs/port/adding_stages.md. A destroyed= naming one of them names the BT3 stage.
SAME_AS_BT3 = {
    0x46: 0, 0x01: 1, 0x49: 3, 0x04: 4, 0x05: 5, 0x29: 6, 0x07: 7, 0x09: 9, 0x0A: 10, 0x21: 11, 0x30: 13, 0x0D: 14,
    0x32: 15, 0x33: 16, 0x15: 17, 0x13: 19, 0x14: 20, 0x38: 21, 0x16: 22, 0x00: 23, 0x3B: 24, 0x3C: 25, 0x1A: 26,
    0x1B: 27, 0x1C: 28, 0x1D: 29, 0x41: 30, 0x42: 31, 0x43: 32, 0x44: 33, 0x22: 34,
}
# Changed versions of a BT3 stage that keep its name: the package's name says it is BT4's.
CHANGED = {0x02: "Planet Namek (BT4)", 0x08: "Hyperbolic Time Chamber (BT4)", 0x0C: "Kame House - Noon (BT4)", 0x12: "Hell (BT4)"}
RANDOM = 0x23


def find_iso(arg):
    if arg:
        return pathlib.Path(arg)
    for p in [pathlib.Path.cwd()] + list(HERE.parents):
        if (p / ISO_NAME).is_file():
            return p / ISO_NAME
    raise SystemExit(f"no BT4 disc image ({ISO_NAME}) found next to the repository or above it: give --iso")


def pack_section(pack, n):
    """Section n of a pack (word 0 the count, word n the offset of section n; the low two bits are flags)."""
    count = struct.unpack_from("<I", pack, 0)[0]
    start = struct.unpack_from("<I", pack, 4 * n)[0] & ~3
    end = struct.unpack_from("<I", pack, 4 * (n + 1))[0] & ~3 if n < count else len(pack)
    return pack[start:end]


def is_empty(b):
    """BT4's empty slot: one sector with a placeholder header, or nothing."""
    return len(b) <= 2048 and (b[:4] == bytes.fromhex("80000024") or not b.strip(b"\0"))


def env_record(model):
    """(id, flags, kind byte, target byte) of the model's environment record (part 1 of the pack, its +0x14)."""
    data = struct.unpack_from("<I", model, 8)[0]
    env = data + struct.unpack_from("<I", model, data + 0x14)[0]
    sid, flags = struct.unpack_from("<ii", model, env)
    return sid, flags, model[env + 0xC], struct.unpack_from("b", model, env + 0xD)[0]


def name_picture(sheets, stage):
    """The select's name picture of `stage`: 512 x 64 RGBA out of sheet stage / 4, row stage % 4."""
    sys.path.insert(0, str(HERE.parent))
    import import_chara as ic  # (its decoder of the PS2's texture layout)
    rows = ic.name_sheet(sheets, stage // 4)[(stage % 4) * 64:(stage % 4) * 64 + 64]
    return b"".join(rows)


def folder_name(order, stage):
    name = CHANGED.get(stage, NAMES[stage])
    return "BT4 %02d %s" % (order, re.sub(r"[^A-Za-z0-9 '()\-]", "", name))


def selected(disc):
    """The BT4 select's stages that BT3 does not have, in that select's order."""
    res = bt4_disc.bpe_decode(bt4_disc.word_at(disc.read_id(0x1C1), 2))
    lst = pack_section(res, 39)
    count = struct.unpack_from("<I", lst, 0)[0]
    ids = struct.unpack_from("<%di" % count, lst, 0x10)
    return res, [s for s in ids if s != RANDOM and s not in SAME_AS_BT3]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", help="the folder to write the packages into (a stages folder)")
    ap.add_argument("--iso", help="the BT4 disc image (SLUS-219.78, Beta 14 Rev2)")
    ap.add_argument("--only", help="BT4 stage ids, comma separated (0x1f,0x2a)")
    ap.add_argument("--list", action="store_true", help="only list what would be made")
    a = ap.parse_args()
    disc = bt4_disc.BT4Disc(str(find_iso(a.iso)))
    res, stages = selected(disc)
    folders = {s: folder_name(k + 1, s) for k, s in enumerate(stages)}
    if a.only:
        want = {int(x, 0) for x in a.only.split(",")}
        stages = [s for s in stages if s in want]
    if a.list or not a.out:
        for s in stages:
            m = disc.read_id(F_MODEL + s)
            sid, flags, kind, target = env_record(m)
            print("0x%02x  %-40s %4.1f MB  destroyed -> %s" % (s, folders[s], len(m) / 1048576.0,
                  "-" if target < 0 else ("0x%02x %s" % (target, NAMES.get(target, "?")))))
        print("%d stages" % len(stages))
        if not a.out:
            return
        return
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    icons = pack_section(res, 46)
    sheets = pack_section(res, 6)
    trans_default = disc.read_id(F_TRANSITION + 4)  # (kind 4: BT4's 0x1BF, the very file BT3 shows: its 0x1C0)
    for s in stages:
        folder = out / folders[s]
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir()
        model = disc.read_id(F_MODEL + s)
        (folder / "model.unk").write_bytes(model)
        split = disc.read_id(F_SPLIT + s)
        if not is_empty(split) and split.rstrip(b"\0") != model.rstrip(b"\0"):
            (folder / "split.unk").write_bytes(split)
        bank = disc.read_id(F_BANK + s)
        if not is_empty(bank):
            (folder / "bank.bin").write_bytes(bank)
        thumb = disc.read_id(F_THUMB + s)
        if not is_empty(thumb):
            (folder / "thumb.bin").write_bytes(thumb)
        (folder / "icon.bin").write_bytes(pack_section(icons, s + 1))
        (folder / "name.rgba").write_bytes(struct.pack("<3I", 512, 64, 1) + name_picture(sheets, s))
        sid, flags, kind, target = env_record(model)
        lines = ["# A stage of the Team BT4 mod (Budokai Tenkaichi 4 Beta 14 Rev2), imported from your BT4 disc",
                 "# by port/tools/bt4/import_bt4_stages.py: BT4 stage 0x%02x, its own name \"%s\"." % (s, NAMES[s]),
                 "name=%s" % CHANGED.get(s, NAMES[s]),
                 "placement=%d" % (s % 35)]
        if s < 0x24:
            lines.append("rules=%d" % s)
        if target >= 0:
            if target in folders:
                lines.append("destroyed=%s" % folders[target])
            elif target in SAME_AS_BT3:
                lines.append("destroyed=disc:%d" % SAME_AS_BT3[target])
            else:
                lines.append("# destroyed: BT4 stage 0x%02x, which is not imported" % target)
            trans = disc.read_id(F_TRANSITION + kind) if kind <= 5 else b""
            if trans and (trans.rstrip(b"\0") != trans_default.rstrip(b"\0") or SAME_AS_BT3.get(target) == 3):
                (folder / "transition.bin").write_bytes(trans)
        lines.append("source=BT4 0x%02x" % s)
        (folder / "stage.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
        print("0x%02x  %s" % (s, folder.name))
    print("%d stage packages in %s" % (len(stages), out))


if __name__ == "__main__":
    main()
