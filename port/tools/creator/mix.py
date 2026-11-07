#!/usr/bin/env python3
"""Per-piece mixing of fighter models (the creator's "Parts" tab). Standard library only.

    python port/tools/creator/mix.py pieces <pack.bin>
    python port/tools/creator/mix.py mix <base.bin> -o <out.bin> --piece hair=<donor.bin> [--piece torso=<donor2.bin>]
                                     [--render front.png] [--check]

    import mix; mix.pieces(pack_bytes); new_pack = mix.mix(base_pack, [(donor_pack, "hair"), ...])
    mix.mix(base, [(d1, "hair"), (d2, "hair+", {"size": 1.1, "nodes": {0x52: {"turn": [0, 0, 20]}}})],
            report=[], adjust={"build": {"torso": 120}, 0x4F: {"shown": False}}, force=True, declip=True)
    mix.layer_nodes(base, selections), mix.base_nodes(base): the nodes a UI can list for the workshop

force (default): budget overflows degrade instead of refusing (palettes shared / merged, cel-shading ramps mapped
to the nearest, donor textures halved, smallest extras left out), every step noted in `report`. declip (default):
hair from another head is scaled to the base's face width, centred on it and pushed out of a per-direction radius
map of the head (later layers further out); swapped limbs / torso get their cross-section matched to the base's at
both ends of each bone. Workshop edits (see mix()) move meshes; extras' joints follow, core joints never move.

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
import argparse, math, pathlib, struct, sys

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
                # MdlTex_RebaseChain scans words for 0x70000000: BT4 models have it at +4 of the last quadword
                if VIF_END not in struct.unpack_from("<4I", chain, len(chain) - 16):
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
        # several records may carry one node (BT4 models: extra meshes as leaf records in front of the node's
        # own record). The game's node table points at the first, the pose walk recomputes the node at every one
        # and the last one processed wins: by_node is that last ("main") record, dups every record of a node.
        self.by_node, self.dups = {}, {}
        for r in self.recs:
            self.by_node[r.node] = r
            self.dups.setdefault(r.node, []).append(r)
        self.last_pop = self.recs[-1].pop

    def is_main(self, r):
        return self.by_node.get(r.node) is r

    def node_points(self, node):
        pts = []
        for r in self.dups.get(node, ()):
            if r.mesh:
                pts += Mesh(r.chain).P
        return pts

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
        stale = False
        if key not in texmap:   # a stale TBP (BT4): the texture with that palette block
            same = [k for k in texmap if k[1] == key[1]]
            if len(same) == 1:
                key, stale = same[0], True
            elif texmap:
                key, stale = min(texmap, key=lambda k: (abs(k[0] - key[0]), k[0])), True
        if key not in texmap:
            raise MixError("chain uses a texture that is not in its texture file (TBP %d CBP %d)" % key)
        v = texmap[key]
        tbp, cbp = v[0], v[1]
        bits = v[2] if len(v) > 2 else None
        if stale and len(v) > 3:   # and that texture's own shape
            bits = v[3]
        if bits is not None:   # a resized texture: its TBW / PSM / TW / TH (bits 14..33)
            lo = (lo & 0x3FFF) | (bits & 0xFFFFC000)
            hi = (hi & ~0x3) | ((bits >> 32) & 0x3)
        struct.pack_into("<II", buf, pos + 20, (lo & ~0x3FFF) | tbp, (hi & 0xFFF8001F) | (cbp << 5))
        r = (hi2 >> 5) & 0x3FFF
        if r not in rampmap and rampmap:   # a shading palette outside the ramps (BT4): the first ramp
            r = min(rampmap)
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

    def tex_by_ofs(self, tbp, cbp, exact=False):
        for t in self.texs:
            if self.material(t.old) and t.tbpOfs == tbp and t.cbpOfs == cbp:
                return t
        if exact:
            return None
        # BT4 models: a TEX0 whose TBP was not moved with its texture; the palette block still names it
        same = [t for t in self.texs if self.material(t.old) and t.cbpOfs == cbp and t.clut]
        if len(same) == 1:
            return same[0]
        # a texture the file does not have at all (BT4 models; the console draws whatever lies in GS memory
        # there): the nearest one
        mats = [t for t in self.texs if self.material(t.old)]
        return min(mats, key=lambda t: (abs(t.tbpOfs - tbp), t.tbpOfs)) if mats else None

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
NODE_NAMES = {
    0x00: "Root", 0x01: "Root 2", 0x02: "Hips root", 0x03: "Pelvis", 0x04: "Tail 1", 0x05: "Tail 2", 0x06: "Tail 3",
    0x07: "Tail 4", 0x08: "Right hip", 0x09: "Right thigh", 0x0A: "Right shin", 0x0B: "Right foot", 0x0C: "Left hip",
    0x0D: "Left thigh", 0x0E: "Left shin", 0x0F: "Left foot", 0x10: "Belly", 0x11: "Chest", 0x12: "Right chest",
    0x13: "Right upper arm", 0x14: "Right forearm", 0x15: "Right hand", 0x16: "Right thumb", 0x17: "Right finger 1a",
    0x18: "Right finger 1b", 0x19: "Right finger 2a", 0x1A: "Right finger 2b", 0x1B: "Right finger 3a",
    0x1C: "Right finger 3b", 0x1D: "Right finger 4a", 0x1E: "Right finger 4b", 0x1F: "Right hand tip",
    0x20: "Left chest", 0x21: "Left upper arm", 0x22: "Left forearm", 0x23: "Left hand", 0x24: "Left thumb",
    0x25: "Left finger 1a", 0x26: "Left finger 1b", 0x27: "Left finger 2a", 0x28: "Left finger 2b",
    0x29: "Left finger 3a", 0x2A: "Left finger 3b", 0x2B: "Left finger 4a", 0x2C: "Left finger 4b",
    0x2D: "Left hand tip", 0x2E: "Neck", 0x2F: "Head", 0x30: "Face", 0x31: "Jaw", 0x32: "Head top", 0x33: "Head 2",
    0x40: "Accessory (hand)", 0x41: "Accessory 2", 0x42: "Accessory (back)", 0x43: "Accessory 4", 0x6F: "Halo",
    0x76: "Hat / extra",
}
EXTRA_WORD = {"hair": "Strand", "torso": "Cloth", "tail": "Tail part", "arms": "Arm part", "hands": "Hand part",
              "legs": "Leg part", "feet": "Foot part"}
# node ids no model on the disc and no game code uses by number (code names 0..0x36, 0x40, 0x41, 0x6F, 0x76;
# BtlObj_IsNodeShown also 0x42, 0x43); 0x44..0x46 last: ids < 71 can be animated
ID_POOL = [i for i in range(0x47, 0x80) if i not in ACCESSORY] + [0x44, 0x45, 0x46]
MAX_RECORDS_FORCE = 112   # guess: two fighters of 112 leave half of the 512-part pool to everything else


class Layer:
    def __init__(self, k, src, piece, add, opts):
        self.k, self.src, self.piece, self.add, self.opts = k, src, piece, add, opts or {}
        self.region = "hair" if piece in ("hair", "head") else piece


def _layers(selections, donors, src_cache):
    out = []
    for k, s in enumerate(selections, 1):
        pack, piece = s[0], s[1]
        opts = s[2] if len(s) > 2 else None
        add = piece.endswith("+")
        piece = piece.rstrip("+")
        if piece not in PIECES:
            raise MixError(f"unknown piece {piece!r} (one of {', '.join(PIECES)})")
        d = src_cache.get(id(pack))
        if d is None:
            d = src_cache[id(pack)] = Source(pack, f"donor {len(src_cache) + 1}")
            donors.append(d)
        out.append(Layer(k, d, piece, add, opts))
    return out


# ---- small vector helpers (game axes: Y down, face towards -Z; the UI's axes: Y up, face towards +Z)
def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_len(a):
    return math.sqrt(v_dot(a, a))


def m_mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def m_vec(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2])


IDENT3 = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def ui_xform(o):
    """(M, Ninv, t) of a workshop entry in game axes: p -> M (p - pivot) + pivot + t; normals by Ninv.
    o: {"scale" or "stretch": [x, y, z] factors, "size": factor, "turn": [x, y, z] degrees (X, then Y, then Z, UI
    axes), "move": [x, y, z] model units in UI axes (a fighter is about 20 high)}."""
    st = o.get("stretch", o.get("scale", (1, 1, 1)))
    size = float(o.get("size", 1))
    sx, sy, sz = (max(0.05, float(st[0]) * size), max(0.05, float(st[1]) * size), max(0.05, float(st[2]) * size))
    rx, ry, rz = (math.radians(float(a)) for a in o.get("turn", (0, 0, 0)))
    # rotations in UI axes, conjugated by C = diag(1, -1, -1) into game axes
    cx, sx_, cy, sy_, cz, sz_ = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    RX = ((1, 0, 0), (0, cx, -sx_), (0, sx_, cx))
    RY = ((cy, 0, sy_), (0, 1, 0), (-sy_, 0, cy))
    RZ = ((cz, -sz_, 0), (sz_, cz, 0), (0, 0, 1))
    R = m_mul(RZ, m_mul(RY, RX))
    C = ((1, 0, 0), (0, -1, 0), (0, 0, -1))
    R = m_mul(C, m_mul(R, C))
    S = ((sx, 0, 0), (0, sy, 0), (0, 0, sz))
    Si = ((1 / sx, 0, 0), (0, 1 / sy, 0), (0, 0, 1 / sz))
    mv = o.get("move", (0, 0, 0))
    t = (float(mv[0]), -float(mv[1]), -float(mv[2]))
    return m_mul(R, S), m_mul(R, Si), t


def is_identity(o):
    if not o:
        return True
    M, _, t = ui_xform(o)
    return all(abs(M[i][j] - IDENT3[i][j]) < 1e-9 for i in range(3) for j in range(3)) and not any(t)


class Mesh:
    """The vertices of one chain, editable in place (game axes)."""

    def __init__(self, chain):
        self.buf = bytearray(chain)
        self.offs, self.strip = [], []
        nb = [0]

        self.hpos = []

        def tex(pos):
            nb[0] += 1
            self.hpos.append(pos)

        def vert(q, w):
            self.offs.append(q)
            self.strip.append(nb[0])
        walk_chain(self.buf, on_tex=tex, on_vertex=vert)
        self.P = [list(struct.unpack_from("<3f", self.buf, q)) for q in self.offs]
        self.N = [list(struct.unpack_from("<3f", self.buf, q + 16)) for q in self.offs]
        self.W = [struct.unpack_from("<f", self.buf, q + 12)[0] for q in self.offs]
        self.ST = [struct.unpack_from("<2f", self.buf, q + 32) for q in self.offs]
        self.dirty = False
        self.log = []

    def header_of(self, ic):
        """The 5 header quadwords of the batch vertex ic belongs to."""
        b = self.strip[ic] - 1 if ic < len(self.strip) else -1
        if not 0 <= b < len(self.hpos):
            return None
        return bytes(self.buf[self.hpos[b] + 4:self.hpos[b] + 84])

    def replay(self, log):
        for name, args in log:
            getattr(self, name)(*args)

    def radial(self, ax, s0, s1=None):
        self.log.append(("radial", (ax, s0, s1)))
        _radial(self, ax, s0, s1)

    def axial(self, j, u, k, ld=None, k0=None, k2=None):
        """Along the unit axis u through j: the bone (0..ld) scaled by k, what lies behind the joint by k0 (the
        parent bone's ratio), what reaches past the far joint by k2 (the next bone's); distance from it kept."""
        self.log.append(("axial", (j, u, k, ld, k0, k2)))
        k0 = k if k0 is None else k0
        k2 = k if k2 is None else k2
        for i, p in enumerate(self.P):
            a = v_dot(v_sub(p, j), u)
            if a < 0:
                b = a * k0
            elif ld is None or a <= ld:
                b = a * k
            else:
                b = ld * k + (a - ld) * k2
            d = b - a
            self.P[i] = [p[0] + u[0] * d, p[1] + u[1] * d, p[2] + u[2] * d]
        self.dirty = True

    def remap_axis(self, j, u, segs):
        """Along the unit axis u through j: piecewise-linear moves of the distance a, segs = [(a0, a1, b0, b1), ...]
        (a in [a0, a1] goes to b0 + (a - a0) * (b1 - b0) / (a1 - a0)); outside every segment, unchanged."""
        self.log.append(("remap_axis", (j, u, segs)))
        for i, p in enumerate(self.P):
            a = v_dot(v_sub(p, j), u)
            for a0, a1, b0, b1 in segs:
                lo, hi = min(a0, a1), max(a0, a1)
                if lo - 1e-6 <= a <= hi + 1e-6 and abs(a1 - a0) > 1e-6:
                    d = b0 + (a - a0) * (b1 - b0) / (a1 - a0) - a
                    self.P[i] = [p[0] + u[0] * d, p[1] + u[1] * d, p[2] + u[2] * d]
                    break
        self.dirty = True

    def welded(self, edges_too=False):
        """Groups of vertices at the same place (the strips repeat them) and the groups on open edges."""
        key, groups = {}, []
        gid = []
        for i, p in enumerate(self.P):
            k = (round(p[0] * 2000), round(p[1] * 2000), round(p[2] * 2000))
            g = key.get(k)
            if g is None:
                g = key[k] = len(groups)
                groups.append([])
            groups[g].append(i)
            gid.append(g)
        edges = {}
        for i in range(len(self.P) - 2):
            if self.strip[i] != self.strip[i + 2]:
                continue
            a, b, c = gid[i], gid[i + 1], gid[i + 2]
            if a == b or b == c or a == c:
                continue
            for e in ((a, b), (b, c), (a, c)):
                e = (min(e), max(e))
                edges[e] = edges.get(e, 0) + 1
        border, bedges = set(), []
        for (a, b), n in edges.items():
            if n == 1:
                border.add(a)
                border.add(b)
                bedges.append((a, b))
        if edges_too:
            return groups, border, bedges
        return groups, border

    def shift(self, dA, dB):
        self.log.append(("shift", (dA, dB)))
        for i, p in enumerate(self.P):
            w = self.W[i]
            for k in range(3):
                p[k] += w * dA[k] + (1 - w) * dB[k]
        self.dirty = True

    def apply(self, M, Ni, t, pivot, weighted=False):
        self.log.append(("apply", (M, Ni, t, pivot, weighted)))
        for i, p in enumerate(self.P):
            q = m_vec(M, v_sub(p, pivot))
            q = (q[0] + pivot[0] + t[0], q[1] + pivot[1] + t[1], q[2] + pivot[2] + t[2])
            n = m_vec(Ni, self.N[i])
            if weighted:
                w = self.W[i]
                q = tuple(p[k] + w * (q[k] - p[k]) for k in range(3))
                n = tuple(self.N[i][k] + w * (n[k] - self.N[i][k]) for k in range(3))
            ln = v_len(n) or 1.0
            self.P[i] = list(q)
            self.N[i] = [n[0] / ln, n[1] / ln, n[2] / ln]
        self.dirty = True

    def bytes(self):
        if self.dirty:
            for i, q in enumerate(self.offs):
                struct.pack_into("<3f", self.buf, q, *self.P[i])
                struct.pack_into("<3f", self.buf, q + 16, *self.N[i])
        return bytes(self.buf)

    def bbox(self):
        if not self.P:
            return None
        lo = [min(p[k] for p in self.P) for k in range(3)]
        hi = [max(p[k] for p in self.P) for k in range(3)]
        return lo, hi


def axis_of(model_nodes, node, origin_of):
    """Bone axis of a core node: from its joint to its main core child's joint (else onwards from the parent)."""
    r = model_nodes.get(node)
    j = origin_of(node)
    best, bl = None, 0
    for c in (r.children if r else []):
        if c.node in CORE and c.node not in FIXED:
            l = v_len(v_sub(origin_of(c.node), j))
            if l > bl:
                best, bl = c.node, l
    if best is not None and bl > 1e-3:
        e = origin_of(best)
    elif r is not None and r.parent is not None:
        d = v_sub(j, origin_of(r.parent.node))
        l = v_len(d)
        if l < 1e-3:
            return None
        e = v_add(j, d)
    else:
        return None
    return j, e


