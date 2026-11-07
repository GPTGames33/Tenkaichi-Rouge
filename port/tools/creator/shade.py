#!/usr/bin/env python3
"""Per-part shading of a fighter's model (the creator's SHADING screen): shadow colour, shading borrowed from another
part, the Super Saiyan glow, the outline. Standard library only.

    python port/tools/creator/shade.py parts <pack.bin>
    python port/tools/creator/shade.py apply <pack.bin> -o <out.bin> --shading '{"hair": {"shadow": "#5020a0"}}'

    import shade; shade.parts(pack_bytes) -> [{key, label, triangles, own_shadow, glow, outline}]
    new_pack = shade.apply(pack_bytes, {"hair": {"shadow": "#5020a0", "glow": True}}, notes=[])

How the game shades a fighter (docs/port/CUSTOM_CHARACTER_RESEARCH.md, "Shading"):

* Cel shading: every batch (one triangle strip) of a mesh record carries two TEX0: its material texture (TEX0_1,
  drawn first) and a shading ramp (TEX0_2: the light's ramp picture at 0x3D40 read through a 256-colour CLUT at
  0x3D00 + the ramp's offset). The second layer is drawn with the same triangles, its u from how much the point faces
  the light, blended Cd - Cs (ALPHA_2 0x62, FIX 0x80): a ramp's colours are what is TAKEN AWAY from the lit picture
  in shadow (Goku's skin ramp: 0x3b4a52 at the darkest, then 0x2b373d, 0x1e262b, then 0 on the lit side). Ramps are
  16x16 PSMCT32 textures [clutFirst, clutFirst + clutCount) of the texture file (entry 0xC), 4 blocks each, at most
  16 (0x3D00..0x3D40).
* Glow (Super Saiyan): byte +0x2D of a material's TexEntry, 0..0x7F (0 = none; SSJ hair 0x50, SSJ bodies 0x14..0x64),
  written per alpha slot (+0x2C, 0..14) into the object's glow table (BtlObjMdl_InitAlphaSlots). The object glow
  pass (ObjGlow_Draw) blooms every pixel by its slot's value: the glow is the picture's own colour, blurred.
* Outline: a pass over the picture after the models (ObjOutline_Draw): a line wherever the object id each material
  leaves in the frame's alpha (row * 15 + slot) changes. Nothing per mesh: one width, one darkness (0x64).

What this writes:

* "shadow": a ramp of its own for the part's batches (tint_ramp): its shadow tones the chosen colour, with an alpha of
  how strong the original's shadow is there (0.55 .. 0.88), its lit tone black as much as the original took away,
  the highlight nothing - odd alphas below 0x80, which the game never has. The port's renderer draws such a ramp
  over the lit picture (Cs * As + Cd * (1 - As): gs_draw.c, "tint ramp") instead of subtracting it, so the shadow IS
  that colour. (A plain subtraction could never make yellow hair's shadows blue.)
* "like": the part's batches use the other part's main ramp (the one most of its triangles use; and that part's
  shadow colour when it has one and this part none).
* "glow": +0x2D of the part's textures (True: 0x50, as SSJ hair; False: 0; a number 1..127 as it is).
* "outline": "none": +0x2E of the part's textures = 1 (a byte the game never reads): the port's outline pass draws no
  line on the slots those textures give (BtlObjMdl_InitAlphaSlots -> Port_OutlineSlot, outline.frag).
  "body" (or null): as the game draws it.
A texture another part uses too (more of its triangles there than in this part) is copied for the part, with a free
slot of its own, when glow / outline differ; when there is no room (texture blocks 0x780, palette blocks 0x40, 15
slots) the change is left out and noted. A ramp beyond the 16 is left out the same way.

Parts are groups of the model's mesh records, by the node they hang on: face (neck, head, face), hair (the strands on
the head, and the head's batches drawn with a texture mostly the strands use: the hairline), chest, waist, upper
arms, forearms, hands, hips (hips and thighs), shins, feet, tail, accessories (sword, halo...: what gameplay flags
switch).
"""
import argparse, json, pathlib, struct, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mix  # noqa: E402

