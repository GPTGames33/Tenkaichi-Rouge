#!/usr/bin/env python3
"""Attack colours of a fighter: read and recolour the effects of its techniques, ki blasts and charge aura.

    python port/tools/creator/fxcolour.py colours  EFFECTS.bin SLOT          main colours of a slot (0..4)
    python port/tools/creator/fxcolour.py preview  EFFECTS.bin SLOT out.png  the slot's textures and colours
    python port/tools/creator/fxcolour.py recolour EFFECTS.bin SLOT out.bin '#3050ff' '#ff3020' [...]
    (SLOT may also be "kiblast" or "aura"; EFFECTS.bin = the character's effects pack, file 0x590 + c*10 + 9)

Where the colours are (decompilation refs; "verified" = read in the C and checked on the files):
- The effects pack has 6 entries. Entry slot + 1 (slot 0, 1 Blast 1; 2, 3 Blast 2; 4 Ultimate) is the slot's own
  pack (fighter object +0x9C[slot], EftShot_GetCharPack). Every technique effect type loads an EMITTER SET from it
  with EftEmit_LoadSet(.., pack, 0, 4) (eft_h.c, eft_i.c, eft_j.c, eft_k.c, eft_l_b.c): entry 4 is the header
  (EftSetHead 0x40, EftSetGroupDef 0x20 per group, EftSetDef 0x40 per part), then for each group of kind != 0 its
  nRes TEXTURE FILES (EftTexSet_Load4/8/16/32; same format as a model's texture file: PSMT8 / PSMT4 + CLUT
  upload), then for each part a definition block "a" and (kinds other than 2 and 14) a block "b". Entries 1..3
  are module extras (models, a ring texture: eft_j.c / eft_k.c). Verified: all 5 slots of every character parse
  to exactly their entry count (805 slot packs, 161 characters; texture formats PSMT8 / PSMT4 only).
  Some slots have an EMPTY set (e.g. Goku (End) slot 3, effect type 2 "shots"): their ball is EftOrbTail
  (eft_ab.c), coloured by the aura colour group from common entry 0x25A; nothing to recolour per character.
- Colour lives in two places, both inside the slot's own pack (so a change never reaches another slot):
  1. the CLUTs (palettes) of the texture files;
  2. RGBA float tracks (0..255, three keys each) at the start of block "b": kinds 5, 9, 10, 12, 15, 18 have
     color[3], colorRange[3], endColor[3], endColorRange[3] (EftPart10Def2 eft_x.h, EftLinkDef2 eft_v.h,
     EftZapKeyTbl eft_ac.h, EftArcKeys eft_s.h, eft_u.h; kind 9 EftQuadDef2 eft_y.h by the same shape, inferred);
     kind 16 color[3] (EftLineDef2 eft_y.h); kind 17 color[3] + a second Vec4[3] (EftRbnAnim eft_ab.h; the second
     track's meaning is not known). Kind 2 (rays, eft_o.h EftRaysDef) has u8 RGBA in block "a" per ray key, kind
     14 (EftAnimPart) a "V000" sprite pack in block "a" whose keys carry u8 tint colours (EftSprKey eft_ae.h).
  Kind 0 parts (light rays, EftEmit_SpawnType0) take fixed colours from code / common entry 0x24E: not per
  character. Beam textures are mostly grey/white with the hue in the float tracks; both are recoloured.
- Ki blasts and charging: entry 6 of the effects pack is the fighter's general effects pack (fighter object
  +0x58, BtlScene_GetCharPackEntry): 2 the charge effect (EftChargeMgr_Init, fighter requests 7 / 8), 3 / 4 the
  ki blasts (EftKiBlastMgr_Init: manager kind 1 entry 3, kind 2 entry 4; also EftBlastChargeMgr entry 4; ki blast
  types 2 / 3 keep a model there instead, EftKiObjMgr_Init / EftKiBombMgr_Init), 6 shock sprites (EftShockMgr),
  7 rush burst, 9 EftCharaFx, 0xB absorb glow. Entries 2..4 are emitter sets with the header at their entry 1
  (EftEmit_LoadSet(.., pack, 2, 1)): recoloured like technique slots.
- The AURA flames are not in the character's files: EftAura_GetColors (eft_n.c) takes the colours from the aura
  parameter file, common file 3 (gamedata 00002.bin) entry 1 at +0x510, EftAuraColors[9] (0xD0 bytes: color,
  alt[4], spark[4], sparkAlt[4]) by aura type (byte 3 of model pack entry 0x12), and colorB[5] at +0xC60 for
  fighters with parameter flags 0x5E. The flame / spark textures are common entries 3 / 2; the "shots" ball
  (EftOrbTail) uses common entry 0x25A by aura colour group. Per fighter, the only data-side choices are the aura
  type byte (pick the nearest of the 9 presets: nearest_aura_type) and the charge effect (entry 6 / 2,
  recolour_aura). A real per-fighter aura colour needs a port hook: EftAura_GetColors returning a per-object
  EftAuraColors (0xD0 bytes, e.g. from the character package) instead of gData->color[type].
- Not per character either: hit sparks / impacts (EftImpact, common pack), the beam struggle spark, the
  full-screen flash. A recoloured beam still explodes in the stock colours where those come from common data.
Standard library only.
"""
import colorsys, struct, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
for _p in (str(HERE), str(HERE.parent)):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import model as mdl            # noqa: E402  (decode_texfile, png)
import custom_fighter as cf    # noqa: E402  (colour_near, colour_shift, texture_cluts, hex_rgb)

