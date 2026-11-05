#!/usr/bin/env python3
"""3D mesh of a fighter's model pack (a character costume file, 252 entries), for a WebGL preview. Standard library only.

    python port/tools/creator/model.py <pack.bin> [-o out_prefix] [--pose rest|relaxed] [--hide-optional]
                                       [--render front.png] [--size 512] [--json mesh.json]

writes <out_prefix>.obj / .mtl and one PNG per texture used (<out_prefix>_tex<N>.png); --render draws an orthographic,
textured front view with a pure Python rasterizer (for checking by eye); --json the dict `extract` returns.

    import model; m = model.extract(open(pack, "rb").read())

The format, as read from the decompilation (src/sys/gfxm_d_c.c ObjDraw_DrawParts, gfxm_e_b.c BtlObjMdl_BindFiles,
gfxm_e_c.c MdlTex_RebaseChain, gfxm_b_b.c Tex_Upload*, docs/systems/vu1/prog0_prog1.md) and checked on the data:

* Pack: u32 count (252), count + 1 offsets, 64-byte aligned parts; game entry n = part n - 1 (BObjFile_GetEntry).
  Entry 3 (part 2) is the model, magic "pmdl"; entry 0xC (part 11) is its texture file (BtlObj_BindTables:
  mdl->unk00 = entry 3, mdl->unk04 = entry 0xC).
* Model header: +0x4C faceTex, +0x4E texFirst, +0x4F texCount, +0x6C offset of the part (mesh record) list,
  +0x70 seam section offset (0 = none).
* Part record (ObjDrawPart): +0 next (byte offset of the next record), +6 last, +8 enabled (0 = no mesh), +0xA node,
  +0xC flags, +0x10 ofsA (the node's joint, model space), +0x20 ofsB (the parent's joint), +0x30 ofsA - ofsB,
  +0x40 / +0x50 centre / extent of the mesh (records with next > 0x40), +0x60 the VIF chain: one DMA "ret" tag whose
  data are VIF codes: per batch UNPACK V4-32 of 5 header quadwords at TOP (GIF tag A+D, TEX0_1 of the texture
  layer, TEX0_2 of the shading ramp, the two strip GIF tags: NLOOP = vertex count n), UNPACK V4-32 of 3n quadwords
  at TOP + 5 (per vertex: position xyz + w = weight of node A; normal xyz; s, t, 1), MSCNT. One batch is ONE
  triangle strip (VU1 program 0).
* Skinning (program 0): p = mix(B (pos - ofsB), A (pos - ofsA), w), A / B = the world matrices of the part's node
  and of its parent. Positions are stored in model space of the bind pose, so with A = translate(ofsA) and
  B = translate(ofsB) p = pos: the rest pose (a T-pose, Y pointing DOWN, the face towards -Z) needs no matrices.
* TEX0 block pointers in the chains are relative to the texture file's packing (TexEntry tbpOfs / cbpOfs).
  Textures: TexEntry (0x40 bytes) at +4 (words); the pixel packet is a ready GS upload (GIF A+D TRXPOS, TRXREG,
  TRXDIR, IMAGE) written as PSMCT32 with DBW from pixBlt and read with the PSM of TEX0 (PSMT4 here, PSMT8 also
  handled); the CLUT is 16 (8x2) or 256 (16x16, CSM1) PSMCT32 entries, alpha 0x80 = opaque. CLAMP is 0: repeat.
* Seam section (model + seamOfs: vtxCount, triCount, vtxOfs, triOfs; 0x70-byte vertices, 0x20-byte triangles) is
  added as plain triangles when present.

Output (`extract`): Y up, face towards +Z (the game's axes turned 180 degrees about X, a rotation, so no mirror).
"""
import argparse, base64, json, math, pathlib, struct, sys, zlib

PACK_COUNT = 252
E_MODEL, E_TEX = 3, 0xC          # game entry numbers (part index = entry - 1)
VIF_UNPACK_V4_32, VIF_MSCNT = 0x6C, 0x17
# nodes BtlObj_IsNodeShown may hide (costume extras such as a halo); kept by default, listed in "parts"
OPTIONAL_NODES = {0x40, 0x41, 0x42, 0x43, 0x6F}


class ModelError(Exception):
    pass


# ----------------------------------------------------------------------------------------------- GS memory
# PSMCT32 / PSMT8 address functions as in creator.py (verified there on the portraits); PSMT4 after the GS manual.
BLOCK32 = [[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23], [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]]
COL32 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15], [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31],
         [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47], [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]]