PARTS = (("face", "Face"), ("hair", "Hair"), ("chest", "Chest"), ("waist", "Waist"), ("upper_arms", "Upper arms"),
         ("forearms", "Forearms"), ("hands", "Hands"), ("hips", "Hips"), ("shins", "Shins"), ("feet", "Feet"),
         ("tail", "Tail"), ("accessories", "Accessories"))
PART_KEYS = tuple(k for k, _ in PARTS)
HEAD = {0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33}
CORE_PART = {}
for _p, _nodes in (("face", HEAD), ("waist", {0x00, 0x01, 0x02, 0x03, 0x10, 0x34, 0x35, 0x36}),
                   ("chest", {0x11, 0x12, 0x20}), ("upper_arms", {0x13, 0x21}), ("forearms", {0x14, 0x22}),
                   ("hands", set(range(0x15, 0x20)) | set(range(0x23, 0x2E))), ("hips", {0x08, 0x09, 0x0C, 0x0D}),
                   ("shins", {0x0A, 0x0E}), ("feet", {0x0B, 0x0F}), ("tail", {0x04, 0x05, 0x06, 0x07})):
    for _n in _nodes:
        CORE_PART[_n] = _p

GLOW_ON = 0x50          # Super Saiyan hair (Vegeta SSJ's hair locks, Goku SSJ3's)
TINT_MAX = 0.88         # how much of the shadow colour the darkest shadow is (the rest: the lit picture)
TINT_SPAN = 0.375       # and the lightest shadow tone that much less of it (0.88 -> 0.55)
E_OUTLINE = 0x2E        # TexEntry byte: 1 = no outline (the port's), 0 = the game's
OUTLINES = ("body", "none")


class ShadeError(Exception):
    pass


def hex_rgb(s):
    s = str(s).strip().lstrip("#")
    if len(s) != 6:
        raise ShadeError(f"a colour is #rrggbb, not {s!r}")
    try:
        return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    except ValueError:
        raise ShadeError(f"a colour is #rrggbb, not {s!r}")


def normalise(shading):
    """The spec's "shading" checked: {part: {"shadow": (r, g, b) | None, "like": part | None, "glow": 0..127 | None,
    "outline": "none" | None}}, only parts with something set."""
    if not shading:
        return {}
    if not isinstance(shading, dict):
        raise ShadeError("shading: {part: {shadow, like, glow, outline}}")
    out = {}
    for k, v in shading.items():
        if k not in PART_KEYS:
            raise ShadeError(f"shading: unknown part {k!r} (one of {', '.join(PART_KEYS)})")
        if not isinstance(v, dict):
            raise ShadeError(f"shading {k}: {{shadow, like, glow, outline}}")
        shadow = hex_rgb(v["shadow"]) if v.get("shadow") else None
        like = v.get("like") or None
        if like is not None and (like not in PART_KEYS or like == k):
            raise ShadeError(f"shading {k}: like {like!r} is not another part")
        g = v.get("glow")
        if g is None:
            glow = None
        elif g is True:
            glow = GLOW_ON
        elif g is False:
            glow = 0
        elif isinstance(g, (int, float)) and 0 <= g <= 127:
            glow = int(g)
        else:
            raise ShadeError(f"shading {k}: glow is true, false, null or 0..127")
        o = v.get("outline")
        if o not in (None, "body", "none"):
            raise ShadeError(f"shading {k}: outline is \"body\" or \"none\"")
        o = "none" if o == "none" else None
        if shadow or like or glow is not None or o:
            out[k] = {"shadow": shadow, "like": like, "glow": glow, "outline": o}
    return out


# ------------------------------------------------------------------------------------------------- the batches
class Batch:
    __slots__ = ("pos", "tex", "ramp", "part", "weight", "node")

    def __init__(self, pos, tex, ramp, weight, node):
        self.pos, self.tex, self.ramp, self.weight, self.node = pos, tex, ramp, weight, node
        self.part = None