EFFECT_ENTRIES = 6
GENERAL_ENTRY = 6              # effects pack entry with the general effects (ki blasts, charge)
SLOT_HEAD = 4                  # technique pack: emitter set header entry
GENERAL_HEAD = 1               # general pack sub-entries: header entry
KIBLAST_ENTRIES = (3, 4)
AURA_ENTRIES = (2,)
# Vec4 colour tracks at the start of block "b", by part kind: (count, which are "base" tracks)
B_TRACKS = {5: 12, 9: 12, 10: 12, 12: 12, 15: 12, 18: 12, 16: 3, 17: 6}
AURA_TABLE, AURA_TABLE_B = 0x510, 0xC60
AURA_ROW = 0xD0


class FxError(Exception):
    pass


# --------------------------------------------------------------------------------------------- pack layout
def _spans(data, start=0, end=None):
    """(start, end) of every part of the pack at data[start:end], absolute offsets; game entry n = index n - 1."""
    end = len(data) if end is None else end
    if end - start < 8:
        raise FxError("not a pack")
    n = struct.unpack_from("<I", data, start)[0]
    if n == 0 or n > 4096 or 4 + (n + 1) * 4 > end - start:
        raise FxError("not a pack")
    offs = struct.unpack_from(f"<{n + 1}I", data, start + 4)
    if list(offs) != sorted(offs) or offs[-1] > end - start:
        raise FxError("unexpected pack layout")
    return [(start + offs[i], start + offs[i + 1]) for i in range(n)]


def _emitter_set(data, span, head_entry):
    """Groups of the emitter set of the pack at span (EftEmit_LoadSet): [{kind, res: [span], parts: [(a, b)]}]."""
    sp = _spans(data, *span)
    if len(sp) < head_entry:
        raise FxError("no header")
    h0, h1 = sp[head_entry - 1]
    if h1 - h0 < 0x40:
        raise FxError("no header")
    ng = data[h0 + 4]
    if h0 + 0x40 + ng * 0x20 > h1:
        raise FxError("bad header")
    nxt = head_entry          # index into sp of the next entry
    groups = []
    for g in range(ng):
        kind, nparts, nres = data[h0 + 0x40 + g * 0x20:h0 + 0x43 + g * 0x20]
        grp = {"kind": kind, "res": [], "parts": []}
        groups.append(grp)
        if kind == 0:
            continue
        for _ in range(nres):
            grp["res"].append(sp[nxt])
            nxt += 1
        for _ in range(nparts):
            a = sp[nxt]
            nxt += 1
            b = None
            if kind not in (2, 14):
                b = sp[nxt]
                nxt += 1
            grp["parts"].append((a, b))
        if nxt > len(sp):
            raise FxError("emitter set runs past its pack")
    if nxt > len(sp):
        raise FxError("emitter set runs past its pack")
    return {"spans": sp, "head": head_entry, "groups": groups, "used": nxt}


# ------------------------------------------------------------------------------------------------- sites
# A site is one colour in the data: ("f32", offset, weight) = 3 floats; ("u8", offset, weight) = 3 bytes;
# ("clut", offset, n, texfile span, texture index) = n RGBA palette entries.
def _is_texfile(data, s, e):
    if e - s < 0x60:
        return False
    count, ent = struct.unpack_from("<II", data, s)
    return 0 < count <= 64 and ent == 8 and 0x20 + count * 0x40 <= e - s