COL8 = [[0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54], [8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62],
        [33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23], [41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31]]
BLOCK4 = [[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15], [16, 18, 24, 26], [17, 19, 25, 27], [20, 22, 28, 30], [21, 23, 29, 31]]


def addr32(bw, x, y):
    """Word address of a PSMCT32 pixel (bw in 64-pixel units, base block 0)."""
    return ((((y & ~0x1F) * bw + ((x >> 1) & ~0x1F) + BLOCK32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + COL32[y & 7][x & 7])


def addr8(bw, x, y):
    """Byte address of a PSMT8 pixel."""
    col = (y & 15) >> 2
    sx = ((x & 15) ^ 4) if (col & 1) else (x & 15)
    return (((((y >> 1) & ~0x1F) * (bw >> 1) + ((x >> 2) & ~0x1F) + BLOCK32[(y >> 4) & 3][(x >> 4) & 7]) << 8)
            + COL8[y & 3][sx] + col * 64)


def addr4(bw, x, y):
    """Nibble address of a PSMT4 pixel: 128 x 128 pages, 32 x 16 blocks, four 32 x 4 columns of 128 nibbles."""
    page = (y >> 7) * (bw >> 1) + (x >> 7)
    block = BLOCK4[(y >> 4) & 7][(x >> 5) & 3]
    col, r, cx = (y >> 2) & 3, y & 3, x & 31
    swap = 2 if ((r >> 1) ^ (col & 1)) & 1 else 0
    nib = ((cx >> 3) & 3) * 2 + (cx & 1) * 8 + (((cx >> 1) & 3) ^ swap) * 32 + (r & 1) * 16 + (r >> 1)
    return ((page * 32 + block) << 9) + col * 128 + nib


def csm1(i):
    return i + 8 if (i & 0x18) == 8 else i - 8 if (i & 0x18) == 16 else i


def png(w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------------------------------- pack
def split_pack(data):
    if len(data) < 8:
        raise ModelError("not a pack")
    n = struct.unpack_from("<I", data)[0]
    if n == 0 or n > 4096 or 4 + (n + 1) * 4 > len(data):
        raise ModelError("not a pack")
    offs = struct.unpack_from(f"<{n + 1}I", data, 4)
    if offs[-1] > len(data) or list(offs) != sorted(offs):
        raise ModelError("unexpected pack layout")
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


# ---------------------------------------------------------------------------------------------- textures
def parse_gs_upload(d, ofs):
    """The ready upload packet of a texture entry: (width, height, data offset) of the IMAGE transfer."""
    w = h = None
    p = ofs
    end = min(len(d), ofs + 0x100)
    while p + 16 <= end:
        lo, hi = struct.unpack_from("<QQ", d, p)
        nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) & 15
        if lo == 0 and hi == 0:      # DMA / VIF words in front of the GIF tag
            p += 16
            continue
        if flg == 0 and hi == 0xE and nreg == 1:   # PACKED A+D
            for k in range(nloop):
                val, reg = struct.unpack_from("<QQ", d, p + 16 + k * 16)
                if reg & 0xFF == 0x52:   # TRXREG
                    w, h = val & 0xFFF, (val >> 32) & 0xFFF
            p += 16 + nloop * 16
            continue
        if flg == 2:                 # IMAGE
            return w, h, p + 16, nloop * 16
        p += 16
    raise ModelError("texture upload packet not understood at 0x%X" % ofs)


def decode_texfile(t):
    """Every texture of a texture file: list of dicts (index, width, height, rgba, tbpOfs, cbpOfs) or None."""
    count, ent = struct.unpack_from("<II", t, 0)
    if count == 0 or count > 256 or ent * 4 + count * 0x40 > len(t):
        raise ModelError("bad texture file")
    out = []
    for i in range(count):
        o = ent * 4 + i * 0x40
        pix, clut, psize, csize, tstep, cstep, pblt, cblt, tofs, cofs = struct.unpack_from("<IIiiiiIIii", t, o)
        tex0 = struct.unpack_from("<Q", t, o + 0x30)[0]
        rec = {"index": i, "tbpOfs": tofs, "cbpOfs": cofs, "tex0": tex0, "rgba": None, "width": 0, "height": 0}
        out.append(rec)
        try:
            rec.update(decode_texture(t, pix * 4, clut * 4 if clut else 0, pblt, tex0))
        except (ModelError, struct.error, IndexError):
            pass
    return out


def decode_texture(t, pix, clut, pblt, tex0):
    psm = (tex0 >> 20) & 0x3F
    tbw = (tex0 >> 14) & 0x3F
    tw, th = 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15)
    csa = (tex0 >> 56) & 0x1F
    uw, uh, dofs, dlen = parse_gs_upload(t, pix)
    dbw = pblt & 0x3F
    if (pblt >> 8) & 0x3F != 0:
        raise ModelError("pixels not uploaded as PSMCT32")
    words = struct.unpack_from(f"<{uw * uh}I", t, dofs)
    # GS memory of the texture (relative to its own block), written as PSMCT32
    mem = {}
    for y in range(uh):
        row = y * uw
        for x in range(uw):
            mem[addr32(dbw, x, y)] = words[row + x]

    def byte(a):
        return (mem.get(a >> 2, 0) >> ((a & 3) * 8)) & 0xFF

    pal = []
    if psm in (0x13, 0x14):
        if not clut:
            raise ModelError("no CLUT")
        cw, ch, cofs, _ = parse_gs_upload(t, clut)
        n = cw * ch
        cl = [t[cofs + k * 4:cofs + k * 4 + 4] for k in range(n)]
        for k in range(256 if psm == 0x13 else 16):
            j = csm1(k) if psm == 0x13 else k + csa * 16
            e = cl[j] if j < n else b"\0\0\0\x80"
            pal.append(bytes((e[0], e[1], e[2], min(255, e[3] * 2))))
    rgba = bytearray(tw * th * 4)
    for y in range(th):
        for x in range(tw):
            if psm == 0x14:
                a = addr4(tbw, x, y)
                v = byte(a >> 1)
                c = pal[(v >> 4) if a & 1 else (v & 15)]
            elif psm == 0x13:
                c = pal[byte(addr8(tbw, x, y))]
            elif psm == 0:
                v = mem.get(addr32(tbw, x, y), 0)
                c = bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, min(255, ((v >> 24) & 0xFF) * 2)))
            else:
                raise ModelError("PSM 0x%X not handled" % psm)
            rgba[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = c
    return {"width": tw, "height": th, "rgba": bytes(rgba), "psm": psm}


# ------------------------------------------------------------------------------------------------- model
def parse_model(e):
    """Records and triangle strips of a pmdl entry (game axes, rest pose)."""
    if len(e) < 0x74 or e[:4] != b"pmdl":
        raise ModelError("entry 3 is not a pmdl model")
    hdr = {"flags": struct.unpack_from("<I", e, 0xC)[0], "faceTex": e[0x4C], "fadeTex": e[0x4D],
           "texFirst": e[0x4E], "texCount": e[0x4F]}
    o = struct.unpack_from("<I", e, 0x6C)[0]
    seam = struct.unpack_from("<i", e, 0x70)[0]
    records, strips = [], []
    for _ in range(4096):
        if o < 0x70 or o + 0x40 > len(e):
            raise ModelError("record outside the model at 0x%X" % o)
        nxt, u04, last, enabled, node, flags = struct.unpack_from("<iHHHHH", e, o)
        ofsA = struct.unpack_from("<3f", e, o + 0x10)
        ofsB = struct.unpack_from("<3f", e, o + 0x20)
        rec = {"node": node, "ofsA": ofsA, "ofsB": ofsB, "enabled": enabled, "flags": flags, "offset": o}
        records.append(rec)
        if nxt > 0x60 and enabled:
            for s in parse_chain(e, o + 0x60, o + nxt):
                s["node"] = node
                s["record"] = len(records) - 1
                strips.append(s)
        if last:
            break
        if nxt < 0x40:
            raise ModelError("bad record length at 0x%X" % o)
        o += nxt
    seams = parse_seams(e, seam) if seam > 0 else []
    return hdr, records, strips, seams


def parse_chain(e, p, end):
    out = []
    while p + 16 <= end:
        t0 = struct.unpack_from("<I", e, p)[0]
        qwc, tid = t0 & 0xFFFF, (t0 >> 28) & 7
        if p + 16 + qwc * 16 > len(e):
            raise ModelError("chain past the model at 0x%X" % p)
        words = struct.unpack_from(f"<{2 + qwc * 4}I", e, p + 8)
        p += 16 + qwc * 16
        mem = {}
        i = 0
        while i < len(words):
            c = words[i]
            cmd, num, imm = (c >> 24) & 0x7F, ((c >> 16) & 0xFF) or 256, c & 0xFFFF
            i += 1
            if cmd >= 0x60:
                vn, vl = (cmd >> 2) & 3, cmd & 3
                if cmd & 0x6F == VIF_UNPACK_V4_32:
                    a = imm & 0x3FF
                    for k in range(num):
                        mem[a + k] = words[i + 4 * k:i + 4 * k + 4]
                bits = (32, 16, 8, 16)[vl]
                comp = vn + 1 if vl != 3 else 1
                i += (num * comp * bits + 31) // 32
            elif cmd == VIF_MSCNT or cmd in (0x14, 0x15):
                if 3 in mem:
                    out.append(batch(mem))
            elif cmd == 0x20:
                i += 1
            elif cmd in (0x30, 0x31):
                i += 4
            elif cmd == 0x4A:
                i += num * 2
            elif cmd in (0x50, 0x51):
                i += (imm or 65536) * 4
        if tid in (0, 6, 7):         # refe / ret / end
            break
    return out


def batch(mem):
    n = mem[3][0] & 0x7FFF
    tex0 = mem[1][0] | (mem[1][1] << 32)
    vs = []
    for v in range(n):
        q = mem.get(5 + 3 * v), mem.get(6 + 3 * v), mem.get(7 + 3 * v)
        if None in q:
            break
        pos = struct.unpack("<4f", struct.pack("<4I", *q[0]))
        nrm = struct.unpack("<3f", struct.pack("<3I", *q[1][:3]))
        st = struct.unpack("<2f", struct.pack("<2I", *q[2][:2]))
        vs.append((pos[:3], pos[3], nrm, st))
    return {"tbp": tex0 & 0x3FFF, "cbp": (tex0 >> 37) & 0x3FFF, "verts": vs}


def parse_seams(e, so):
    if so + 16 > len(e):
        return []
    vc, tc, vo, to = struct.unpack_from("<iiii", e, so)
    if not (0 < vc < 10000 and 0 < tc < 10000) or vo + vc * 0x70 > len(e) or to + tc * 0x20 > len(e):
        return []
    verts = []
    for i in range(vc):
        b = vo + i * 0x70
        pos = struct.unpack_from("<4f", e, b)
        nrm = struct.unpack_from("<3f", e, b + 0x10)
        node = struct.unpack_from("<i", e, b + 0x1C)[0]
        uv = struct.unpack_from("<2f", e, b + 0x20)
        verts.append((pos[:3], pos[3], nrm, uv, node))
    tris = []
    for i in range(tc):
        b = to + i * 0x20
        idx = struct.unpack_from("<3i", e, b)
        tex0 = struct.unpack_from("<Q", e, b + 0x10)[0]
        if all(0 <= k < vc for k in idx):
            tris.append((idx, tex0 & 0x3FFF, (tex0 >> 37) & 0x3FFF))
    return [{"verts": verts, "tris": tris}]


# -------------------------------------------------------------------------------------------------- pose
def skeleton(records):
    """Nodes with their joint (ofsA) and parent, the parent found as the latest earlier record whose joint is this
    record's ofsB (the records come in hierarchy order). Where joints coincide the choice is a guess; it changes
    nothing in the rest pose. Game axes."""
    bones, seen = {}, []
    for r in records:
        if r["node"] in bones:
            continue
        cand = [q["node"] for q in reversed(seen)
                if q["node"] != r["node"] and all(abs(a - b) < 1e-4 for a, b in zip(q["ofsA"], r["ofsB"]))]
        # joints can coincide (several nodes at the hips): prefer a skeleton node over a costume extra
        parent = next((n for n in cand if n not in OPTIONAL_NODES), cand[0] if cand else None)
        bones[r["node"]] = {"node": r["node"], "parent": parent, "joint": r["ofsA"], "parentJoint": r["ofsB"]}
        seen.append(r)
    return bones


def rot_z(deg):
    """Rotation about Z by `deg` degrees (from +X towards +Y)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))


def mat_mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def mat_vec(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


IDENT = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def world_matrices(bones, local_rot):
    """Per node (R, t): the world transform p -> R (p - joint) + t that program 0 calls A, for local rotations
    about each joint (dict node -> 3x3); nodes without one inherit their parent's."""
    out = {}

    def get(n, depth=0):
        if n in out:
            return out[n]
        b = bones.get(n)
        if b is None or depth > 200:
            return IDENT, (0.0, 0.0, 0.0)
        j = b["joint"]
        if b["parent"] is None:
            pr, base_t = IDENT, j
        else:
            pr, pt = get(b["parent"], depth + 1)
            pj = bones[b["parent"]]["joint"]
            base_t = tuple(pt[k] + v for k, v in enumerate(mat_vec(pr, tuple(j[i] - pj[i] for i in range(3)))))
        r = mat_mul(pr, local_rot.get(n, IDENT))
        out[n] = (r, base_t)
        return out[n]

    for n in bones:
        get(n)
    return out


# upper arm -> elbow nodes of the humanoid skeleton (Goku (End): 0x13 / 0x21 at the shoulders, 0x14 / 0x22 elbows)
ARMS = ((0x13, 0x14), (0x21, 0x22))


def relaxed_pose(bones, down=20.0):
    """Arms lowered from the bind T-pose to `down` degrees from vertical, turned about Z at the shoulders."""
    rot = {}
    for upper, elbow in ARMS:
        if upper not in bones or elbow not in bones or bones[elbow]["parent"] != upper:
            continue
        s, e = bones[upper]["joint"], bones[elbow]["joint"]
        dx, dy = e[0] - s[0], e[1] - s[1]
        if dx * dx + dy * dy < 1e-6:
            continue
        side = 1.0 if dx >= 0 else -1.0
        tx, ty = side * math.sin(math.radians(down)), math.cos(math.radians(down))   # game Y points down
        rot[upper] = rot_z(math.degrees(math.atan2(ty, tx) - math.atan2(dy, dx)))
    return rot


# ----------------------------------------------------------------------------------------------- extract
def extract(pack_bytes, pose="rest", hide_optional=False, textures=True):
    """Mesh of a model pack. Returns a dict:
    positions, normals, uvs (flat lists; Y up, the face towards +Z; one unit = the game's unit, the fighter is about
    21 high), indices (triangles, counter-clockwise seen from outside), groups [{texture, start, count}] (ranges of
    `indices` per texture), textures [{index, width, height, png (base64)}], texture_png (base64 of the texture used
    most), parts [{node, texture, start, count, optional}], bones [{node, parent, head, parentHead}] (joint positions
    in the output axes and pose), joints (per vertex: the node of matrix A and its parent, matrix B), weights (per
    vertex: weight of A; p = mix(B (pos - jointB), A (pos - jointA), w)), bbox {min, max}, header, stats.
    pose: "rest" (the bind T-pose, as stored) or "relaxed" (arms lowered, skinned as VU1 program 0 does).
    hide_optional: leave out the parts of OPTIONAL_NODES (halo, small extras)."""
    parts = split_pack(pack_bytes)
    model = None
    if len(parts) > E_MODEL - 1 and parts[E_MODEL - 1][:4] == b"pmdl":
        model = parts[E_MODEL - 1]
    else:
        model = next((p for p in parts if p[:4] == b"pmdl"), None)
    if model is None:
        raise ModelError("no pmdl model in the pack")
    hdr, records, strips, seams = parse_model(model)
    bones = skeleton(records)

    tex_list = []
    if textures and len(parts) > E_TEX - 1:
        try:
            tex_list = decode_texfile(parts[E_TEX - 1])
        except (ModelError, struct.error):
            tex_list = []

    def tex_of(tbp, cbp):
        for t in tex_list:
            if t["tbpOfs"] == tbp and t["cbpOfs"] == cbp:
                return t["index"]
        for t in tex_list:
            if t["tbpOfs"] == tbp:
                return t["index"]
        return -1

    if pose == "relaxed":
        mats = world_matrices(bones, relaxed_pose(bones))
    elif pose == "rest":
        mats = None
    else:
        raise ModelError("pose must be rest or relaxed")

    def skin(pos, w, nrm, node):
        if mats is None:
            return pos, nrm
        b = bones.get(node)
        par = b["parent"] if b and b["parent"] is not None else node
        ra, ta = mats.get(node, (IDENT, (0, 0, 0)))
        rb, tb = mats.get(par, (IDENT, (0, 0, 0)))
        ja, jb = bones[node]["joint"], bones[par]["joint"]
        pa = [ta[k] + v for k, v in enumerate(mat_vec(ra, tuple(pos[i] - ja[i] for i in range(3))))]
        pb = [tb[k] + v for k, v in enumerate(mat_vec(rb, tuple(pos[i] - jb[i] for i in range(3))))]
        na, nb = mat_vec(ra, nrm), mat_vec(rb, nrm)
        return (tuple(pb[k] + (pa[k] - pb[k]) * w for k in range(3)),
                tuple(nb[k] + (na[k] - nb[k]) * w for k in range(3)))

    P, N, UV, J, W = [], [], [], [], []
    tris = []   # (texture, node, i0, i1, i2)

    def add_vertex(pos, w, nrm, st, node):
        p, n = skin(pos, w, nrm, node)
        b = bones.get(node)
        P.append(p)
        N.append(n)
        UV.append(st)
        J.append((node, b["parent"] if b and b["parent"] is not None else node))
        W.append(w)
        return len(P) - 1

    for s in strips:
        tex = tex_of(s["tbp"], s["cbp"])
        idx = [add_vertex(pos, w, nrm, st, s["node"]) for pos, w, nrm, st in s["verts"]]
        for i in range(len(idx) - 2):
            tris.append((tex, s["node"], idx[i], idx[i + 1], idx[i + 2]))
    for sec in seams:
        base = len(P)
        for pos, w, nrm, uv, node in sec["verts"]:
            add_vertex(pos, w, nrm, uv, node if node in bones else 0)
        for (a, b, c), tbp, cbp in sec["tris"]:
            tris.append((tex_of(tbp, cbp), sec["verts"][a][4], base + a, base + b, base + c))

    # game axes (Y down, face -Z) -> Y up, face +Z: turn 180 degrees about X
    P = [(x, -y, -z) for x, y, z in P]
    N = [(x, -y, -z) for x, y, z in N]

    def sub(a, b):
        return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    out_tris, dropped = [], 0
    for tex, node, a, b, c in tris:
        if hide_optional and node in OPTIONAL_NODES:
            continue
        u, v = sub(P[b], P[a]), sub(P[c], P[a])
        fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2] < 1e-14:
            dropped += 1          # degenerate (strip joins)
            continue
        vn = tuple(N[a][k] + N[b][k] + N[c][k] for k in range(3))
        if fn[0] * vn[0] + fn[1] * vn[1] + fn[2] * vn[2] < 0:
            b, c = c, b
        out_tris.append((tex, node, a, b, c))
    out_tris.sort(key=lambda t: (t[0], t[1]))

    indices, groups, part_ranges = [], [], []
    for tex, node, a, b, c in out_tris:
        if not groups or groups[-1]["texture"] != tex:
            groups.append({"texture": tex, "start": len(indices), "count": 0})
        if not part_ranges or part_ranges[-1]["node"] != node or part_ranges[-1]["texture"] != tex:
            part_ranges.append({"node": node, "texture": tex, "start": len(indices), "count": 0,
                                "optional": node in OPTIONAL_NODES})
        indices += (a, b, c)
        groups[-1]["count"] += 3
        part_ranges[-1]["count"] += 3

    used = sorted({g["texture"] for g in groups if g["texture"] >= 0})
    texs = []
    for t in tex_list:
        if t["index"] in used and t["rgba"]:
            texs.append({"index": t["index"], "width": t["width"], "height": t["height"],
                         "png": base64.b64encode(png(t["width"], t["height"], t["rgba"])).decode("ascii")})
    main = max(groups, key=lambda g: g["count"])["texture"] if groups else -1
    texture_png = next((t["png"] for t in texs if t["index"] == main), None)

    def axes(j):
        return [j[0], -j[1], -j[2]]

    bone_out = []
    for n, b in sorted(bones.items()):
        head = b["joint"]
        if mats is not None and n in mats:
            head = mats[n][1]
        parent_head = None
        if b["parent"] is not None:
            parent_head = b["parentJoint"] if mats is None else mats[b["parent"]][1]
        bone_out.append({"node": n, "parent": b["parent"], "head": axes(head),
                         "parentHead": axes(parent_head) if parent_head else None})

    used_v = sorted(set(indices))
    lo = [min(P[i][k] for i in used_v) for k in range(3)] if used_v else [0, 0, 0]
    hi = [max(P[i][k] for i in used_v) for k in range(3)] if used_v else [0, 0, 0]
    return {
        "positions": [c for p in P for c in p],
        "normals": [c for n in N for c in n],
        "uvs": [c for t in UV for c in t],
        "indices": indices,
        "groups": groups,
        "textures": texs,
        "texture_png": texture_png,
        "parts": part_ranges,
        "bones": bone_out,
        "joints": [c for j in J for c in j],
        "weights": W,
        "bbox": {"min": lo, "max": hi},
        "header": hdr,
        "stats": {"vertices": len(P), "triangles": len(indices) // 3, "strips": len(strips),
                  "degenerate_dropped": dropped, "seam_vertices": sum(len(s["verts"]) for s in seams),
                  "pose": pose},
    }