def record_spans(e):
    """(record header offset, chain start, chain end, node) of every record of a pmdl (Model's walk)."""
    o = struct.unpack_from("<I", e, 0x6C)[0]
    out = []
    for _ in range(4096):
        nxt, pop, last, enabled = struct.unpack_from("<iHHH", e, o)
        ln = 0x40 if last else nxt
        node = struct.unpack_from("<H", e, o + 0xA)[0]
        if enabled and ln > 0x60:
            out.append((o, o + 0x60, o + ln, node))
        if last:
            break
        o += ln
    return out


def chain_batches(buf, start, end, node):
    out = []

    def tex(pos):
        lo, hi = struct.unpack_from("<II", buf, pos + 20)
        hi2 = struct.unpack_from("<I", buf, pos + 40)[0]
        n = struct.unpack_from("<I", buf, pos + 52)[0] & 0x7FFF   # the strip's GIF tag: its vertex count
        out.append(Batch(pos, (lo & 0x3FFF, (hi >> 5) & 0x3FFF), (hi2 >> 5) & 0x3FFF, max(1, n - 2), node))
    mix.walk_chain(buf, start, end, on_tex=tex)
    return out


def part_of(m, r):
    """The part a record belongs to (by its node, or the core node it hangs on)."""
    if r.node in mix.ACCESSORY:
        return "accessories"
    if r.node in mix.CORE:
        return CORE_PART.get(r.node, "waist")
    p = r.parent
    while p is not None and p.node not in mix.CORE:
        if p.node in mix.ACCESSORY:
            return "accessories"
        p = p.parent
    if p is None:
        return "waist"
    return "hair" if p.node in HEAD else CORE_PART.get(p.node, "waist")


class Shaded:
    """A model pack, its batches and their parts."""

    def __init__(self, pack_bytes):
        self.src = mix.Source(pack_bytes, "model")
        self.parts = [bytearray(p) for p in self.src.parts]
        self.pm = self.parts[mix.E_MODEL - 1]
        m = self.src.model
        recs = [r for r in m.recs if r.mesh]
        spans = record_spans(self.pm)
        if len(spans) != len(recs):
            raise ShadeError("the model's records are not what its chains say")
        self.batches = []
        for r, (_, a, b, node) in zip(recs, spans):
            part = part_of(m, r)
            for bt in chain_batches(self.pm, a, b, node):
                bt.part = part
                self.batches.append(bt)
        # the hairline: the head's batches drawn with a texture most of whose triangles are the strands'
        use = {}
        for bt in self.batches:
            use.setdefault(bt.tex, {}).setdefault(bt.part, 0)
            use[bt.tex][bt.part] += bt.weight
        for bt in self.batches:
            if bt.part == "face" and bt.node in HEAD:
                u = use[bt.tex]
                if u.get("hair", 0) > u.get("face", 0):
                    bt.part = "hair"
        # the mouth shapes (entries 4..0xB: node 0x30's chain for each) are the face's
        self.mouth = []
        for n in mix.E_MOUTH_A:
            c = self.parts[n - 1]
            if len(c) >= 32:
                try:
                    for bt in chain_batches(c, 0, len(c), 0x30):
                        bt.part = "hair" if use.get(bt.tex, {}).get("hair", 0) > use.get(bt.tex, {}).get("face", 0) \
                            else "face"
                        self.mouth.append((n, bt))
                except mix.MixError:
                    pass

    def part_keys(self):
        have = {bt.part for bt in self.batches}
        return [k for k in PART_KEYS if k in have]

    def weight(self, part):
        return sum(bt.weight for bt in self.batches if bt.part == part)

    def main(self, part, what):
        """The ramp (what = "ramp") or texture ("tex") most of a part's triangles use."""
        w = {}
        for bt in self.batches:
            if bt.part == part:
                k = getattr(bt, what)
                w[k] = w.get(k, 0) + bt.weight
        return max(sorted(w), key=lambda k: w[k]) if w else None


# --------------------------------------------------------------------------------------------- ramps, textures
def ramp_entries(t):
    """(data offset, entry count) of a ramp texture's colours in its upload packet."""
    w, h, dofs, _ = mix.mdlx.parse_gs_upload(t.pix, 0)
    return dofs, w * h


