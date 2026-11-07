#!/usr/bin/env python3
"""Every character of the Team BT4 mod that BT3 does not have, as an added character of its own (a character
package each, docs/port/character_packages.md), with all its costumes (BT4 has up to 9; the game takes 16), moves,
animations, effects, voices, portrait, roster chip, and its transformations and fusions linked to the other imported
ones or to BT3's own. With --extras, also the costumes BT4 has beyond BT3's for the characters both games have, as
a package per character that replaces nothing but adds them ("<name> extra costumes (BT4)": `replaces=<id>` with
costume 5 and on, each with the BT3 character's own moves).

    python port/tools/bt4/import_bt4_roster.py --game "<the game's folder>" [--chars DIR] [--work DIR] [--extras]
                                               [--only "Jiren,Hit"] [--list]

Needs the BT4 disc image next to the repository (SLUS_219.78.DBZBT4B14REV2ENG.iso) and a build with character
packages (the Custom Character build: 1024 added characters at most, 85 in one battle). The folders go into <game>/characters/ (or
--chars) as "<name> (BT4)"; a list of what was made is .bt4_roster.json there (running it again replaces them).
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
    ap.add_argument("--chars", help="the characters folder to write (default: <game>/characters)")
    ap.add_argument("--work", help="a folder for the work files (default: <game>/custom_fighters/.cache/bt4_roster)")
    ap.add_argument("--extras", action="store_true",
                    help="also the costumes BT4 has beyond BT3's for the characters both games have")
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
    extras = []  # (BT3 id, BT4 id, the BT4 costumes past BT3's)
    if a.extras:
        for c4, c3 in sorted(pair.items(), key=lambda t: t[1]):
            if not 0 <= c3 < len(cf.NAMES) or c4 >= len(ch.costumes):
                continue
            have, more = disc.costume_count(c3), min(ch.costumes[c4], cf.COSTUME_MAX)
            if have > 0 and more > have and all(e[0] != c3 or len(e[2]) < more - have for e in extras):
                extras = [e for e in extras if e[0] != c3] + [(c3, c4, list(range(have, more)))]  # (two BT4 ids for one: the one with more)
        extras.sort()
        if a.only:
            want = {w.strip().lower() for w in a.only.split(",")}
            extras = [e for e in extras if cf.NAMES[e[0]].lower() in want]
    if a.list:
        for c, nm, form, n in todo:
            print(f"  BT4 {c:3d}  {nm}{' - ' + form if form else ''}  ({n} costume{'s' if n > 1 else ''})")
        for c3, c4, ks in extras:
            print(f"  BT3 {c3:3d}  {cf.NAMES[c3]}: {len(ks)} more costume{'s' if len(ks) > 1 else ''} from BT4 {c4} "
                  f"(costumes {ks[0] + 1}..{ks[-1] + 1})")
        return
    import unicodedata
    plain = lambda t: unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode()  # (folder names: plain letters)
    folder_of = {c: cf.folder_name(plain(f"{nm}{' ' + form if form else ''} (BT4)")) for c, nm, form, n in todo}
    chars = pathlib.Path(a.chars) if a.chars else game / "characters"
    chars.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(a.work) if a.work else game / "custom_fighters" / ".cache" / "bt4_roster"
    work.mkdir(parents=True, exist_ok=True)
    P = cf.portrait_module()
    made = []
    for k, (c, nm, form, n) in enumerate(todo):
        cid = cf.BT4_BASE + c
        base = base_for(f"{nm} {form}")
        spec = {"name": f"{nm}"[:63], "title": form[:95], "base": {"chara": base},
                "costumes": [{"chara": cid, "costume": i + 1} for i in range(min(cf.COSTUME_MAX, n))],
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
        # the roster chip and the team pictures: the BT4 disc's own (write_package already took them)
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
        print(f"  {k + 1:3d}/{len(todo)}  {folder_of[c]}  (base {cf.NAMES[base]}; {min(cf.COSTUME_MAX, n)} costumes; "
              f"{len(lines)} links)", flush=True)
    for k, (c3, c4, ks) in enumerate(extras):
        folder = cf.folder_name(plain(f"{cf.NAMES[c3]} extra costumes (BT4)"))
        dest = chars / folder
        if dest.exists():
            shutil.rmtree(dest)
        try:
            cf.write_extra_costumes(disc, c3, [(cf.BT4_BASE + c4, kk) for kk in ks], chars, folder,
                                    author="Team BT4 (imported from your BT4 disc)")
        except cf.Fail as e:
            print(f"  ! {cf.NAMES[c3]} extra costumes: left out: {e}")
            if dest.exists():
                shutil.rmtree(dest)
            continue
        made.append(folder)
        print(f"  extra {k + 1:3d}/{len(extras)}  {folder}  (costumes {ks[0] + 1}..{ks[-1] + 1} from BT4 {c4})", flush=True)
    (chars / ".bt4_roster.json").write_text(json.dumps(made, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"done: {len(made)} BT4 characters in {chars}")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
