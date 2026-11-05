#!/usr/bin/env python3
"""Custom Character creator: a window (in the browser) for port/tools/custom_fighter.py.

    python port/tools/creator/creator.py --game "<the game's folder>" [--iso <BT3 USA image>] [--port 8765]

It reads the characters from the game's data (or the disc image), keeps the fighters you make as JSON files in
<game>/custom_fighters/, and installs them: over a roster costume (gamedata/mods/, works with every build) or as a
character package (characters/, for builds with character packages). Only this machine can reach it (127.0.0.1).
Standard library only.
"""
import argparse, http.server, json, pathlib, re, struct, sys, threading, traceback, urllib.parse, webbrowser, zlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import custom_fighter as cf  # noqa: E402


# --------------------------------------------------------------------------------------------------- portraits
def bpe_decode(d):
    """The game's byte-pair packing (sys/bpe.c; port/src/gs/fight_panels_ui.cpp bpe_decode)."""
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


BLOCK32 = [[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23], [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]]
COL32 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15], [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31],
         [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47], [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]]
COL8 = [[0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54], [8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62],
        [33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23], [41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31]]


def addr32(bw, x, y):
    return ((((y & ~0x1F) * bw + ((x >> 1) & ~0x1F) + BLOCK32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + COL32[y & 7][x & 7])


def addr8(bw, x, y):
    col = (y & 15) >> 2
    sx = ((x & 15) ^ 4) if (col & 1) else (x & 15)
    return (((((y >> 1) & ~0x1F) * (bw >> 1) + ((x >> 2) & ~0x1F) + BLOCK32[(y >> 4) & 3][(x >> 4) & 7]) << 8)
            + COL8[y & 3][sx] + col * 64)


ART_W, ART_H = 256, 512
_SRC = None


def source_index():
    """For each pixel of the 8-bit picture, the byte of the 32-bit upload it is read from (the same for every
    portrait: worked out once)."""
    global _SRC
    if _SRC is None:
        where = {}
        for y in range(256):
            for x in range(128):
                base = (addr32(2, x, y) * 4) & 0xFFFFC
                for k in range(4):
                    where[base + k] = (y * 128 + x) * 4 + k
        _SRC = [where.get(addr8(4, x, y) & 0xFFFFF, 0) for y in range(ART_H) for x in range(ART_W)]
    return _SRC


def csm1(i):
    return i + 8 if (i & 0x18) == 8 else i - 8 if (i & 0x18) == 16 else i


def png(w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def portrait_png(data):
    raw = bpe_decode(data)
    if raw is None or len(raw) < 0x60:
        return None
    f0, f1 = struct.unpack_from("<II", raw, 0x20)
    if f0 * 4 + 0x60 + 131072 > len(raw) or f1 * 4 + 0x60 + 1024 > len(raw):
        return None
    pix, clut = raw[f0 * 4 + 0x60:], raw[f1 * 4 + 0x60:]
    pal = []
    for i in range(256):
        e = clut[csm1(i) * 4:csm1(i) * 4 + 4]
        pal.append(bytes((e[0], e[1], e[2], 255 if e[3] >= 0x80 else e[3] * 2)))
    idx = source_index()
    out = b"".join(pal[pix[i]] for i in idx)
    # the head is in the top half: keep 256 x 256 of it, halved to 128 x 128 for the grid
    small = bytearray()
    for y in range(0, 256, 2):
        row = out[y * ART_W * 4:(y + 1) * ART_W * 4]
        for x in range(0, ART_W, 2):
            small += row[x * 4:x * 4 + 4]
    return png(128, 128, bytes(small))


# ------------------------------------------------------------------------------------------------------- state
class App:
    def __init__(self, game, iso):
        self.game = pathlib.Path(game)
        self.iso = iso
        args = argparse.Namespace(game=game, iso=iso)
        self.disc = cf.Disc(cf.open_source(args))
        self.lock = threading.Lock()
        self.fighters = self.game / "custom_fighters"
        self.fighters.mkdir(exist_ok=True)
        self.cache = self.game / "custom_fighters" / ".cache"
        self.cache.mkdir(exist_ok=True)
        self._roster = None

    def roster(self):
        if self._roster is None:
            cache = self.cache / "roster.json"
            if cache.exists():
                self._roster = json.loads(cache.read_text(encoding="utf-8"))
            else:
                out = []
                for c, name in enumerate(cf.NAMES):
                    costumes = [k + 1 for k in range(4) if self.disc.has_costume(c, k)]
                    try:
                        moves = cf.move_names(self.disc.model(c, 0))[:5]
                    except Exception:
                        moves = []
                    out.append({"id": c, "name": name, "costumes": costumes, "moves": moves})
                self._roster = out
                cache.write_text(json.dumps(out), encoding="utf-8")
        return self._roster

    def portrait(self, c):
        p = self.cache / f"face_{c:03d}.png"
        if not p.exists():
            data = portrait_png(self.disc.raw(cf.PORTRAIT_FILE + c))
            if data is None:
                return None
            p.write_bytes(data)
        return p.read_bytes()

    def list_fighters(self):
        out = []
        for p in sorted(self.fighters.glob("*.json")):
            try:
                spec = json.loads(p.read_text(encoding="utf-8-sig"))
                out.append({"file": p.name, "spec": spec})
            except Exception as e:
                out.append({"file": p.name, "error": str(e)})
        return out

    def path_of(self, name):
        if not re.fullmatch(r"[^/\\:*?\"<>|]{1,80}\.json", name or ""):
            raise cf.Fail("bad file name")
        return self.fighters / name

    def save(self, name, spec):
        if not spec.get("name"):
            raise cf.Fail("give your fighter a name")
        if not name:
            name = re.sub(r"[^A-Za-z0-9 _-]+", "", spec["name"]).strip().replace(" ", "_")[:60] or "fighter"
            name += ".json"
        cf.Fighter(self.disc, spec, name)  # checks it
        self.path_of(name).write_text(json.dumps(spec, indent=2, ensure_ascii=False), encoding="utf-8")
        return name

    def describe(self, spec):
        return cf.Fighter(self.disc, spec, "fighter").describe()

    def model(self, spec, costume=0):
        """The 3D model of a costume of the fighter, for the preview (port/tools/creator/model.py)."""
        import model as mdl
        spec = dict(spec, name=spec.get("name") or "preview")
        f = cf.Fighter(self.disc, spec, "preview")
        look = f.costumes[min(costume, len(f.costumes) - 1)]
        key = json.dumps([look, spec.get("parts")], sort_keys=True)
        if getattr(self, "_model_key", None) != key:
            self._model = mdl.extract(f.model(look, False).bytes(), pose="relaxed", hide_optional=True)
            self._model_key = key
        m = self._model
        return {k: m[k] for k in ("positions", "normals", "uvs", "indices", "groups", "textures", "bbox")}

    def install(self, name):
        spec = json.loads(self.path_of(name).read_text(encoding="utf-8-sig"))
        f, slot, files = cf.replace_files(self.disc, spec)
        mods = self.game / "gamedata" / "mods"
        for part, index in cf.mods_names(slot):
            old = mods / part / f"{index:05d}.bin"
            if (part, index) not in files and old.exists():
                old.unlink()
        for (part, index), data in sorted(files.items()):
            (mods / part).mkdir(parents=True, exist_ok=True)
            (mods / part / f"{index:05d}.bin").write_bytes(data)
        return (f"Installed in place of {cf.NAMES[slot[0]]}, costume {slot[1] + 1}. Restart the game, then pick that "
                f"character and costume.")

    def uninstall(self, name):
        spec = json.loads(self.path_of(name).read_text(encoding="utf-8-sig"))
        slot = cf.ref(spec["slot"])
        mods = self.game / "gamedata" / "mods"
        n = 0
        for part, index in cf.mods_names(slot):
            p = mods / part / f"{index:05d}.bin"
            if p.exists():
                p.unlink()
                n += 1
        return f"{cf.NAMES[slot[0]]}, costume {slot[1] + 1} is back to the disc's ({n} files removed)."

    def package(self, name):
        folder = cf.write_package(self.disc, self.path_of(name), self.game / "characters", {})
        return (f"Character package written to characters/{folder}. It shows on the roster in a build with "
                f"character packages (the custom-character branch).")

    def delete(self, name):
        self.path_of(name).unlink()
        return "deleted"


# ------------------------------------------------------------------------------------------------------ server
class Handler(http.server.BaseHTTPRequestHandler):
    app = None

    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode("utf-8")
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "max-age=86400" if ctype == "image/png" else "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        try:
            if u.path in ("/", "/index.html"):
                return self.send(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
            if u.path == "/api/roster":
                return self.send(200, self.app.roster())
            if u.path == "/api/fighters":
                return self.send(200, self.app.list_fighters())
            m = re.fullmatch(r"/face/(\d+)\.png", u.path)
            if m:
                with self.app.lock:
                    data = self.app.portrait(int(m.group(1)))
                return self.send(200, data, "image/png") if data else self.send(404, {"error": "no portrait"})
            self.send(404, {"error": "not found"})
        except Exception as e:
            traceback.print_exc()
            self.send(500, {"error": str(e)})

    def do_POST(self):
        if self.headers.get("Origin") not in (None, f"http://127.0.0.1:{self.server.server_port}",
                                              f"http://localhost:{self.server.server_port}"):
            return self.send(403, {"error": "forbidden"})
        try:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            a = self.app
            with a.lock:
                act = self.path
                if act == "/api/save":
                    return self.send(200, {"file": a.save(body.get("file"), body["spec"])})
                if act == "/api/model":
                    return self.send(200, a.model(body["spec"], int(body.get("costume", 0))))
                if act == "/api/describe":
                    return self.send(200, {"text": a.describe(body["spec"])})
                if act == "/api/install":
                    return self.send(200, {"text": a.install(body["file"])})
                if act == "/api/uninstall":
                    return self.send(200, {"text": a.uninstall(body["file"])})
                if act == "/api/package":
                    return self.send(200, {"text": a.package(body["file"])})
                if act == "/api/delete":
                    return self.send(200, {"text": a.delete(body["file"])})
            self.send(404, {"error": "not found"})
        except cf.Fail as e:
            self.send(400, {"error": str(e)})
        except Exception as e:
            traceback.print_exc()
            self.send(500, {"error": f"{type(e).__name__}: {e}"})


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", required=True, help="the game's folder (Tenkaichi3Decomp.exe, gamedata/)")
    ap.add_argument("--iso", help="the BT3 USA disc image (default: the game's data)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    try:
        Handler.app = App(args.game, args.iso)
    except cf.Fail as e:
        sys.exit(f"error: {e}")
    def warm():  # the roster and every portrait, made once in the background (later starts read the cache)
        app = Handler.app
        with app.lock:
            roster = app.roster()
        for c in roster:
            if c["costumes"]:
                with app.lock:
                    app.portrait(c["id"])
    threading.Thread(target=warm, daemon=True).start()
    http.server.ThreadingHTTPServer.allow_reuse_address = False  # (Windows lets a second server take a used port)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    except OSError:
        url = f"http://127.0.0.1:{args.port}/"
        print(f"The creator is already open: {url}", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        return
    url = f"http://127.0.0.1:{args.port}/"
    print(f"Custom Character creator: {url}  (Ctrl+C to stop)", flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
