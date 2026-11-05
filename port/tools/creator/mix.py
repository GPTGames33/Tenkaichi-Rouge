#!/usr/bin/env python3
"""Per-piece mixing of fighter models (the creator's "Parts" tab). Standard library only.

    python port/tools/creator/mix.py pieces <pack.bin>
    python port/tools/creator/mix.py mix <base.bin> -o <out.bin> --piece hair=<donor.bin> [--piece torso=<donor2.bin>]
                                     [--render front.png] [--check]

    import mix; mix.pieces(pack_bytes); new_pack = mix.mix(base_pack, [(donor_pack, "hair"), ...])

What the game reads, and so what a mix has to keep consistent (from the decompilation):

* The model (entry 3, "pmdl") is ONE list of records (header +0x6C, linked by `next`, `last` on the final one) that is
  at the same time the skeleton (BtlObjPose_CalcMatrices: depth-first order, `pop` at +4 = matrix-stack levels closed
  after the node, `rest` at +0x30 = translation from the parent, animated for node ids < 71), the bounds list
  (BtlObj_UpdateBounds: centre +0x40 relative to `origin` +0x10, extent +0x50, +0x5C read by gameplay as a size
  factor), the part list (BtlObjMdl_AllocParts: one pool part of 512 per record; node table by id, so ids must be
  unique) and the mesh list (ObjDraw_DrawParts: `enabled` != 0 -> VIF chain at +0x60, drawn by VU1 program 0 with
  p = mix(B (pos - ofsB), A (pos - ofsA), w), ofsA = +0x10, ofsB = +0x20, A / B the node's and its parent's matrix).
  Header +8 is the record count. Each chain ends with a 0x70000000 word, which MdlTex_RebaseChain scans for.
* MdlTex_RebaseChain (bind time): main chains' TEX0 become (old - lowest) + 0x3480 / 0x3C00, so the lowest TBP and
  CBP used by the main chains must be 0; mouth chains (entries 4..0xB) are rebased without the "lowest" (old + base).
  The second TEX0 (shading ramp): CBP = old + 0x3D00, i.e. old = block offset of a ramp CLUT texture.
* Texture file (entry 0xC): textures [texFirst, texFirst + texCount) are re-uploaded every frame at 0x3480 + tbpOfs
  (CLUTs at 0x3C00 + cbpOfs), ramp CLUTs [clutFirst, clutFirst + clutCount) at 0x3D00 + tbpOfs. GS budget for one
  fighter: pixels 0x3480..0x3C00 (0x780 blocks), CLUTs 0x3C00..0x3C40 (the 64x64 frame ObjDraw_AddClutPass tints;
  0x3D00 is the hard end), ramps 0x3D00..0x3D40 (0x3D40 is the light's ramp texture).
* Secondary motion (entry 0x17): 16 type-A and 8 type-B slots, matched by node id (>= 0x47) against the records.
* Body volumes (entry 2) and attack nodes use core node ids 0..0x36 (present in every model); accessories 0x40..0x43,
  0x6F, 0x76 are switched by gameplay flags (BtlObj_IsNodeShown, BtlObj_GetNodeByCode) and stay the base's.
* Face: node 0x30 swaps its chain for mouth shapes (entries 4..0xB), the face texture (header +0x4C) is replaced by
  eye frames (entry 0xD) and mouth textures (entries 0xE..0x11); a jaw face sets header flags 0xC00000.

Mixing rules (the safe subset):
* Base records are never removed or moved: the skeleton, rest translations, origins, pops and gameplay values stay the
  base's. A swapped core node gets the donor's mesh: the vertices are moved by w * (baseA - donorA) + (1 - w) *
  (baseB - donorB) so they hang on the base's joints. A base extra (hair lock, sash, cape...) of a swapped region
  keeps its record without a mesh (enabled = 0) and leaves the chain slots.
* The donor's extras of the region are added as new records under the base's node they hang on, moved by that
  node's joint offset; ids are kept when free (>= 0x47), else renumbered into 0x5F..0x6E (unused by every model on
  the disc and not special in the code). Their chain slots are copied into free slots of the base's entry 0x17.
* Donor textures are appended to the base's material textures, ramp CLUTs merged (identical ones shared), every TEX0
  in every chain (and the seam section) repointed; the GS budget is checked and the mix refused if it does not fit.
"""
import argparse, pathlib, struct, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import model as mdlx  # noqa: E402

PACK_COUNT = 252
E_BODY, E_MODEL, E_TEX, E_CHAIN = 2, 3, 0xC, 0x17
E_MOUTH_A = list(range(4, 0xC))      # node 0x30 chains per mouth shape
E_EYES = 0xD
E_MOUTH_B = list(range(0xE, 0x12))   # face textures for mouth shapes 5..8
VIF_TEX, VIF_END = 0x6C058000, 0x70000000

TEX_BLOCKS = 0x3C00 - 0x3480
CLUT_BLOCKS = 0x40                   # what the CLUT tint pass covers (hard limit 0x100)
RAMP_BLOCKS = 0x3D40 - 0x3D00
MAX_RECORDS = 80                     # the disc's largest model has 69; parts come from a pool of 512
FREE_IDS = list(range(0x5F, 0x6F))   # node ids no model on the disc uses

ACCESSORY = {0x40, 0x41, 0x42, 0x43, 0x6F, 0x76}
CORE = set(range(0x37))
REGIONS = {
    "head": {0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33},
    "torso": {0x03, 0x10, 0x11, 0x12, 0x20},      # 0x12 / 0x20 carry the two halves of the chest
    "tail": {0x04, 0x05, 0x06, 0x07},
    "arms": {0x13, 0x14, 0x21, 0x22},
    "hands": set(range(0x15, 0x20)) | set(range(0x23, 0x2E)),
    "legs": {0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E},
    "feet": {0x0B, 0x0F},
}
FIXED = {0x00, 0x01, 0x02, 0x34, 0x35, 0x36}
PIECES = ("hair", "head", "torso", "tail", "arms", "hands", "legs", "feet")
PIECE_HELP = {
    "hair": "hair locks / antennae: the extra nodes on the head (the scalp and hairline are part of the head mesh)",
    "head": "whole head: neck, head, face (with mouth shapes, eye frames, face texture) and everything on it",
    "torso": "pelvis, belt, chest and shoulders (0x12 / 0x20), and what hangs on them (belt ends, capes, back items)",
    "tail": "tail nodes 4..7 (only some characters have a tail mesh)",
    "arms": "upper arms and forearms",
    "hands": "hands and fingers (gloves)",
    "legs": "thighs and shins",
    "feet": "feet (boots)",
}
CORE_REGION = {n: r for r, s in REGIONS.items() for n in s}


class MixError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------- pack
def split(data):
    if len(data) < 8 or struct.unpack_from("<I", data)[0] != PACK_COUNT:
        raise MixError("not a character model pack (252 entries)")
    parts = mdlx.split_pack(data)
    if len(parts) != PACK_COUNT or parts[E_MODEL - 1][:4] != b"pmdl":
        raise MixError("entry 3 is not a pmdl model")
    return [bytes(p) for p in parts]