def _tex_sites(data, s, e, label):
    out = []
    t = data[s:e]
    for index, off, n in cf.texture_cluts(t):
        out.append({"type": "clut", "at": s + off, "n": n, "tex": (s, e, index), "label": label})
    return out


def _floats_ok(vals):
    return all(v == v and -1.0 <= v <= 1024.0 for v in vals)


def _set_sites(data, es, label):
    sites = []
    for gi, grp in enumerate(es["groups"]):
        kind = grp["kind"]
        for ri, (s, e) in enumerate(grp["res"]):
            if _is_texfile(data, s, e):
                sites += _tex_sites(data, s, e, f"{label} group {gi} (kind {kind}) texture set {ri}")
        for pi, (a, b) in enumerate(grp["parts"]):
            where = f"{label} group {gi} (kind {kind}) part {pi}"
            if b is not None and kind in B_TRACKS:
                n = B_TRACKS[kind]
                if b[1] - b[0] >= n * 16:
                    vals = struct.unpack_from(f"<{n * 4}f", data, b[0])
                    if _floats_ok(vals):
                        for j in range(n):
                            base = (j // 3) in (0, 2) if n == 12 else True
                            sites.append({"type": "f32", "at": b[0] + j * 16, "weight": 1.0 if base else 0.0,
                                          "label": where, "track": j})
            elif kind == 2:
                s0, e0 = a
                if e0 - s0 >= 0x4B8:
                    cnt = struct.unpack_from("<i", data, s0 + 0x4B0)[0]
                    if 0 < cnt <= 10:
                        for r in range(cnt):
                            for k in range(3):
                                sites.append({"type": "u8", "at": s0 + r * 0x78 + k * 0x10 + 0xC, "weight": 1.0,
                                              "label": where})
            elif kind == 14:
                sites += _v000_sites(data, a, where)
    return sites


def _v000_sites(data, span, where):
    s, e = span
    if e - s < 0x40 or data[s:s + 4] != b"V000":
        return []
    nl = data[s + 4]
    keys = struct.unpack_from("<i", data, s + 8)[0]
    out = []
    k = 0
    for i in range(nl):
        lo = s + 0x20 + i * 0x20
        if lo + 0x20 > e:
            break
        for _ in range(data[lo + 4]):
            ko = s + keys + k * 0x40 + 0x30
            k += 1
            if s < ko and ko + 4 <= e:
                out.append({"type": "u8", "at": ko, "weight": 0.5, "label": where + " sprite key"})
    return out


def _extra_sites(data, span, label, depth=0):
    """Texture files in a module extra entry: a texture file itself, a model pack (252 entries, textures in entry
    0xC: the model of a prop shot / ki blast object), or a small pack of those (eft_k.c ring model + texture)."""
    s, e = span
    if e <= s:
        return []
    if _is_texfile(data, s, e):
        return _tex_sites(data, s, e, label + " texture")
    if depth > 1:
        return []
    try:
        sp = _spans(data, s, e)
    except (FxError, struct.error):
        return []
    if len(sp) == mdl.PACK_COUNT:
        t = sp[mdl.E_TEX - 1]
        return _tex_sites(data, t[0], t[1], label + " model texture") if _is_texfile(data, *t) else []
    if len(sp) > 16:
        return []
    out = []
    for i, p in enumerate(sp):
        out += _extra_sites(data, p, f"{label}.{i + 1}", depth + 1)
    return out


def _sub_sites(data, span, head_entry, label):
    """Sites of an emitter-set pack (header at entry head_entry), plus the texture files of the entries the set
    does not use (module extras)."""
    out = []
    try:
        es = _emitter_set(data, span, head_entry)
    except (FxError, struct.error, IndexError):
        es = None
    try:
        sp = _spans(data, *span)
    except FxError:
        return out
    used = set()
    if es is not None:
        out += _set_sites(data, es, label)
        used = set(range(head_entry - 1, es["used"]))
    for i, p in enumerate(sp):
        if i not in used:
            out += _extra_sites(data, p, f"{label} entry {i + 1}")
    return out


def _eff_spans(eff):
    sp = _spans(eff)
    if len(sp) != EFFECT_ENTRIES:
        raise FxError("not an effects pack (expected 6 entries, found %d)" % len(sp))
    return sp


def slot_sites(eff, slot):
    if not 0 <= slot <= 4:
        raise FxError("slot is 0..4")
    sp = _eff_spans(eff)
    return _sub_sites(eff, sp[slot], SLOT_HEAD, f"slot {slot}")


def general_sites(eff, entries):
    sp = _eff_spans(eff)
    g = _spans(eff, *sp[GENERAL_ENTRY - 1])
    out = []
    for n in entries:
        if n <= len(g) and g[n - 1][1] > g[n - 1][0]:
            out += _sub_sites(eff, g[n - 1], GENERAL_HEAD, f"general entry {n}")
    return out


# ------------------------------------------------------------------------------------------------ colours
def _hsv(rgb):
    return colorsys.rgb_to_hsv(*(min(255, max(0, v)) / 255 for v in rgb))


def fx_near(rgb, ref, rng=1.0):
    """Whether rgb is a shade of ref for effects: greys (and white cores) by brightness as colour_near does,
    colours by hue alone (an effect's gradient runs from dark to light shades of one hue)."""
    h, s, v = _hsv(rgb)
    H, S, V = _hsv(ref)
    if S < 0.15 or s < 0.12:
        return cf.colour_near(bytes(int(min(255, max(0, x))) for x in rgb), bytes(ref), rng)
    if v < 0.03:
        return False
    dh = min(abs(h - H), 1 - abs(h - H))
    return dh < 0.07 * rng and abs(s - S) < 0.75 * rng


def _read(data, site):
    t = site["type"]
    if t == "f32":
        return list(struct.unpack_from("<4f", data, site["at"]))
    if t == "u8":
        return list(data[site["at"]:site["at"] + 4])
    raise ValueError(t)


def _samples(data, sites):
    """(rgb, weight, where) of the visible colours: palette textures by their pixels (alpha x brightness), params
    by their base tracks."""
    out = []
    seen = set()
    for st in sites:
        if st["type"] == "clut":
            s, e, idx = st["tex"]
            if (s, idx) in seen:
                continue
            seen.add((s, idx))
            px = _decoded(data, s, e).get(idx)
            if not px:
                continue
            n = len(px) // 4
            step = max(1, n // 2048)
            for k in range(0, n, step):
                r, g, b, a = px[k * 4:k * 4 + 4]
                w = a / 255 * max(r, g, b) / 255
                if w > 0.05:
                    out.append(((r, g, b), w, "texture palette"))
        elif st["weight"] > 0:
            r, g, b, a = _read(data, st)
            if max(r, g, b) >= 8:
                out.append(((r, g, b), st["weight"], "param"))
    return out


_dec_cache = {}


def _decoded(data, s, e):
    key = hash(bytes(data[s:e]))
    if key not in _dec_cache:
        res = {}
        try:
            for t in mdl.decode_texfile(bytes(data[s:e])):
                if t.get("rgba"):
                    res[t["index"]] = t
        except (mdl.ModelError, struct.error, IndexError):
            pass
        if len(_dec_cache) > 256:
            _dec_cache.clear()
        _dec_cache[key] = res
    return {i: t["rgba"] for i, t in _dec_cache[key].items()}


def _group(samples, limit=8):
    out = []
    for where in ("param", "texture palette"):
        groups = []   # [ref, weight, sum r, g, b]
        for rgb, w, wh in samples:
            if wh != where:
                continue
            for g in groups:
                if fx_near(rgb, g[0]):
                    g[1] += w
                    g[2] += rgb[0] * w; g[3] += rgb[1] * w; g[4] += rgb[2] * w
                    break
            else:
                groups.append([tuple(min(255, max(0, round(x))) for x in rgb), w, rgb[0] * w, rgb[1] * w, rgb[2] * w])
        total = sum(g[1] for g in groups) or 1
        groups.sort(key=lambda g: -g[1])
        for i, g in enumerate(groups[:limit]):
            if g[1] / total < 0.02:
                continue
            col = tuple(min(255, max(0, round(g[k] / g[1]))) for k in (2, 3, 4))
            out.append({"key": f"{'param' if where == 'param' else 'tex'}{i + 1}", "rgb": "#%02x%02x%02x" % col,
                        "where": where, "share": round(g[1] / total, 3)})
    return out


def _parse_changes(changes):
    out = []
    for c in changes:
        out.append((cf.hex_rgb(c["from"]), cf.hex_rgb(c["to"]), float(c.get("range", 1.5))))
    return out


def _shift(rgb, ref, to):
    """colour_shift for values that may exceed 255 (kept in proportion)."""
    top = max(255.0, *rgb)
    scale = top / 255.0
    base = bytes(int(round(min(255, max(0, v / scale)))) for v in rgb)
    new = cf.colour_shift(base, bytes(ref), bytes(to))
    return [v * scale for v in new]


def _apply(data, sites, changes):
    d = bytearray(data)
    ch = _parse_changes(changes)
    done = set()
    for st in sites:
        if st["at"] in done:
            continue
        done.add(st["at"])
        if st["type"] == "clut":
            for k in range(st["n"]):
                a = st["at"] + k * 4
                rgb = tuple(d[a:a + 3])
                for ref, to, rng in ch:
                    if fx_near(rgb, ref, rng):
                        d[a:a + 3] = bytes(cf.colour_shift(bytes(rgb), bytes(ref), bytes(to)))
                        break
            continue
        v = _read(d, st)
        rgb = v[:3]
        for ref, to, rng in ch:
            if max(rgb) > 0.5 and fx_near(rgb, ref, rng):
                new = _shift(rgb, ref, to)
                if st["type"] == "f32":
                    struct.pack_into("<3f", d, st["at"], *[float(round(x, 3)) for x in new])
                else:
                    d[st["at"]:st["at"] + 3] = bytes(int(round(min(255, x))) for x in new)
                break
    return bytes(d)


# ------------------------------------------------------------------------------------------------- API
def attack_colours(eff_pack_bytes, slot):
    """Main colours of technique slot `slot` (0, 1 Blast 1; 2, 3 Blast 2; 4 Ultimate):
    [{"key", "rgb": "#rrggbb", "where": "param" / "texture palette", "share"}] (share within its `where`)."""
    return _group(_samples(eff_pack_bytes, slot_sites(eff_pack_bytes, slot)))


def recolour_attack(eff_pack_bytes, slot, changes):
    """The effects pack with slot `slot` recoloured: changes = [{"from": "#rrggbb", "to": "#rrggbb", "range": 1.0}].
    Only palette entries and colour tracks inside the slot's own entry change; sizes and offsets stay."""
    return _apply(eff_pack_bytes, slot_sites(eff_pack_bytes, slot), changes)


TINT_GREY = 0.12   # a main colour less saturated than this is a grey / the white core: kept by a tint


def dominant(cols):
    """The colour a tint is about (the panel's sliders start on it): the most used coloured one, the colour tracks
    before the palettes; None when the move has none (a grey or white effect)."""
    for where in ("param", "texture palette"):
        for c in cols:
            if c["where"] == where and _hsv(cf.hex_rgb(c["rgb"]))[1] >= TINT_GREY:
                return c["rgb"]
    return None


def tint_changes(cols, hue, strength, light):
    """One colour for a whole move (the Technique Colour panel): the changes (as recolour_attack takes them) that
    give its dominant colour hue `hue` (degrees), saturation strength / 100 and brightness light / 100, and its
    other colours the same hue with their saturation and brightness kept in proportion to it (the shading of a
    beam's gradient stays). Greys and the white cores stay, unless the move has no colour at all: then its greys
    take the hue."""
    dom = dominant(cols)
    if dom is None:
        grey = [c for c in cols if max(cf.hex_rgb(c["rgb"])) > 24]
        ref = max((_hsv(cf.hex_rgb(c["rgb"]))[2] for c in grey), default=1.0) or 1.0
        out = []
        for c in grey:
            v = _hsv(cf.hex_rgb(c["rgb"]))[2]
            out.append({"from": c["rgb"], "to": _hex(hue / 360, strength / 100, light / 100 * v / ref), "range": 1.5})
        return out
    _, S, V = _hsv(cf.hex_rgb(dom))
    out = []
    for c in cols:
        h, s, v = _hsv(cf.hex_rgb(c["rgb"]))
        if s < TINT_GREY:
            continue
        out.append({"from": c["rgb"], "to": _hex(hue / 360, strength / 100 * s / max(S, 0.05),
                                                   light / 100 * v / max(V, 0.02)), "range": 1.5})
    return out


def _hex(h, s, v):
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, min(1.0, max(0.0, s)), min(1.0, max(0.0, v)))
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))