def _lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def tint_ramp(t, rgb, ref=None):
    """A copy of ramp t's pixels as a tint ramp (odd alphas below 0x80, drawn over the picture by the port): its
    shadow entries the colour rgb, as strong as the original's shadow is there (0.55 .. 0.88); its lit tone black, as
    much as the original took away; the highlight nothing. Which tones are shadow: how much each darkens `ref` (the
    colour of the textures drawn with the ramp): a quarter of the darkest's or less is the lit side; and in the
    ramps of four tones (dark 48 entries, mid 80, lit 80, nothing 48: most ramps) the lightest one too. (The Super
    Saiyan ramps have two shadow tones, one that hardly darkens the hair, and nothing on most of their entries.)"""
    pix = bytearray(t.pix)
    dofs, n = ramp_entries(t)
    if dofs + n * 4 > len(pix):
        raise ShadeError("a shading ramp is shorter than its picture")
    if is_tint(t):   # (one made before: the same strengths, the shadow in the new colour)
        for k in range(n):
            o = dofs + k * 4
            if pix[o:o + 3] != b"\0\0\0":
                pix[o:o + 3] = bytes(rgb)
        return bytes(pix)
    ref = ref or (200, 200, 200)
    sub = [tuple(pix[dofs + k * 4:dofs + k * 4 + 3]) for k in range(n)]
    drops = {}
    for d in set(sub):
        drops[d] = _lum(ref) - _lum([max(0, ref[j] - d[j]) for j in range(3)])
    top = max(drops.values()) or 1.0
    nonzero = sorted((d for d in drops if any(d)), key=lambda d: drops[d])
    lit = {d for d in nonzero if drops[d] <= 0.25 * top}
    if len(nonzero) >= 3 and sub.count((0, 0, 0)) < 0.4 * n:
        lit.add(nonzero[0])
    shadow = [drops[d] for d in nonzero if d not in lit]
    low = min(shadow) if shadow else top
    for k in range(n):
        d = sub[k]
        if any(d) and d not in lit:   # shadow: the colour
            f = (drops[d] - low) / (top - low) if top > low else 1.0
            a, c = (1.0 - TINT_SPAN + TINT_SPAN * f) * TINT_MAX, rgb
        else:   # the lit tone (darkened about as before: on a light surface; another part may borrow it) or the highlight
            a, c = _lum(d) / 200.0, (0, 0, 0)
        a = min(0x7F, int(round(a * 128)) | 1)
        pix[dofs + k * 4:dofs + k * 4 + 4] = bytes((c[0], c[1], c[2], a))
    return bytes(pix)


def is_tint(t):
    try:
        dofs, n = ramp_entries(t)
    except mix.mdlx.ModelError:
        return False
    a = [t.pix[dofs + k * 4 + 3] for k in range(n) if dofs + k * 4 + 3 < len(t.pix)]
    return bool(a) and all(x < 0x80 and x & 1 for x in a)