def join(parts):
    """custom_fighter.join_pack: u32 count, count + 1 offsets, header 1024 bytes, 64-byte aligned parts."""
    parts = [p + b"\0" * (-len(p) % 64) for p in parts]
    head_size = -(-(4 + (len(parts) + 1) * 4) // 64) * 64
    pos, offs = head_size, []
    for p in parts:
        offs.append(pos)
        pos += len(p)
    offs.append(pos)
    head = struct.pack(f"<I{len(parts) + 1}I", len(parts), *offs)
    return head + b"\0" * (head_size - len(head)) + b"".join(parts)


def entry(parts, n):
    return parts[n - 1]


# --------------------------------------------------------------------------------------------------- model
class Rec:
    __slots__ = ("node", "pop", "last", "enabled", "hdr", "chain", "parent", "children", "src", "index")

    def __init__(self, raw, chain):
        self.hdr = bytearray(raw)            # 0x40 or 0x60 bytes
        self.chain = bytes(chain)            # bytes from +0x60 to the record's end (with the end word)
        self.pop, self.last, self.enabled, self.node = struct.unpack_from("<HHHH", raw, 4)
        self.parent = None
        self.children = []
        self.src = None
        self.index = -1

    @property
    def mesh(self):
        return self.enabled != 0 and len(self.chain) > 0

    def vec(self, off):
        return struct.unpack_from("<3f", self.hdr, off)

    @property
    def ofsA(self):
        return self.vec(0x10)

    @property
    def ofsB(self):
        return self.vec(0x20)


class Model:
    def __init__(self, e):
        if len(e) < 0x90 or e[:4] != b"pmdl":
            raise MixError("not a pmdl model")
        self.raw = e
        self.meshOfs = struct.unpack_from("<I", e, 0x6C)[0]
        self.head = bytearray(e[:self.meshOfs])
        self.recs = []
        o = self.meshOfs
        for _ in range(4096):
            nxt, pop, last, enabled = struct.unpack_from("<iHHH", e, o)
            if last:
                ln = 0x40
            else:
                ln = nxt
            if ln < 0x40 or ln % 16 or o + ln > len(e):
                raise MixError("bad record at 0x%X" % o)
            if ln > 0x40 and ln < 0x60:
                raise MixError("record of 0x%X bytes at 0x%X" % (ln, o))
            hdr = e[o:o + min(ln, 0x60)]
            chain = e[o + 0x60:o + ln] if ln > 0x60 else b""
            if enabled and chain:
                end = struct.unpack_from("<I", chain, len(chain) - 16)[0]
                if end != VIF_END:
                    raise MixError("chain of node 0x%X does not end with 0x70000000" % hdr[0xA])
            r = Rec(hdr, chain)
            r.index = len(self.recs)
            self.recs.append(r)
            if last:
                self.end = o + 0x40
                break
            o += ln
        else:
            raise MixError("record list does not end")
        self.tail = e[self.end:]
        self.seamOfs = struct.unpack_from("<i", e, 0x70)[0]
        if self.seamOfs and self.seamOfs < self.end:
            raise MixError("seam section in front of the records' end")
        # hierarchy from the pops (BtlObjPose_CalcMatrices)
        stack, cur = [], None
        for r in self.recs:
            stack.append(cur)
            r.parent = cur
            if cur is not None:
                cur.children.append(r)
            cur = r
            for _ in range(r.pop):
                cur = stack.pop() if stack else None
        self.by_node = {}
        for r in self.recs:
            if r.node in self.by_node:
                raise MixError("node 0x%X twice in the model" % r.node)
            self.by_node[r.node] = r
        self.last_pop = self.recs[-1].pop

    def attach_core(self, r):
        """The nearest core node (0..0x36) above an extra node."""
        p = r.parent
        while p is not None and p.node not in CORE:
            p = p.parent
        return p

    def region(self, r):
        if r.node in ACCESSORY:
            return "accessories"
        if r.node in FIXED:
            return "root"
        if r.node in CORE:
            return CORE_REGION.get(r.node, "root")
        p = r.parent
        while p is not None and p.node not in CORE:
            if p.node in ACCESSORY:
                return "accessories"
            p = p.parent
        if p is None:
            return "root"
        reg = CORE_REGION.get(p.node, "root")
        return "hair" if reg == "head" else reg

    def seams(self):
        if not self.seamOfs:
            return None
        vc, tc, vo, to = struct.unpack_from("<4i", self.raw, self.seamOfs)
        return vc, tc, vo, to


def depth(r):
    d = 0
    while r.parent is not None:
        r = r.parent
        d += 1
    return d


# ---------------------------------------------------------------------------------------------- VIF chains
def walk_chain(buf, start=0, end=None, on_tex=None, on_vertex=None):
    """Walks a VIF chain (one DMA ret tag, its data, the end qword) in a bytearray. on_tex(pos) gets the byte offset
    of the 0x6C058000 code of every header block; on_vertex(pos, w) that of every position quadword. Checks the
    layout program 0 expects and returns the number of batches."""
    end = len(buf) if end is None else end
    p = start
    batches = 0
    while p + 16 <= end:
        t0 = struct.unpack_from("<I", buf, p)[0]
        qwc, tid = t0 & 0xFFFF, (t0 >> 28) & 7
        if tid == 7 and t0 == VIF_END:
            break
        wstart = p + 8
        wend = p + 16 + qwc * 16
        if wend > end:
            raise MixError("chain longer than its record")
        i = wstart
        while i < wend:
            c = struct.unpack_from("<I", buf, i)[0]
            cmd, num, imm = (c >> 24) & 0x7F, ((c >> 16) & 0xFF) or 256, c & 0xFFFF
            i += 4
            if cmd >= 0x60:
                if cmd & 0x6F != 0x6C:
                    raise MixError("unexpected UNPACK 0x%X in a model chain" % cmd)
                addr = imm & 0x3FF
                if c == VIF_TEX:
                    if on_tex:
                        on_tex(i - 4)
                elif addr == 5 and num % 3 == 0:
                    if on_vertex:
                        for k in range(0, num, 3):
                            q = i + k * 16
                            on_vertex(q, struct.unpack_from("<f", buf, q + 12)[0])
                else:
                    raise MixError("unexpected UNPACK at 0x%X (addr %d, %d qwords)" % (i - 4, addr, num))
                i += num * 16
            elif cmd == 0x17:
                batches += 1
            elif cmd == 0:
                pass
            else:
                raise MixError("unexpected VIF code 0x%08X in a model chain" % c)
        p = wend
        if tid in (6, 7):
            break
    return batches


def chain_texrefs(chain):
    """(tbp, cbp, ramp_cbp) of every batch of a chain."""
    out = []
    buf = bytearray(chain)

    def tex(pos):
        lo, hi = struct.unpack_from("<II", buf, pos + 20)
        lo2, hi2 = struct.unpack_from("<II", buf, pos + 36)
        out.append((lo & 0x3FFF, (hi >> 5) & 0x3FFF, (hi2 >> 5) & 0x3FFF))
    walk_chain(buf, on_tex=tex)
    return out


def rewrite_chain(chain, texmap, rampmap, dA=None, dB=None):
    """A copy of a chain with TEX0 repointed (texmap[(tbp, cbp)] -> (tbp, cbp), rampmap[cbp] -> cbp) and the
    vertices moved by w * dA + (1 - w) * dB."""
    buf = bytearray(chain)

    def tex(pos):
        lo, hi = struct.unpack_from("<II", buf, pos + 20)
        hi2 = struct.unpack_from("<I", buf, pos + 40)[0]
        key = (lo & 0x3FFF, (hi >> 5) & 0x3FFF)
        if key not in texmap:
            raise MixError("chain uses a texture that is not in its texture file (TBP %d CBP %d)" % key)
        tbp, cbp = texmap[key]
        struct.pack_into("<II", buf, pos + 20, (lo & ~0x3FFF) | tbp, (hi & 0xFFF8001F) | (cbp << 5))
        r = (hi2 >> 5) & 0x3FFF
        if r not in rampmap:
            raise MixError("chain uses a ramp CLUT that is not in its texture file (%d)" % r)
        struct.pack_into("<I", buf, pos + 40, (hi2 & 0xFFF8001F) | (rampmap[r] << 5))

    def vert(pos, w):
        if dA is None:
            return
        x, y, z = struct.unpack_from("<3f", buf, pos)
        struct.pack_into("<3f", buf, pos, x + w * dA[0] + (1 - w) * dB[0], y + w * dA[1] + (1 - w) * dB[1],
                         z + w * dA[2] + (1 - w) * dB[2])
    walk_chain(buf, on_tex=tex, on_vertex=vert)
    return bytes(buf)


def empty_chain():
    """A DMA ret of nothing and the end word: what a record without a mesh carries when it is longer than 0x40."""
    return struct.pack("<4I", 0x60000000, 0, 0, 0) + struct.pack("<4I", VIF_END, 0, 0, 0)


# ------------------------------------------------------------------------------------------------ textures
class Tex:
    __slots__ = ("ent", "pix", "clut", "src", "old")

    def __init__(self, ent, pix, clut, src, old):
        self.ent, self.pix, self.clut, self.src, self.old = bytearray(ent), pix, clut, src, old

    def f(self, off):
        return struct.unpack_from("<i", self.ent, off)[0]

    tbpOfs = property(lambda s: s.f(0x20))
    cbpOfs = property(lambda s: s.f(0x24))
    tbpStep = property(lambda s: s.f(0x10))
    cbpStep = property(lambda s: s.f(0x14))


def parse_texfile(t, src):
    count, eo = struct.unpack_from("<II", t, 0)
    if count == 0 or count > 255 or eo * 4 + count * 0x40 > len(t):
        raise MixError("bad texture file")
    out = []
    for i in range(count):
        e = t[eo * 4 + i * 0x40:eo * 4 + i * 0x40 + 0x40]
        pix, clut, ps, cs = struct.unpack_from("<IIii", e, 0)
        out.append(Tex(e, t[pix * 4:pix * 4 + ps] if pix else b"", t[clut * 4:clut * 4 + cs] if clut else b"", src, i))
    return out, t[8:0x10], eo


def build_texfile(texs, eo=8):
    """Header (count, entry table in words, total pixel blocks, total CLUT blocks), entries, then every texture's
    pixel and CLUT packets in order (the layout of the files on the disc)."""
    count = len(texs)
    pos = eo * 4 + count * 0x40
    body = bytearray()
    ents = bytearray()
    for t in texs:
        e = bytearray(t.ent)
        if t.pix:
            struct.pack_into("<I", e, 0, (pos + len(body)) // 4)
            body += t.pix
            body += b"\0" * (-len(body) % 16)
        else:
            struct.pack_into("<I", e, 0, 0)
        if t.clut:
            struct.pack_into("<I", e, 4, (pos + len(body)) // 4)
            body += t.clut
            body += b"\0" * (-len(body) % 16)
        else:
            struct.pack_into("<I", e, 4, 0)
        struct.pack_into("<II", e, 0x38, 0, 0)
        ents += e
    tsum = sum(t.tbpStep for t in texs)
    csum = sum(t.cbpStep for t in texs)
    head = struct.pack("<IIII", count, eo, tsum, csum) + b"\0" * (eo * 4 - 16)
    return bytes(head + ents + body)


# ------------------------------------------------------------------------------------------------- source
class Source:
    """One pack (base or donor), parsed."""

    def __init__(self, data, name):
        self.name = name
        self.parts = split(data)
        self.model = Model(entry(self.parts, E_MODEL))
        h = self.model.head
        self.texFirst, self.texCount, self.clutFirst, self.clutCount = h[0x4E], h[0x4F], h[0x50], h[0x51]
        self.faceTex, self.fadeTex, self.fadeFirst, self.fadeCount, self.noAlpha = h[0x4C], h[0x4D], h[0x52], h[0x53], h[0x54]
        self.flags = struct.unpack_from("<I", h, 0xC)[0]
        self.texs, self.texhead, self.texeo = parse_texfile(entry(self.parts, E_TEX), self)
        if self.texFirst != 0 or self.texFirst + self.texCount > len(self.texs):
            raise MixError(f"{name}: unexpected texture ranges")
        ch = entry(self.parts, E_CHAIN)
        self.chain = bytearray(ch) if len(ch) >= 0x350 else None

    def material(self, i):
        return self.texFirst <= i < self.texFirst + self.texCount

    def ramp(self, i):
        return self.clutFirst <= i < self.clutFirst + self.clutCount

    def tex_by_ofs(self, tbp, cbp):
        for t in self.texs:
            if self.material(t.old) and t.tbpOfs == tbp and t.cbpOfs == cbp:
                return t
        return None

    def ramp_by_ofs(self, ofs):
        for t in self.texs:
            if self.ramp(t.old) and t.tbpOfs == ofs:
                return t
        return None


# -------------------------------------------------------------------------------------------------- pieces
def pieces(pack_bytes):
    """The model's records grouped into pieces. A list of dicts: name, about, nodes (with a mesh), hidden_nodes
    (records without a mesh), textures (indices), triangles, mixable."""
    s = Source(pack_bytes, "pack")
    m = s.model
    groups = {}
    for r in m.recs:
        reg = m.region(r)
        g = groups.setdefault(reg, {"nodes": [], "hidden_nodes": [], "textures": set(), "triangles": 0})
        if r.mesh:
            g["nodes"].append(r.node)
            for strip in mdlx.parse_chain(r.chain, 0, len(r.chain)):
                g["triangles"] += max(0, len(strip["verts"]) - 2)
                t = s.tex_by_ofs(strip["tbp"], strip["cbp"])
                if t is not None:
                    g["textures"].add(t.old)
        else:
            g["hidden_nodes"].append(r.node)
    out = []
    order = list(PIECES) + ["accessories", "root"]
    for name in order:
        g = groups.get(name, {"nodes": [], "hidden_nodes": [], "textures": set(), "triangles": 0})
        if name == "head":   # the whole head includes the hair
            h = groups.get("hair")
            if h:
                g = {"nodes": g["nodes"] + h["nodes"], "hidden_nodes": g["hidden_nodes"] + h["hidden_nodes"],
                     "textures": g["textures"] | h["textures"], "triangles": g["triangles"] + h["triangles"]}
        about = PIECE_HELP.get(name, {"accessories": "sword, sheath, halo and the like: switched by gameplay flags, "
                                                     "kept from the base", "root": "skeleton root, never mixed"}.get(name))
        out.append({"name": name, "about": about, "nodes": g["nodes"], "hidden_nodes": g["hidden_nodes"],
                    "textures": sorted(g["textures"]), "triangles": g["triangles"],
                    "mixable": name in PIECES and bool(g["nodes"])})
    return out


# ----------------------------------------------------------------------------------------------------- mix
def mix(base_pack, selections, report=None):
    """A new pack: the base with the chosen pieces from donors. selections: [(donor_pack, piece_name), ...]; later
    selections win where they overlap ("head" then "hair" = one donor's head with another's hair). report (a list)
    gets notes. Raises MixError when the result would not be safe (GS budget, ids, slots)."""
    notes = report if report is not None else []
    base = Source(base_pack, "base")
    donors = {}
    core_src = {}          # core node -> Source
    extra_src = {}         # region of extras -> Source
    for k, (pack, piece) in enumerate(selections):
        if piece not in PIECES:
            raise MixError(f"unknown piece {piece!r} (one of {', '.join(PIECES)})")
        d = donors.get(id(pack))
        if d is None:
            d = donors[id(pack)] = Source(pack, f"donor {len(donors) + 1}")
        if piece == "hair":
            extra_src["hair"] = d
        else:
            for n in REGIONS[piece]:
                core_src[n] = d
            extra_src["hair" if piece == "head" else piece] = d
    head_src = core_src.get(0x30)

    bm = base.model
    # ---- base records
    plan = []
    for r in bm.recs:
        reg = bm.region(r)
        src = None
        if r.node in CORE and r.node not in FIXED:
            src = core_src.get(r.node)
        elif reg not in ("accessories", "root") and r.node not in CORE:
            src = extra_src.get(reg)
        plan.append((r, src))

    # ---- textures used
    uses = {}              # Source -> set of texture indices (material) needed
    ramp_uses = {}

    def need(src, chain):
        for tbp, cbp, rc in chain_texrefs(chain):
            t = src.tex_by_ofs(tbp, cbp)
            if t is None:
                raise MixError(f"{src.name}: a chain names a texture the file does not have (TBP {tbp})")
            uses.setdefault(src, set()).add(t.old)
            rt = src.ramp_by_ofs(rc)
            if rt is None:
                raise MixError(f"{src.name}: a chain names a ramp CLUT the file does not have ({rc})")
            ramp_uses.setdefault(src, set()).add(rt.old)

    for r, src in plan:
        if src is None and r.mesh:
            need(base, r.chain)
        elif src is not None and r.node in CORE:
            dr = src.model.by_node.get(r.node)
            if dr is not None and dr.mesh:
                need(src, dr.chain)
    # donor extras to add
    additions = []         # (donor Source, donor root Rec, base attach Rec)
    for reg, src in extra_src.items():
        if src is base:
            continue
        dm = src.model
        for dr in dm.recs:
            if dr.node in CORE or dm.region(dr) != reg:
                continue
            if dr.parent is not None and dr.parent.node in CORE:   # root of an extra subtree
                attach = bm.by_node.get(dr.parent.node)
                if attach is None:
                    raise MixError(f"base has no node 0x{dr.parent.node:X}")
                additions.append((src, dr, attach))

    def subtree(r):
        out = [r]
        for c in r.children:
            if c.node not in ACCESSORY:
                out += subtree(c)
        return out

    for src, dr, _ in additions:
        for x in subtree(dr):
            if x.mesh:
                need(src, x.chain)
    if head_src is not None:
        for n in E_MOUTH_A:
            c = entry(head_src.parts, n)
            if c:
                need(head_src, c)
        uses.setdefault(head_src, set()).add(head_src.faceTex)
    else:
        for n in E_MOUTH_A:
            c = entry(base.parts, n)
            if c:
                need(base, c)
    seam = bm.seams()
    if seam:
        vc, tc, vo, to = seam
        raw = bm.raw[to + 0x10:to + 0x20]
        tb, ts = struct.unpack("<QQ", raw)
        t = base.tex_by_ofs(tb & 0x3FFF, (tb >> 37) & 0x3FFF)
        if t is not None:
            uses.setdefault(base, set()).add(t.old)
        rt = base.ramp_by_ofs((ts >> 37) & 0x3FFF)
        if rt is not None:
            ramp_uses.setdefault(base, set()).add(rt.old)

    # ---- the new texture file
    keep_base = [t for t in base.texs if base.material(t.old)]
    mats = list(keep_base)
    for src in donors.values():
        for i in sorted(uses.get(src, ())):
            mats.append(src.texs[i])

    def fits(ms):
        return sum(t.tbpStep for t in ms) <= TEX_BLOCKS and sum(t.cbpStep for t in ms) <= CLUT_BLOCKS

    if not fits(mats):   # drop the base's textures no remaining chain uses
        used = uses.get(base, set()) | {base.faceTex}
        if base.noAlpha:
            used.add(base.noAlpha)
        dropped = [t.old for t in keep_base if t.old not in used]
        mats = [t for t in mats if t.src is not base or t.old in used]
        notes.append(f"dropped unused base textures {dropped} to fit the GS budget")
    tsum, csum = sum(t.tbpStep for t in mats), sum(t.cbpStep for t in mats)
    if tsum > TEX_BLOCKS:
        raise MixError(f"textures need {tsum} GS blocks, a fighter has {TEX_BLOCKS} (0x3480..0x3C00)")
    if csum > CLUT_BLOCKS:
        raise MixError(f"palettes need {csum} GS blocks, the fighter palette area has {CLUT_BLOCKS} (0x3C00..0x3C40)")
    # the lowest TBP / CBP of the main chains must be 0 (MdlTex_RebaseChain subtracts it)
    main_used = set()
    for r, src in plan:
        if src is None and r.mesh:
            main_used |= {(base, base.tex_by_ofs(a, b).old) for a, b, _ in chain_texrefs(r.chain)}
        elif src is not None and r.node in CORE:
            dr = src.model.by_node.get(r.node)
            if dr is not None and dr.mesh:
                main_used |= {(src, src.tex_by_ofs(a, b).old) for a, b, _ in chain_texrefs(dr.chain)}
    for src, dr, _ in additions:
        for x in subtree(dr):
            if x.mesh:
                main_used |= {(src, src.tex_by_ofs(a, b).old) for a, b, _ in chain_texrefs(x.chain)}
    if not ((mats[0].src, mats[0].old) in main_used and mats[0].clut):
        first = next((t for t in mats if (t.src, t.old) in main_used and t.clut), None)
        if first is None:
            raise MixError("no main chain uses a texture with a palette")
        mats.remove(first)
        mats.insert(0, first)
    # slots (byte 0x2C of an entry): the per-material id of the alpha table, 0..14
    slots_used = {t.ent[0x2C] for t in mats if t.src is base}
    slot_map = {}
    shared = 0
    for t in mats:
        if t.src is base:
            continue
        t.ent = bytearray(t.ent)
        key = (id(t.src), t.ent[0x2C])
        if key not in slot_map:
            s = t.ent[0x2C]
            if s in slots_used or s > 14:
                free = [k for k in range(15) if k not in slots_used]
                if free:
                    s = free[0]
                else:   # share a base slot with the same base alpha (what the slot carries besides its id)
                    same = [b.ent[0x2C] for b in mats if b.src is base and b.ent[0x2D] == t.ent[0x2D]]
                    s = same[len(slot_map) % len(same)] if same else 0
                    shared += 1
            slots_used.add(s)
            slot_map[key] = s
        t.ent[0x2C] = slot_map[key]
    if shared:
        notes.append(f"{shared} donor material slot(s) share a base slot (same alpha): all 15 are taken")
    # ramps
    ramps = [t for t in base.texs if base.ramp(t.old)]
    ramp_new = {}
    for t in ramps:
        ramp_new[(base, t.old)] = t
    for src in donors.values():
        for i in sorted(ramp_uses.get(src, ())):
            t = src.texs[i]
            same = next((b for b in ramps if b.pix == t.pix and b.tbpStep == t.tbpStep), None)
            if same is None:
                ramps.append(t)
                same = t
            ramp_new[(src, i)] = same
    if sum(t.tbpStep for t in ramps) > RAMP_BLOCKS:   # keep only the ramps something still uses
        required = {id(ramp_new[(src, i)]) for src, ix in ramp_uses.items() for i in ix}
        dropped = [t.old for t in ramps if t.src is base and id(t) not in required]
        ramps = [t for t in ramps if id(t) in required]
        notes.append(f"dropped unused base shading ramps {dropped}")
    if any(t.tbpStep != 4 for t in ramps):
        raise MixError("a shading ramp CLUT is not 4 blocks (BtlObjLight_FindRes1 assumes 4)")
    others = [t for t in base.texs if not base.material(t.old) and not base.ramp(t.old)]
    # positions
    out_texs = []
    pos_of = {}
    tb = cb = 0
    for t in mats:
        e = Tex(t.ent, t.pix, t.clut, t.src, t.old)
        struct.pack_into("<ii", e.ent, 0x20, tb, cb)
        pos_of[(t.src, t.old)] = (tb, cb, len(out_texs))
        tb += t.tbpStep
        cb += t.cbpStep
        out_texs.append(e)
    clut_first = len(out_texs)
    rb = 0
    ramp_pos = {}
    for t in ramps:
        e = Tex(t.ent, t.pix, t.clut, t.src, t.old)
        struct.pack_into("<i", e.ent, 0x20, rb)
        ramp_pos[id(t)] = rb
        rb += t.tbpStep
        out_texs.append(e)
    if rb > RAMP_BLOCKS:
        raise MixError(f"shading ramps need {rb} GS blocks, there are {RAMP_BLOCKS} (0x3D00..0x3D40)")
    other_first = len(out_texs)
    for t in others:
        out_texs.append(Tex(t.ent, t.pix, t.clut, t.src, t.old))

    def maps(src):
        tm, rm = {}, {}
        for t in src.texs:
            if src.material(t.old) and (src, t.old) in pos_of:
                a, b, _ = pos_of[(src, t.old)]
                tm[(t.tbpOfs, t.cbpOfs)] = (a, b)
            if src.ramp(t.old) and (src, t.old) in ramp_new:
                if id(ramp_new[(src, t.old)]) in ramp_pos:
                    rm[t.tbpOfs] = ramp_pos[id(ramp_new[(src, t.old)])]
        return tm, rm

    mapcache = {}

    def remap(src, chain, dA=None, dB=None):
        if src not in mapcache:
            mapcache[src] = maps(src)
        tm, rm = mapcache[src]
        return rewrite_chain(chain, tm, rm, dA, dB)

    def new_index(src, old):
        if src.material(old):
            v = pos_of.get((src, old))
            return v[2] if v else None
        if src is base and base.ramp(old):
            t = ramp_new.get((base, old))
            return clut_first + ramps.index(t) if t in ramps else None
        if src is base:
            k = [t.old for t in others].index(old) if old in [t.old for t in others] else None
            return None if k is None else other_first + k
        return None

    # ---- records
    def sub(a, b):
        return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def add3(hdr, off, d):
        x, y, z = struct.unpack_from("<3f", hdr, off)
        struct.pack_into("<3f", hdr, off, x + d[0], y + d[1], z + d[2])

    # part flag bit 0 = drawn by the fade pass (program 1); only meaningful when the base model fades
    fade_mask = 0xFFFF if base.flags & 1 else 0xFFFE
    hidden = set()
    used_ids = {r.node for r in bm.recs}
    # ids >= 0x47 are generic (chains find their slot by id); 0x6F and 0x76 are switched by gameplay flags
    free_ids = [i for i in FREE_IDS if i not in used_ids] +                [i for i in range(0x47, 0x76) if i not in used_ids and i not in FREE_IDS and i not in ACCESSORY]
    out = []               # list of Rec (new objects), base order, with donor subtrees inserted
    clone = {}
    for r, src in plan:
        n = Rec(r.hdr, r.chain)
        n.src = base
        clone[r] = n
        if src is None:
            if r.mesh:
                n.chain = remap(base, r.chain)
        elif r.node in CORE:
            dr = src.model.by_node.get(r.node)
            if r.node in ADOPT and dr is not None and adopt_ok(bm, src.model, r.node):
                # nodes that bring their own bones: models without a tail have nodes 4..7 sitting on the hips (the
                # donor's tail hung on those would fold into the pelvis); the jaw / head nodes 0x31..0x33 sit at
                # different places inside different heads. Moved by the anchor's (3 / 0x2F) joint offset.
                anchor = ADOPT[r.node]
                d = sub(bm.by_node[anchor].ofsA, src.model.by_node[anchor].ofsA)
                hdr = bytearray(dr.hdr[:0x40]) + (bytearray(dr.hdr[0x40:0x60]) if dr.mesh else bytearray(0x20))
                hdr[0:8] = r.hdr[0:8]
                add3(hdr, 0x10, d)
                add3(hdr, 0x20, d)
                if dr.mesh:
                    add3(hdr, 0x40, d)
                if len(r.hdr) >= 0x60:
                    hdr[0x5C:0x60] = r.hdr[0x5C:0x60]
                struct.pack_into("<H", hdr, 0xC, struct.unpack_from("<H", dr.hdr, 0xC)[0] & fade_mask)
                struct.pack_into("<H", hdr, 8, dr.enabled if dr.mesh else 0)
                n.hdr = hdr
                n.enabled = dr.enabled if dr.mesh else 0
                if dr.mesh:
                    n.chain = remap(src, dr.chain, d, d)
                else:
                    n.chain = empty_chain()
                    hidden.add(r.node)
                if r.node == 4:
                    notes.append("tail: nodes 4..7 take the donor's bone lengths")
                if r.node == 0x31:
                    notes.append("head: nodes 0x31..0x33 take the donor's bone offsets")
            elif dr is not None and dr.mesh:
                dA, dB = sub(r.ofsA, dr.ofsA), sub(r.ofsB, dr.ofsB)
                hdr = bytearray(r.hdr[:0x40]) + bytearray(dr.hdr[0x40:0x60])
                add3(hdr, 0x40, dA)
                if len(r.hdr) >= 0x60:
                    hdr[0x5C:0x60] = r.hdr[0x5C:0x60]        # gameplay reads +0x5C: keep the base's
                struct.pack_into("<H", hdr, 8, dr.enabled)
                struct.pack_into("<H", hdr, 0xC, struct.unpack_from("<H", dr.hdr, 0xC)[0] & fade_mask)
                n.hdr = hdr
                n.enabled = dr.enabled
                n.chain = remap(src, dr.chain, dA, dB)
            else:
                hide(n, r)
                hidden.add(r.node)
        else:
            hide(n, r)
            hidden.add(r.node)
        out.append(n)
    for r in bm.recs:   # hierarchy of the clones
        n = clone[r]
        n.parent = clone[r.parent] if r.parent is not None else None
        n.children = [clone[c] for c in r.children]

    # hidden records below a node that took the donor's bone: their origins follow the parent (origin = parent
    # origin + rest, ofsB = parent origin, as in every model on the disc); nothing of them is drawn
    def follow(n):
        for c in n.children:
            if not c.enabled and c.node in hidden:
                po = struct.unpack_from("<3f", n.hdr, 0x10)
                rest = struct.unpack_from("<3f", c.hdr, 0x30)
                struct.pack_into("<3f", c.hdr, 0x10, po[0] + rest[0], po[1] + rest[1], po[2] + rest[2])
                struct.pack_into("<3f", c.hdr, 0x20, *po)
            follow(c)
    for r in bm.recs:
        if r.parent is None:
            follow(clone[r])

    # donor extras
    renum = {}             # (Source, old id) -> new id
    for src, dr, attach in additions:
        parent_clone = clone[attach]
        d = sub(struct.unpack_from("<3f", parent_clone.hdr, 0x10), src.model.by_node[dr.parent.node].ofsA)

        def copy(x, par):
            nid = x.node
            if nid < 0x47 or nid in used_ids or nid in ACCESSORY:
                if not free_ids:
                    raise MixError("no free node ids left for the donor's extra parts")
                nid = free_ids.pop(0)
            used_ids.add(nid)
            renum[(src, x.node)] = nid
            n = Rec(x.hdr, x.chain)
            n.src = src
            n.node = nid
            n.hdr = bytearray(x.hdr)
            struct.pack_into("<H", n.hdr, 0xA, nid)
            add3(n.hdr, 0x10, d)
            add3(n.hdr, 0x20, d)
            if len(n.hdr) >= 0x60:
                add3(n.hdr, 0x40, d)
            struct.pack_into("<H", n.hdr, 0xC, struct.unpack_from("<H", n.hdr, 0xC)[0] & fade_mask)
            if x.mesh:
                n.chain = remap(src, x.chain, d, d)
            n.parent = par
            par.children.append(n)
            for c in x.children:
                if c.node not in ACCESSORY:
                    copy(c, n)
        copy(dr, parent_clone)

    # serialize depth first, pops from the depths
    order = []

    def dfs(n):
        order.append(n)
        for c in n.children:
            dfs(c)
    roots = [clone[r] for r in bm.recs if r.parent is None]
    for rt in roots:
        dfs(rt)
    if len(order) > MAX_RECORDS:
        raise MixError(f"{len(order)} records: more than {MAX_RECORDS}")
    if order[-1] is not clone[bm.recs[-1]]:
        raise MixError("internal: the base's last record moved")
    depths = {id(n): depth(n) for n in order}
    body = bytearray()
    for k, n in enumerate(order):
        if k + 1 < len(order):
            nxt = order[k + 1]
            pop = depths[id(n)] - (depths[id(nxt.parent)] if nxt.parent is not None else -1)
            last = 0
        else:
            pop, last = bm.last_pop, 1
        hdr = bytearray(n.hdr)
        if last:
            hdr = hdr[:0x40]
            chain = b""
        else:
            chain = n.chain
            if len(hdr) < 0x60 and chain:
                raise MixError("internal: chain on a short record")
        ln = len(hdr) + len(chain) if not last else 0x40
        struct.pack_into("<iHH", hdr, 0, ln if not last else struct.unpack_from("<i", n.hdr, 0)[0], pop, last)
        body += hdr + chain
    # header
    head = bytearray(bm.head)
    struct.pack_into("<I", head, 8, len(order))
    face_new = new_index(head_src, head_src.faceTex) if head_src is not None else new_index(base, base.faceTex)
    if face_new is None:
        raise MixError("the face texture did not make it into the texture file")
    head[0x4C] = face_new
    head[0x4E] = 0
    head[0x4F] = len(mats)
    head[0x50] = clut_first
    head[0x51] = len(ramps)
    if base.fadeCount:
        head[0x52] = new_index(base, base.fadeFirst)
    if base.fadeTex:
        v = new_index(base, base.fadeTex)
        head[0x4D] = v if v is not None else 0
    if base.noAlpha:
        v = new_index(base, base.noAlpha)
        head[0x54] = v if v is not None else 0
    if head_src is not None:
        fl = struct.unpack_from("<I", head, 0xC)[0]
        fl = (fl & ~0xC00000) | (head_src.flags & 0xC00000)
        struct.pack_into("<I", head, 0xC, fl)
    new_end = len(head) + len(body)
    tail = bytearray(bm.tail)
    if bm.seamOfs:
        delta = new_end - bm.end
        so = bm.seamOfs - bm.end
        struct.pack_into("<i", head, 0x70, bm.seamOfs + delta)
        vc, tc, vo, to = struct.unpack_from("<4i", tail, so)
        struct.pack_into("<4i", tail, so, vc, tc, vo + delta, to + delta)
        # seam triangles: their TEX0 pairs (tex0Base relative to the model's blocks, tex0Shade to the ramps)
        tm, rm = maps(base)
        for i in range(tc):
            b = to - bm.end + i * 0x20 + 0x10
            t0, t1 = struct.unpack_from("<QQ", tail, b)
            key = (t0 & 0x3FFF, (t0 >> 37) & 0x3FFF)
            if key in tm:
                a, c = tm[key]
                t0 = (t0 & ~0x3FFF & 0xFFF8001FFFFFFFFF) | a | (c << 37)
            r = (t1 >> 37) & 0x3FFF
            if r in rm:
                t1 = (t1 & 0xFFF8001FFFFFFFFF) | (rm[r] << 37)
            struct.pack_into("<QQ", tail, b, t0, t1)
    pmdl = bytes(head + body + tail)

    # ---- the other entries
    parts = list(base.parts)
    parts[E_MODEL - 1] = pmdl
    parts[E_TEX - 1] = build_texfile(out_texs, base.texeo)
    if head_src is not None:
        hd = head_src
        r30 = bm.by_node[0x30]
        d30 = hd.model.by_node.get(0x30)
        dA = sub(r30.ofsA, d30.ofsA) if d30 else (0, 0, 0)
        dB = sub(r30.ofsB, d30.ofsB) if d30 else (0, 0, 0)
        for n in E_MOUTH_A:
            c = entry(hd.parts, n)
            parts[n - 1] = remap(hd, c, dA, dB) if c else b""
        for n in [E_EYES] + E_MOUTH_B:
            parts[n - 1] = entry(hd.parts, n)
        notes.append("face: mouth shapes, eye frames and mouth textures from the head's donor")
    else:
        for n in E_MOUTH_A:
            c = entry(base.parts, n)
            if c:
                parts[n - 1] = remap(base, c)
    parts[E_CHAIN - 1] = merge_chains(base, order, renum, hidden, notes)
    return join(parts)


TAIL = {4, 5, 6, 7}
ADOPT = {4: 3, 5: 3, 6: 3, 7: 3, 0x31: 0x2F, 0x32: 0x2F, 0x33: 0x2F}   # node -> anchor


def adopt_ok(bm, dm, node):
    """Whether `node` can take the donor's bone: in both models it hangs where the base's does (tail: the chain
    3 -> 4 -> 5 -> 6 -> 7; head nodes: on 0x2F) and only nodes of the same group or extras that move with it (no
    core node, no gameplay accessory) hang below it in the base."""
    group = TAIL if node in TAIL else {0x31, 0x32, 0x33}
    for n in group:   # all of the group or none
        for m in (bm, dm):
            r = m.by_node.get(n)
            if r is None or r.parent is None:
                return False
            want = (3 if n == 4 else n - 1) if n in TAIL else 0x2F
            if r.parent.node != want:
                return False
        stack = list(bm.by_node[n].children)
        while stack:
            c = stack.pop()
            if (c.node in CORE and c.node not in group) or c.node in ACCESSORY:
                return False
            stack += c.children
    return True


def hide(n, r):
    """A record that keeps its place in the skeleton but draws nothing."""
    hdr = bytearray(r.hdr[:0x40]) + (bytearray(r.hdr[0x40:0x60]) if len(r.hdr) >= 0x60 else bytearray(0x20))
    struct.pack_into("<H", hdr, 8, 0)
    n.hdr = hdr
    n.enabled = 0
    n.chain = empty_chain()


# chain parameter block layout (BObjChainParam): (offset, element size) of every per-slot array
CHAIN_A = [(0x000, 1), (0x010, 4), (0x050, 4), (0x090, 4), (0x0D0, 1), (0x0E0, 2), (0x100, 1), (0x110, 4),
           (0x150, 4), (0x190, 1), (0x1A0, 2), (0x1C0, 1), (0x1D0, 4), (0x210, 4), (0x250, 1), (0x260, 4),
           (0x2A0, 4), (0x2E0, 1)]
CHAIN_B = [(0x2F0, 1), (0x2F8, 1), (0x300, 4), (0x320, 4), (0x340, 1), (0x348, 1)]


def merge_chains(base, order, renum, hidden, notes):
    """Entry 0x17 for the mix: the base's slots minus the nodes the mix hid, plus the donors' slots of the added
    nodes (in free slots; a slot is free when its id is 0 or names no record). BObjChainA_Build takes the FIRST
    slot with a node's id, so no other slot may keep an added node's id."""
    present = {n.node for n in order}
    if base.chain is None and not renum:
        return entry(base.parts, E_CHAIN)
    blk = bytearray(base.chain) if base.chain is not None else bytearray(0x380)
    for layout, count in ((CHAIN_A, 16), (CHAIN_B, 8)):
        nofs = layout[0][0]
        for i in range(count):
            if blk[nofs + i] in hidden:
                blk[nofs + i] = 0
    added = set(renum.values())
    for (src, old), new in renum.items():
        if src.chain is None:
            continue
        for layout, count in ((CHAIN_A, 16), (CHAIN_B, 8)):
            nofs = layout[0][0]
            slots = [i for i in range(count) if src.chain[nofs + i] == old]
            if not slots:
                continue
            i = slots[0]
            stale = [j for j in range(count) if blk[nofs + j] == new]
            free = stale + [j for j in range(count) if j not in stale and
                            (blk[nofs + j] == 0 or (blk[nofs + j] not in present and blk[nofs + j] not in added))]
            if not free:
                notes.append(f"no free secondary-motion slot for node 0x{new:X}: it stays still")
                continue
            j = free[0]
            for k in stale[1:]:
                blk[nofs + k] = 0
            for off, size in layout:
                blk[off + j * size:off + (j + 1) * size] = src.chain[off + i * size:off + (i + 1) * size]
            blk[nofs + j] = new
    return bytes(blk)


# ---------------------------------------------------------------------------------------------- checking
def check(pack_bytes):
    """Walks what BtlObjMdl_Create / the pose, bounds, chain, draw and bind passes walk; returns a list of problems
    (empty = consistent)."""
    bad = []
    try:
        s = Source(pack_bytes, "mix")
    except MixError as e:
        return [str(e)]
    m = s.model
    e = m.raw
    if struct.unpack_from("<I", e, 8)[0] != len(m.recs):
        bad.append("header +8 is not the record count")
    if len(m.recs) > MAX_RECORDS:
        bad.append("too many records")
    o = m.meshOfs
    for r in m.recs:
        if o % 16:
            bad.append("record not 16-byte aligned")
        o += struct.unpack_from("<i", e, o)[0] if not r.last else 0x40
    ids = [r.node for r in m.recs]
    if len(set(ids)) != len(ids) or max(ids) >= 0x80:
        bad.append("node ids not unique or out of range")
    # pose walk: the matrix stack never runs dry before the end
    stack = 0
    for k, r in enumerate(m.recs):
        stack += 1
        if r.pop > stack and k + 1 < len(m.recs):
            bad.append(f"pose walk pops past the root at node 0x{r.node:X}")
        stack = max(0, stack - r.pop)
    # skeleton: every node's origin is its parent's origin plus its rest translation, ofsB is the parent's origin
    # (true for all 698 models on the disc)
    for r in m.recs:
        if r.parent is None:
            continue
        rest, po = r.vec(0x30), r.parent.ofsA
        if max(abs(r.ofsA[k] - po[k] - rest[k]) for k in range(3)) > 1e-3 or                 max(abs(r.ofsB[k] - po[k]) for k in range(3)) > 1e-3:
            bad.append(f"node 0x{r.node:X}: origin / rest / parent origin disagree")
    # MdlTex_RebaseChain's word scan: from +0x60 to the 0x70000000 word, inside the record
    o = m.meshOfs
    for r in m.recs:
        ln = struct.unpack_from("<i", e, o)[0] if not r.last else 0x40
        if r.enabled:
            p, hits = o + 0x60, 0
            while p < o + ln and struct.unpack_from("<I", e, p)[0] != VIF_END:
                hits += struct.unpack_from("<I", e, p)[0] == VIF_TEX
                p += 4
            if p >= o + ln:
                bad.append(f"node 0x{r.node:X}: no end word inside the record")
            elif hits != walk_chain(bytearray(r.chain)):
                bad.append(f"node 0x{r.node:X}: texture blocks and batches disagree")
        o += ln
    for n in CORE:
        if n not in m.by_node:
            bad.append(f"core node 0x{n:X} missing")
    # body volumes
    b = entry(s.parts, E_BODY)
    if len(b) >= 0x10 + 0x1D0:
        p = 0x10
        while p + 0x1D0 <= len(b):
            fl, node = struct.unpack_from("<Ii", b, p)
            if node not in m.by_node:
                bad.append(f"body volume on missing node 0x{node:X}")
            if fl & 1:
                break
            p += 0x1D0
    # textures: every TEX0 names a material texture of the right shape; main chains' lowest TBP / CBP is 0
    mins = [1 << 30, 1 << 30]
    for r in m.recs:
        if not r.mesh:
            continue
        try:
            refs = chain_texrefs(r.chain)
        except MixError as x:
            bad.append(f"node 0x{r.node:X}: {x}")
            continue
        for tbp, cbp, rc in refs:
            mins[0], mins[1] = min(mins[0], tbp), min(mins[1], cbp)
            if s.tex_by_ofs(tbp, cbp) is None:
                bad.append(f"node 0x{r.node:X}: TEX0 names no texture (TBP {tbp} CBP {cbp})")
            if s.ramp_by_ofs(rc) is None:
                bad.append(f"node 0x{r.node:X}: ramp CLUT {rc} missing")
    if mins != [0, 0]:
        bad.append(f"lowest TBP / CBP of the main chains is {mins}, must be 0")
    tsum = sum(t.tbpStep for t in s.texs if s.material(t.old))
    csum = sum(t.cbpStep for t in s.texs if s.material(t.old))
    rsum = max([t.tbpOfs + t.tbpStep for t in s.texs if s.ramp(t.old)] or [0])
    if tsum > TEX_BLOCKS or csum > CLUT_BLOCKS or rsum > RAMP_BLOCKS:
        bad.append(f"GS budget: {tsum} pixel, {csum} CLUT, {rsum} ramp blocks")
    if not s.material(s.faceTex):
        bad.append("face texture outside the material range")
    for n in E_MOUTH_A:
        c = entry(s.parts, n)
        if c:
            for tbp, cbp, rc in chain_texrefs(c):
                if s.tex_by_ofs(tbp, cbp) is None:
                    bad.append(f"mouth chain {n}: TEX0 names no texture")
    eyes = entry(s.parts, E_EYES)
    if eyes:
        try:
            et, _, _ = parse_texfile(eyes, s)
            ft = s.texs[s.faceTex]
            for t in et:
                if t.tbpStep > ft.tbpStep or struct.unpack_from("<Q", t.ent, 0x30)[0] != struct.unpack_from("<Q", ft.ent, 0x30)[0]:
                    bad.append("eye frame not the shape of the face texture")
        except MixError as x:
            bad.append(f"eyes: {x}")
    # chains: links per type within the slots
    if s.chain is not None:
        for nofs, count in ((0, 16), (0x2F0, 8)):
            slots = {s.chain[nofs + i] for i in range(count) if s.chain[nofs + i]}
            links = sum(1 for r in m.recs if r.node >= 0x47 and r.node in slots)
            ids = [s.chain[nofs + i] for i in range(count) if s.chain[nofs + i]]
            if len(ids) != len(set(ids)):
                bad.append("a node id in two chain slots")
            if links > count:
                bad.append("too many chain links")
    # seam vertices hang on existing nodes
    if m.seamOfs:
        vc, tc, vo, to = struct.unpack_from("<4i", e, m.seamOfs)
        for i in range(vc):
            node = struct.unpack_from("<i", e, vo + i * 0x70 + 0x1C)[0]
            if node not in m.by_node:
                bad.append(f"seam vertex on missing node 0x{node:X}")
                break
    return bad


# ---------------------------------------------------------------------------------------------------- CLI
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sp = ap.add_subparsers(dest="cmd", required=True)
    a1 = sp.add_parser("pieces")
    a1.add_argument("pack")
    a2 = sp.add_parser("mix")
    a2.add_argument("base")
    a2.add_argument("-o", "--out", required=True)
    a2.add_argument("--piece", action="append", default=[], help="name=donor_pack.bin (repeatable)")
    a2.add_argument("--render", help="front view PNG of the result")
    a2.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "pieces":
        for p in pieces(pathlib.Path(a.pack).read_bytes()):
            print(f"{p['name']:12} {'mix' if p['mixable'] else '   '} {p['triangles']:5} tris  nodes "
                  f"{' '.join('%02X' % n for n in p['nodes'])}  textures {p['textures']}")
        return 0
    sel = []
    cache = {}
    for s in a.piece:
        name, _, path = s.partition("=")
        if path not in cache:
            cache[path] = pathlib.Path(path).read_bytes()
        sel.append((cache[path], name))
    notes = []
    try:
        out = mix(pathlib.Path(a.base).read_bytes(), sel, notes)
    except MixError as e:
        print("refused:", e)
        return 1
    pathlib.Path(a.out).write_bytes(out)
    for n in notes:
        print("note:", n)
    if a.check:
        probs = check(out)
        print("check:", "ok" if not probs else probs)
    if a.render:
        m = mdlx.extract(out, pose="relaxed", hide_optional=True)
        pathlib.Path(a.render).write_bytes(mdlx.render(m, 512, "front"))
    print(a.out, len(out), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