def tint_of(rgb):
    """(hue 0..360, strength 0..100, light 0..100) of a colour."""
    h, s, v = _hsv(cf.hex_rgb(rgb))
    return h * 360, s * 100, v * 100


def match_changes(eff_a, slot_a, eff_b, slot_b):
    """Slot slot_a of effects pack eff_a in the colours of slot slot_b of eff_b: its dominant colour becomes the
    other's (the rest follows, tint_changes). [] when either has no colour."""
    dom = dominant(attack_colours(eff_b, slot_b))
    if dom is None:
        return []
    return tint_changes(attack_colours(eff_a, slot_a), *tint_of(dom))


def kiblast_colours(eff_pack_bytes):
    return _group(_samples(eff_pack_bytes, general_sites(eff_pack_bytes, KIBLAST_ENTRIES)))


def recolour_kiblast(eff_pack_bytes, changes):
    """Ki blasts (general entries 3 and 4 of effects pack entry 6; also the Blast-charge glow, which reads entry 4)."""
    return _apply(eff_pack_bytes, general_sites(eff_pack_bytes, KIBLAST_ENTRIES), changes)


def aura_colours(eff_pack_bytes, common3_bytes=None, aura_type=None):
    """The fighter's own charge effect (general entry 2), and with common file 3 and the aura type the preset aura
    colours it uses (where "aura table": shared with every fighter of that type)."""
    out = _group(_samples(eff_pack_bytes, general_sites(eff_pack_bytes, AURA_ENTRIES)))
    if common3_bytes is not None and aura_type is not None:
        for name, rgba in aura_table(common3_bytes)[aura_type].items():
            if max(rgba[:3]) >= 8:
                out.append({"key": f"aura_{name}", "rgb": "#%02x%02x%02x" % tuple(min(255, round(x)) for x in rgba[:3]),
                            "where": "aura table", "share": 0.0})
    return out