# --------------------------------------------------------------------------------------------------- output
def write_obj(m, prefix):
    prefix = pathlib.Path(prefix)
    name = prefix.name
    tex_files = {}
    for t in m["textures"]:
        f = prefix.with_name(f"{name}_tex{t['index']}.png")
        f.write_bytes(base64.b64decode(t["png"]))
        tex_files[t["index"]] = f.name
    with open(prefix.with_suffix(".mtl"), "w", encoding="ascii") as f:
        for g in sorted({g["texture"] for g in m["groups"]}):
            f.write(f"newmtl tex{g}\nKd 1 1 1\n")
            if g in tex_files:
                f.write(f"map_Kd {tex_files[g]}\n")
            f.write("\n")
    p, n, uv = m["positions"], m["normals"], m["uvs"]
    with open(prefix.with_suffix(".obj"), "w", encoding="ascii") as f:
        f.write(f"# {m['stats']}\nmtllib {prefix.with_suffix('.mtl').name}\n")
        for i in range(0, len(p), 3):
            f.write("v %.5f %.5f %.5f\n" % (p[i], p[i + 1], p[i + 2]))
        for i in range(0, len(uv), 2):
            f.write("vt %.5f %.5f\n" % (uv[i], 1.0 - uv[i + 1]))   # OBJ: v up
        for i in range(0, len(n), 3):
            f.write("vn %.4f %.4f %.4f\n" % (n[i], n[i + 1], n[i + 2]))
        idx = m["indices"]
        for part in m["parts"]:
            f.write(f"g node_{part['node']:02x}\nusemtl tex{part['texture']}\n")
            for i in range(part["start"], part["start"] + part["count"], 3):
                a, b, c = idx[i] + 1, idx[i + 1] + 1, idx[i + 2] + 1
                f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
    return prefix.with_suffix(".obj")


