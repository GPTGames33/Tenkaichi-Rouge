#!/usr/bin/env python3
"""A fighter's pictures from one RGBA picture (a PNG screenshot of its 3D preview, any size, clear background).

    python portrait.py make <in.png> --template <portrait file> -o portrait.bin
                       [--framing auto|body|fit] [--chip chip.bin --chip-template <chip file>]
                       [--member1 member_1p.bin --member2 member_2p.bin --member-template <member file(s)>]
                       [--preview out.png]
    python portrait.py test <gamedata/pzs3us1 folder> <out folder>       (round trips and sizes, see test())

What it writes (Dragon Ball Z Budokai Tenkaichi 3, PS2 USA; docs/port/character_packages.md):

* portrait.bin: the large menu picture (disc file 0x2F9 + chara). Byte-pair packed (the game's sys/bpe.c; the
  unpacked size and the packed size, then blocks: a pair table, a 2-byte size hi/lo, the packed bytes), unpacked a
  texture file: at +0x20 the word offsets of the pixels (+0x60: a 128 x 256 32-bit upload holding the 256 x 512
  8-bit picture in the GS's PSMT8 layout) and of the CLUT (+0x60: 256 RGBA entries in CSM1 order, alpha 0..0x80).
  Everything but those two blocks of data is kept from a template (a real portrait file). The game reads it into
  0x16800 = 92,160 bytes, so the packed file must fit that; colours are reduced until it does.
  The menus show the 256 x 512 picture twice as wide (512 x 512 on the screen, as port/src/gs/modmgr.c art_fit
  draws it): the framing is worked out on a 512 x 512 "screen" square and squeezed to 256 wide.
* chip.bin: the roster square (one 5,504-byte entry of the select's chip pack): a 64 x 64 texture of the same kind,
  unpacked; the disc's chips hold the face in x 8..55, y 11..51 and leave the rest clear.
* member_1p.bin / member_2p.bin: the fight's team picture (disc file 8 + chara*2 + side, 3,424 bytes, at most
  0x1000): a 64 x 32 texture, the eyes in a slanted band (y 13..28); the 2p one is the 1p one mirrored. The band's
  shape (its alpha) is taken from the template.

Standard library only.
"""
import argparse, itertools, operator, pathlib, struct, sys, time, zlib
from array import array
from collections import Counter

ART_W, ART_H = 256, 512
PORTRAIT_MAX = 0x16800          # the buffer every menu reads a portrait into (plat_charas.c PORTRAIT_MAX)
CHIP_SIZE = 5504
MEMBER_MAX = 0x1000