def recolour_aura(eff_pack_bytes, changes):
    """Recolours the fighter's charge effect (general entry 2). The flames themselves are global, see the module
    notes (nearest_aura_type / recolour_aura_table)."""
    return _apply(eff_pack_bytes, general_sites(eff_pack_bytes, AURA_ENTRIES), changes)


def _aura_file(common3):
    sp = _spans(common3)
    s, e = sp[0]
    if e - s < AURA_TABLE_B + 5 * AURA_ROW:
        raise FxError("not common file 3")
    return s


AURA_FIELDS = ["color"] + [f"alt{i}" for i in range(4)] + [f"spark{i}" for i in range(4)] + [f"sparkAlt{i}" for i in range(4)]


def aura_table(common3_bytes, b=False):
    """The aura colour presets of common file 3 (gamedata/pzs3us1/00002.bin): [{field: (r, g, b, a)}] by type."""
    s = _aura_file(common3_bytes)
    base, n = (AURA_TABLE_B, 5) if b else (AURA_TABLE, 9)
    out = []
    for t in range(n):
        v = struct.unpack_from("<52f", common3_bytes, s + base + t * AURA_ROW)
        out.append({AURA_FIELDS[k]: v[k * 4:k * 4 + 4] for k in range(13)})
    return out


def nearest_aura_type(rgb):
    """The stock aura type (0..7) whose flame colour (alt0) is nearest to rgb ("#rrggbb"), from the table of the
    USA disc; to write into byte 3 of model pack entry 0x12. Type 8 (multicolour) is left out."""
    table = {0: (16, 24, 180), 1: (49, 33, 180), 2: (178, 142, 26), 3: (147, 38, 63), 4: (16, 172, 36),
             5: (44, 65, 105), 6: (138, 18, 94), 7: (43, 28, 193)}
    h, s, v = _hsv(cf.hex_rgb(rgb))

    def dist(c):
        H, S, V = _hsv(c)
        dh = min(abs(h - H), 1 - abs(h - H))
        return dh * 4 + abs(s - S) + abs(v - V) * 0.5
    return min(table, key=lambda t: dist(table[t]))