def radial_scale(mesh, ax, s0, s1=None):
    mesh.radial(ax, s0, s1)


def _radial(mesh, ax, s0, s1=None):
    """Scale every vertex's distance from the bone axis (joints stay): s0 at the joint end, s1 at the far end."""
    j, e = ax
    d = v_sub(e, j)
    L2 = v_dot(d, d)
    s1 = s0 if s1 is None else s1
    for i, p in enumerate(mesh.P):
        t = v_dot(v_sub(p, j), d) / L2
        c = (j[0] + d[0] * t, j[1] + d[1] * t, j[2] + d[2] * t)
        if t < 0:     # behind the joint (the parent's side): fade out over a quarter of the bone
            s = 1 + (s0 - 1) * max(0.0, 1 + t / 0.25)
        elif t > 1:   # past the far joint
            s = 1 + (s1 - 1) * max(0.0, 1 - (t - 1) / 0.25)
        else:
            s = s0 + (s1 - s0) * t
        mesh.P[i] = [c[k] + (p[k] - c[k]) * s for k in range(3)]
    mesh.dirty = True


def end_radii(points, ax):
    """Median distance from the bone axis near the joint (t < 0.3) and near the far end (t > 0.7)."""
    j, e = ax
    d = v_sub(e, j)
    L2 = v_dot(d, d)
    a, b = [], []
    for p in (points.P if isinstance(points, Mesh) else points):
        t = v_dot(v_sub(p, j), d) / L2
        c = (j[0] + d[0] * t, j[1] + d[1] * t, j[2] + d[2] * t)
        r = v_len(v_sub(p, c))
        if 0 <= t < 0.3:
            a.append(r)
        elif 0.7 < t <= 1:
            b.append(r)
    med = lambda x: sorted(x)[len(x) // 2] if len(x) >= 6 else None
    return med(a), med(b)


class HeadShell:
    """The head's outer surface as a radius per direction around its centre (12 x 24 bins), for pushing hair out."""
    NE, NA = 12, 24

    def __init__(self, pts):
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        self.c = tuple((lo[k] + hi[k]) / 2 for k in range(3))
        self.width = hi[0] - lo[0]
        self.lo, self.hi = lo, hi
        self.R = [[0.0] * self.NA for _ in range(self.NE)]
        for p in pts:
            e, a, r = self.bin(p)
            if r > self.R[e][a]:
                self.R[e][a] = r
        for _ in range(3):   # fill empty bins from their neighbours
            for e in range(self.NE):
                for a in range(self.NA):
                    if self.R[e][a] == 0:
                        nb = [self.R[e2][(a + da) % self.NA] for e2 in (e - 1, e, e + 1) if 0 <= e2 < self.NE
                              for da in (-1, 0, 1)]
                        nb = [x for x in nb if x > 0]
                        if nb:
                            self.R[e][a] = sum(nb) / len(nb)
        self.radius = sum(map(sum, self.R)) / (self.NE * self.NA)

    def bin(self, p):
        d = v_sub(p, self.c)
        r = v_len(d)
        if r < 1e-6:
            return self.NE // 2, 0, 0.0
        el = math.acos(max(-1.0, min(1.0, d[1] / r)))
        az = math.atan2(d[2], d[0])
        e = min(self.NE - 1, int(el / math.pi * self.NE))
        a = int((az + math.pi) / (2 * math.pi) * self.NA) % self.NA
        return e, a, r

    def push(self, mesh, margin, grow):
        n = 0
        for i, p in enumerate(mesh.P):
            e, a, r = self.bin(p)
            R = self.R[e][a] + margin
            target = r * grow
            if target < R:
                target = R
                n += 1
            if r > 1e-6 and target != r:
                f = target / r
                mesh.P[i] = [self.c[k] + (p[k] - self.c[k]) * f for k in range(3)]
        mesh.dirty = True
        return n


# ---- texture tools (GS swizzles from model.py)
def _upload_mem(t):
    w, h, dofs, _ = mdlx.parse_gs_upload(t.pix, 0)
    dbw = struct.unpack_from("<I", t.ent, 0x18)[0] & 0x3F
    words = struct.unpack_from(f"<{w * h}I", t.pix, dofs)
    mem = {}
    for y in range(h):
        for x in range(w):
            mem[mdlx.addr32(dbw, x, y)] = words[y * w + x]
    return mem, w, h, dofs


def tex_shape(t):
    tex0 = struct.unpack_from("<Q", t.ent, 0x30)[0]
    return (tex0 >> 20) & 0x3F, (tex0 >> 14) & 0x3F, 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15)


def tex_indices(t):
    psm, tbw, tw, th = tex_shape(t)
    mem, _, _, _ = _upload_mem(t)

    def byte(a):
        return (mem.get(a >> 2, 0) >> ((a & 3) * 8)) & 0xFF
    out = []
    for y in range(th):
        for x in range(tw):
            if psm == 0x14:
                a = mdlx.addr4(tbw, x, y)
                v = byte(a >> 1)
                out.append((v >> 4) if a & 1 else (v & 15))
            else:
                out.append(byte(mdlx.addr8(tbw, x, y)))
    return out