# ============================================================================================ PNG in and out
def png_read(data):
    """(w, h, RGBA bytearray) of a PNG (8 or 16 bits a channel, grey/RGB/palette, with or without alpha)."""
    if isinstance(data, (str, pathlib.Path)):
        data = pathlib.Path(data).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    at, idat, pal, trns, head = 8, [], None, None, None
    while at + 8 <= len(data):
        n, tag = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + n]
        at += 12 + n
        if tag == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"PLTE":
            pal = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IEND":
            break
    w, h, depth, kind, _, _, lace = head
    if lace:
        raise ValueError("interlaced PNG: save it without interlacing")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[kind]
    if depth < 8 and kind != 3 and kind != 0:
        raise ValueError("unsupported PNG bit depth")
    bpp = max(1, chans * depth // 8)
    line = (w * chans * depth + 7) // 8
    raw = zlib.decompress(b"".join(idat))
    rows, prev = [], bytes(line)
    for y in range(h):
        f = raw[y * (line + 1)]
        cur = raw[y * (line + 1) + 1:(y + 1) * (line + 1)]
        cur = _unfilter(f, cur, prev, bpp)
        rows.append(cur)
        prev = cur
    out = bytearray(w * h * 4)
    for y, r in enumerate(rows):
        o = y * w * 4
        if depth == 16:
            r = r[0::2]
        if kind == 6:
            out[o:o + w * 4] = r
        elif kind == 2:
            dst = memoryview(out)[o:o + w * 4]
            dst[0::4], dst[1::4], dst[2::4] = r[0::3], r[1::3], r[2::3]
            dst[3::4] = b"\xff" * w
        elif kind == 0 or kind == 4:
            if kind == 0 and depth < 8:
                r = _unpack_bits(r, w, depth)
                r = bytes(v * 255 // ((1 << depth) - 1) for v in r)
            g = r[0::2] if kind == 4 else r
            dst = memoryview(out)[o:o + w * 4]
            dst[0::4] = dst[1::4] = dst[2::4] = g[:w]
            dst[3::4] = r[1::2] if kind == 4 else b"\xff" * w
        else:
            idx = r if depth == 8 else _unpack_bits(r, w, depth)
            lut = []
            for i in range(256):
                c = pal[i * 3:i * 3 + 3] if pal and i * 3 + 3 <= len(pal) else b"\0\0\0"
                a = trns[i] if trns is not None and i < len(trns) else 255
                lut.append(c + bytes((a,)))
            out[o:o + w * 4] = b"".join(lut[i] for i in idx[:w])
    return w, h, out


def _unpack_bits(r, w, depth):
    per = 8 // depth
    mask = (1 << depth) - 1
    return bytes((r[x // per] >> (8 - depth * (x % per + 1))) & mask for x in range(w))


_M7 = None


def _unfilter(f, cur, prev, bpp):
    n = len(cur)
    if f == 0:
        return bytes(cur)
    if f == 2:  # up: bytewise add without carries, on big integers (SWAR)
        global _M7
        if _M7 is None or len(_M7[0]) != n:
            _M7 = (b"\x7f" * n, int.from_bytes(b"\x7f" * n, "little"), int.from_bytes(b"\x80" * n, "little"))
        a, b = int.from_bytes(cur, "little"), int.from_bytes(prev, "little")
        s = ((a & _M7[1]) + (b & _M7[1])) ^ ((a ^ b) & _M7[2])
        return s.to_bytes(n, "little")
    out = bytearray(cur)
    if f == 1:  # sub: a running sum per channel
        for k in range(bpp):
            out[k::bpp] = bytes(map((255).__and__, itertools.accumulate(cur[k::bpp])))
        return bytes(out)
    if f == 3:
        for i in range(n):
            a = out[i - bpp] if i >= bpp else 0
            out[i] = (out[i] + ((a + prev[i]) >> 1)) & 255
        return bytes(out)
    if f == 4:
        if not any(prev):  # (paeth over an empty line is sub)
            return _unfilter(1, cur, prev, bpp)
        for i in range(n):
            if i >= bpp:
                a, c = out[i - bpp], prev[i - bpp]
            else:
                a = c = 0
            b = prev[i]
            pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
            out[i] = (out[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        return bytes(out)
    raise ValueError("bad PNG filter")


def png_write(w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def as_picture(rgba_or_png):
    """(w, h, RGBA bytearray) from PNG bytes, a path, or a (w, h, rgba) tuple."""
    if isinstance(rgba_or_png, tuple):
        w, h, px = rgba_or_png
        if len(px) != w * h * 4:
            raise ValueError("RGBA size does not match w x h")
        return w, h, bytearray(px)
    return png_read(rgba_or_png)


# ============================================================================================ resampling
def resample(pic, x0, y0, cw, ch, tw, th):
    """The source rectangle (x0, y0, cw, ch) (floats, may reach outside: clear there) as tw x th RGBA, an
    area-averaged box filter in premultiplied alpha (at least one source pixel wide: smooth when enlarging)."""
    w, h, px = pic
    sx, sy = cw / tw, ch / th

    def spans(o, s, size, n, limit):
        bw = max(s, 1.0)
        out = []
        for i in range(n):
            c = o + (i + 0.5) * s
            a, b = c - bw / 2, c + bw / 2
            out.append((min(max(a, 0.0), limit), min(max(b, 0.0), limit), bw))
        return out
    xs = spans(x0, sx, w, tw, float(w))
    ys = spans(y0, sy, h, th, float(h))
    rlo = max(0, int(min(a for a, b, _ in ys)))
    rhi = min(h, int(max(b for a, b, _ in ys)) + 1)
    mul = operator.mul

    def integ(P, v, t):  # the integral of a piecewise constant row up to t, from its prefix sums
        i = int(t)
        if i >= len(v):
            return P[-1]
        return P[i] + (t - i) * v[i]
    # horizontal pass: per source row, the 4 premultiplied sums of each output column
    hr = {}
    for y in range(rlo, rhi):
        row = px[y * w * 4:(y + 1) * w * 4]
        A = row[3::4]
        if not any(A):
            hr[y] = None
            continue
        chans = [list(map(mul, row[k::4], A)) for k in range(3)] + [list(A)]
        sums = []
        for v in chans:
            P = list(itertools.accumulate(v, initial=0))
            sums.append([(integ(P, v, b) - integ(P, v, a)) / bw for a, b, bw in xs])
        hr[y] = sums
    out = bytearray(tw * th * 4)
    # vertical pass, column by column
    for x in range(tw):
        cols = []
        for k in range(4):
            v = [hr[y][k][x] if hr[y] is not None else 0.0 for y in range(rlo, rhi)]
            cols.append((v, list(itertools.accumulate(v, initial=0))))
        for yy, (a, b, bw) in enumerate(ys):
            a -= rlo
            b -= rlo
            if b <= a:
                continue
            r, g, bl, al = ((integ(P, v, b) - integ(P, v, a)) / bw for v, P in cols)
            if al > 0.5:
                o = (yy * tw + x) * 4
                out[o] = min(255, int(r / al + 0.5))
                out[o + 1] = min(255, int(g / al + 0.5))
                out[o + 2] = min(255, int(bl / al + 0.5))
                out[o + 3] = min(255, int(al + 0.5))
    return out


def alpha_box(pic, thresh=128):
    """(left, top, right, bottom) (inclusive) of the pixels at least `thresh` opaque, or None."""
    w, h, px = pic
    lut = bytes(1 if v >= thresh else 0 for v in range(256))
    top = bot = None
    lo, hi = w, -1
    for y in range(h):
        a = px[y * w * 4 + 3:(y + 1) * w * 4:4].translate(lut)
        i = a.find(1)
        if i < 0:
            continue
        top = y if top is None else top
        bot = y
        lo = min(lo, i)
        hi = max(hi, a.rfind(1))
    return None if top is None else (lo, top, hi, bot)


def head_centre(pic, box, frac=0.12):
    """The middle (x) of what is opaque in the top `frac` of the figure: the head."""
    w, h, px = pic
    lo, top, hi, bot = box
    hb = bot - top + 1
    sx = n = 0
    for y in range(top, min(bot + 1, top + max(2, int(hb * frac)))):
        a = px[y * w * 4 + 3:(y + 1) * w * 4:4]
        for x in range(lo, hi + 1):
            if a[x] >= 128:
                sx += x
                n += 1
    return sx / n if n else (lo + hi) / 2


def frame(pic, framing="auto", squeeze=2.0):
    """The picture as the 256 x 512 stored portrait (RGBA, alpha 0..255).

    The disc's portraits (decoded and looked at on a 512 x 512 screen square: the menus show them twice as wide):
    the fighter from the top of the head (y 0..70 of 512) down to the belt or the thighs, the face near the middle
    (x ~250 of 512, y 140..190), the figure filling the width and leaving the picture at the sides and the bottom,
    the background clear. 'auto' does that for a full-body picture: the square is half the figure's height, its top
    a little above the head, centred on the head. 'body': the whole figure in the square. 'fit': the whole picture
    in the square, its shape kept (as the port's own manager does). squeeze: how much wider the menus show it."""
    w, h, _ = pic
    box = alpha_box(pic)
    if framing == "fit" or box is None:
        side = max(w, h)
        x0, y0 = (w - side) / 2, (h - side) / 2
    elif framing == "body":
        lo, top, hi, bot = box
        side = max(hi - lo + 1, bot - top + 1) * 1.04
        x0, y0 = (lo + hi + 1) / 2 - side / 2, (top + bot + 1) / 2 - side / 2
    elif framing == "auto":
        lo, top, hi, bot = box
        hb = bot - top + 1
        side = hb * 0.5
        cx = head_centre(pic, box)
        x0, y0 = cx - side / 2, top - side * 0.06
    else:
        raise ValueError("framing is auto, body or fit")
    # the screen square is side x side (512 x 512 on the screen); the 256 stored columns are `squeeze` screen
    # pixels each, so they cover side * squeeze / 2 of the source, about the square's middle
    sw = side * squeeze / 2.0
    return resample(pic, x0 + (side - sw) / 2, y0, sw, side, ART_W, ART_H)


# ============================================================================================ colours
def quantize(rgba, colours=256, clear_index=0):
    """(palette of `colours` (r, g, b, a 0..0x80) tuples, index bytes) for RGBA (alpha 0..255). What is clear (PS2
    alpha 0) is index clear_index (0, 0, 0, 0); the rest a median cut over a 5-bit histogram, then k-means steps."""
    n = len(rgba) // 4
    A2 = bytes(((a + 1) >> 1) for a in range(256))  # 255 -> 0x80
    al = rgba[3::4].translate(A2)
    keys = list(map(lambda r, g, b, a: (r >> 3) << 15 | (g >> 3) << 10 | (b >> 3) << 5 | (a >> 3) if a else -1,
                    rgba[0::4], rgba[1::4], rgba[2::4], al))
    hist = Counter(keys)
    hist.pop(-1, None)
    # exact sums per key, for the means
    sums = {}
    for i, k in enumerate(keys):
        if k >= 0:
            s = sums.get(k)
            o = i * 4
            if s is None:
                sums[k] = [rgba[o], rgba[o + 1], rgba[o + 2], al[i]]
            else:
                s[0] += rgba[o]
                s[1] += rgba[o + 1]
                s[2] += rgba[o + 2]
                s[3] += al[i]
    pts = []  # (r, g, b, a*2 (as weighed), count, key)
    for k, c in hist.items():
        s = sums[k]
        pts.append((s[0] / c, s[1] / c, s[2] / c, s[3] / c * 2.0, c, k))
    want = colours - 1
    if len(pts) <= want:
        cents = [p[:4] for p in pts]
    else:
        cents, order = _kmeans(pts, _median_cut(pts, want), 3)
    if len(pts) <= want:
        order = list(range(len(pts)))
    pal = [(0, 0, 0, 0)] * colours
    acc = [[0.0, 0.0, 0.0, 0.0, 0] for _ in cents]
    for p, j in zip(pts, order):
        a = acc[j]
        c = p[4]
        a[0] += p[0] * c
        a[1] += p[1] * c
        a[2] += p[2] * c
        a[3] += p[3] * c
        a[4] += c
    slots = [i for i in range(colours) if i != clear_index]
    entry = {}
    for j, a in enumerate(acc):
        if a[4]:
            pal[slots[j]] = (int(a[0] / a[4] + .5), int(a[1] / a[4] + .5), int(a[2] / a[4] + .5),
                             max(1, min(0x80, int(a[3] / a[4] / 2 + .5))))
    for p, j in zip(pts, order):
        entry[p[5]] = slots[j]
    entry[-1] = clear_index
    return pal, bytes(map(entry.__getitem__, keys))


def _median_cut(pts, want):
    def stats(box):
        tot = sum(p[4] for p in box)
        best = (-1.0, 0)
        for ch in range(4):
            m = sum(p[ch] * p[4] for p in box) / tot
            v = sum((p[ch] - m) ** 2 * p[4] for p in box)
            if v > best[0]:
                best = (v, ch)
        return best[0], best[1], tot
    boxes = [(pts,) + stats(pts)]
    while len(boxes) < want:
        i = max(range(len(boxes)), key=lambda k: boxes[k][1] if len(boxes[k][0]) > 1 else -1)
        box, err, ch, tot = boxes[i]
        if len(box) < 2 or err <= 0:
            break
        box = sorted(box, key=lambda p: p[ch])
        run, cut = 0, 1
        for k in range(len(box) - 1):
            run += box[k][4]
            cut = k + 1
            if run * 2 >= tot:
                break
        a, b = box[:cut], box[cut:]
        boxes[i] = (a,) + stats(a)
        boxes.append((b,) + stats(b))
    out = []
    for box, _, _, tot in boxes:
        out.append(tuple(sum(p[ch] * p[4] for p in box) / tot for ch in range(4)))
    return out


def _assign(pts, cents, guess=None, near=None):
    out = []
    for i, p in enumerate(pts):
        r, g, b, a = p[0], p[1], p[2], p[3]
        cand = near[guess[i]] if guess is not None else range(len(cents))
        best, bd = 0, 1e18
        for j in cand:
            c = cents[j]
            d = (r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2 + (a - c[3]) ** 2
            if d < bd:
                bd, best = d, j
        out.append(best)
    return out


def _kmeans(pts, cents, steps):
    cents = list(cents)
    order = None
    for step in range(steps):
        if order is None and len(pts) * len(cents) > 1_500_000:
            # a first assignment through the nearest few centres of a coarse guess (the brightest-sorted list)
            srt = sorted(range(len(cents)), key=lambda j: sum(cents[j]))
            near = {j: srt[max(0, k - 24):k + 24] for k, j in enumerate(srt)}
            lum = [sum(p[:4]) for p in pts]
            import bisect
            sv = [sum(cents[j]) for j in srt]
            guess = [srt[min(len(srt) - 1, bisect.bisect_left(sv, l))] for l in lum]
            order = _assign(pts, cents, guess, near)
        elif order is None:
            order = _assign(pts, cents)
        else:
            near = _neighbours(cents, 16)
            order = _assign(pts, cents, order, near)
        acc = [[0.0, 0.0, 0.0, 0.0, 0] for _ in cents]
        for p, j in zip(pts, order):
            a = acc[j]
            c = p[4]
            a[0] += p[0] * c
            a[1] += p[1] * c
            a[2] += p[2] * c
            a[3] += p[3] * c
            a[4] += c
        cents = [tuple(v / a[4] for v in a[:4]) if a[4] else cents[j] for j, a in enumerate(acc)]
    order = _assign(pts, cents, order, _neighbours(cents, 16))
    return cents, order


def _neighbours(cents, k):
    out = {}
    for j, c in enumerate(cents):
        d = sorted(range(len(cents)), key=lambda i: sum((c[t] - cents[i][t]) ** 2 for t in range(4)))
        out[j] = d[:k]
    return out


# ============================================================================================ byte pairs
def bpe_decode(d):
    """The game's Bpe_Decode (src/sys/bpe.c), as port/tools/creator/creator.py has it."""
    total, pack = struct.unpack_from("<II", d)
    if total == 0 or total > (8 << 20) or pack + 8 > len(d):
        return None
    p, end, out = 8, 8 + pack, bytearray()
    while p < end and len(out) < total:
        left, right = list(range(256)), [0] * 256
        c = 0
        count = d[p]; p += 1
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c >= 256:
                break
            i = 0
            while i <= count and c < 256:
                v = d[p]; p += 1
                left[c] = v
                if c != v:
                    right[c] = d[p]; p += 1
                i += 1
                c += 1
            if c >= 256:
                break
            count = d[p]; p += 1
        size16 = d[p] * 256 + d[p + 1]
        p += 2
        stack = []
        while True:
            if stack:
                ch = stack.pop()
            else:
                if size16 == 0:
                    break
                size16 -= 1
                ch = d[p]; p += 1
            if ch == left[ch]:
                out.append(ch)
            else:
                stack.append(right[ch])
                stack.append(left[ch])
    return bytes(out[:total])


def _bpe_block(data, threshold=4, max_depth=96):
    """One block: (pair table bytes + size + packed bytes). Philip Gage's byte pair encoding: the most frequent pair
    replaced by a byte the block never had, again and again; a code's pair may hold earlier codes (never a byte
    that was in the block), so expanding it is exact."""
    used = set(data)
    free = [c for c in range(256) if c not in used]
    left = list(range(256))
    right = [0] * 256
    depth = [0] * 256
    buf = bytes(data)
    while free and len(buf) > 3:
        n = len(buf) & ~1
        ev = array("H")
        ev.frombytes(buf[:n])
        od = array("H")
        od.frombytes(buf[1:1 + ((len(buf) - 1) & ~1)])
        cnt = Counter(ev)
        cnt.update(od)
        # the most frequent pairs that share no byte with each other: replacing one leaves the others' counts as
        # they are, so several are replaced for one count of the pairs (Gage's order, nearly)
        taken, syms, floor = [], set(), threshold
        for k, c in cnt.most_common(48):
            floor = max(floor, c * 3 // 4) if not taken else floor  # (only pairs nearly as frequent as the first)
            if c < floor or len(taken) >= len(free):
                break
            a, b = k & 255, k >> 8  # (little-endian: the first byte is the low one)
            if a in syms or b in syms or max(depth[a], depth[b]) + 1 > max_depth:
                continue
            pair = bytes((a, b))
            if a == b and buf.count(pair) < threshold:
                continue
            taken.append((a, b, pair))
            syms.update((a, b))
            if len(taken) >= 24:
                break
        if not taken:
            break
        for a, b, pair in taken:
            code = free.pop()
            left[code], right[code] = a, b
            depth[code] = max(depth[a], depth[b]) + 1
            buf = buf.replace(pair, bytes((code,)))
    out = bytearray()

    def entry(c):
        out.append(left[c])
        if left[c] != c:
            out.append(right[c])
    c = 0
    while c < 256:
        if left[c] == c:  # a run of codes that stand for themselves: a count above 127 skips it, then one entry
            run = 1
            while c + run < 256 and left[c + run] == c + run:
                run += 1
            s = min(run, 128)
            out.append(127 + s)
            c += s
            if c == 256:
                break
            entry(c)
            c += 1
        else:  # a count n, then n + 1 entries (a lone unused code between pairs is listed as itself)
            n = 1
            while c + n < 256 and n < 128 and (left[c + n] != c + n or (c + n + 1 < 256 and left[c + n + 1] != c + n + 1)):
                n += 1
            out.append(n - 1)
            for k in range(n):
                entry(c + k)
            c += n
    if len(buf) > 0x7FFF:
        raise ValueError("block too large")
    out += bytes((len(buf) >> 8, len(buf) & 255)) + buf
    return bytes(out)


def bpe_encode(data, block=8192, chars=216, threshold=4):
    """Bytes as the game's Bpe_Decode reads them: the unpacked size, the packed size, then the blocks, padded to 16
    bytes as the disc's files are. A block ends at `block` bytes or before the byte that would make it hold more than
    `chars` different bytes (the rest are its codes for pairs)."""
    out, at, n = bytearray(), 0, len(data)
    while at < n:
        end, seen = at, set()
        lim = min(n, at + min(block, 0x7FFF))
        while end < lim:
            b = data[end]
            if b not in seen:
                if len(seen) >= chars:
                    break
                seen.add(b)
            end += 1
        out += _bpe_block(data[at:end], threshold)
        at = end
    res = struct.pack("<II", n, len(out)) + bytes(out)
    return res + bytes(-len(res) % 16)


def bpe_best(data):
    """The smallest of a few block shapes (block length, most different bytes a block may hold)."""
    best = None
    for block, chars in ((8192, 216), (16384, 224), (4096, 200)):
        enc = bpe_encode(data, block, chars)
        if best is None or len(enc) < len(best):
            best = enc
    return best


# ============================================================================================ texture files
def tex_parse(body):
    """(w, h, pixels at, CLUT at) of an unpacked texture file of the game (one 8-bit texture, 256 colours)."""
    if len(body) < 0x60 or struct.unpack_from("<I", body, 0)[0] != 1:
        raise ValueError("not a texture file of the game (one texture with a palette)")
    rec = struct.unpack_from("<I", body, 4)[0] * 4
    pix, clut, pix_size, clut_size = struct.unpack_from("<IIii", body, rec)
    tex0 = struct.unpack_from("<Q", body, rec + 0x30)[0]
    psm, w, h = tex0 >> 20 & 0x3F, 1 << (tex0 >> 26 & 0xF), 1 << (tex0 >> 30 & 0xF)
    pix, clut = pix * 4 + 0x60, clut * 4 + 0x60
    if psm != 0x13 or pix + w * h > len(body) or clut + 1024 > len(body):
        raise ValueError("a texture this tool does not know (not 8-bit with 256 colours)")
    return w, h, pix, clut


def pic_place(x, y, w):
    """Where pixel (x, y) of a PSMT8 texture `w` wide lies in its data uploaded as 32-bit (the same as creator.py's
    addr32/addr8 source_index for the portrait: checked for every pixel in test())."""
    block = (y & ~0xF) * w + (x & ~0xF) * 2
    swap = ((y + 2) >> 2 & 1) * 4
    row = ((y & ~3) >> 1) + (y & 1) & 7
    return block + row * w * 2 + ((x + swap) & 7) * 4 + (y >> 1 & 1) + (x >> 2 & 2)


def csm1(i):
    return i + 8 if (i & 0x18) == 8 else i - 8 if (i & 0x18) == 16 else i


_PLACE = {}


def tex_write(template_body, pal, idx):
    """The template texture file with its pixels and CLUT replaced (everything else kept)."""
    body = bytearray(template_body)
    w, h, pix, clut = tex_parse(body)
    if len(idx) != w * h:
        raise ValueError("picture size does not match the template's texture")
    place = _PLACE.get(w * 65536 + h)
    if place is None:
        place = _PLACE[w * 65536 + h] = [pic_place(x, y, w) for y in range(h) for x in range(w)]
    data = bytearray(w * h)
    for i, v in zip(place, idx):
        data[i] = v
    body[pix:pix + w * h] = data
    c = bytearray(1024)
    for i, (r, g, b, a) in enumerate(pal):
        k = csm1(i) * 4
        c[k:k + 4] = bytes((r, g, b, a))
    body[clut:clut + 1024] = c
    return bytes(body)


def tex_rgba(body):
    """(w, h, RGBA, alpha doubled as the port shows it) of an unpacked texture file."""
    w, h, pix, clut = tex_parse(body)
    pal = []
    for i in range(256):
        e = body[clut + csm1(i) * 4:clut + csm1(i) * 4 + 4]
        pal.append(bytes((e[0], e[1], e[2], 255 if e[3] >= 0x80 else e[3] * 2)))
    return w, h, b"".join(pal[body[pix + pic_place(x, y, w)]] for y in range(h) for x in range(w))


def _unpack_template(template_bytes):
    if struct.unpack_from("<I", template_bytes, 0)[0] == 1:
        return bytes(template_bytes)  # (kept unpacked)
    raw = bpe_decode(template_bytes)
    if raw is None:
        raise ValueError("the template is not a packed texture file")
    return raw


# ============================================================================================ the three files
def portrait_rgba(rgba_or_png, framing="auto", squeeze=2.0):
    """The 256 x 512 stored picture (RGBA) a picture gives: framed (see frame()), or taken as it is if it is
    already 256 x 512 and framing is 'as-is'."""
    pic = as_picture(rgba_or_png)
    if framing == "as-is":
        if pic[:2] != (ART_W, ART_H):
            pic = (ART_W, ART_H, resample(pic, 0, 0, pic[0], pic[1], ART_W, ART_H))
        return bytes(pic[2])
    return bytes(frame(pic, framing, squeeze))


def portrait_bin(rgba_or_png, template_bytes, framing="auto", squeeze=2.0, stored=None, info=None):
    """portrait.bin (packed, at most 92,160 bytes) from a picture and a real portrait file (packed or not)."""
    body = _unpack_template(template_bytes)
    w, h, pix, clut = tex_parse(body)
    if (w, h) != (ART_W, ART_H):
        raise ValueError(f"the template is a {w} x {h} texture, not a portrait (256 x 512)")
    px = stored if stored is not None else portrait_rgba(rgba_or_png, framing, squeeze)
    for colours in (256, 192, 128, 96, 64, 48, 32, 16):
        pal, idx = quantize(px, colours)
        raw = tex_write(body, pal + [(0, 0, 0, 0)] * (256 - len(pal)), idx)
        packed = bpe_encode(raw)
        if len(packed) > PORTRAIT_MAX * 1.12:  # (far over: fewer colours straight away)
            continue
        packed = bpe_best(raw)
        if bpe_decode(packed) != raw:
            raise RuntimeError("the packed portrait does not unpack to the picture (a fault of this tool)")
        if len(packed) <= PORTRAIT_MAX:
            if info is not None:
                info.update(colours=colours, packed=len(packed), unpacked=len(raw))
            return packed
    raise ValueError("this picture does not pack into 92,160 bytes even with 16 colours")


def skin(p):
    return p[3] >= 200 and p[2] > 90 and p[0] > 150 and 15 <= p[0] - p[1] <= 75 and 20 <= p[1] - p[2] <= 85


def head_square(px):
    """(x, y, size) of the head in a stored portrait, as port/src/gs/fight_panels_ui.cpp head_of finds it."""
    top, lo, hi = -1, ART_W, -1
    for y in range(400):
        if sum(1 for x in range(ART_W) if px[(y * ART_W + x) * 4 + 3] >= 0x80) >= 6:
            top = y
            break
    if top < 0:
        return 58, 18, 140
    for y in range(top + 30, min(ART_H, top + 130)):
        for x in range(ART_W):
            if px[(y * ART_W + x) * 4 + 3] >= 0x80:
                lo, hi = min(lo, x), max(hi, x)
    if hi < lo:
        lo, hi = 48, 208
    size = max(110, min(168, hi - lo + 1))
    x = (lo + hi + 1) // 2 - size // 2
    x = max(0, min(ART_W - size, x))
    return x, top + 4, size


def face_point(px):
    """(x, y) of the face in a stored portrait (fight_panels_ui.cpp face_of: the first band of skin under the top of
    the head; without skin, from the head's square)."""
    hx, hy, hs = head_square(px)
    xa, xb, top = max(0, hx - 20), min(ART_W, hx + hs + 20), hy - 4
    fx, fy = hx + hs * 0.5, hy + hs * 0.45
    first = -1
    for y in range(max(0, top), min(ART_H, top + 260)):
        if sum(1 for x in range(xa, xb) if skin(px[(y * ART_W + x) * 4:(y * ART_W + x) * 4 + 4])) >= 8:
            first = y
            break
    if first >= 0:
        sx = n = 0
        for y in range(first, min(ART_H, first + 55)):
            for x in range(xa, xb):
                if skin(px[(y * ART_W + x) * 4:(y * ART_W + x) * 4 + 4]):
                    sx += x
                    n += 1
        if n >= 150:
            fx, fy = sx / n, first + 28.0
    return fx, fy, hs


def _skinish(p):  # (skin under a 3D preview's light: warmer than it is blue, not grey, not too dark)
    r, g, b = p[0], p[1], p[2]
    return p[3] >= 200 and r > 100 and r >= g >= b and 25 <= r - b <= 160 and g - b >= 8


def face_geometry(px):
    """(x, face centre y, eye line y, head width) in a stored portrait made from a 3D preview: the head's column
    from the top of the figure; the chin, the narrowest line between the head and the largest widening below it
    (the shoulders); the eyes from the skin under the hair (the first lines of skin above the chin), else in
    proportion to the head's height. Falls back on face_point() (the port's own guess) when there is no chin."""
    box = alpha_box((ART_W, ART_H, px))
    if box is None:
        fx, fy, hs = face_point(px)
        return fx, fy, fy - 4, hs
    top = box[1]
    cx = int(head_centre((ART_W, ART_H, px), box, 0.08))
    widths = []
    for y in range(top, min(ART_H, top + 400), 2):
        a = px[y * ART_W * 4 + 3:(y + 1) * ART_W * 4:4]
        if a[cx] < 128:
            widths.append((y, 0, cx))
            continue
        l = r = cx
        while l > 0 and a[l - 1] >= 128:
            l -= 1
        while r < ART_W - 1 and a[r + 1] >= 128:
            r += 1
        widths.append((y, r - l + 1, (l + r) / 2))
    jump, at = 0, None
    for i in range(15, len(widths) - 4):  # (from 30 lines under the top)
        d = widths[i + 4][1] - widths[i][1]
        if d > jump:
            jump, at = d, i + 4
    if at is None or jump < 30:
        fx, fy, hs = face_point(px)
        return fx, fy, fy - 4, hs
    span = [i for i in range(max(10, at - 60), at) if widths[i][1] > 8]  # (within 120 lines over the shoulders)
    if not span:
        fx, fy, hs = face_point(px)
        return fx, fy, fy - 4, hs
    least = min(widths[i][1] for i in span)
    lo = max(i for i in span if widths[i][1] <= least * 1.12)  # (the lowest of the narrowest: the chin, the neck)
    chin, fx = widths[lo][0], widths[lo][2]
    head_w = max(w for _, w, _ in widths[max(0, lo - 30):lo]) if lo > 0 else 120
    xa, xb = max(0, int(fx) - 50), min(ART_W, int(fx) + 50)
    first = None
    for y in range(top, chin):
        row = px[y * ART_W * 4:(y + 1) * ART_W * 4]
        if sum(1 for x in range(xa, xb) if _skinish(row[x * 4:x * 4 + 4])) >= 6:
            first = y
            break
    hh = chin - top
    if first is not None and 25 <= chin - first <= 200:
        eyes = first + 0.42 * (chin - first)
        fy = first + 0.55 * (chin - first)
    else:
        eyes = chin - min(100, max(35, 0.33 * hh))
        fy = chin - min(90, max(45, 0.25 * hh))
    return fx, fy, eyes, head_w


def _small(stored, rect, dst, tw, th):
    """The stored portrait's rectangle rect = (x, y, w, h) put on the tw x th area dst = (x, y, w, h) of a tw x th
    picture (the rest from outside the rectangle, continuing the same scale)."""
    rx, ry, rw, rh = rect
    dx, dy, dw, dh = dst
    sx, sy = rw / dw, rh / dh
    return resample((ART_W, ART_H, stored), rx - dx * sx, ry - dy * sy, tw * sx, th * sy, tw, th)


def _plain_template(template_bytes, size_max):
    body = bytes(template_bytes)
    if struct.unpack_from("<I", body, 0)[0] != 1:
        raw = bpe_decode(body)
        if raw is None:
            raise ValueError("the template is not a texture file of the game")
        body = raw
    if len(body) > size_max:
        raise ValueError("the template is larger than such a file may be")
    return body


def chip_bin(rgba_or_png, template_bytes, framing="auto", squeeze=2.0, stored=None):
    """chip.bin (5,504 bytes, unpacked) for the roster: the face in x 8..55, y 11..51 of the 64 x 64 texture, clear
    around it (as all 161 disc chips are). The face's square is the one the disc's chips show (measured on several:
    about 120 x 120 of the stored portrait, centred on the face point), taken from the portrait made of the picture."""
    body = bytearray(_plain_template(template_bytes, CHIP_SIZE))
    body += bytes(CHIP_SIZE - len(body))
    w, h, pix, clut = tex_parse(body)
    px = stored if stored is not None else portrait_rgba(rgba_or_png, framing, squeeze)
    fx, fy, eyes, hs = face_geometry(px)
    s = 120
    ax, ay, aw, ah = w * 8 // 64, h * 11 // 64, w * 48 // 64, h * 41 // 64
    small = bytearray(_small(px, (fx - s / 2, fy - s / 2, s, s), (ax, ay, aw, ah), w, h))
    for y in range(h):
        for x in range(w):
            if not (ax <= x < ax + aw and ay <= y < ay + ah):
                small[(y * w + x) * 4:(y * w + x) * 4 + 4] = b"\0\0\0\0"
    pal, idx = quantize(small, 256)
    return tex_write(bytes(body), pal, idx)


def member_bins(rgba_or_png, template_1p, template_2p=None, framing="auto", squeeze=2.0, stored=None):
    """(member_1p.bin, member_2p.bin): the eyes in the band of the 64 x 32 team picture. The band's shape is the
    templates' own alpha (the disc's members fade out towards the inside of the screen); the 2p picture is the 1p
    one mirrored, as the disc's are."""
    px = stored if stored is not None else portrait_rgba(rgba_or_png, framing, squeeze)
    fx, fy, eyes, hs = face_geometry(px)
    rw, rh = 76, 27
    outs = []
    for side, tpl in enumerate((template_1p, template_2p if template_2p is not None else template_1p)):
        body = _plain_template(tpl, MEMBER_MAX)
        w, h, pix, clut = tex_parse(body)
        _, _, mask = tex_rgba(body)
        bx, by, bw, bh = w * 12 // 64, h * 13 // 32, w * 40 // 64, h * 16 // 32
        small = bytearray(_small(px, (fx - rw / 2, eyes - rh / 2, rw, rh), (bx, by, bw, bh), w, h))
        if side == 1:
            m = bytearray(len(small))
            for y in range(h):
                for x in range(w):
                    m[(y * w + x) * 4:(y * w + x) * 4 + 4] = small[(y * w + w - 1 - x) * 4:(y * w + w - x) * 4]
            small = m
        mx = max(mask[3::4]) or 255
        for i in range(w * h):
            small[i * 4 + 3] = small[i * 4 + 3] * min(255, mask[i * 4 + 3] * 255 // mx) // 255
        pal, idx = quantize(small, 256)
        outs.append(tex_write(body, pal, idx))
    return outs[0], outs[1]


# ============================================================================================ test and CLI
def bpe_decode_ps2(d):
    """src/sys/bpe.c Bpe_Decode step by step, with its limits: blocks until the input is exactly at the packed size,
    a 256-entry stack, the block size a signed 16-bit count. Raises if a file would overrun any of them."""
    size, pack = struct.unpack_from("<ii", d)
    p, end, out = 8, 8 + pack, bytearray()
    while p != end:
        if p > end:
            raise ValueError("blocks run past the packed size")
        left, right = list(range(256)), [0] * 256
        count = d[p]; p += 1
        c = 0
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            if c > 256:
                raise ValueError("skip past 256")
            for _ in range(count + 1):
                if c >= 256:
                    raise ValueError("pair table past 256")
                left[c] = d[p]; p += 1
                if c != left[c]:
                    right[c] = d[p]; p += 1
                c += 1
            if c == 256:
                break
            count = d[p]; p += 1
        block = d[p] << 8 | d[p + 1]
        p += 2
        if block > 0x7FFF:
            raise ValueError("block size negative as the game's s16")
        stack, deepest = [], 0
        while True:
            if stack:
                c = stack.pop()
            else:
                if block == 0:
                    break
                block -= 1
                c = d[p]; p += 1
            if c == left[c]:
                out.append(c)
            else:
                stack += (right[c], left[c])
                deepest = max(deepest, len(stack))
                if deepest > 256:
                    raise ValueError("stack past 256")
    if len(out) != size:
        raise ValueError("unpacked size differs")
    return bytes(out)


def test(game_dir, out_dir):
    """Round trips on the disc's portraits and on synthetic screenshots; PNGs and sizes into out_dir."""
    game, out = pathlib.Path(game_dir), pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import creator as C
    idx = C.source_index()
    assert all(idx[y * ART_W + x] == pic_place(x, y, ART_W) for y in range(ART_H) for x in range(ART_W)), "placement"
    print("pic_place == creator.source_index for all 131072 pixels")
    lines = []
    for c in (0, 1, 2, 5, 10, 20, 40, 80, 120, 160):
        f = game / f"{0x2F8 + c:05d}.bin"
        orig = f.read_bytes()
        raw = C.bpe_decode(orig)
        t = time.time()
        rep = bpe_best(raw)
        t1 = time.time() - t
        ok = C.bpe_decode(rep) == raw and bpe_decode_ps2(rep) == raw
        # the disc picture through the whole path (taken as it is), re-quantized
        _, _, rgba = tex_rgba(raw)
        t = time.time()
        info = {}
        made = portrait_bin(None, orig, stored=rgba, info=info)
        t2 = time.time() - t
        back = C.portrait_png(made)
        (out / f"disc_{c:03d}_remade.png").write_bytes(png_write(ART_W, ART_H, tex_rgba(C.bpe_decode(made))[2]))
        line = (f"chara {c:3d}: disc {len(orig):6d} B; re-packed same bytes {len(rep):6d} B ({t1:.1f} s, round trip "
                f"{'ok' if ok else 'FAILED'}); re-quantized {len(made):6d} B, {info['colours']} colours ({t2:.1f} s), "
                f"creator decodes it: {back is not None}")
        print(line)
        lines.append(line)
    tpl = (game / f"{0x2F8:05d}.bin").read_bytes()
    for name, pic in synthetic().items():
        (out / f"syn_{name}_in.png").write_bytes(png_write(*pic))
        t = time.time()
        info = {}
        st = portrait_rgba(pic)
        t_frame = time.time() - t
        made = portrait_bin(None, tpl, stored=st, info=info)
        t_all = time.time() - t
        raw = C.bpe_decode(made)
        assert bpe_decode_ps2(made) == raw
        (out / f"syn_{name}_portrait.png").write_bytes(png_write(ART_W, ART_H, tex_rgba(raw)[2]))
        line = (f"synthetic {name} {pic[0]}x{pic[1]}: {len(made)} B, {info['colours']} colours, framing {t_frame:.1f} s, "
                f"total {t_all:.1f} s")
        print(line)
        lines.append(line)
    (out / "sizes.txt").write_text("\n".join(lines) + "\n")


def synthetic():
    """A few full-body 'screenshots' drawn here: shaded ellipses (a head, a body, arms, legs) on a clear background,
    one with noise (the worst case for packing)."""
    import math, random
    out = {}
    for name, (W, H, noise, grad) in {"flat": (600, 900, 0, False), "shaded": (800, 1200, 0, True),
                                      "noisy": (700, 1000, 24, True), "small": (240, 360, 0, True)}.items():
        rnd = random.Random(1)
        px = bytearray(W * H * 4)
        parts = [  # (cx, cy, rx, ry, colour) in units of the height
            (0.5, 0.36, 0.13, 0.17, (230, 110, 30)), (0.5, 0.62, 0.11, 0.16, (40, 60, 160)),
            (0.33, 0.38, 0.05, 0.15, (240, 190, 150)), (0.67, 0.38, 0.05, 0.15, (240, 190, 150)),
            (0.43, 0.82, 0.05, 0.16, (230, 110, 30)), (0.57, 0.82, 0.05, 0.16, (230, 110, 30)),
            (0.5, 0.13, 0.075, 0.085, (245, 200, 160)), (0.5, 0.075, 0.09, 0.055, (20, 20, 30)),
            (0.47, 0.13, 0.012, 0.008, (255, 255, 255)), (0.53, 0.13, 0.012, 0.008, (255, 255, 255))]
        for y in range(H):
            v = y / H
            for x in range(W):
                u = (x - W / 2) / H + 0.5
                col = None
                for cx, cy, rx, ry, c in parts:
                    d = ((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2
                    if d < 1:
                        sh = 1 - 0.45 * d if grad else 1
                        sh *= 0.8 + 0.2 * math.cos((u - cx) * 9)
                        col = tuple(max(0, min(255, int(k * sh + (rnd.randint(-noise, noise) if noise else 0)))) for k in c)
                        edge = min(1.0, (1 - d) * 60)
                if col:
                    o = (y * W + x) * 4
                    px[o:o + 4] = bytes(col + (int(255 * edge),))
        out[name] = (W, H, px)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    mk = sub.add_parser("make", help="the files from a picture")
    mk.add_argument("picture")
    mk.add_argument("--template", required=True, help="a real portrait file (the disc's 0x2F9 + chara, or a package's)")
    mk.add_argument("-o", "--out", required=True, help="portrait.bin to write")
    mk.add_argument("--framing", default="auto", choices=("auto", "body", "fit", "as-is"))
    mk.add_argument("--squeeze", type=float, default=2.0, help="how much wider the menus show the portrait (2)")
    mk.add_argument("--chip", help="chip.bin to write")
    mk.add_argument("--chip-template", help="a real chip (5,504 bytes); needed with --chip")
    mk.add_argument("--member1", help="member_1p.bin to write")
    mk.add_argument("--member2", help="member_2p.bin to write")
    mk.add_argument("--member-template", nargs="+", help="a real member file (1p, and optionally 2p)")
    mk.add_argument("--preview", help="a PNG of the stored portrait as written (256 x 512)")
    ts = sub.add_parser("test", help="round trips and sizes")
    ts.add_argument("pzs3us1")
    ts.add_argument("out")
    a = ap.parse_args(argv)
    if a.cmd == "test":
        test(a.pzs3us1, a.out)
        return
    t = time.time()
    stored = portrait_rgba(pathlib.Path(a.picture).read_bytes(), a.framing, a.squeeze)
    info = {}
    data = portrait_bin(None, pathlib.Path(a.template).read_bytes(), stored=stored, info=info)
    pathlib.Path(a.out).write_bytes(data)
    print(f"{a.out}: {len(data)} bytes packed ({info['colours']} colours), {time.time() - t:.1f} s")
    if a.preview:
        pathlib.Path(a.preview).write_bytes(png_write(ART_W, ART_H, tex_rgba(bpe_decode(data))[2]))
    if a.chip:
        if not a.chip_template:
            sys.exit("--chip needs --chip-template (a real chip.bin, e.g. one import_chara.py wrote)")
        chip = chip_bin(None, pathlib.Path(a.chip_template).read_bytes(), stored=stored)
        pathlib.Path(a.chip).write_bytes(chip)
        print(f"{a.chip}: {len(chip)} bytes")
    if a.member1 or a.member2:
        if not a.member_template:
            sys.exit("--member1/--member2 need --member-template (a real member_1p.bin [member_2p.bin])")
        tps = [pathlib.Path(p).read_bytes() for p in a.member_template]
        m1, m2 = member_bins(None, tps[0], tps[1] if len(tps) > 1 else None, stored=stored)
        for path, d in ((a.member1, m1), (a.member2, m2)):
            if path:
                pathlib.Path(path).write_bytes(d)
                print(f"{path}: {len(d)} bytes")


if __name__ == "__main__":
    main()