def recolour_aura_table(common3_bytes, aura_type, changes, b=False):
    """GLOBAL: recolours preset `aura_type` in common file 3 (every fighter with that aura type changes)."""
    s = _aura_file(common3_bytes)
    base = AURA_TABLE_B if b else AURA_TABLE
    sites = [{"type": "f32", "at": s + base + aura_type * AURA_ROW + k * 16, "weight": 1.0} for k in range(13)]
    return _apply(common3_bytes, sites, changes)


# ------------------------------------------------------------------------------------------------ preview
def _sheet(data, sites, tile=96, cols=6):
    texs = []
    seen = set()
    for st in sites:
        if st["type"] != "clut":
            continue
        s, e, idx = st["tex"]
        if (s, idx) in seen:
            continue
        seen.add((s, idx))
        dec = _decoded(data, s, e)
        if idx in dec:
            t = [x for x in mdl.decode_texfile(bytes(data[s:e])) if x["index"] == idx][0]
            texs.append((t["width"], t["height"], dec[idx]))
    params = [_read(data, st)[:3] for st in sites if st["type"] in ("f32", "u8") and st["weight"] > 0]
    sw, pcols = 16, cols * tile // 16
    rows = -(-len(texs) // cols) if texs else 0
    prow = -(-len(params) // pcols) if params else 0
    W, H = cols * tile, max(1, rows * tile + (prow * sw + 4 if params else 0))
    img = bytearray(W * H * 4)
    for i in range(W * H):  # dark checker: additive effects show as on screen
        x, y = i % W, i // W
        c = 24 if ((x // 8) + (y // 8)) & 1 else 40
        img[i * 4:i * 4 + 4] = bytes((c, c, c, 255))
    for n, (w, h, px) in enumerate(texs):
        ox, oy = (n % cols) * tile, (n // cols) * tile
        sc = (tile - 4) / max(w, h)
        for y in range(tile - 4):
            sy = int(y / sc)
            if sy >= h:
                break
            for x in range(tile - 4):
                sx = int(x / sc)
                if sx >= w:
                    break
                r, g, b, a = px[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
                o = ((oy + 2 + y) * W + ox + 2 + x) * 4
                bg = img[o]
                img[o:o + 3] = bytes(min(255, bg + (v * a) // 255) for v in (r, g, b))
    py = rows * tile + 4
    for n, rgb in enumerate(params):
        ox, oy = (n % pcols) * sw, py + (n // pcols) * sw
        col = bytes(int(min(255, max(0, v))) for v in rgb) + b"\xff"
        for y in range(1, sw - 1):
            for x in range(1, sw - 1):
                o = ((oy + y) * W + ox + x) * 4
                img[o:o + 4] = col
    return mdl.png(W, H, bytes(img))


def attack_preview_png(eff_pack_bytes, slot, tile=96):
    """PNG contact sheet of slot `slot`: its textures (with their palettes, alpha over a dark checker), then a
    strip of the colour tracks (base keys) of its parts."""
    return _sheet(eff_pack_bytes, slot_sites(eff_pack_bytes, slot), tile)


def kiblast_preview_png(eff_pack_bytes, tile=96):
    return _sheet(eff_pack_bytes, general_sites(eff_pack_bytes, KIBLAST_ENTRIES), tile)


def aura_preview_png(eff_pack_bytes, tile=96):
    return _sheet(eff_pack_bytes, general_sites(eff_pack_bytes, AURA_ENTRIES), tile)


# ---------------------------------------------------------------------------------------------------- CLI
def _which(data, slot):
    if slot == "kiblast":
        return lambda: kiblast_colours(data), lambda ch: recolour_kiblast(data, ch), lambda: kiblast_preview_png(data)
    if slot == "aura":
        return lambda: aura_colours(data), lambda ch: recolour_aura(data, ch), lambda: aura_preview_png(data)
    s = int(slot)
    return (lambda: attack_colours(data, s), lambda ch: recolour_attack(data, s, ch),
            lambda: attack_preview_png(data, s))


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) < 3:
        sys.exit(__doc__)
    cmd, path, slot = argv[:3]
    data = pathlib.Path(path).read_bytes()
    colours, recolour, preview = _which(data, slot)
    if cmd == "colours":
        for c in colours():
            print(f"{c['key']:8} {c['rgb']}  {c['where']:16} {c['share']:.3f}")
    elif cmd == "preview":
        pathlib.Path(argv[3]).write_bytes(preview())
    elif cmd == "recolour":
        pairs = argv[4:]
        ch = [{"from": pairs[i], "to": pairs[i + 1]} for i in range(0, len(pairs) - 1, 2)]
        pathlib.Path(argv[3]).write_bytes(recolour(ch))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