def render(m, size=512, view="front", textured=True):
    """Orthographic view (front: looking along -Z, back: along +Z, side: along -X) with a z-buffer, texture and a
    little shading. Returns PNG bytes."""
    p, uv, idx, nr = m["positions"], m["uvs"], m["indices"], m["normals"]
    lo, hi = m["bbox"]["min"], m["bbox"]["max"]
    ax = 2 if view == "side" else 0
    span = max(hi[ax] - lo[ax], hi[1] - lo[1]) * 1.05
    cx, cy = (hi[ax] + lo[ax]) / 2, (hi[1] + lo[1]) / 2
    sc = size / span
    texs = {}
    if textured:
        for t in m["textures"]:
            texs[t["index"]] = (t["width"], t["height"], decode_png_rgba(base64.b64decode(t["png"])))
    tri_tex = [0] * (len(idx) // 3)
    for g in m["groups"]:
        for i in range(g["start"] // 3, (g["start"] + g["count"]) // 3):
            tri_tex[i] = g["texture"]
    W = H = size
    zb = [-1e30] * (W * H)
    img = bytearray(b"\x30\x30\x38\xff" * (W * H))

    def proj(i):
        if view == "side":
            x, depth = -p[i * 3 + 2], p[i * 3]
        elif view == "back":
            x, depth = -p[i * 3], -p[i * 3 + 2]
        else:
            x, depth = p[i * 3], p[i * 3 + 2]
        return ((x - (cx if view != "back" else -cx)) * sc + W / 2, (cy - p[i * 3 + 1]) * sc + H / 2, depth)

    L = (0.3, 0.5, 0.8)
    ln = math.sqrt(sum(c * c for c in L))
    L = tuple(c / ln for c in L)
    for t in range(len(idx) // 3):
        a, b, c = idx[t * 3:t * 3 + 3]
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = proj(a), proj(b), proj(c)
        den = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(den) < 1e-9:
            continue
        tex = texs.get(tri_tex[t])
        nsum = [nr[a * 3 + k] + nr[b * 3 + k] + nr[c * 3 + k] for k in range(3)]
        nl = math.sqrt(sum(v * v for v in nsum)) or 1
        shade = 0.55 + 0.45 * max(0.0, sum(nsum[k] / nl * L[k] for k in range(3)))
        minx, maxx = max(0, int(min(x0, x1, x2))), min(W - 1, int(max(x0, x1, x2)) + 1)
        miny, maxy = max(0, int(min(y0, y1, y2))), min(H - 1, int(max(y0, y1, y2)) + 1)
        for py in range(miny, maxy + 1):
            fy = py + 0.5
            for px in range(minx, maxx + 1):
                fx = px + 0.5
                w1 = ((fx - x0) * (y2 - y0) - (x2 - x0) * (fy - y0)) / den
                w2 = ((x1 - x0) * (fy - y0) - (fx - x0) * (y1 - y0)) / den
                w0 = 1 - w1 - w2
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * z0 + w1 * z1 + w2 * z2
                k = py * W + px
                if z <= zb[k]:
                    continue
                if tex:
                    tw, th, data = tex
                    u = w0 * uv[a * 2] + w1 * uv[b * 2] + w2 * uv[c * 2]
                    v = w0 * uv[a * 2 + 1] + w1 * uv[b * 2 + 1] + w2 * uv[c * 2 + 1]
                    ti = ((int(math.floor(v * th)) % th) * tw + int(math.floor(u * tw)) % tw) * 4
                    col = data[ti:ti + 3]
                    if data[ti + 3] < 64:
                        continue
                else:
                    col = (200, 200, 200)
                zb[k] = z
                img[k * 4:k * 4 + 4] = bytes((min(255, int(col[0] * shade)), min(255, int(col[1] * shade)),
                                              min(255, int(col[2] * shade)), 255))
    return png(W, H, img)


def decode_png_rgba(d):
    """Reads back the PNGs written by png() (8-bit RGBA, filter 0)."""
    w, h = struct.unpack_from(">II", d, 16)
    p, idat = 8, b""
    while p < len(d):
        n = struct.unpack_from(">I", d, p)[0]
        if d[p + 4:p + 8] == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    raw = zlib.decompress(idat)
    return b"".join(raw[y * (w * 4 + 1) + 1:(y + 1) * (w * 4 + 1)] for y in range(h))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("pack", help="a character model pack (gamedata/pzs3us1/NNNNN.bin, e.g. 01453.bin = Goku (End))")
    ap.add_argument("-o", "--out", help="output prefix (default: the pack's name, next to it)")
    ap.add_argument("--pose", default="rest", choices=("rest", "relaxed"))
    ap.add_argument("--hide-optional", action="store_true", help="leave out costume extras (halo and the like)")
    ap.add_argument("--render", help="write an orthographic front view PNG here")
    ap.add_argument("--side", help="write an orthographic side view PNG here")
    ap.add_argument("--back", help="write an orthographic back view PNG here")
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--json", help="write the extract() dict as JSON here")
    a = ap.parse_args(argv)
    m = extract(pathlib.Path(a.pack).read_bytes(), pose=a.pose, hide_optional=a.hide_optional)
    prefix = pathlib.Path(a.out) if a.out else pathlib.Path(a.pack).with_suffix("")
    obj = write_obj(m, prefix)
    lo, hi = m["bbox"]["min"], m["bbox"]["max"]
    size = [hi[k] - lo[k] for k in range(3)]
    print(f"{obj}: {m['stats']['vertices']} vertices, {m['stats']['triangles']} triangles, "
          f"{len(m['textures'])} textures, {len(m['bones'])} nodes")
    print("size (w, h, d): %.2f %.2f %.2f  height / width %.2f" % (size[0], size[1], size[2], size[1] / max(size[0], 1e-9)))
    if a.render:
        pathlib.Path(a.render).write_bytes(render(m, a.size, "front"))
        print("front view:", a.render)
    if a.side:
        pathlib.Path(a.side).write_bytes(render(m, a.size, "side"))
        print("side view:", a.side)
    if a.back:
        pathlib.Path(a.back).write_bytes(render(m, a.size, "back"))
        print("back view:", a.back)
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(m), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
