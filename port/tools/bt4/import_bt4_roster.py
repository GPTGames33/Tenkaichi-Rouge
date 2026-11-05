#!/usr/bin/env python3
"""Every character of the Team BT4 mod that BT3 does not have, as an added character of its own (a character
package each, docs/port/character_packages.md), with its costumes (up to 4), moves, animations, effects, voices,
portrait, roster chip, and its transformations and fusions linked to the other imported ones or to BT3's own.

    python port/tools/bt4/import_bt4_roster.py --game "<the game's folder>" [--only "Jiren,Hit"] [--list]

Needs the BT4 disc image next to the repository (SLUS_219.78.DBZBT4B14REV2ENG.iso) and a build with character
packages (the Custom Character build: 85 added characters at most). The folders go into <game>/characters/ as
"<name> (BT4)"; a list of what was made is characters/.bt4_roster.json (running it again replaces them).
"""
import argparse, json, pathlib, re, shutil, struct, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import custom_fighter as cf  # noqa: E402

PLACEHOLDERS = {161, 162, 163, 164}  # the menus' Random, ???, Password Character, Not selectable


def pairing():
    """BT4 id -> BT3 id of the characters both games have (port/tools/bt4: bt3_to_bt4_chara.json, else by name)."""
    for p in (HERE / "bt3_to_bt4_chara.json", HERE.parents[3] / "research" / "bt3_to_bt4_chara.json"):
        if p.is_file():
            m = json.loads(p.read_text(encoding="utf-8"))
            return {int(v): int(k) for k, v in m.items()}
    raise SystemExit("no BT3 <-> BT4 pairing table (bt3_to_bt4_chara.json)")


def base_for(label):
    """A BT3 character to stand on (AI behaviour, roster tables): the one whose name starts the same way."""
    low = label.lower()
    best, score = 3, 0
    for c, n in enumerate(cf.NAMES):
        words = n.lower().replace("(", " ").replace(")", " ").split()
        s = 0
        for w in words:
            if w in low:
                s += 1
            else:
                break
        if s > score:
            best, score = c, s
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", required=True)
    ap.add_argument("--only", help="names (comma separated) to import, for testing")
    ap.add_argument("--list", action="store_true", help="only list what would be imported")
    a = ap.parse_args()
    game = pathlib.Path(a.game)
    disc = cf.Disc(cf.GameData(game))
    pair = pairing()
    names = cf.bt4()  # (opens the BT4 disc)
    ch = names.ch
    todo = []
    for c, label, n in cf.bt4_names():
        if c in pair or c in PLACEHOLDERS or not n:
            continue
        nm, form = ch.names()[c]
        nm = (nm or label[5:]).strip()
        form = (form or "").strip()
        todo.append((c, nm, form, n))
    if a.only:
        want = {w.strip().lower() for w in a.only.split(",")}
        todo = [t for t in todo if t[1].lower() in want or f"{t[1]} ({t[2]})".lower() in want]
    print(f"{len(todo)} BT4 characters to import")
    if a.list:
        for c, nm, form, n in todo:
            print(f"  BT4 {c:3d}  {nm}{' - ' + form if form else ''}  ({n} costume{'s' if n > 1 else ''})")
        return
    import unicodedata
    plain = lambda t: unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode()  # (folder names: plain letters)
    folder_of = {c: cf.folder_name(plain(f"{nm}{' ' + form if form else ''} (BT4)")) for c, nm, form, n in todo}
    chars = game / "characters"
    chars.mkdir(exist_ok=True)
    work = game / "custom_fighters" / ".cache" / "bt4_roster"
    work.mkdir(parents=True, exist_ok=True)
    P = cf.portrait_module()
    made = []
    for k, (c, nm, form, n) in enumerate(todo):
        cid = cf.BT4_BASE + c
        base = base_for(f"{nm} {form}")
        spec = {"name": f"{nm}"[:63], "title": form[:95], "base": {"chara": base},
                "costumes": [{"chara": cid, "costume": i + 1} for i in range(min(4, n))],
                "moveset": {"chara": cid}, "voice": {"chara": cid}, "portrait": {"chara": cid}}
        sp = work / f"{c:03d}.json"
        sp.write_text(json.dumps(spec, ensure_ascii=False), encoding="utf-8")
        tmp = work / "out"
        if tmp.exists():
            shutil.rmtree(tmp)
        try:
            built = cf.write_package(disc, sp, tmp, {})
        except cf.Fail as e:
            print(f"  ! {nm} {form}: left out: {e}")
            continue
        dest = chars / folder_of[c]
        if dest.exists():
            shutil.rmtree(dest)
        shutil.move(str(tmp / built), str(dest))
        # the roster chip and the team pictures, from the BT4 portrait
        try:
            stored = P.tex_rgba(P._unpack_template(disc.portrait(cid)))[2]  # (the BT4 portrait as it is stored)
            chip = disc.chip(base)
            if chip and stored:
                (dest / "chip.bin").write_bytes(P.chip_bin(None, chip, stored=stored))
            if stored:
                m1, m2 = P.member_bins(None, disc.member(base, 0), disc.member(base, 1), stored=stored)
                (dest / "member_1p.bin").write_bytes(m1)
                (dest / "member_2p.bin").write_bytes(m2)
        except Exception as e:
            print(f"  ({nm}: chip not made: {e})")
        # transformations and fusions: the BT4 ids of its table, as links here
        par = disc.model(cid, 0)[0x12]

        def link(t):
            if t == 0xFF:
                return "none"
            if t in folder_of:
                return folder_of[t]
            if t in pair:
                return f"disc:{pair[t]}"
            return "none"
        lines = []
        for slot, key in enumerate(("left", "up", "right", "down")):
            t = par[0x98 + slot]
            if t != 0xFF:
                lines.append(f"{key}={link(t)}")
        for f in range(3):
            t = par[0xB4 + f]
            if t != 0xFF:
                lines.append(f"fusion{f + 1}={link(t)}")
                partners = [link(x) for x in par[0xBA + f * 4:0xBA + f * 4 + 4] if x != 0xFF]
                if partners:
                    lines.append(f"partner{f + 1}={','.join(partners)}")
        txt = (dest / "character.txt").read_text(encoding="utf-8")
        txt = re.sub(r"^author=.*$", "author=Team BT4 (imported from your BT4 disc)", txt, flags=re.M)
        (dest / "character.txt").write_text(txt + "".join(l + "\n" for l in lines), encoding="utf-8")
        made.append(folder_of[c])
        print(f"  {k + 1:3d}/{len(todo)}  {folder_of[c]}  (base {cf.NAMES[base]}; {len(lines)} links)", flush=True)
    (chars / ".bt4_roster.json").write_text(json.dumps(made, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"done: {len(made)} BT4 characters in {chars}")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