def halve_texture(t):
    """A copy of texture entry t at half the width and height (nearest pixel): new upload packet, TEX0 TBW / TW /
    TH, pixel size, block count, BITBLTBUF width. UVs are normalised, so the chains only need the new TEX0 bits."""
    psm, tbw, tw, th = tex_shape(t)
    if psm not in (0x13, 0x14) or tw < 16 or th < 16:
        return None
    idx = tex_indices(t)
    nw, nh = tw // 2, th // 2
    small = [idx[(2 * y) * tw + 2 * x] for y in range(nh) for x in range(nw)]
    ntbw = nw // 64 if nw >= 128 else 0
    w, h, dofs, _ = mdlx.parse_gs_upload(t.pix, 0)
    rect = find_rect(psm, ntbw, nw, nh)   # a PSMCT32 rectangle that writes exactly the texture's blocks
    if rect is None:
        return None
    uw, uh, dbw = rect
    nbytes = {}
    blocks = set()
    for y in range(nh):
        for x in range(nw):
            v = small[y * nw + x]
            if psm == 0x14:
                a = mdlx.addr4(ntbw, x, y)
                b = a >> 1
                cur = nbytes.get(b, 0)
                nbytes[b] = (cur & 0x0F) | (v << 4) if a & 1 else (cur & 0xF0) | v
                blocks.add(a >> 9)
            else:
                a = mdlx.addr8(ntbw, x, y)
                nbytes[a] = v
                blocks.add(a >> 8)
    words = []
    for y in range(uh):
        for x in range(uw):
            wa = mdlx.addr32(dbw, x, y) * 4
            words.append(nbytes.get(wa, 0) | (nbytes.get(wa + 1, 0) << 8) | (nbytes.get(wa + 2, 0) << 16) |
                         (nbytes.get(wa + 3, 0) << 24))
    data = struct.pack(f"<{len(words)}I", *words)
    if len(data) % 16:
        return None
    # the packet: the original head (DIRECT, A+D TRXPOS / TRXREG / TRXDIR, IMAGE tag) with new sizes
    head = bytearray(t.pix[:dofs])
    old_nloop = h * w * 4 // 16
    pad = len(t.pix) - dofs - (h * w * 4)
    new_nloop = len(data) // 16
    for q in range(0, dofs, 16):
        lo, hi = struct.unpack_from("<QQ", head, q)
        if hi == 0x52:   # TRXREG
            struct.pack_into("<Q", head, q, uw | (uh << 32))
        if (lo >> 58) & 3 == 2 and q + 16 == dofs:   # IMAGE tag
            struct.pack_into("<Q", head, q, (lo & ~0x7FFF) | (len(data) // 16))
    for q in range(0, 16, 4):   # VIF DIRECT word in front: its count covers the GIF data
        c = struct.unpack_from("<I", head, q)[0]
        if (c >> 24) & 0x7F == 0x50:
            imm = c & 0xFFFF
            struct.pack_into("<I", head, q, (c & ~0xFFFF) | max(0, imm - (old_nloop - new_nloop)))
    pix = bytes(head) + data + b"\0" * max(0, pad)
    e = bytearray(t.ent)
    struct.pack_into("<i", e, 0x08, len(pix))
    struct.pack_into("<i", e, 0x10, max(blocks) + 1)
    struct.pack_into("<I", e, 0x18, (struct.unpack_from("<I", e, 0x18)[0] & ~0x3F) | dbw)
    tex0 = struct.unpack_from("<Q", e, 0x30)[0]
    tex0 = (tex0 & ~(0xFFFFF << 14)) | (ntbw << 14) | (psm << 20) | ((((tex0 >> 26) & 15) - 1) << 26) | \
           ((((tex0 >> 30) & 15) - 1) << 30)
    struct.pack_into("<Q", e, 0x30, tex0)
    n = Tex(e, pix, t.clut, t.src, t.old)
    return n


def tex0_blocks(tex0):
    """GS blocks a TEX0's TBW / PSM / TW / TH reach (1 + the highest block of the texture)."""
    psm, tbw = (tex0 >> 20) & 0x3F, (tex0 >> 14) & 0x3F
    w, h = 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15)
    hi = 0
    if psm == 0x14:
        for y in range(0, h, 16):
            for x in range(0, w, 32):
                hi = max(hi, mdlx.addr4(tbw, x, y) >> 9)
    elif psm == 0x13:
        for y in range(0, h, 16):
            for x in range(0, w, 16):
                hi = max(hi, mdlx.addr8(tbw, x, y) >> 8)
    else:
        for y in range(0, h, 8):
            for x in range(0, w, 8):
                hi = max(hi, mdlx.addr32(tbw, x, y) >> 6)
    return hi + 1


def find_rect(psm, tbw, w, h):
    """(width, height, DBW) of the PSMCT32 upload whose blocks are exactly those of a PSMT4 / PSMT8 texture."""
    want = set()
    bw, bh = (32, 16) if psm == 0x14 else (16, 16)
    for y in range(0, h, bh):
        for x in range(0, w, bw):
            want.add((mdlx.addr4(tbw, x, y) >> 9) if psm == 0x14 else (mdlx.addr8(tbw, x, y) >> 8))
    nbytes = len(want) * 256
    for rw in (8, 16, 32, 64, 128, 256, 512):
        rh = nbytes // 4 // rw
        if rh < 8 or rh * rw * 4 != nbytes or rh % 8:
            continue
        for dbw in sorted({rw // 64, max(1, rw // 64)}):
            got = {mdlx.addr32(dbw, x, y) >> 6 for y in range(0, rh, 8) for x in range(0, rw, 8)}
            if got == want:
                return rw, rh, dbw
    return None


def clut_colors(t):
    if not t.clut:
        return []
    cw, ch, cofs, _ = mdlx.parse_gs_upload(t.clut, 0)
    return [t.clut[cofs + k * 4:cofs + k * 4 + 3] for k in range(cw * ch)]


def color_dist(a, b):
    if len(a) != len(b) or not a:
        return 1e9
    return sum(abs(x[0] - y[0]) + abs(x[1] - y[1]) + abs(x[2] - y[2]) for x, y in zip(a, b)) / len(a)


def ramp_colors(t):
    w, h, dofs, _ = mdlx.parse_gs_upload(t.pix, 0)
    return [t.pix[dofs + k * 4:dofs + k * 4 + 3] for k in range(w * h)]


# ---- the mix
def mix(base_pack, selections, report=None, adjust=None, force=True, declip=True, fit=True, ids=None):
    """A new pack: the base with pieces from donors.

    The workshop's parts list (a layer's opts and adjust alike): "copies": [{"node": id, "stretch", "size", "move",
    "turn", "shown"}, ...] a duplicate of that strand / extra (a new record next to it, the edits relative to where
    the strand is before its own edits); adjust "regions": {"hair" | "head" | "torso" | ...: {the per-node keys}} the
    base's own nodes of a piece all at once; adjust "patch": false leaves out the strips and caps that close the
    holes between pieces (fit). ids (a dict, filled in): {(layer k, donor node): node id in the model, ("copy",
    layer k or 0 for the base, n): node id of the copy}.

    selections: [(donor_pack, piece) or (donor_pack, piece, opts), ...] in layer order. piece is one of PIECES; a
    trailing "+" ("hair+", "torso+") adds that donor's extras of the region on top and keeps the model's own (any
    number of layers). Later selections win where they replace the same thing.
    opts (all optional): "scale" / "stretch": [x, y, z] factors, "size": factor, "turn": [x, y, z] degrees,
    "move": [x, y, z] model units (UI axes: X right, Y up, Z towards the viewer; a fighter is ~20 high), applied to
    the layer around its attach joint; "nodes": {donor node id: {"shown": bool, "stretch", "size", "move",
    "turn"}} per node around that node's joint (extras: children follow; core nodes: that node's mesh only).
    adjust: the same per-node dict for the base's own nodes ({node id: {...}}), plus "build": {"torso", "arms",
    "legs", "head", "hands", "feet": percent (100 = unchanged)} = thickness around each bone (joints stay put;
    "head" scales the head and what is on it).
    force (default): never refuse for the GS budget, ids or record count: degrade (share / merge palettes and
    ramps, halve donor textures, drop the smallest extras) and say so in report. declip (default): fit hair to the
    head and push it out of the head, match swapped limbs' cross-sections at the seams, layer hairs outwards.
    fit (default): automatic alignment of pieces from characters of another size: each swapped bone's mesh is
    stretched / shortened to the base's joint-to-joint length (half strength past the joints), a donor head is
    scaled to the base's face size (dead zone 12 %, damped), feet to the base's ankle / ground / foot size, capes and
    other hanging extras with the body height, open edges snapped onto the neighbouring piece's open edges, and the
    base's seam patch left out where a piece it joins is swapped.
    Models with several records per node (BT4) are accepted as donors and bases: all records of a node move
    together; extra donor meshes of a swapped node become leaf records in front of the node's own one."""
    notes = report if report is not None else []
    adjust = adjust or {}
    cap = MAX_RECORDS_FORCE if force else MAX_RECORDS
    base = Source(base_pack, "base")
    donors, cache = [], {}
    layers = _layers(selections, donors, cache)
    core_layer, extra_layer, add_layers = {}, {}, []
    for L in layers:
        if L.add:
            add_layers.append(L)
            continue
        if L.piece != "hair":
            for n in REGIONS[L.piece]:
                core_layer[n] = L
        extra_layer[L.region] = L
    # nodes 4..7 are a tail only in some models (others use them for big body parts): a donor "tail" larger
    # than three quarters of its own body is not taken
    for L in layers:
        if L.piece != "tail" or L.add:
            continue
        pts = [p for n in TAIL for p in L.src.model.node_points(n)]
        hd = _height(L.src.model) or 1.0
        if pts and max(_extent(pts, k) for k in range(3)) > 0.75 * hd:
            for n in TAIL:
                if core_layer.get(n) is L:
                    del core_layer[n]
            if extra_layer.get("tail") is L:
                del extra_layer["tail"]
            notes.append(f"layer {L.k}: the donor's nodes 4..7 are not a tail (too big); the base's tail is kept")
    head_layer = core_layer.get(0x30)
    head_src = head_layer.src if head_layer else None
    bm = base.model

    def fail(msg):
        raise MixError(msg)

    # ---- plan the base records
    plan = []
    for r in bm.recs:
        reg = bm.region(r)
        L = None
        if r.node in CORE and r.node not in FIXED:
            L = core_layer.get(r.node)
        elif reg not in ("accessories", "root") and r.node not in CORE:
            L = extra_layer.get(reg)
        plan.append((r, L))

    def subtree(r):   # an extra and the extras below it (a core node below an extra starts its own subtree)
        out = [r]
        for c in r.children:
            if c.node not in ACCESSORY and c.node not in CORE:
                out += subtree(c)
        return out

    def tris(r):
        return sum(max(0, len(s["verts"]) - 2) for s in mdlx.parse_chain(r.chain, 0, len(r.chain))) if r.mesh else 0

    # ---- donor extras to add, then what fits (records, ids)
    adds = []   # [layer, donor root rec, base attach rec]
    for L in [extra_layer[k] for k in extra_layer] + add_layers:
        if L.add or extra_layer.get(L.region) is L:
            dm = L.src.model
            for dr in dm.recs:
                if dr.node in CORE or dm.region(dr) != L.region:
                    continue
                if dr.parent is not None and dr.parent.node in CORE and dr.parent.node in bm.by_node:
                    adds.append((L, dr, bm.by_node[dr.parent.node]))
    used_ids = {r.node for r in bm.recs}
    pool = [i for i in (ID_POOL if force else FREE_IDS + [i for i in range(0x47, 0x76) if i not in ACCESSORY])
            if i not in used_ids]
    wanted = []   # (layer, donor rec, depth) breadth first, bigger first within a depth
    for L, dr, _ in adds:
        for x in subtree(dr):
            d, p = 0, x
            while p is not dr:
                p, d = p.parent, d + 1
            wanted.append((d, -tris(x), L.k, L, x))
    wanted.sort(key=lambda w: (w[0], w[1], w[2]))
    room = cap - len(bm.recs)
    accepted = {}   # (layer k, donor node) -> new id
    dropped = []
    for d, _, _, L, x in wanted:
        par_ok = d == 0 or (L.k, x.parent.node) in accepted
        nid = x.node
        if par_ok and room > 0:
            if nid < 0x47 or nid in used_ids or nid in ACCESSORY:
                nid = pool.pop(0) if pool else None
            elif nid in pool:
                pool.remove(nid)
            if nid is not None:
                used_ids.add(nid)
                accepted[(L.k, x.node)] = nid
                room -= 1
                continue
        dropped.append((L, x))
    if dropped:
        if not force:
            fail(f"{len(dropped)} donor extras do not fit (record count {cap} or node ids)")
        notes.append(f"left out {len(dropped)} small extra part(s) (record limit {cap} / free node ids): "
                     + ", ".join(f"layer {L.k} node 0x{x.node:X}" for L, x in dropped[:8]))
    if ids is not None:
        ids.update(accepted)
        for L in layers:
            for r_ in L.src.model.recs:
                if r_.node in CORE:
                    ids.setdefault((L.k, r_.node), r_.node)

    # ---- textures used
    uses, ramp_uses = {}, {}
    odd_ramps = set()

    def need(src, chain):
        for tbp, cbp, rc in chain_texrefs(chain):
            t = src.tex_by_ofs(tbp, cbp)
            if t is None:
                fail(f"{src.name}: a chain names a texture the file does not have (TBP {tbp})")
            uses.setdefault(src, set()).add(t.old)
            rt = src.ramp_by_ofs(rc)
            if rt is None:   # BT4 models: a shading TEX0 with a palette block outside the ramps
                rt = next((x for x in src.texs if src.ramp(x.old)), None)
                odd_ramps.add(src.name)
                if rt is None:
                    fail(f"{src.name}: a chain names a ramp CLUT the file does not have ({rc})")
            ramp_uses.setdefault(src, set()).add(rt.old)

    main_used = set()

    def need_main(src, chain):
        need(src, chain)
        main_used.update((src, src.tex_by_ofs(a, b).old) for a, b, _ in chain_texrefs(chain))

    for r, L in plan:
        if L is None and r.mesh:
            need_main(base, r.chain)
        elif L is not None and r.node in CORE and bm.is_main(r):
            for dr in L.src.model.dups.get(r.node, ()):
                if dr.mesh:
                    need_main(L.src, dr.chain)
    for L, dr, _ in adds:
        for x in subtree(dr):
            if x.mesh and (L.k, x.node) in accepted:
                need_main(L.src, x.chain)
    face_src = head_src or base
    for n in E_MOUTH_A:
        c = entry(face_src.parts, n)
        if c:
            need(face_src, c)
    uses.setdefault(face_src, set()).add(face_src.faceTex)
    # the seam patch (CPU-skinned triangles that close the crotch / hips of some models): the base's while
    # nothing it joins is swapped; else the seam of the donor that supplies most of its nodes (moved like that
    # donor's meshes); else the base's (kept: leaving it out opened holes)
    def seam_nodes_of(model_):
        sm = model_.seams()
        if not sm:
            return set()
        vc_, tc_, vo_, to_ = sm
        return {struct.unpack_from("<i", model_.raw, vo_ + i * 0x70 + 0x1C)[0] for i in range(vc_)}
    supplied = {}
    for n_, L_ in core_layer.items():
        supplied.setdefault(L_, set()).add(n_)
    seam_layer = None
    if fit and seam_nodes_of(bm) & set(core_layer) or fit and not bm.seamOfs:
        best_ = 0
        for L_, ns_ in supplied.items():
            sn = seam_nodes_of(L_.src.model)
            score = len(sn & ns_)
            if sn and score > best_ and all(x in bm.by_node for x in sn):
                seam_layer, best_ = L_, score
        if seam_layer is not None and not bm.seamOfs and best_ < 2:
            seam_layer = None
    if seam_layer is not None:
        dsm = seam_layer.src.model.seams()
        tb, ts = struct.unpack("<QQ", seam_layer.src.model.raw[dsm[3] + 0x10:dsm[3] + 0x20])
        t = seam_layer.src.tex_by_ofs(tb & 0x3FFF, (tb >> 37) & 0x3FFF)
        if t is not None:
            uses.setdefault(seam_layer.src, set()).add(t.old)
        rt = seam_layer.src.ramp_by_ofs((ts >> 37) & 0x3FFF)
        if rt is not None:
            ramp_uses.setdefault(seam_layer.src, set()).add(rt.old)
    seam = bm.seams()
    if seam:
        vc, tc, vo, to = seam
        tb, ts = struct.unpack("<QQ", bm.raw[to + 0x10:to + 0x20])
        t = base.tex_by_ofs(tb & 0x3FFF, (tb >> 37) & 0x3FFF)
        if t is not None:
            uses.setdefault(base, set()).add(t.old)
        rt = base.ramp_by_ofs((ts >> 37) & 0x3FFF)
        if rt is not None:
            ramp_uses.setdefault(base, set()).add(rt.old)

    if odd_ramps:
        notes.append("a shading reference outside the ramp table (BT4 model) now uses the first ramp")
    # ---- material textures: base, then donors; exact duplicates shared
    mats = [t for t in base.texs if base.material(t.old)]
    alias = {}   # (src, old) -> the Tex that stands for it
    for t in mats:
        alias[(base, t.old)] = t
    for src in donors:
        for i in sorted(uses.get(src, ())):
            t = src.texs[i]
            same = next((m for m in mats if m.pix == t.pix and m.clut == t.clut and m.ent[0x30:0x38] == t.ent[0x30:0x38]),
                        None)
            if same is not None:
                alias[(src, i)] = same
            else:
                mats.append(t)
                alias[(src, i)] = t
    faces = {id(alias[(face_src, face_src.faceTex)])}
    if base.noAlpha and (base, base.noAlpha) in alias:
        faces.add(id(alias[(base, base.noAlpha)]))
    for i in [base.fadeTex] + list(range(base.fadeFirst, base.fadeFirst + base.fadeCount)):
        if (base, i) in alias:   # named by the header: kept, never resized
            faces.add(id(alias[(base, i)]))
    used_now = {id(alias[(s, i)]) for s, ix in uses.items() for i in ix}
    if sum(t.tbpStep for t in mats) > TEX_BLOCKS or sum(t.cbpStep for t in mats) > CLUT_BLOCKS:
        drop = [t for t in mats if t.src is base and id(t) not in used_now and id(t) not in faces]
        if drop:
            mats = [t for t in mats if t not in drop]
            notes.append(f"dropped {len(drop)} unused base texture(s) to fit the GS budget")
    # pixels: halve the largest donor textures (then base ones) until they fit
    halved = 0
    while sum(t.tbpStep for t in mats) > TEX_BLOCKS:
        if not force:
            fail(f"textures need {sum(t.tbpStep for t in mats)} GS blocks, a fighter has {TEX_BLOCKS}")
        cand = sorted([t for t in mats if id(t) not in faces], key=lambda t: (t.src is base, -t.tbpStep))
        cand = [t for t in cand if tex_shape(t)[2] >= 16 and tex_shape(t)[3] >= 16]
        if not cand:
            fail("textures cannot be made to fit the GS budget")
        big = cand[0]
        small = halve_texture(big)
        if small is None:
            faces.add(id(big))   # cannot shrink this one; try the next
            continue
        k = mats.index(big)
        mats[k] = small
        for key, v in list(alias.items()):
            if v is big:
                alias[key] = small
        if id(big) in used_now:
            used_now.add(id(small))
        halved += 1
    if halved:
        notes.append(f"halved the resolution of {halved} texture(s) to fit the 0x780-block texture budget")
    # the lowest TBP / CBP of the main chains must be 0 (MdlTex_RebaseChain subtracts it)
    main_ids = {id(alias[k]) for k in main_used}
    if not (id(mats[0]) in main_ids and mats[0].clut):
        first = next((t for t in mats if id(t) in main_ids and t.clut), None)
        if first is None:
            fail("no main chain uses a texture with a palette")
        mats.remove(first)
        mats.insert(0, first)
    # palettes: share identical, then merge the most similar until they fit the tint pass's 64 blocks
    groups = [[t, [t]] for t in mats if t.clut]   # [leader Tex, [members]]: members share the leader's CLUT
    if sum(g[0].cbpStep for g in groups) > CLUT_BLOCKS and force:   # identical palettes first
        ng = []
        for g0 in groups:
            g = next((g for g in ng if g[0].clut == g0[0].clut and g[0].cbpStep == g0[0].cbpStep), None)
            if g:
                g[1].extend(g0[1])
            else:
                ng.append(g0)
        if len(ng) < len(groups):
            notes.append(f"{len(groups) - len(ng)} identical palette(s) shared")
        groups = ng
    if sum(g[0].cbpStep for g in groups) > CLUT_BLOCKS:
        if not force:
            fail(f"palettes need {sum(g[0].cbpStep for g in groups)} GS blocks, the palette area has {CLUT_BLOCKS}")
        cols = {id(g[0]): clut_colors(g[0]) for g in groups}
        merged = 0
        while sum(g[0].cbpStep for g in groups) > CLUT_BLOCKS:
            best = None
            for i in range(len(groups)):
                for j in range(len(groups)):
                    if i == j or groups[i][0].cbpStep != groups[j][0].cbpStep:
                        continue
                    a, b = groups[i], groups[j]
                    dist = color_dist(cols[id(a[0])], cols[id(b[0])])
                    if b[0].src is base:
                        dist *= 2   # keep the base's own colours where possible
                    if best is None or dist < best[0]:
                        best = (dist, i, j)
            if best is None:
                fail("palettes cannot be made to fit")
            _, i, j = best
            groups[i][1].extend(groups[j][1])
            if j == 0:   # keep palette 0 at block 0
                groups[i], groups[j] = groups[j], groups[i]
                i, j = j, i
            del groups[j]
            merged += 1
        notes.append(f"merged {merged} similar palette(s) to fit the 64-block palette area")
    # slots (byte 0x2C): the per-material id of the alpha table, 0..14
    slots_used = {t.ent[0x2C] for t in mats if t.src is base}
    slot_map, shared = {}, 0
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
                else:
                    same = [b.ent[0x2C] for b in mats if b.src is base and b.ent[0x2D] == t.ent[0x2D]]
                    s = same[len(slot_map) % len(same)] if same else 0
                    shared += 1
            slots_used.add(s)
            slot_map[key] = s
        t.ent[0x2C] = slot_map[key]
    if shared:
        notes.append(f"{shared} donor material slot(s) share a base slot (same alpha): all 15 are taken")
    # ramps: base, donors' new ones; over 16: drop unused base ones, then map to the nearest kept one
    ramps = [t for t in base.texs if base.ramp(t.old)]
    ramp_new = {(base, t.old): t for t in ramps}
    for src in donors:
        for i in sorted(ramp_uses.get(src, ())):
            t = src.texs[i]
            if t.tbpStep != 4 and ramps:   # (BT4) a ramp of another size: the base's first one instead
                ramp_new[(src, i)] = ramps[0]
                continue
            same = next((b for b in ramps if b.pix == t.pix and b.tbpStep == t.tbpStep), None)
            if same is None:
                ramps.append(t)
                same = t
            ramp_new[(src, i)] = same
    if sum(t.tbpStep for t in ramps) > RAMP_BLOCKS:
        required = {id(ramp_new[(src, i)]) for src, ix in ramp_uses.items() for i in ix}
        required |= {id(ramp_new[(base, i)]) for i in [base.fadeTex] + list(range(base.fadeFirst, base.fadeFirst +
                     base.fadeCount)) if (base, i) in ramp_new}
        nd = [t for t in ramps if t.src is base and id(t) not in required]
        if nd:
            ramps = [t for t in ramps if t not in nd]
            notes.append(f"dropped {len(nd)} unused base shading ramp(s)")
    if sum(t.tbpStep for t in ramps) > RAMP_BLOCKS:
        if not force:
            fail(f"shading ramps need {sum(t.tbpStep for t in ramps)} GS blocks, there are {RAMP_BLOCKS}")
        keep = ramps[:RAMP_BLOCKS // 4]
        extra = ramps[RAMP_BLOCKS // 4:]
        kc = [ramp_colors(t) for t in keep]
        for t in extra:
            c = ramp_colors(t)
            near = min(range(len(keep)), key=lambda k: color_dist(kc[k], c))
            for key, v in list(ramp_new.items()):
                if v is t:
                    ramp_new[key] = keep[near]
        ramps = keep
        notes.append(f"{len(extra)} shading ramp(s) (cel shading) mapped to the most similar kept one")
    if any(t.tbpStep != 4 for t in ramps if t.src is not base):
        fail("a shading ramp CLUT is not 4 blocks (BtlObjLight_FindRes1 assumes 4)")
    if any(t.tbpStep != 4 for t in ramps):
        notes.append("the base's shading ramps are not all 4 blocks (as on its own disc); kept as they are")
    others = [t for t in base.texs if not base.material(t.old) and not base.ramp(t.old)]

    # ---- positions in the new file
    out_texs, pos_of = [], {}
    tb = 0
    clut_at = {}
    cb = 0
    for g in groups:
        for m in g[1]:
            clut_at[id(m)] = cb
        cb += g[0].cbpStep
    for t in mats:
        e = Tex(t.ent, t.pix, t.clut, t.src, t.old)
        c = clut_at.get(id(t), 0)
        struct.pack_into("<ii", e.ent, 0x20, tb, c)
        if t.clut and not g_leader(groups, t):
            struct.pack_into("<i", e.ent, 0x14, 0)   # shares another texture's palette
        pos_of[id(t)] = (tb, c, len(out_texs), struct.unpack_from("<Q", t.ent, 0x30)[0])
        tb += t.tbpStep
        out_texs.append(e)
    clut_first = len(out_texs)
    ramp_pos, rb = {}, 0
    for t in ramps:
        e = Tex(t.ent, t.pix, t.clut, t.src, t.old)
        struct.pack_into("<i", e.ent, 0x20, rb)
        ramp_pos[id(t)] = rb
        rb += t.tbpStep
        out_texs.append(e)
    other_first = len(out_texs)
    for t in others:
        out_texs.append(Tex(t.ent, t.pix, t.clut, t.src, t.old))

    mapcache = {}

    def maps(src):
        if src in mapcache:
            return mapcache[src]
        tm, rm = {}, {}
        for t in src.texs:
            if src.material(t.old) and (src, t.old) in alias and id(alias[(src, t.old)]) in pos_of:
                a, b, _, tex0 = pos_of[id(alias[(src, t.old)])]
                old0 = struct.unpack_from("<Q", t.ent, 0x30)[0]
                tm[(t.tbpOfs, t.cbpOfs)] = (a, b, tex0 if tex0 != old0 else None, tex0)
            if src.ramp(t.old) and (src, t.old) in ramp_new and id(ramp_new[(src, t.old)]) in ramp_pos:
                rm[t.tbpOfs] = ramp_pos[id(ramp_new[(src, t.old)])]
        mapcache[src] = (tm, rm)
        return tm, rm

    def remap(src, chain):
        tm, rm = maps(src)
        return rewrite_chain(chain, tm, rm)

    def new_index(src, old):
        if src.material(old):
            v = pos_of.get(id(alias.get((src, old))))
            return v[2] if v else None
        if src is base and base.ramp(old):
            t = ramp_new.get((base, old))
            return clut_first + ramps.index(t) if t in ramps else None
        if src is base:
            olds = [t.old for t in others]
            return other_first + olds.index(old) if old in olds else None
        return None

    # ---- records
    def add3(hdr, off, d):
        x, y, z = struct.unpack_from("<3f", hdr, off)
        struct.pack_into("<3f", hdr, off, x + d[0], y + d[1], z + d[2])

    fade_mask = 0xFFFF if base.flags & 1 else 0xFFFE
    hidden = set()
    extra_dups = []  # (base main rec, layer, [more donor mesh records of the node])
    adopted = {}     # node -> new origin of a node that took the donor's bone
    hb_ = _height(bm)
    scale_cache = {}

    def body_scale(L, node):
        """The donor's size against the base's (head-joint to foot-joint height): its pieces are scaled by it
        before the joint offsets, so a piece of a bigger or smaller character does not fold at its joints. Not
        for the head (fitted to the face) and not without fit."""
        if not fit or node in (0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33):
            return 1.0
        if L not in scale_cache:
            hd = _height(L.src.model)
            s_ = max(0.4, min(2.5, hb_ / hd)) if hb_ and hd else 1.0
            scale_cache[L] = 1.0 if abs(s_ - 1) < 0.05 else s_
        return scale_cache[L]
    clone = {}
    meshes = {}      # clone -> Mesh (vertex edits), created on demand
    kind = {}        # clone -> "base" / "core" (donor core mesh) / "adopt" / "extra" (donor extra)
    layer_of = {}    # clone -> Layer
    donor_rec = {}   # clone -> donor Rec (core swaps, extras)
    for r, L in plan:
        n = Rec(r.hdr, r.chain)
        n.src = base
        clone[r] = n
        kind[n] = "base"
        if L is None:
            if r.mesh:
                n.chain = remap(base, r.chain)
        elif r.node in CORE and not bm.is_main(r):
            hide(n, r)
            hidden.add(r.node)
        elif r.node in CORE:
            src = L.src
            dmeshes = [x for x in src.model.dups.get(r.node, ()) if x.mesh]
            dr = src.model.by_node.get(r.node)
            if dmeshes and not (r.node in ADOPT and adopt_ok(bm, src.model, r.node)):
                dr = dmeshes[0]
                extra_dups.append((r, L, dmeshes[1:]))
            layer_of[n] = L
            if r.node in ADOPT and dr is not None and adopt_ok(bm, src.model, r.node):
                # the donor's bone (rest) from the base parent's place: origin = parent origin + rest, so the
                # skeleton adds up even where the donor's own origins do not (BT4 models)
                ps = body_scale(L, r.node)
                pnew = adopted.get(r.parent.node, r.parent.ofsA) if r.parent is not None else (0.0, 0.0, 0.0)
                onew = v_add(pnew, tuple(ps * x for x in dr.vec(0x30)))
                adopted[r.node] = onew
                d = v_sub(onew, tuple(ps * x for x in dr.ofsA))
                dB_ = v_sub(pnew, tuple(ps * x for x in dr.ofsB))
                hdr = bytearray(dr.hdr[:0x40]) + (bytearray(dr.hdr[0x40:0x60]) if dr.mesh else bytearray(0x20))
                hdr[0:8] = r.hdr[0:8]
                struct.pack_into("<3f", hdr, 0x10, *onew)
                struct.pack_into("<3f", hdr, 0x20, *pnew)
                struct.pack_into("<3f", hdr, 0x30, *v_sub(onew, pnew))
                if len(r.hdr) >= 0x60:
                    hdr[0x5C:0x60] = r.hdr[0x5C:0x60]
                struct.pack_into("<H", hdr, 0xC, struct.unpack_from("<H", dr.hdr, 0xC)[0] & fade_mask)
                struct.pack_into("<H", hdr, 8, dr.enabled if dr.mesh else 0)
                n.hdr = hdr
                n.enabled = dr.enabled if dr.mesh else 0
                if dr.mesh:
                    n.chain = remap(src, dr.chain)
                    meshes[n] = Mesh(n.chain)
                    if ps != 1.0:
                        meshes[n].apply(((ps, 0, 0), (0, ps, 0), (0, 0, ps)), IDENT3, (0, 0, 0), (0, 0, 0))
                    meshes[n].shift(d, dB_)
                    kind[n], donor_rec[n] = "adopt", dr
                else:
                    n.chain = empty_chain()
                    hidden.add(r.node)
                if r.node == 4:
                    notes.append("tail: nodes 4..7 take the donor's bone lengths")
                if r.node == 0x31:
                    notes.append("head: nodes 0x31..0x33 take the donor's bone offsets")
            elif dr is not None and dr.mesh:
                ps = body_scale(L, r.node)
                dA = v_sub(r.ofsA, tuple(ps * x for x in dr.ofsA))
                dB = v_sub(r.ofsB, tuple(ps * x for x in dr.ofsB))
                hdr = bytearray(r.hdr[:0x40]) + bytearray(dr.hdr[0x40:0x60])
                if len(r.hdr) >= 0x60:
                    hdr[0x5C:0x60] = r.hdr[0x5C:0x60]
                struct.pack_into("<H", hdr, 8, dr.enabled)
                struct.pack_into("<H", hdr, 0xC, struct.unpack_from("<H", dr.hdr, 0xC)[0] & fade_mask)
                n.hdr = hdr
                n.enabled = dr.enabled
                n.chain = remap(src, dr.chain)
                meshes[n] = Mesh(n.chain)
                if ps != 1.0:   # the donor scaled to the base's size first: the joint offsets that remain are small
                    meshes[n].apply(((ps, 0, 0), (0, ps, 0), (0, 0, ps)), IDENT3, (0, 0, 0), (0, 0, 0))
                meshes[n].shift(dA, dB)
                kind[n], donor_rec[n] = "core", dr
            else:
                hide(n, r)
                hidden.add(r.node)
        else:
            hide(n, r)
            hidden.add(r.node)
    for r in bm.recs:
        n = clone[r]
        n.parent = clone[r.parent] if r.parent is not None else None
        n.children = [clone[c] for c in r.children]
    by_node = {n.node: clone[r] for r in bm.recs for n in [clone[r]] if bm.is_main(r)}
    # the donor's further mesh records of a swapped node: leaf records in front of the node's own, with the base's
    # skeleton values (the pose walk takes rest from every record) and the donor's mesh
    for r, L, more in extra_dups:
        main = clone[r]
        par = main.parent
        for x in more:
            n = Rec(r.hdr[:0x40] + (r.hdr[0x40:0x60] if len(r.hdr) >= 0x60 else bytes(0x20)), b"")
            n.hdr = bytearray(r.hdr[:0x40]) + bytearray(x.hdr[0x40:0x60])
            if len(r.hdr) >= 0x60:
                n.hdr[0x5C:0x60] = r.hdr[0x5C:0x60]
            struct.pack_into("<H", n.hdr, 8, x.enabled)
            struct.pack_into("<H", n.hdr, 0xC, struct.unpack_from("<H", x.hdr, 0xC)[0] & fade_mask)
            n.src, n.node, n.enabled = base, r.node, x.enabled
            n.chain = remap(L.src, x.chain)
            meshes[n] = Mesh(n.chain)
            ps = body_scale(L, r.node)
            if ps != 1.0:
                meshes[n].apply(((ps, 0, 0), (0, ps, 0), (0, 0, ps)), IDENT3, (0, 0, 0), (0, 0, 0))
            meshes[n].shift(v_sub(r.ofsA, tuple(ps * c for c in x.ofsA)), v_sub(r.ofsB, tuple(ps * c for c in x.ofsB)))
            kind[n], layer_of[n], donor_rec[n] = "core", L, x
            n.parent = par
            if par is not None:
                par.children.insert(par.children.index(main), n)

    def all_of(node):
        """Every record clone that carries `node` (duplicates move together)."""
        return [n for n in kind if n.node == node]

    def follow(n):
        for c in n.children:
            if not c.enabled and c.node in hidden and c.node not in CORE:   # core origins are the base's
                po = struct.unpack_from("<3f", n.hdr, 0x10)
                rest = struct.unpack_from("<3f", c.hdr, 0x30)
                struct.pack_into("<3f", c.hdr, 0x10, po[0] + rest[0], po[1] + rest[1], po[2] + rest[2])
                struct.pack_into("<3f", c.hdr, 0x20, *po)
            follow(c)
    for r in bm.recs:
        if r.parent is None:
            follow(clone[r])

    def origin(n):
        return struct.unpack_from("<3f", n.hdr, 0x10)

    def mesh_of(n):
        if n not in meshes and n.enabled and n.chain:
            meshes[n] = Mesh(n.chain)
        return meshes.get(n)

    # donor extras (joint offset only for now; fitting below)
    renum = []       # (src, old id, new id)
    joints = {}      # extra clone -> origin (moves with the workshop)
    for L, dr, attach in adds:
        if (L.k, dr.node) not in accepted:
            continue
        parent_clone = clone[attach]
        d = v_sub(origin(parent_clone), dr.parent.ofsA)

        def copy(x, par):
            nid = accepted[(L.k, x.node)]
            renum.append((L.src, x.node, nid))
            n = Rec(x.hdr, x.chain)
            n.src, n.node, n.hdr = L.src, nid, bytearray(x.hdr)
            struct.pack_into("<H", n.hdr, 0xA, nid)
            add3(n.hdr, 0x10, d)
            add3(n.hdr, 0x20, d)
            struct.pack_into("<H", n.hdr, 0xC, struct.unpack_from("<H", n.hdr, 0xC)[0] & fade_mask)
            kind[n], layer_of[n], donor_rec[n] = "extra", L, x
            if x.mesh:
                n.chain = remap(L.src, x.chain)
                meshes[n] = Mesh(n.chain)
                meshes[n].shift(d, d)
            n.parent = par
            par.children.append(n)
            for c in x.children:
                if c.node not in ACCESSORY and c.node not in CORE and (L.k, c.node) in accepted:
                    copy(c, n)
        copy(dr, parent_clone)

    def descendants(n):
        out = [n]
        for c in n.children:
            out += descendants(c)
        return out

    for n in clone.values():
        if n.node not in CORE:
            joints[n] = origin(n)
    for n in kind:
        if kind[n] == "extra":
            joints[n] = origin(n)

    moved = {n for n in kind if kind[n] == "extra"}

    def move_subtree(n, M, Ni, t, pivot, nodes=None):
        """Applies p -> M (p - pivot) + pivot + t to the meshes and joints of n and everything below it."""
        for x in (nodes if nodes is not None else descendants(n)):
            m = mesh_of(x)
            if m is not None:
                m.apply(M, Ni, t, pivot)
            if x in joints:
                q = m_vec(M, v_sub(joints[x], pivot))
                joints[x] = (q[0] + pivot[0] + t[0], q[1] + pivot[1] + t[1], q[2] + pivot[2] + t[2])
                moved.add(x)

    def hide_clone(n):
        n.enabled = 0
        struct.pack_into("<H", n.hdr, 8, 0)
        if len(n.hdr) < 0x60:
            n.hdr = bytearray(n.hdr) + bytearray(0x20)
        n.chain = empty_chain()
        meshes.pop(n, None)
        hidden.add(n.node)

    stats = {"fit_nodes": 0, "pushed": 0, "hair_scaled": 0, "build": 0, "length": 0, "head": 0, "feet": 0,
             "extras": 0, "snapped": 0, "bridged": 0, "capped": 0}
    if fit:
        _fit(bm, plan, clone, by_node, kind, layer_of, donor_rec, meshes, all_of, origin, layers, move_subtree,
             mesh_of, stats, extra_layer, scale_cache)

    if fit:
        _hems(bm, by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats)

    # ---- (b) swapped core pieces: cross-section matched to the base's at the seams
    if declip:
        for r, L in plan:
            n = clone[r]
            if kind.get(n) != "core" or not r.mesh or r.node in (0x2F, 0x30, 0x31, 0x32, 0x33):
                continue
            ax = axis_of(by_node, r.node, lambda k: origin(by_node[k]))
            if ax is None:
                continue
            ra0, ra1 = end_radii(bm.node_points(r.node), ax)
            da0, da1 = end_radii([p for x in all_of(r.node) if x in meshes and kind.get(x) == "core"
                                  for p in meshes[x].P], ax)
            if not (ra0 and da0):
                continue
            lo_, hi_ = (0.6, 1.6) if fit else (0.75, 1.33)
            # damped, with a dead zone: the radius samples of a small mesh are noisy
            soft = lambda x: 1.0 if abs(x - 1) < 0.08 else max(lo_, min(hi_, 1 + (x - 1) * 0.7))
            s0 = soft(ra0 / da0)
            s1 = soft(ra1 / da1) if (ra1 and da1) else s0
            if abs(s0 - 1) > 0.02 or abs(s1 - 1) > 0.02:
                for x in all_of(r.node):
                    if x in meshes and kind.get(x) == "core":
                        radial_scale(meshes[x], ax, s0, s1)
                stats["fit_nodes"] += 1

    if fit:
        _snap(bm, by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats)

    # ---- workshop on the core pieces (mesh only: the base's joints stay where the game expects them)
    def pivot_of_core(L, node):
        """The topmost joint of the piece above `node` (torso: pelvis, arms: each upper arm)."""
        x = by_node[node]
        while x.parent is not None and x.parent.node in REGIONS.get(L.piece, ()):
            x = x.parent
        return origin(x)

    for L in layers:
        lo = {k: v for k, v in L.opts.items() if k != "nodes"}
        for n in [n for n in kind if layer_of.get(n) is L and kind[n] in ("core", "adopt")]:
            if L.opts.get("shown", True) is False:   # (the whole layer taken off)
                hide_clone(n)
                continue
            if not is_identity(lo) and n in meshes:
                M, Ni, t = ui_xform(lo)
                meshes[n].apply(M, Ni, t, pivot_of_core(L, n.node), weighted=_is_piece_root(L, by_node, n.node))
            o = node_key(L.opts.get("nodes") or {}, donor_rec[n].node)
            if o is None:
                continue
            if o.get("shown", True) is False:
                hide_clone(n)
            elif not is_identity(o) and n in meshes:
                M, Ni, t = ui_xform(o)
                meshes[n].apply(M, Ni, t, origin(n), weighted=True)
    regions = adjust.get("regions") if isinstance(adjust.get("regions"), dict) else {}
    for reg, o in regions.items():   # the base's own core nodes of a piece, all at once
        if not isinstance(o, dict) or reg not in REGIONS:
            continue
        mine = [n for n in kind if kind[n] == "base" and n.node in REGIONS[reg] and n.enabled]
        if o.get("shown", True) is False:
            for n in mine:
                hide_clone(n)
        elif not is_identity(o):
            M, Ni, t = ui_xform(o)
            for n in mine:
                x = n
                while x.parent is not None and x.parent.node in REGIONS[reg]:
                    x = x.parent
                if mesh_of(n) is not None:
                    mesh_of(n).apply(M, Ni, t, origin(x), weighted=n.parent is None or n.parent.node not in REGIONS[reg])
    for n in [n for n in kind if kind[n] == "base"]:
        if n.node not in CORE:
            continue
        o = base_opts(adjust, n.node)
        if o is None:
            continue
        if o.get("shown", True) is False:
            if n.enabled:
                hide_clone(n)
        elif not is_identity(o) and mesh_of(n) is not None:
            M, Ni, t = ui_xform(o)
            mesh_of(n).apply(M, Ni, t, origin(n), weighted=True)

    # ---- build: thickness around the bones (joints stay); "head" scales the head and what is on it
    build = adjust.get("build") or {}
    head_nodes = [n for k in (0x2F, 0x30, 0x31, 0x32, 0x33) for n in all_of(k)]
    head_build = None
    for reg, pct in build.items():
        f = max(0.3, min(3.0, float(pct) / 100.0))
        if reg not in REGIONS or abs(f - 1) < 1e-3:
            continue
        if reg == "head":
            pts = [p for h in head_nodes if mesh_of(h) for p in mesh_of(h).P]
            if not pts:
                continue
            c = origin(by_node[0x2F]) if 0x2F in by_node else                 tuple((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 for k in range(3))
            S = ((f, 0, 0), (0, f, 0), (0, 0, f))   # about the head joint: the head stays on the neck
            for h in head_nodes:
                if mesh_of(h):
                    mesh_of(h).apply(S, IDENT3, (0, 0, 0), c)
            head_build = (S, c)
            stats["build"] += 1
            continue
        for node in REGIONS[reg]:
            ax = axis_of(by_node, node, lambda k: origin(by_node[k])) if node in by_node else None
            for n in all_of(node):
                m = mesh_of(n)
                if m is not None and ax is not None:
                    radial_scale(m, ax, f)
        stats["build"] += 1

    # ---- (a) hair from another head: scaled and centred onto the (final) head
    final_head_src = head_src or base
    refit = [L for L in layers if L.region == "hair" and L.src is not final_head_src]
    shell = None
    if declip:
        pts = [p for h in head_nodes if mesh_of(h) for p in mesh_of(h).P]
        shell = HeadShell(pts) if len(pts) > 30 else None
    if shell is not None:
        for L in refit:
            mine = [n for n in kind if kind[n] == "extra" and layer_of.get(n) is L]
            # reference: the face (skin, no hair cap) when both have one, else the head mesh
            bface = [p for x in all_of(0x30) if mesh_of(x) is not None and x.enabled for p in mesh_of(x).P]
            dface = L.src.model.node_points(0x30)
            if bface and dface:
                dpts, bpts = dface, bface
            else:
                dpts = L.src.model.node_points(0x2F)
                bpts = [p for x in all_of(0x2F) if mesh_of(x) is not None and x.enabled for p in mesh_of(x).P]
            if not mine or not dpts or not bpts:
                continue
            dlo = [min(p[k] for p in dpts) for k in range(3)]
            dhi = [max(p[k] for p in dpts) for k in range(3)]
            blo = [min(p[k] for p in bpts) for k in range(3)]
            bhi = [max(p[k] for p in bpts) for k in range(3)]
            bc = tuple((blo[k] + bhi[k]) / 2 for k in range(3))
            s = head_ratio(bpts, dpts) or 1.0
            M = ((s, 0, 0), (0, s, 0), (0, 0, s))
            for n in [n for n in mine if layer_of.get(n.parent) is not L]:
                # where the donor's head centre is after the joint offset this root got
                dpar = donor_rec[n].parent
                off = v_sub(origin(n.parent), dpar.ofsA) if dpar is not None else (0, 0, 0)
                dc = v_add(tuple((dlo[k] + dhi[k]) / 2 for k in range(3)), off)
                move_subtree(n, M, IDENT3, v_sub(bc, dc), dc)
            if abs(s - 1) > 0.02:
                stats["hair_scaled"] += 1
    # the head build for what hangs on the head and was not fitted to it
    if head_build is not None:
        S, c = head_build
        neck = by_node.get(0x2E)
        for n in list(kind):
            if n.node in CORE or n.parent is None or n.parent.node not in (0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33):
                continue
            if kind.get(n) == "extra" and layer_of.get(n) in refit and shell is not None:
                continue
            move_subtree(n, S, IDENT3, (0, 0, 0), c)

    # ---- workshop on extras (the layer around its attach joint, per node around the node; children follow)
    def add_copies(opts, k, of):
        """The workshop's duplicates of strands (opts["copies"]): a new leaf record beside the strand, with its
        mesh as it is now (before the strand's own edits) and the copy's own edits."""
        for ci, c in enumerate(opts.get("copies") or []):
            if not isinstance(c, dict) or c.get("shown", True) is False:
                continue
            try:
                node = int(c["node"], 0) if isinstance(c.get("node"), str) else int(c.get("node"))
            except (KeyError, TypeError, ValueError):
                continue
            n = of.get(node)
            if n is None or n.parent is None or not n.enabled or not n.chain:
                continue
            if not pool or len(kind) >= cap:
                notes.append(f"a copy of node 0x{node:X} left out: no record or node id left")
                continue
            nid = pool.pop(0)
            used_ids.add(nid)
            m0 = mesh_of(n)
            x = Rec(n.hdr, m0.bytes() if m0 is not None else n.chain)
            x.hdr = bytearray(n.hdr)
            struct.pack_into("<H", x.hdr, 0xA, nid)
            x.node, x.src, x.enabled = nid, n.src, n.enabled
            meshes[x] = Mesh(x.chain)
            kind[x], layer_of[x], donor_rec[x] = "copy", layer_of.get(n), donor_rec.get(n)
            x.parent = n.parent
            n.parent.children.insert(n.parent.children.index(n) + 1, x)
            joints[x] = joints.get(n, origin(n))
            moved.add(x)
            o = {a: b for a, b in c.items() if a not in ("node", "shown")}
            if not is_identity(o):
                M, Ni, t = ui_xform(o)
                move_subtree(x, M, Ni, t, joints[x], nodes=[x])
            if ids is not None:
                ids[("copy", k, ci)] = nid

    for L in layers:
        lo = {k: v for k, v in L.opts.items() if k not in ("nodes", "copies")}
        mine = [n for n in kind if kind[n] == "extra" and layer_of.get(n) is L]
        if L.opts.get("shown", True) is False:   # (the whole layer taken off)
            for n in mine:
                hide_clone(n)
            continue
        if not is_identity(lo):
            M, Ni, t = ui_xform(lo)
            for n in [n for n in mine if layer_of.get(n.parent) is not L]:
                move_subtree(n, M, Ni, t, origin(n.parent))
        add_copies(L.opts, L.k, {donor_rec[n].node: n for n in mine if n in donor_rec})
        for n in mine:
            o = node_key(L.opts.get("nodes") or {}, donor_rec[n].node)
            if o is None:
                continue
            if o.get("shown", True) is False:
                for x in descendants(n):
                    hide_clone(x)
            elif not is_identity(o):
                M, Ni, t = ui_xform(o)
                move_subtree(n, M, Ni, t, joints.get(n, origin(n)))
    for reg, o in regions.items():   # the base's own extras of a piece, all at once ("head": its hair too)
        if not isinstance(o, dict):
            continue
        regs = {"hair"} | ({"head"} if reg == "head" else set()) if reg in ("hair", "head") else {reg}
        mine = [n for r, n in clone.items() if n.node not in CORE and bm.region(r) in regs and kind.get(n) == "base"]
        roots = [n for n in mine if n.parent is not None and n.parent not in mine]
        if o.get("shown", True) is False:
            for n in mine:
                if n.enabled:
                    hide_clone(n)
        elif not is_identity(o):
            M, Ni, t = ui_xform(o)
            for n in roots:
                move_subtree(n, M, Ni, t, origin(n.parent))
    add_copies(adjust, 0, {n.node: n for n in kind if kind[n] == "base" and n.node not in CORE})
    for n in [n for n in kind if kind[n] == "base"]:
        if n.node in CORE:
            continue
        o = base_opts(adjust, n.node)
        if o is None:
            continue
        if o.get("shown", True) is False:
            for x in descendants(n):
                if x.enabled:
                    hide_clone(x)
        elif not is_identity(o):
            M, Ni, t = ui_xform(o)
            move_subtree(n, M, Ni, t, joints.get(n, origin(n)))

    # ---- (a) / (c) push fitted hair out of the head; later layers a little further out
    if shell is not None:
        for rank, L in enumerate(refit):
            margin = shell.radius * (0.02 + 0.015 * rank)
            grow = 1.0 + 0.008 * rank
            for n in [n for n in kind if kind[n] == "extra" and layer_of.get(n) is L and n in meshes]:
                stats["pushed"] += shell.push(meshes[n], margin, grow)
    for k, text in (("length", "{} swapped part(s) stretched or shortened to the base's bone lengths"),
                    ("head", "the donor head scaled by {:.2f} to the base's head size"),
                    ("feet", "{} foot / feet fitted to the base's ankle and ground level"),
                    ("extras", "{} hanging part(s) (capes, belts, tails) scaled with the body size"),
                    ("hems", "{} open end(s) of swapped parts moved to where the base's own parts ended"),
                    ("snapped", "{} seam point(s) snapped shut where pieces meet")):
        if stats.get(k):
            notes.append("fit: " + text.format(stats[k]))
    if stats["fit_nodes"]:
        notes.append(f"declip: {stats['fit_nodes']} swapped part(s) scaled to the base's thickness at the joints")
    if stats["hair_scaled"]:
        notes.append(f"declip: {stats['hair_scaled']} hair layer(s) scaled to the base's head")
    if stats["pushed"]:
        notes.append(f"declip: {stats['pushed']} hair vertices pushed out of the head")

    patch = adjust.get("patch", True) is not False   # (the workshop's "Patch holes": the strips and caps)
    bridges = _bridge(by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats, notes) if fit and patch else {}
    if fit and patch:
        for n_, bs_ in _cap_exposed(bm, clone, kind, meshes, mesh_of, stats).items():
            bridges.setdefault(n_, []).extend(bs_)
    if stats.get("bridged"):
        notes.append(f"fit: {stats['bridged']} gap(s) between pieces of different characters bridged with a strip")
    if stats.get("capped"):
        notes.append(f"fit: {stats['capped']} opening(s) left by hidden or swapped parts closed with a cap")

    # ---- write meshes, joints, bounds back into the records
    for n, m in meshes.items():
        if not n.enabled or not m.dirty:
            continue
        n.chain = m.bytes()
        bb = m.bbox()
        if bb and len(n.hdr) >= 0x60:
            lo, hi = bb
            w5c = n.hdr[0x5C:0x60]
            struct.pack_into("<3f", n.hdr, 0x40, *((lo[k] + hi[k]) / 2 for k in range(3)))
            struct.pack_into("<f", n.hdr, 0x4C, 1.0)
            struct.pack_into("<3f", n.hdr, 0x50, *((hi[k] - lo[k]) / 2 for k in range(3)))
            n.hdr[0x5C:0x60] = w5c

    for n, bs in bridges.items():
        base_chain = meshes[n].bytes() if n in meshes else n.chain
        n.chain = _append_batches(base_chain, bs)
        add = sum((len(b) - 96) // 48 for b in bs)
        n.enabled = min(0xFFFF, n.enabled + max(0, add))
        struct.pack_into("<H", n.hdr, 8, n.enabled)

    def fix_joints(n):
        for c in n.children:
            if c in joints and (c in moved or n in moved):
                o = joints[c]
                po = origin(n)
                struct.pack_into("<3f", c.hdr, 0x10, *o)
                struct.pack_into("<3f", c.hdr, 0x20, *po)
                struct.pack_into("<3f", c.hdr, 0x30, *v_sub(o, po))
            fix_joints(c)
    for r in bm.recs:
        if r.parent is None:
            fix_joints(clone[r])

    # ---- serialize depth first, pops from the depths
    order = []

    def dfs(n):
        order.append(n)
        for c in n.children:
            dfs(c)
    for r in bm.recs:
        if r.parent is None:
            dfs(clone[r])
    if order[-1] is not clone[bm.recs[-1]]:
        fail("internal: the base's last record moved")
    depths = {id(n): depth(n) for n in order}
    body = bytearray()
    for k, n in enumerate(order):
        last = k + 1 == len(order)
        if not last:
            nxt = order[k + 1]
            pop = depths[id(n)] - (depths[id(nxt.parent)] if nxt.parent is not None else -1)
        else:
            pop = bm.last_pop
        hdr = bytearray(n.hdr)
        if last:
            hdr, chain = hdr[:0x40], b""
        else:
            chain = n.chain if n.enabled or len(hdr) > 0x40 else b""
            if len(hdr) < 0x60 and chain:
                fail("internal: chain on a short record")
        ln = len(hdr) + len(chain) if not last else struct.unpack_from("<i", n.hdr, 0)[0]
        struct.pack_into("<iHH", hdr, 0, ln, pop, 1 if last else 0)
        body += hdr + chain
    head = bytearray(bm.head)
    struct.pack_into("<I", head, 8, len(order))
    face_new = new_index(face_src, face_src.faceTex)
    if face_new is None:
        fail("the face texture did not make it into the texture file")
    head[0x4C] = face_new
    head[0x4E] = 0
    head[0x4F] = len(mats)
    head[0x50] = clut_first
    head[0x51] = len(ramps)
    if base.fadeCount:
        v = new_index(base, base.fadeFirst)
        if v is None:   # the fade range did not survive: no fade-pass uploads
            head[0x52], head[0x53] = 0, 0
        else:
            head[0x52] = v
    if base.fadeTex:
        v = new_index(base, base.fadeTex)
        head[0x4D] = v if v is not None else 0
    if base.noAlpha:
        v = new_index(base, base.noAlpha)
        head[0x54] = v if v is not None else 0
    if head_src is not None:
        fl = struct.unpack_from("<I", head, 0xC)[0]
        struct.pack_into("<I", head, 0xC, (fl & ~0xC00000) | (head_src.flags & 0xC00000))
    new_end = len(head) + len(body)
    tail = bytearray(bm.tail)
    seam_on = bool(bm.seamOfs)
    if seam_layer is not None:
        seam_on = False
        sec = _seam_from(seam_layer, bm, by_node, kind, layer_of, meshes, maps)
        if bm.seamOfs:   # the base's own seam section is replaced
            tail = bytearray(tail[:bm.seamOfs - bm.end])
        tail += b"\0" * (-(new_end + len(tail)) % 16)
        so = new_end + len(tail)
        vc_, tc_ = struct.unpack_from("<2i", sec, 0)
        struct.pack_into("<4i", sec, 0, vc_, tc_, so + 16, so + 16 + vc_ * 0x70)
        tail += sec + b"\0" * 32
        struct.pack_into("<i", head, 0x70, so)
        notes.append(f"fit: the seam patch of layer {seam_layer.k}'s donor closes the joints of its piece")
    if seam_on:
        delta = new_end - bm.end
        so = bm.seamOfs - bm.end
        struct.pack_into("<i", head, 0x70, bm.seamOfs + delta)
        vc, tc, vo, to = struct.unpack_from("<4i", tail, so)
        struct.pack_into("<4i", tail, so, vc, tc, vo + delta, to + delta)
        tm, rm = maps(base)
        for i in range(tc):
            b = to - bm.end + i * 0x20 + 0x10
            t0, t1 = struct.unpack_from("<QQ", tail, b)
            key = (t0 & 0x3FFF, (t0 >> 37) & 0x3FFF)
            if key in tm:
                a, c, bits = tm[key][:3]
                if bits is not None:
                    t0 = (t0 & ~(0xFFFFF << 14)) | (bits & (0xFFFFF << 14))
                t0 = (t0 & ~0x3FFF & 0xFFF8001FFFFFFFFF) | a | (c << 37)
            r = (t1 >> 37) & 0x3FFF
            if r in rm:
                t1 = (t1 & 0xFFF8001FFFFFFFFF) | (rm[r] << 37)
            struct.pack_into("<QQ", tail, b, t0, t1)
    pmdl = bytes(head + body + tail)

    parts = list(base.parts)
    parts[E_MODEL - 1] = pmdl
    parts[E_TEX - 1] = build_texfile(out_texs, base.texeo)
    # the face chains (mouth shapes) get the face mesh's own vertex edits
    face_clone = by_node.get(0x30)
    for n_ in E_MOUTH_A:
        c = entry(face_src.parts, n_)
        if not c:
            parts[n_ - 1] = b""
            continue
        c = remap(face_src, c)
        m = Mesh(c)
        if face_clone in meshes:
            m.replay(meshes[face_clone].log)
        elif head_src is not None:
            d30 = head_src.model.by_node.get(0x30)
            if d30 is not None:
                m.shift(v_sub(bm.by_node[0x30].ofsA, d30.ofsA), v_sub(bm.by_node[0x30].ofsB, d30.ofsB))
        parts[n_ - 1] = m.bytes()
    if head_src is not None:
        for n_ in [E_EYES] + E_MOUTH_B:
            parts[n_ - 1] = entry(head_src.parts, n_)
        notes.append("face: mouth shapes, eye frames and mouth textures from the head's donor")
    hidden = {i for i in hidden if not any(c.enabled for c in order if c.node == i)}
    parts[E_CHAIN - 1] = merge_chains(base, order, renum, hidden, notes)
    return join(parts)


def node_key(d, node):
    """d[node] with the key as an int or a hex / decimal string."""
    for k in (node, hex(node), "0x%X" % node, "0x%02X" % node, str(node)):
        if k in d:
            return d[k]
    return None


def base_opts(adjust, node):
    """The workshop's edits of a base node: adjust[node], or adjust["nodes"][node] (as the creators write them, like a
    layer's opts)."""
    o = node_key(adjust, node)
    return o if o is not None else node_key(adjust.get("nodes") or {}, node)


def head_ratio(bpts, dpts):
    """How much to scale a donor head (or hair) to the base's: from the face's width x height, with a dead zone
    (faces of the same build differ by up to ~12 % in shape) and damped, so a character keeps some of its look."""
    if not bpts or not dpts:
        return None
    bx, by = _extent(bpts, 0), _extent(bpts, 1)
    dx, dy = _extent(dpts, 0), _extent(dpts, 1)
    if min(bx, by, dx, dy) < 1e-3:
        return None
    s = math.sqrt((bx * by) / (dx * dy))
    if abs(s - 1) < 0.12:
        return 1.0
    return max(0.6, min(1.7, s ** 0.7))


def _height(model):
    """Head joint to right foot joint: the body size the fit compares."""
    h, f = model.by_node.get(0x2F), model.by_node.get(0x0B)
    if h is None or f is None:
        return None
    v = abs(h.ofsA[1] - f.ofsA[1])
    return v if v > 1e-3 else None


def _extent(points, k):
    return (max(p[k] for p in points) - min(p[k] for p in points)) if points else 0.0


def _fit(bm, plan, clone, by_node, kind, layer_of, donor_rec, meshes, all_of, origin, layers, move_subtree,
         mesh_of, stats, extra_layer, layer_scale=None):
    """Automatic alignment of donor pieces to the base's proportions (before the thickness match)."""
    HEAD = (0x2F, 0x30, 0x31, 0x32, 0x33)
    layer_scale = layer_scale or {}
    swapped = {}
    for n in kind:
        if kind[n] == "core" and n.enabled:
            swapped.setdefault(n.node, []).append(n)
    # bone length: each swapped node's mesh spans the base's joint-to-joint distance
    for node, ns in swapped.items():
        if node in HEAD or node in (0x0B, 0x0F) or node not in by_node:
            continue
        L = layer_of.get(ns[0])
        dm = L.src.model
        b = by_node[node]
        jb = origin(b)
        best, bl = None, 0.0
        for c in b.children:
            if c.node in CORE and c.node not in FIXED and c.node in by_node and by_node[c.node] is c:
                l = v_len(v_sub(origin(c), jb))
                if l > bl:
                    best, bl = c, l
        dN, dC = dm.by_node.get(node), dm.by_node.get(best.node) if best is not None else None
        if best is None or bl < 0.05 or dN is None or dC is None:
            continue
        eb = origin(best)
        u = tuple(x / bl for x in v_sub(eb, jb))
        sc = layer_scale.get(L, 1.0)
        ed = v_add(jb, tuple(sc * x for x in v_sub(dC.ofsA, dN.ofsA)))
        ld = v_dot(v_sub(ed, jb), u)
        if ld < 0.05 * bl:
            continue
        k = max(0.4, min(2.5, bl / ld))

        def ratio(bn, bc, dn_, dc_):
            if bn is None or bc is None or dn_ is None or dc_ is None:
                return k
            l1, l2 = v_len(v_sub(origin(bc), origin(bn))), sc * v_len(v_sub(dc_.ofsA, dn_.ofsA))
            return max(0.4, min(2.5, l1 / l2)) if l1 > 0.05 and l2 > 0.05 else k
        # behind the joint: the parent bone's ratio; past the far joint: the next bone's
        bp = b.parent if b.parent is not None and b.parent.node in CORE else None
        k0 = ratio(bp, b, dm.by_node.get(bp.node) if bp else None, dN)
        nxt, nl = None, 0.0
        for c in best.children:
            if c.node in CORE and c.node in by_node and by_node[c.node] is c:
                l = v_len(v_sub(origin(c), eb))
                if l > nl:
                    nxt, nl = c, l
        k2 = ratio(best, nxt, dC, dm.by_node.get(nxt.node) if nxt else None) if nxt is not None else k
        # a dead zone (pieces of similar builds stay as they are) and half strength outside the bone itself
        # (collars, shoulder pads, knee caps hang past the joints and should not be dragged along)
        dz = lambda x: 1.0 if abs(x - 1) < 0.1 else x
        cap = lambda x: max(0.8, min(1.25, x))
        k, k0, k2 = dz(k), dz(cap(1 + (k0 - 1) * 0.5)), dz(cap(1 + (k2 - 1) * 0.5))
        if max(abs(k - 1), abs(k0 - 1), abs(k2 - 1)) > 0.03:
            for n in ns:
                if n in meshes:
                    meshes[n].axial(jb, u, k, ld, k0, k2)
            stats["length"] += 1
    # head: the donor's head to the size of the base's (a child's head on an adult and the other way round)
    hl = next((layer_of[n] for n in kind if kind[n] in ("core", "adopt") and n.node == 0x2F and n.enabled), None)
    if hl is not None and 0x2F in by_node:
        dm = hl.src.model
        ref = 0x30 if bm.node_points(0x30) and dm.node_points(0x30) else 0x2F
        s = head_ratio(bm.node_points(ref), dm.node_points(ref))
        if s is not None:
            if abs(s - 1) > 0.03:
                S = ((s, 0, 0), (0, s, 0), (0, 0, s))
                pv = origin(by_node[0x2F])
                for n in kind:
                    if n.node in HEAD and kind[n] in ("core", "adopt") and layer_of.get(n) is hl and n in meshes:
                        meshes[n].apply(S, IDENT3, (0, 0, 0), pv)
                hair = extra_layer.get("hair")
                if hair is hl:
                    for n in [n for n in kind if kind[n] == "extra" and layer_of.get(n) is hl and
                              layer_of.get(n.parent) is not hl]:
                        move_subtree(n, S, IDENT3, (0, 0, 0), pv)
                stats["head"] = s
    # feet: to the base's ankle and ground level (game Y points down: the ground is the largest Y)
    for node in (0x0B, 0x0F):
        ns = [n for n in swapped.get(node, ()) if n in meshes]
        if not ns or node not in by_node:
            continue
        bp = bm.node_points(node)
        dp = [p for n in ns for p in meshes[n].P]
        if not bp or not dp:
            continue
        j = origin(by_node[node])
        gb, gd = max(p[1] for p in bp), max(p[1] for p in dp)
        ky = max(0.6, min(1.67, (gb - j[1]) / (gd - j[1]))) if gd - j[1] > 0.05 and gb - j[1] > 0.05 else 1.0
        kx = max(0.6, min(1.67, _extent(bp, 0) / max(1e-3, _extent(dp, 0))))
        kz = max(0.6, min(1.67, _extent(bp, 2) / max(1e-3, _extent(dp, 2))))
        if max(abs(ky - 1), abs(kx - 1), abs(kz - 1)) > 0.03:
            S = ((kx, 0, 0), (0, ky, 0), (0, 0, kz))
            Si = ((1 / kx, 0, 0), (0, 1 / ky, 0), (0, 0, 1 / kz))
            for n in ns:
                meshes[n].apply(S, Si, (0, 0, 0), j)
            stats["feet"] += 1
    # capes, belt ends, tails and other hanging extras: with the body size, around where they hang
    hb = _height(bm)
    for L in layers:
        if L.region == "hair":
            continue
        hd = _height(L.src.model)
        if not hb or not hd:
            continue
        r = max(0.5, min(2.0, hb / hd))
        if abs(r - 1) < 0.03:
            continue
        S = ((r, 0, 0), (0, r, 0), (0, 0, r))
        roots = [n for n in kind if kind[n] == "extra" and layer_of.get(n) is L and layer_of.get(n.parent) is not L]
        for n in roots:
            move_subtree(n, S, IDENT3, (0, 0, 0), origin(n.parent))
        stats["extras"] += len(roots)


def _span(points, j, u, lo=0.01, hi=0.99):
    """Where the points start and end along the axis (u through j), near-extremes (a stray vertex does not count)."""
    a = sorted(v_dot(v_sub(p, j), u) for p in points)
    if len(a) < 8:
        return None
    return a[int(lo * (len(a) - 1))], a[int(hi * (len(a) - 1))]


def _hems(bm, by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats):
    """A swapped piece's open ends to where the base's own piece ended: where a piece meets a part of another
    character (trousers on shoes, a sleeve on a glove, a waistband under a belt), its mesh is stretched or shortened
    along the bone so that it covers what the base's piece covered (a tall boot's short trouser leg reaches a short
    shoe, a child's high waistband goes back under the base's belt). The bone's middle stays put."""
    src_of = lambda n: layer_of.get(n) if kind.get(n) not in (None, "base") else None

    def other(x, lay):   # x's meshes come from another source than lay
        ms = [y for y in all_of(x.node) if y.enabled and mesh_of(y) is not None]
        return bool(ms) and src_of(ms[0]) is not lay

    for node, b in list(by_node.items()):
        if node not in CORE or node in FIXED or node in (0x2F, 0x30, 0x31, 0x32, 0x33):
            continue
        mine = [n for n in all_of(node) if kind.get(n) == "core" and n.enabled and n in meshes]
        if not mine:
            continue
        lay = layer_of.get(mine[0])
        bp = bm.node_points(node)
        ax = axis_of(by_node, node, lambda k: origin(by_node[k]))
        if not bp or ax is None:
            continue
        j = ax[0]
        L = v_len(v_sub(ax[1], j))
        if L < 0.05:
            continue
        u = tuple(x / L for x in v_sub(ax[1], j))
        # the neighbours at each end: parent and siblings at the joint, core children at the far end
        par = b.parent
        near = []
        if par is not None:
            near = [par] + [c for c in par.children if c is not b and c.node in CORE and c.node not in FIXED]
        far = [c for c in b.children if c.node in CORE and c.node not in FIXED]
        at_start = any(other(x, lay) for x in near)
        at_end = any(other(x, lay) for x in far)
        if not (at_start or at_end):
            continue
        bs = _span(bp, j, u)
        ds = _span([p for n in mine for p in meshes[n].P], j, u)
        if bs is None or ds is None:
            continue
        tol = 0.03 * L + 0.02
        do0 = at_start and abs(ds[0] - bs[0]) > tol
        do1 = at_end and abs(ds[1] - bs[1]) > tol
        if not (do0 or do1) or ds[1] - ds[0] < 0.1:
            continue
        # one linear map of the whole mesh along the bone: both ends to the base's, or one end with the other kept
        t0 = bs[0] if do0 else ds[0]
        t1 = bs[1] if do1 else ds[1]
        k = (t1 - t0) / (ds[1] - ds[0])
        if not 0.5 <= k <= 2.0:
            continue
        far_ = 10 * (ds[1] - ds[0]) + 10
        segs = [(ds[0] - far_, ds[1] + far_, t0 - far_ * k, t1 + far_ * k)]
        if segs:
            for n in mine:
                meshes[n].remap_axis(j, u, segs)
            stats["hems"] = stats.get("hems", 0) + len(segs)
    # the collar: a torso of another character than the neck reaches no higher than the base's own torso did (a
    # child's collar round the chin of an adult, a high coat collar over the face), what lies above the chest
    # joint squeezed down; game Y points down
    TOP = (0x11, 0x12, 0x20)
    tm = [n for nd in TOP for n in all_of(nd) if kind.get(n) == "core" and n.enabled and n in meshes]
    neck = by_node.get(0x2E)
    if tm and neck is not None and 0x12 in by_node and other(neck, layer_of.get(tm[0])):
        bp = [p for nd in TOP for p in bm.node_points(nd)]
        dp = [p for n in tm for p in meshes[n].P]
        if len(bp) >= 8 and len(dp) >= 8:
            anchor = origin(by_node[0x12])[1]
            b_top = sorted(p[1] for p in bp)[len(bp) // 100]
            d_top = sorted(p[1] for p in dp)[len(dp) // 100]
            lift_b, lift_d = anchor - b_top, anchor - d_top      # how far above the chest joint each reaches
            if lift_d > lift_b + 0.05 and lift_b > 0.05:
                k = max(0.4, lift_b / lift_d)
                far_ = 100.0
                for n in tm:
                    meshes[n].remap_axis((0.0, anchor, 0.0), (0.0, -1.0, 0.0), [(0.0, far_, 0.0, far_ * k)])
                stats["hems"] = stats.get("hems", 0) + 1


def _snap(bm, by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats):
    """Closes gaps where a swapped piece meets a piece of another source: the swapped mesh's open-edge points move
    onto the nearest open-edge point of the neighbouring mesh (parent or child node) when it is close. Skin weights
    stay as they are (both sides of a seam are weighted to the same two bones)."""
    border_cache = {}

    def border(mesh):
        if id(mesh) not in border_cache:
            groups, b = mesh.welded()
            border_cache[id(mesh)] = (groups, b)
        return border_cache[id(mesh)]
    done = set()
    for node, b in list(by_node.items()):
        if node not in CORE or node in (0x30, 0x31, 0x32, 0x33):
            continue
        mine = [n for n in all_of(node) if kind.get(n) == "core" and n.enabled and n in meshes]
        if not mine:
            continue
        lay = layer_of.get(mine[0])
        nbr = []
        for q in ([b.parent] if b.parent is not None else []) + [c for c in b.children if c.node in CORE]:
            for x in all_of(q.node):
                if not x.enabled or layer_of.get(x) is lay:
                    continue
                mm = mesh_of(x)
                if mm is not None:
                    nbr.append(mm)
        if not nbr:
            continue
        pts = []
        for mm in nbr:
            groups, bd = border(mm)
            pts += [mm.P[groups[g][0]] for g in bd]
        if not pts:
            continue
        rad = []
        for n in mine:
            for p in meshes[n].P[::7]:
                rad.append(v_len(v_sub(p, origin(b))))
        rad.sort()
        tol = max(0.04, min(0.5, 0.12 * (rad[len(rad) // 2] if rad else 1.0)))
        cell = {}
        for p in pts:
            cell.setdefault((int(p[0] // tol), int(p[1] // tol), int(p[2] // tol)), []).append(p)
        for n in mine:
            mesh = meshes[n]
            groups, bd = mesh.welded()
            for g in bd:
                p = mesh.P[groups[g][0]]
                cx, cy, cz = int(p[0] // tol), int(p[1] // tol), int(p[2] // tol)
                best, bdist = None, tol
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            for q in cell.get((cx + dx, cy + dy, cz + dz), ()):
                                d = v_len(v_sub(p, q))
                                if d < bdist:
                                    best, bdist = q, d
                if best is not None and bdist > 1e-5:
                    for i in groups[g]:
                        mesh.P[i] = list(best)
                    mesh.dirty = True
                    stats["snapped"] += 1


def _loops(mesh):
    """Open-edge loops of a mesh: lists of welded-group ids in order (vertices that sit at one place are one)."""
    groups, border, edges = mesh.welded(edges_too=True)
    adj = {}
    for a, b in edges:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    seen, loops = set(), []
    for start in adj:
        if start in seen:
            continue
        loop, prev, cur = [start], None, start
        seen.add(start)
        while True:
            nxt = [x for x in adj[cur] if x != prev and x not in seen]
            if not nxt:
                break
            prev, cur = cur, nxt[0]
            seen.add(cur)
            loop.append(cur)
        if len(loop) >= 4:
            loops.append(loop)
    return groups, loops


def _ring(mesh, j, u, max_dist, loose=False):
    """The open-edge loop of `mesh` that goes round the bone axis (j, u) closest to j: (vertex indices, radius)."""
    groups, loops = _loops(mesh)
    best = None
    for loop in loops:
        idx = [groups[g][0] for g in loop]
        pts = [mesh.P[i] for i in idx]
        c = tuple(sum(p[k] for p in pts) / len(pts) for k in range(3))
        a = v_dot(v_sub(c, j), u)                       # along the bone
        off = v_sub(v_sub(c, j), tuple(u[k] * a for k in range(3)))
        rad = sum(v_len(v_sub(p, c)) for p in pts) / len(pts)
        if rad < 1e-3 or v_len(off) > (1.5 * rad + 0.3 if loose else 0.6 * rad + 0.05) or abs(a) > max_dist:
            continue                                    # not round the axis, or too far from the joint
        score = abs(a) + v_len(off)
        if best is None or score < best[0]:
            best = (score, idx, rad, c)
    return (best[1], best[2], best[3]) if best else None


def _bridge(by_node, kind, layer_of, meshes, all_of, origin, mesh_of, stats, notes):
    """Closes the gap where a node's mesh and its parent's mesh come from different sources: a strip of triangles
    from the node's open ring at the joint to the parent's open ring, added to the node's own chain as one more
    batch (the node side keeps its skin weights, the parent side is weighted fully to the parent, which is the
    node's second matrix: the bridge bends with the joint). Returns {clone: [batch bytes, ...]}."""
    out = {}
    src_of = lambda n: layer_of.get(n) if kind.get(n) != "base" else None
    for node, c in by_node.items():
        if node not in CORE or node in FIXED or node in (0x30, 0x31, 0x32, 0x33) or c.parent is None:
            continue
        p = c.parent
        if p.node not in CORE:
            continue
        cm = [x for x in all_of(node) if x.enabled and mesh_of(x) is not None]
        pm = [x for x in all_of(p.node) if x.enabled and mesh_of(x) is not None]
        if not cm or not pm or src_of(cm[0]) is src_of(pm[0]):
            continue
        j = origin(c)
        ax = axis_of(by_node, node, lambda k: origin(by_node[k]))
        if ax is None:
            continue
        u = v_sub(ax[1], ax[0])
        L_ = v_len(u)
        if L_ < 1e-3:
            continue
        u = tuple(x / L_ for x in u)
        reach = 0.5 * L_ + 0.3
        rc = next((r for r in (_ring(mesh_of(x), j, u, reach) for x in cm) if r), None)
        if rc is None:
            continue
        x_c = next(x for x in cm if _ring(mesh_of(x), j, u, reach) == rc)
        rp, mp = None, None
        # the parent's opening: round the parent's own bone, anywhere along its far half (a short sleeve or a
        # trouser leg that ends above the knee still gets bridged to the piece below)
        pax = axis_of(by_node, p.node, lambda k: origin(by_node[k]))
        if pax is not None and v_len(v_sub(pax[1], pax[0])) > 1e-3:
            lp = v_len(v_sub(pax[1], pax[0]))
            up = tuple(x / lp for x in v_sub(pax[1], pax[0]))
            preach = 0.95 * lp + 0.3
        else:
            up, preach, lp = u, reach, L_
        for x in pm:
            r = _ring(mesh_of(x), j, up, preach) or _ring(mesh_of(x), j, u, reach) or                 _ring(mesh_of(x), j, up, preach, loose=True)
            if r and (rp is None or v_len(v_sub(r[2], rc[2])) < v_len(v_sub(rp[2], rc[2]))):
                rp, mp = r, mesh_of(x)
        mc = mesh_of(x_c)
        if rp is not None and v_dot(v_sub(rp[2], rc[2]), up) > 0.05 * lp:
            continue                                    # the parent's opening lies past the node's: they overlap
        if rp is not None and min(v_dot(v_sub(q, rp[2]), up) for x in cm for q in mesh_of(x).P) < 0.05 * lp:
            continue                                    # the node's mesh reaches up to the parent's opening (a boot
            #                                             top under a trouser leg): nothing open between them
        gap = v_len(v_sub(rp[2], rc[2])) + abs(rp[1] - rc[1]) if rp is not None else 1e9
        if rp is None or gap > max(1.5 * max(rc[1], rp[1]) + 0.5, 0.7 * lp):
            # no ring of the neighbour to meet: close the node's own opening with a cap (hidden inside the
            # neighbour where it overlaps; a see-through hole where it does not)
            bs = _cap_batches(mc, rc[0], rc[2], tuple(-x for x in u))
            if bs:
                out.setdefault(x_c, []).extend(bs)
                stats["capped"] += 1
            continue
        if gap < 0.02:
            continue                                    # closed already
        # both rings sampled at K angles round the axis
        e1 = (1.0, 0.0, 0.0) if abs(u[0]) < 0.9 else (0.0, 1.0, 0.0)
        e1 = v_sub(e1, tuple(u[k] * v_dot(e1, u) for k in range(3)))
        e1 = tuple(x / v_len(e1) for x in e1)
        e2 = (u[1] * e1[2] - u[2] * e1[1], u[2] * e1[0] - u[0] * e1[2], u[0] * e1[1] - u[1] * e1[0])

        def ang(pt, cen):
            d = v_sub(pt, cen)
            return math.atan2(v_dot(d, e2), v_dot(d, e1))
        K = 12
        pick_c, pick_p = [], []
        for k in range(K):
            a = -math.pi + (k + 0.5) * 2 * math.pi / K
            pick_c.append(min(rc[0], key=lambda i: abs((ang(mc.P[i], rc[2]) - a + math.pi) % (2 * math.pi) - math.pi)))
            pick_p.append(min(rp[0], key=lambda i: abs((ang(mp.P[i], rp[2]) - a + math.pi) % (2 * math.pi) - math.pi)))
        verts = []
        for k in list(range(K)) + [0]:
            ic, ip = pick_c[k], pick_p[k]
            verts.append((mc.P[ic], mc.W[ic], mc.N[ic], mc.ST[ic]))
            verts.append((mp.P[ip], 0.0, mp.N[ip], mc.ST[ic]))   # parent side: the parent's matrix only
        hdr = mc.header_of(ic=pick_c[0])
        if hdr is None:
            continue
        out.setdefault(x_c, []).append(_batch_bytes(hdr, verts))
        stats["bridged"] += 1
    return out


def _cap_batches(mesh, loop, centre, normal):
    """A fan from the loop's vertices to its centre, as strips of at most 37 vertices (the output buffer of program
    0 holds 38): r0 c r1 c r2 ... (every other triangle is degenerate)."""
    if len(loop) < 3:
        return []
    w = sum(mesh.W[i] for i in loop) / len(loop)
    ring = list(loop) + [loop[0]]
    out, k = [], 0
    while k < len(ring) - 1:
        seg = ring[k:k + 18]
        verts = []
        for j, i in enumerate(seg):
            verts.append((mesh.P[i], mesh.W[i], mesh.N[i], mesh.ST[i]))
            if j + 1 < len(seg):
                verts.append((centre, w, normal, mesh.ST[i]))
        hdr = mesh.header_of(seg[0])
        if hdr is None or len(verts) < 3:
            break
        out.append(_batch_bytes(hdr, verts))
        k += len(seg) - 1
    return out


def _cap_exposed(bm, clone, kind, meshes, mesh_of, stats):
    """Openings of the base's meshes that the base's own extras (hair locks, capes...) covered: when the mix hides
    those extras, each open loop of the parent's mesh that lies under one of them gets a cap."""
    out = {}
    for r in bm.recs:
        n = clone.get(r)
        if n is None or not r.mesh or n.enabled or r.node in CORE:
            continue
        par = r.parent
        while par is not None and par.node not in CORE:
            par = par.parent
        if par is None:
            continue
        pc = clone[par]
        pm = mesh_of(pc) if pc.enabled else None
        if pm is None:
            continue
        hp = Mesh(r.chain).P
        if not hp:
            continue
        lo = [min(p[k] for p in hp) for k in range(3)]
        hi = [max(p[k] for p in hp) for k in range(3)]
        pad = 0.15 * max(hi[k] - lo[k] for k in range(3)) + 0.05
        groups, loops = _loops(pm)
        for loop in loops:
            idx = [groups[g][0] for g in loop]
            c = tuple(sum(pm.P[i][k] for i in idx) / len(idx) for k in range(3))
            if not all(lo[k] - pad <= c[k] <= hi[k] + pad for k in range(3)):
                continue
            key = (id(pc), tuple(sorted(idx))[:4])
            if key in stats.setdefault("_capped_loops", set()):
                continue
            stats["_capped_loops"].add(key)
            nrm = tuple(sum(pm.N[i][k] for i in idx) for k in range(3))
            ln = v_len(nrm) or 1.0
            bs = _cap_batches(pm, idx, c, tuple(x / ln for x in nrm))
            if bs:
                out.setdefault(pc, []).extend(bs)
                stats["capped"] += 1
    return out


def _batch_bytes(hdr, verts):
    """VIF words of one program-0 batch: UNPACK 5 header quadwords at TOP, UNPACK 3n vertex quadwords at TOP + 5,
    MSCNT; NOPs in front so that it is whole quadwords."""
    n = len(verts)
    h = bytearray(hdr)
    for q in (3, 4):   # the strip tags: NLOOP = vertex count
        w0 = struct.unpack_from("<I", h, q * 16)[0]
        struct.pack_into("<I", h, q * 16, (w0 & ~0x7FFF) | n)
    words = [VIF_TEX] + list(struct.unpack("<20I", bytes(h))) + [0x6C008005 | ((3 * n) << 16)]
    body = bytearray()
    for p, w, nr, st in verts:
        body += struct.pack("<4f", p[0], p[1], p[2], w)
        body += struct.pack("<4f", nr[0], nr[1], nr[2], 0.0)
        body += struct.pack("<4f", st[0], st[1], 1.0, 0.0)
    tail = [0x17000000]
    total = len(words) + len(body) // 4 + len(tail)
    pad = (-total) % 4
    return struct.pack(f"<{pad}I", *([0] * pad)) + struct.pack(f"<{len(words)}I", *words) + bytes(body) + \
        struct.pack("<I", tail[0])


def _append_batches(chain, batches):
    """The chain with more batches in front of its end: the DMA tag's quadword count grows."""
    t0 = struct.unpack_from("<I", chain, 0)[0]
    qwc = t0 & 0xFFFF
    data_end = 16 + qwc * 16
    extra = b"".join(batches)
    if len(extra) % 16:
        return chain
    nq = qwc + len(extra) // 16
    if nq > 0xFFFF:
        return chain
    head = bytearray(chain[:16])
    struct.pack_into("<I", head, 0, (t0 & ~0xFFFF) | nq)
    return bytes(head) + chain[16:data_end] + extra + chain[data_end:]


def _seam_from(L, bm, by_node, kind, layer_of, meshes, maps):
    """The donor's seam section (header, vertices, triangles) with its vertices moved like the donor's meshes of
    the same nodes (their edit log replayed) and its TEX0 repointed."""
    dm = L.src.model
    vc, tc, vo, to = dm.seams()
    raw = dm.raw
    sec = bytearray(16) + bytearray(raw[vo:vo + vc * 0x70]) + bytearray(raw[to:to + tc * 0x20])
    struct.pack_into("<2i", sec, 0, vc, tc)
    bynode = {}
    for i in range(vc):
        bynode.setdefault(struct.unpack_from("<i", raw, vo + i * 0x70 + 0x1C)[0], []).append(i)
    for node, ids in bynode.items():
        m = Mesh.__new__(Mesh)
        m.P = [list(struct.unpack_from("<3f", raw, vo + i * 0x70)) for i in ids]
        m.W = [struct.unpack_from("<f", raw, vo + i * 0x70 + 12)[0] for i in ids]
        m.N = [list(struct.unpack_from("<3f", raw, vo + i * 0x70 + 0x10)) for i in ids]
        m.log, m.dirty = [], False
        src_mesh = next((meshes[x] for x in kind if x.node == node and kind[x] in ("core", "adopt") and
                         layer_of.get(x) is L and x in meshes), None)
        if src_mesh is not None:
            m.replay(src_mesh.log)
        elif node in bm.by_node and node in dm.by_node:
            b_, d_ = bm.by_node[node], dm.by_node[node]
            m.shift(v_sub(b_.ofsA, d_.ofsA), v_sub(b_.ofsB, d_.ofsB))
        for k, i in enumerate(ids):
            o = 16 + i * 0x70
            struct.pack_into("<3f", sec, o, *m.P[k])
            struct.pack_into("<3f", sec, o + 0x10, *m.N[k])
    tm, rm = maps(L.src)
    for i in range(tc):
        o = 16 + vc * 0x70 + i * 0x20 + 0x10
        t0, t1 = struct.unpack_from("<QQ", sec, o)
        key = (t0 & 0x3FFF, (t0 >> 37) & 0x3FFF)
        if key not in tm:
            same = [k for k in tm if k[1] == key[1]] or sorted(tm, key=lambda k: abs(k[0] - key[0]))[:1]
            key = same[0] if same else key
        if key in tm:
            a, c, bits = tm[key][:3]
            if bits is not None:
                t0 = (t0 & ~(0xFFFFF << 14)) | (bits & (0xFFFFF << 14))
            t0 = (t0 & ~0x3FFF & 0xFFF8001FFFFFFFFF) | a | (c << 37)
        r = (t1 >> 37) & 0x3FFF
        if r not in rm and rm:
            r = min(rm)
        if r in rm:
            t1 = (t1 & 0xFFF8001FFFFFFFFF) | (rm[r] << 37)
        struct.pack_into("<QQ", sec, o, t0, t1)
    return sec


def _ancestors(n):
    out = []
    while n.parent is not None:
        n = n.parent
        out.append(n)
    return out


def _is_piece_root(L, by_node, node):
    x = by_node[node]
    return x.parent is None or x.parent.node not in REGIONS.get(L.piece, ())


def g_leader(groups, t):
    return any(g[0] is t for g in groups)


def _replay(m, face_clone, meshes, build, by_node, origin, adjust, layers, layer_of):
    """Mouth-shape chains replace the face mesh: give them the face's head-build and workshop edits."""
    if face_clone is None:
        return
    f = float(build.get("head", 100)) / 100.0 if build else 1.0
    fm = meshes.get(face_clone)
    L = layer_of.get(face_clone)
    if L is not None:
        lo = {k: v for k, v in L.opts.items() if k != "nodes"}
        if not is_identity(lo):
            M, Ni, t = ui_xform(lo)
            x = face_clone
            while x.parent is not None and x.parent.node in REGIONS.get(L.piece, ()):
                x = x.parent
            m.apply(M, Ni, t, origin(x))
        o = node_key(L.opts.get("nodes") or {}, 0x30)
        if o and not is_identity(o):
            M, Ni, t = ui_xform(o)
            m.apply(M, Ni, t, origin(face_clone), weighted=True)
    o = base_opts(adjust, 0x30)
    if L is None and o and not is_identity(o):
        M, Ni, t = ui_xform(o)
        m.apply(M, Ni, t, origin(face_clone), weighted=True)
    if abs(f - 1) > 1e-3:
        pts = []
        for k in (0x2F, 0x30, 0x31, 0x32, 0x33):
            n = by_node.get(k)
            if n is not None and n in meshes:
                pts += meshes[n].P
        if pts:
            # the head meshes are already scaled: invert to find the centre the build used
            c = tuple((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 for k in range(3))
            f = max(0.3, min(3.0, f))
            m.apply(((f, 0, 0), (0, f, 0), (0, 0, f)), IDENT3, (0, 0, 0), c)


# ---- node lists for the UI
def _node_entry(m, r, name):
    strips = mdlx.parse_chain(r.chain, 0, len(r.chain)) if r.mesh else []
    t = sum(max(0, len(s["verts"]) - 2) for s in strips)
    return {"node": r.node, "name": name, "parent": r.parent.node if r.parent is not None else None,
            "region": m.region(r), "triangles": t, "points": len(Mesh(r.chain).P) if r.mesh else 0}


def base_nodes(base_pack):
    """The base model's nodes with a mesh: [{node, name, parent, region, triangles}] (keys for `adjust`)."""
    s = Source(base_pack, "base")
    m = s.model
    out, counts = [], {}
    for r in m.recs:
        if not r.mesh:
            continue
        reg = m.region(r)
        if r.node in NODE_NAMES:
            name = NODE_NAMES[r.node]
        else:
            counts[reg] = counts.get(reg, 0) + 1
            name = f"{EXTRA_WORD.get(reg, 'Part')} {counts[reg]}"
        out.append(_node_entry(m, r, name))
    return out


def layer_nodes(base_pack, selections):
    """For each selection, the donor nodes it brings: [[{node (donor id, the key of opts["nodes"]), name ("Strand
    2.3" = layer 2, strand 3; core nodes by body part), parent, region, triangles}], ...]."""
    out = []
    cache = {}
    for k, s in enumerate(selections, 1):
        pack, piece = s[0], s[1].rstrip("+")
        if piece not in PIECES:
            raise MixError(f"unknown piece {piece!r}")
        src = cache.get(id(pack)) or cache.setdefault(id(pack), Source(pack, "donor"))
        m = src.model
        region = "hair" if piece in ("hair", "head") else piece
        items, i = [], 0
        for r in m.recs:
            if r.node in CORE:
                if piece != "hair" and not s[1].endswith("+") and r.node in REGIONS[piece] and r.mesh:
                    items.append(_node_entry(m, r, NODE_NAMES.get(r.node, f"Node 0x{r.node:X}")))
                continue
            if m.region(r) == region and r.mesh:
                i += 1
                items.append(_node_entry(m, r, f"{EXTRA_WORD.get(region, 'Part')} {k}.{i}"))
        out.append(items)
    return out


TAIL = {4, 5, 6, 7}
ADOPT = {4: 3, 5: 3, 6: 3, 7: 3, 0x31: 0x2F, 0x32: 0x2F, 0x33: 0x2F}   # node -> anchor


def adopt_ok(bm, dm, node):
    """Whether `node` can take the donor's bone: in both models it hangs where the base's does (tail: the chain
    3 -> 4 -> 5 -> 6 -> 7; head nodes: on 0x2F) and only nodes of the same group or extras that move with it (no
    core node, no gameplay accessory) hang below it in the base."""
    group = TAIL if node in TAIL else {0x31, 0x32, 0x33}
    for n in group:   # all of the group or none
        for m in (bm, dm):
            if len(m.dups.get(n, ())) > 1:
                return False
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
    slot with a node's id, so no other slot may keep an added node's id.
    A slot of the base that names none of the base's own records is cleared first, in both tables: the disc's
    models list ids they do not have (Vegeta's type B slots 0x47..0x4E, say), and an added node renumbered onto
    such an id would also be stepped by that slot. Type B steps after type A and overwrites the rotation, so the
    donor's hair took the base's type B direction and gravity (pointing up and back) instead of its own."""
    present = {n.node for n in order}
    if base.chain is None and not renum:
        return entry(base.parts, E_CHAIN)
    blk = bytearray(base.chain) if base.chain is not None else bytearray(0x380)
    own = {r.node for r in base.model.recs}
    for layout, count in ((CHAIN_A, 16), (CHAIN_B, 8)):
        nofs = layout[0][0]
        for i in range(count):
            if blk[nofs + i] in hidden or blk[nofs + i] not in own:
                blk[nofs + i] = 0
    renum = list(renum.items()) if isinstance(renum, dict) else renum
    renum = [(k[0], k[1], v) for k, v in renum] if renum and len(renum[0]) == 2 else renum
    added = {new for _, _, new in renum}
    still = []
    for src, old, new in renum:
        if src.chain is None or new in hidden or new not in present:
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
                still.append(new)
                continue
            j = free[0]
            for k in stale[1:]:
                blk[nofs + k] = 0
            for off, size in layout:
                blk[off + j * size:off + (j + 1) * size] = src.chain[off + i * size:off + (i + 1) * size]
            blk[nofs + j] = new
    if still:
        notes.append(f"{len(still)} added hair / cloth part(s) hang still: the 16 + 8 secondary-motion slots are "
                     f"taken (nodes {' '.join('%02X' % n for n in still[:12])})")
    return bytes(blk)


# ---------------------------------------------------------------------------------------------- checking
def check(pack_bytes, base=None, donors=()):
    """Walks what BtlObjMdl_Create / the pose, bounds, chain, draw and bind passes walk; returns a list of problems
    (empty = consistent)."""
    bad = []
    # what the source packs already do (BT4 models): TEX0 shapes their chains use, eye frames larger than the face
    src_shapes, src_eyes = set(), set()
    for sp in ([base] if base is not None else []) + list(donors):
        try:
            ss = Source(sp, "source")
        except MixError:
            continue
        for ch in [r.chain for r in ss.model.recs if r.mesh] + [entry(ss.parts, n) for n in E_MOUTH_A]:
            if ch:
                b_ = bytearray(ch)
                walk_chain(b_, on_tex=lambda pos, b_=b_: src_shapes.add(
                    (struct.unpack_from("<I", b_, pos + 20)[0] | (struct.unpack_from("<I", b_, pos + 24)[0] << 32))
                    & (0xFFFFF << 14)))
        if entry(ss.parts, E_EYES):
            src_eyes.add(entry(ss.parts, E_EYES))
    try:
        s = Source(pack_bytes, "mix")
    except MixError as e:
        return [str(e)]
    m = s.model
    e = m.raw
    if struct.unpack_from("<I", e, 8)[0] != len(m.recs):
        bad.append("header +8 is not the record count")
    if len(m.recs) > MAX_RECORDS_FORCE:
        bad.append("too many records")
    o = m.meshOfs
    for r in m.recs:
        if o % 16:
            bad.append("record not 16-byte aligned")
        o += struct.unpack_from("<i", e, o)[0] if not r.last else 0x40
    ids = [r.node for r in m.recs]
    if max(ids) >= 0x80:
        bad.append("node id out of range")
    for node, rs in m.dups.items():   # several records of a node: all but the last must be leaves
        if any(r.children for r in rs[:-1]):
            bad.append(f"node 0x{node:X}: a duplicate record that is not the last has children")
    # pose walk: the matrix stack never runs dry before the end
    stack = 0
    for k, r in enumerate(m.recs):
        stack += 1
        if r.pop > stack and k + 1 < len(m.recs):
            bad.append(f"pose walk pops past the root at node 0x{r.node:X}")
        stack = max(0, stack - r.pop)
    # skeleton: every node's origin is its parent's origin plus its rest translation, ofsB is the parent's origin
    # (true for all 698 models on the disc)
    known = set()   # nodes the base pack already has like that (BT4 models: origins that do not add up)
    if base is not None:
        try:
            bmm = Source(base, "base").model
            for r in bmm.recs:
                if r.parent is not None and (
                        max(abs(r.ofsA[k] - r.parent.ofsA[k] - r.vec(0x30)[k]) for k in range(3)) > 1e-3 or
                        max(abs(r.ofsB[k] - r.parent.ofsA[k]) for k in range(3)) > 1e-3):
                    known.add(r.node)
        except MixError:
            pass
    for r in m.recs:
        if r.parent is None or not m.is_main(r) or r.node in known:
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
    base_nodes_ = set(Source(base, "base").model.by_node) if base is not None else CORE
    for n in CORE:
        if n not in m.by_node and n in base_nodes_:
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
            if s.tex_by_ofs(tbp, cbp, exact=True) is None:
                bad.append(f"node 0x{r.node:X}: TEX0 names no texture (TBP {tbp} CBP {cbp})")
            if s.ramp_by_ofs(rc) is None:
                bad.append(f"node 0x{r.node:X}: ramp CLUT {rc} missing")
    if mins != [0, 0]:
        bad.append(f"lowest TBP / CBP of the main chains is {mins}, must be 0")
    # every TEX0 has the shape (TBW, PSM, TW, TH) of the texture it names
    def shape_check(chain, what):
        buf = bytearray(chain)

        def tex(pos):
            lo, hi = struct.unpack_from("<II", buf, pos + 20)
            t = s.tex_by_ofs(lo & 0x3FFF, (hi >> 5) & 0x3FFF, exact=True)
            if t is not None:
                want = struct.unpack_from("<Q", t.ent, 0x30)[0]
                got = lo | (hi << 32)
                if (got ^ want) & (0xFFFFF << 14) and tex0_blocks(got) > max(t.tbpStep, tex0_blocks(want)) and                         (got & (0xFFFFF << 14)) not in src_shapes:
                    bad.append(f"{what}: TEX0 reads past texture {t.old}")
        walk_chain(buf, on_tex=tex)
    for r in m.recs:
        if r.mesh:
            shape_check(r.chain, f"node 0x{r.node:X}")
    # (mouth chains are not checked: three models on the disc name a texture of another shape there already)
    tsum = max([t.tbpOfs + t.tbpStep for t in s.texs if s.material(t.old)] or [0])
    csum = max([t.cbpOfs + max(1, t.cbpStep) for t in s.texs if s.material(t.old) and t.clut] or [0])
    rsum = max([t.tbpOfs + t.tbpStep for t in s.texs if s.ramp(t.old)] or [0])
    if tsum > TEX_BLOCKS or csum > CLUT_BLOCKS or rsum > RAMP_BLOCKS:
        bad.append(f"GS budget: {tsum} pixel, {csum} CLUT, {rsum} ramp blocks")
    if not s.material(s.faceTex):
        bad.append("face texture outside the material range")
    for n in E_MOUTH_A:
        c = entry(s.parts, n)
        if c:
            for tbp, cbp, rc in chain_texrefs(c):
                if s.tex_by_ofs(tbp, cbp, exact=True) is None:
                    bad.append(f"mouth chain {n}: TEX0 names no texture")
    eyes = entry(s.parts, E_EYES)
    if eyes:
        try:
            et, _, _ = parse_texfile(eyes, s)
            ft = s.texs[s.faceTex]
            for t in et:
                if t.tbpStep > ft.tbpStep and eyes not in src_eyes:   # (BT4 eye frames: other shapes / sizes)
                    bad.append("eye frame larger than the face texture")
        except MixError as x:
            bad.append(f"eyes: {x}")
    # chains: links per type within the slots
    if s.chain is not None:
        for nofs, count in ((0, 16), (0x2F0, 8)):
            slots = {s.chain[nofs + i] for i in range(count) if s.chain[nofs + i]}
            links = sum(1 for r in m.recs if r.node >= 0x47 and r.node in slots)   # every record makes a link
            ids = [s.chain[nofs + i] for i in range(count) if s.chain[nofs + i]]
            if len(ids) != len(set(ids)):
                bad.append("a node id in two chain slots")
            if links > count:
                bad.append("too many chain links")
        recs = {r.node for r in m.recs if r.node >= 0x47}   # (no disc model has a node in both tables)
        if recs & set(s.chain[0:16]) & set(s.chain[0x2F0:0x2F8]):
            bad.append("a node in a type A and a type B chain slot (stepped twice)")
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