def texture_rgb(texfile, i):
    """The average colour of texture i's opaque pixels, or None."""
    sys.path.insert(0, str(HERE.parent))
    import custom_fighter as cf
    lay = cf.tex_layout(texfile, i)
    if lay is None:
        return None
    px = cf.tex_rgba(texfile, lay)
    s = [0, 0, 0]
    n = 0
    for p in range(0, len(px), 16):   # (every 4th pixel)
        if px[p + 3] >= 128:
            s[0] += px[p]; s[1] += px[p + 1]; s[2] += px[p + 2]
            n += 1
    return tuple(v // n for v in s) if n else None


def own_shadow(sh, part, texfile):
    """About the colour a part's shadow has as the game draws it: its main texture's colour less its main ramp's
    darkest entry ("#rrggbb"), or None."""
    t = sh.src.tex_by_ofs(*sh.main(part, "tex")) if sh.main(part, "tex") else None
    r = sh.src.ramp_by_ofs(sh.main(part, "ramp")) if sh.main(part, "ramp") is not None else None
    if t is None or r is None:
        return None
    rgb = texture_rgb(texfile, t.old)
    if rgb is None:
        return None
    try:
        dofs, n = ramp_entries(r)
    except mix.mdlx.ModelError:
        return None
    if is_tint(r):
        k = max(range(n), key=lambda k: r.pix[dofs + k * 4 + 3])
        a = r.pix[dofs + k * 4 + 3] / 128.0
        c = tuple(int(rgb[j] * (1 - a) + r.pix[dofs + k * 4 + j] * a) for j in range(3))
    else:
        k = max(range(n), key=lambda k: sum(r.pix[dofs + k * 4:dofs + k * 4 + 3]))
        c = tuple(max(0, rgb[j] - r.pix[dofs + k * 4 + j]) for j in range(3))
    return "#%02x%02x%02x" % c


def parts(pack_bytes):
    """The parts of a model for the SHADING screen: [{key, label, triangles, own_shadow, glow (its textures' largest
    glow), outline}]."""
    sh = Shaded(pack_bytes)
    tf = sh.parts[mix.E_TEX - 1]
    out = []
    for k in sh.part_keys():
        texs = {bt.tex for bt in sh.batches if bt.part == k}
        ts = [t for t in (sh.src.tex_by_ofs(*x, exact=True) for x in texs) if t is not None]
        out.append({"key": k, "label": dict(PARTS)[k], "triangles": sh.weight(k), "own_shadow": own_shadow(sh, k, tf),
                    "glow": max((t.ent[0x2D] for t in ts), default=0),
                    "outline": "none" if ts and all(t.ent[E_OUTLINE] == 1 for t in ts) else "body"})
    return out


# ------------------------------------------------------------------------------------------------------- apply
def apply(pack_bytes, shading, notes=None):
    """The pack with the shading (the spec's "shading", see normalise) put on. Parts the model does not have are
    skipped. Nothing set: the pack as it is."""
    want = normalise(shading)
    if not want:
        return pack_bytes
    notes = notes if notes is not None else []
    sh = Shaded(pack_bytes)
    src = sh.src
    have = set(sh.part_keys())
    for k in list(want):
        if k not in have:
            del want[k]
    if not want:
        return pack_bytes
    if src.clutFirst != src.texCount:
        notes.append("shading left out: this model's texture file is not laid out as the game's (materials, ramps)")
        return pack_bytes
    mats = [t for t in src.texs if src.material(t.old)]
    ramps = [t for t in src.texs if src.ramp(t.old)]
    others = [t for t in src.texs if not src.material(t.old) and not src.ramp(t.old)]
    all_b = sh.batches + [bt for _, bt in sh.mouth]

    # ---- ramps: "like" (whose main ramp) and "shadow" (a tint ramp of it)
    def resolve(p):
        seen = {p}
        while p in want and want[p]["like"] and want[p]["like"] in have and want[p]["like"] not in seen:
            p = want[p]["like"]
            seen.add(p)
        return p

    ramp_at = {t.tbpOfs: t for t in ramps}
    rb = max((t.tbpOfs + t.tbpStep for t in ramps), default=0)
    tints = {}   # (ramp ofs, rgb) -> new ramp ofs
    new_ramps = []
    full = []
    refs, tex_rgb = {}, {}

    def ramp_ref(ofs):
        """The colour of the textures drawn with a ramp (by their triangles), what its shadow darkens."""
        if ofs not in refs:
            s, wsum = [0.0, 0.0, 0.0], 0
            for bt in sh.batches:
                if bt.ramp != ofs:
                    continue
                if bt.tex not in tex_rgb:
                    tt = src.tex_by_ofs(*bt.tex)
                    tex_rgb[bt.tex] = texture_rgb(sh.parts[mix.E_TEX - 1], tt.old) if tt is not None else None
                c = tex_rgb[bt.tex]
                if c is not None:
                    for j in range(3):
                        s[j] += c[j] * bt.weight
                    wsum += bt.weight
            refs[ofs] = tuple(int(v / wsum) for v in s) if wsum else None
        return refs[ofs]

    def tinted(ofs, rgb):
        if (ofs, rgb) in tints:
            return tints[(ofs, rgb)]
        t = ramp_at.get(ofs)
        if t is None:
            return None
        dofs, n = ramp_entries(t)
        if not is_tint(t) and not any(max(t.pix[dofs + k * 4:dofs + k * 4 + 3]) for k in range(n)):
            return ofs   # (a ramp that takes nothing away: unshaded, as the eyes; it stays so)
        nonlocal rb
        if rb + 4 > mix.RAMP_BLOCKS or t.tbpStep != 4:
            same = [v for (o, c), v in tints.items() if c == rgb]   # no room: the colour's first tint ramp
            return same[0] if same else None
        e = mix.Tex(t.ent, tint_ramp(t, rgb, ramp_ref(ofs)), t.clut, t.src, t.old)
        struct.pack_into("<i", e.ent, 0x20, rb)
        tints[(ofs, rgb)] = rb
        ramp_at[rb] = e
        new_ramps.append(e)
        rb += 4
        return tints[(ofs, rgb)]

    ramp_map = {}   # id(batch) -> new ramp ofs
    for p in PART_KEYS:
        if p not in want:
            continue
        w = want[p]
        q = resolve(p) if w["like"] else p
        colour = w["shadow"] or (want.get(q, {}).get("shadow") if q != p else None)
        if q == p and not colour:
            continue
        borrowed = sh.main(q, "ramp") if q != p else None
        skipped = 0
        for bt in all_b:
            if bt.part != p:
                continue
            r = borrowed if borrowed is not None else bt.ramp
            if colour:
                nr = tinted(r, colour)
                if nr is None:
                    skipped += bt.weight
                    nr = r
                r = nr
            if r != bt.ramp:
                ramp_map[id(bt)] = r
        if skipped:
            full.append(dict(PARTS)[p])
    if full:
        notes.append(f"no room for more shading ramps (16 a fighter): {', '.join(full)} kept some of their own shadows")

    # ---- textures: glow and outline, per texture (a copy for the part where another part uses it more)
    def state(t):
        return (min(t.ent[0x2D], 0x7F), 1 if t.ent[E_OUTLINE] == 1 else 0)

    def wish(p, t):
        g, o = state(t)
        w = want.get(p)
        if not w:
            return g, o
        return (w["glow"] if w["glow"] is not None else g, (1 if w["outline"] == "none" else 0) if w["outline"] else o)

    by_tex = {}
    for bt in all_b:
        by_tex.setdefault(bt.tex, {}).setdefault(bt.part, 0)
        by_tex[bt.tex][bt.part] += bt.weight
    tb = max((t.tbpOfs + t.tbpStep for t in mats), default=0)
    cb = max((t.cbpOfs + max(t.cbpStep, 1) for t in mats if t.clut), default=0)
    slots_used = {t.ent[0x2C] for t in mats}
    new_mats, tex_map, changed, left = [], {}, {}, []
    face = src.texs[src.faceTex] if 0 <= src.faceTex < len(src.texs) else None
    for key in sorted(by_tex):
        t = src.tex_by_ofs(*key, exact=True)
        if t is None:
            continue
        users = by_tex[key]
        wishes = {p: wish(p, t) for p in users}
        if all(v == state(t) for v in wishes.values()):
            continue
        if src.noAlpha and t.old == src.noAlpha:
            notes.append("the texture the game keeps out of the glow and outline (the model's own choice) left as it is")
            continue
        top = max(sorted(users), key=lambda p: users[p])
        if wishes[top] != state(t):
            changed[id(t)] = (t, wishes[top])   # the texture takes what the part using it most wants
        for p, v in wishes.items():
            if v == wishes[top]:
                continue
            if p not in want:
                continue   # (a part that asked nothing: it goes with the texture)
            # a copy of the texture for this part
            leader = t if t.cbpStep else next((m for m in mats if m.cbpOfs == t.cbpOfs and m.cbpStep), None)
            step = leader.cbpStep if leader is not None else 1
            clut = t.clut or (leader.clut if leader is not None else b"")
            free = [s for s in range(15) if s not in slots_used]
            if (tb + t.tbpStep > mix.TEX_BLOCKS or cb + step > mix.CLUT_BLOCKS or not free or not clut
                    or (face is not None and t is face)):
                left.append(dict(PARTS)[p])
                continue
            e = mix.Tex(t.ent, t.pix, clut, t.src, t.old)
            if not t.cbpStep and leader is not None:   # (a palette it shared: the leader's size and transfer)
                e.ent[0x0C:0x10] = leader.ent[0x0C:0x10]
                e.ent[0x1C:0x20] = leader.ent[0x1C:0x20]
            struct.pack_into("<i", e.ent, 0x14, step)
            struct.pack_into("<ii", e.ent, 0x20, tb, cb)
            e.ent[0x2C] = free[0]
            e.ent[0x2D], e.ent[E_OUTLINE] = v[0], v[1]
            slots_used.add(free[0])
            new_mats.append(e)
            tex_map[(key, p)] = (tb, cb)
            tb += t.tbpStep
            cb += step
    if left:
        notes.append(f"no room for a texture of their own (glow, outline): {', '.join(sorted(set(left)))} "
                     "go with the part that uses their texture most")
    # a changed texture sharing its slot with one that stays: a free slot of its own
    for t, v in changed.values():
        t.ent = bytearray(t.ent)
        t.ent[0x2D], t.ent[E_OUTLINE] = v[0], v[1]
    for t, v in changed.values():
        mates = [m for m in mats if m is not t and m.ent[0x2C] == t.ent[0x2C] and state(m) != v]
        if mates:
            free = [s for s in range(15) if s not in slots_used]
            if free:
                t.ent[0x2C] = free[0]
                slots_used.add(free[0])
            else:
                notes.append("all 15 material slots are taken: a texture's glow / outline is shared with another one")

    # ---- the texture file and the model header
    pm = sh.pm
    nd, nr = len(new_mats), len(new_ramps)
    if nd or nr or changed:
        old_count, old_cf, old_cc = src.texCount, src.clutFirst, src.clutCount
        texs = mats + new_mats + ramps + new_ramps + others
        sh.parts[mix.E_TEX - 1] = bytearray(mix.build_texfile(texs, src.texeo))

        def shift(i):
            if i >= old_cf + old_cc:
                return i + nd + nr
            if i >= old_count:
                return i + nd
            return i
        pm[0x4F] = old_count + nd
        pm[0x50] = old_cf + nd
        pm[0x51] = old_cc + nr
        for off in (0x4C, 0x4D, 0x54):
            if pm[off]:
                pm[off] = shift(pm[off])
        if pm[0x53]:
            pm[0x52] = shift(pm[0x52])
    # ---- the chains
    for buf, batches in [(pm, sh.batches)] + [(sh.parts[n - 1], [bt]) for n, bt in sh.mouth]:
        for bt in batches:
            r = ramp_map.get(id(bt))
            if r is not None:
                hi2 = struct.unpack_from("<I", buf, bt.pos + 40)[0]
                struct.pack_into("<I", buf, bt.pos + 40, (hi2 & 0xFFF8001F) | (r << 5))
            nt = tex_map.get((bt.tex, bt.part))
            if nt is not None:
                lo, hi = struct.unpack_from("<II", buf, bt.pos + 20)
                struct.pack_into("<II", buf, bt.pos + 20, (lo & ~0x3FFF) | nt[0], (hi & 0xFFF8001F) | (nt[1] << 5))
    return mix.join([bytes(p) for p in sh.parts])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("parts")
    a.add_argument("pack")
    b = sub.add_parser("apply")
    b.add_argument("pack")
    b.add_argument("-o", "--out", required=True)
    b.add_argument("--shading", required=True, help="JSON: {part: {shadow, like, glow, outline}}")
    args = ap.parse_args(argv)
    data = pathlib.Path(args.pack).read_bytes()
    if args.cmd == "parts":
        print(json.dumps(parts(data), indent=1))
        return 0
    notes = []
    out = apply(data, json.loads(args.shading), notes)
    pathlib.Path(args.out).write_bytes(out)
    for n in notes:
        print("note:", n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
