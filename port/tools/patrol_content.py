"""patrol_content.py: Corrupt Patrol's content (port/assets/patrol/*.json) made one document, written as
port/assets/patrol/content.json and as port/src/gs/patrol_content.inc (kPatrolContent, the same text in raw string
pieces, built into the game: port/src/gs/patrol_ui.cpp). Checks it as it goes: places, quest givers, masters (every
one of the 161 disc characters is unlocked exactly once)."""
import json, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[2]
src = root / "port" / "assets" / "patrol"
locs = json.loads((src / "locations.json").read_text(encoding="utf-8"))
story = json.loads((src / "story.json").read_text(encoding="utf-8"))
quests, masters = [], {}
for f in ("quests_b.json", "quests_c.json"):
    d = json.loads((src / f).read_text(encoding="utf-8"))
    quests += d["quests"]
    masters.update(d["masters"])
masters.update(story["masters"])
ids = {l["id"] for l in locs}
for l in locs:
    extra = story.get("locations", {}).get(l["id"], {})
    l.setdefault("desc", extra.get("desc", ""))
    l.setdefault("arrive", extra.get("arrive", ""))
errors = []
unlocks = {}
for c in story["chapters"]:
    for m in c["masters"]:
        unlocks.setdefault(m, []).append(c["id"])
    if c["location"] not in ids:
        errors.append("chapter %s: place %s" % (c["id"], c["location"]))
givers = {(p["chara"], l["id"]) for l in locs for p in l["people"] if p["role"] == "quests"}
for q in quests:
    for m in q["reward"]["masters"]:
        unlocks.setdefault(m, []).append(q["id"])
    if (q["giver"], q["location"]) not in givers:
        errors.append("quest %s: giver %s is not at %s" % (q["id"], q["giver"], q["location"]))
for k, v in masters.items():
    if v.get("home") not in ids:
        errors.append("master %s: home %s" % (k, v.get("home")))
for i in range(161):
    if len(unlocks.get(i, [])) != 1:
        errors.append("master %d unlocked by %s" % (i, unlocks.get(i)))
    if str(i) not in masters:
        errors.append("master %d has no lines" % i)
doc = {"format": 1, "locations": locs, "welcome": story["welcome"], "ending": story["ending"], "services": story["services"],
       "chapters": story["chapters"], "quests": quests, "masters": masters}
text = json.dumps(doc, ensure_ascii=True, indent=1)
(src / "content.json").write_text(text + "\n", encoding="utf-8")
pieces = [text[i:i + 30000] for i in range(0, len(text), 30000)]
inc = ["// Made by port/tools/patrol_content.py from port/assets/patrol/*.json: do not edit (edit those and run it again).",
       "static const char *const kPatrolContent[] = {"]
inc += ['R"PCX(' + p + ')PCX",' for p in pieces]
inc += ["NULL,", "};", ""]
(root / "port" / "src" / "gs" / "patrol_content.inc").write_text("\n".join(inc), encoding="utf-8", newline="\n")
print("%d places, %d chapters, %d quests, %d masters, %d bytes" % (len(locs), len(story["chapters"]), len(quests), len(masters), len(text)))
for e in errors:
    print("problem:", e)
sys.exit(1 if errors else 0)
