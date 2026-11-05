"""Read-only access to the files of the DBZ Budokai Tenkaichi 4 mod disc (Team BT4 Beta 14 Rev2, SLUS-21978).

How the disc stores its data (found from the ISO, BIN/DBZ4.BIN and the ELF, see the report):

* /DATA/PZS4US0..3.AFS are AFS archives WITHOUT their AFS header/table: file data starts at byte 0.
* /BIN/DBZ4.BIN is the table: four CRI ADXF partition-info blocks ("ptinfo"), one per archive, back to back
  (each starts on a 16-byte boundary):
      +0x000 u32 next (0)   +0x004 u32 size of this block   +0x008 u32 nfile   +0x00C u16 nentry, s8 type, s8 rev
      +0x010 char fname[256] ("pzs4us0.afs" ...)   +0x110 u32 curdir   +0x114 u32 ofst (sectors)
      +0x118 u16 fsize[nfile]  (rev 0: sizes in 2048-byte sectors)
  CRI adxf_GetFnameRangeEx (ELF 0x26BAE0): start sector of file flid = ofst + fsize[0] + sum(fsize[1..flid]),
  length = fsize[flid + 1] sectors. fsize[0] is the (stripped, so 0) AFS header; flid runs 0..nfile-2.
  Only sector counts are stored, so a file is returned rounded up to 2048 bytes (exactly what the game reads).
* Global file ids (BT4's File_OpenById at 0x2654D8 -> 0x100954 / 0x100974 / 0x19BDC4 / 0x19BE04):
      id <= 0                 -> partition 0, index id - 0x10CC0   (unused)
      1      <= id < 0xD48    -> partition 1 (pzs4us1), index id - 1        (same as BT3's pzs3us1)
      0xD48  <= id < 0x10CC0  -> partition 2 (pzs4us2), index id - 0xD48
      0x10CC0 <= id < 0x20000 -> partition 0 (pzs4us0), index id - 0x10CC0
      id >= 0x20000           -> partition 3 (pzs4us3), index id - 0x20000  (BT3's pzs3us2 sounds, ADX)
* The ELF loader at 0x200500 opens cdrom0:/BIN/DBZ4.BIN (backslashes on disc), reads block pt (DBZ4 offsets 0/0x18BB0/0x1A760/0x2A890,
  sizes 0x18BA8/0x1BA8/0x10128/0xEB7A, tables at 0x20073C/0x20074C) into fixed buffers and stores the pointer in
  adxf_ptinfo[pt] (0x2E5E40). So: the number of files per partition is fixed by the ELF; sizes/offsets come
  only from DBZ4.BIN's u16 sector counts (a file's start = cumulative sum, files must stay contiguous).
"""
import struct

SECTOR = 2048
PT_FIRST_ID = {1: 1, 2: 0xD48, 0: 0x10CC0, 3: 0x20000}


def id_to_partition(file_id):
    """(partition, index) exactly as the BT4 ELF computes it."""
    if file_id >= 0x20000:
        return 3, file_id - 0x20000
    if file_id <= 0:
        pt = 0
    elif file_id < 0xD48:
        pt = 1
    elif file_id < 0x10CC0:
        pt = 2
    else:
        pt = 0
    return pt, file_id - PT_FIRST_ID[pt]


def partition_to_id(pt, index):
    return PT_FIRST_ID[pt] + index


class Iso9660:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _dir(self, lba, size):
        d = self.read(lba * SECTOR, size)
        p = 0
        while p < len(d):
            ln = d[p]
            if ln == 0:
                p = (p // SECTOR + 1) * SECTOR
                continue
            ext, sz = struct.unpack_from('<I', d, p + 2)[0], struct.unpack_from('<I', d, p + 10)[0]
            name = d[p + 33:p + 33 + d[p + 32]].decode('latin1').split(';')[0]
            yield name, ext, sz, d[p + 25]
            p += ln

    def find(self, path):
        """'/DATA/PZS4US1.AFS' -> (lba, size)"""
        pvd = self.read(16 * SECTOR, SECTOR)
        lba, size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
        for part in path.strip('/').split('/'):
            for name, ext, sz, fl in self._dir(lba, size):
                if name.upper() == part.upper():
                    lba, size = ext, sz
                    break
            else:
                raise FileNotFoundError(path)
        return lba, size

    def read_file(self, path):
        lba, size = self.find(path)
        return self.read(lba * SECTOR, size)


class PtInfo:
    """One ADXF partition-info block of DBZ4.BIN."""

    def __init__(self, blob, base):
        self.base = base
        _, self.size, self.nfile, self.nentry, self.type, self.rev = struct.unpack_from('<IIIHbb', blob, base)
        self.fname = blob[base + 0x10:base + 0x110].split(b'\0')[0].decode()
        self.curdir, self.ofst = struct.unpack_from('<II', blob, base + 0x110)
        if self.rev == 1:
            raise NotImplementedError('rev 1 (u32 byte sizes) ptinfo')
        self.fsize = list(struct.unpack_from('<%dH' % self.nfile, blob, base + 0x118))
        self.starts = []  # start sector of each file
        s = self.ofst + self.fsize[0]
        for n in self.fsize[1:]:
            self.starts.append(s)
            s += n
        self.end_sector = s

    @property
    def count(self):
        return self.nfile - 1

    def range(self, index):
        """(start sector, sector count) of file `index` inside the archive."""
        if not 0 <= index < self.count:
            raise IndexError('%s: index %d out of 0..%d' % (self.fname, index, self.count - 1))
        return self.starts[index], self.fsize[index + 1]


class BT4Disc:
    def __init__(self, iso_path):
        self.iso = Iso9660(iso_path)
        self.dbz4 = self.iso.read_file('/BIN/DBZ4.BIN')
        self.partitions = {}  # partition number -> PtInfo
        base, order = 0, []
        while base + 0x118 <= len(self.dbz4):
            pt = PtInfo(self.dbz4, base)
            order.append(pt)
            base = (base + pt.size + 15) & ~15
        for pt in order:
            num = int(pt.fname.lower().replace('pzs4us', '').split('.')[0])
            self.partitions[num] = pt
        self.archives = {num: self.iso.find('/DATA/' + pt.fname.upper()) for num, pt in self.partitions.items()}

    # --- locating ---
    def locate(self, file_id):
        """file id -> (partition, index, absolute byte offset in the ISO, byte length (sector rounded))"""
        pt, idx = id_to_partition(file_id)
        start, n = self.partitions[pt].range(idx)
        lba, size = self.archives[pt]
        if (start + n) * SECTOR > size:
            raise ValueError('file %#x runs past the end of %s' % (file_id, self.partitions[pt].fname))
        return pt, idx, (lba + start) * SECTOR, n * SECTOR

    def ids(self, pt):
        """all valid ids of partition pt"""
        first = PT_FIRST_ID[pt]
        return range(first, first + self.partitions[pt].count)

    def all_ids(self):
        for pt in (1, 2, 0, 3):
            yield from self.ids(pt)

    # --- reading ---
    def read_id(self, file_id):
        """The file's bytes as the game reads them (whole sectors)."""
        _, _, off, n = self.locate(file_id)
        return self.iso.read(off, n)

    def read_head(self, file_id, n=SECTOR):
        _, _, off, size = self.locate(file_id)
        return self.iso.read(off, min(n, size))

    def read_index(self, pt, index):
        return self.read_id(partition_to_id(pt, index))


# --- packs and text (same container formats as BT3) ---
def pack_parts(data):
    """A pack: u32 count, count u32 offsets; part i = data[off[i]:off[i+1]] (the last one runs to the end)."""
    n = struct.unpack_from('<I', data, 0)[0]
    offs = list(struct.unpack_from('<%dI' % n, data, 4))
    ends = offs[1:] + [len(data)]
    return [data[o:e] if e > o else b'' for o, e in zip(offs, ends)]


def word_at(pack, n):
    """PACK_AT / MPACK_AT of the game: pack + (u32 word n & ~3) (word 0 is the count, word 1 the first offset)."""
    return pack[struct.unpack_from('<I', pack, 4 * n)[0] & ~3:]


def bpe_decode(src):
    """Bpe_Decode (BT3 src/sys/bpe.c): u32 unpacked size, u32 packed size, then byte-pair blocks."""
    size, packed = struct.unpack_from('<iI', src, 0)
    p, end, out = 8, 8 + packed, bytearray()
    while p < end and len(out) < size:
        left, right = list(range(256)), [0] * 256
        count, c = src[p], 0
        p += 1
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = src[p]
                p += 1
                if c != left[c]:
                    right[c] = src[p]
                    p += 1
                c += 1
            if c == 256:
                break
            count = src[p]
            p += 1
        n = (src[p] << 8) | src[p + 1]
        p += 2
        stack = []
        while True:
            if stack:
                c = stack.pop()
            else:
                if n == 0:
                    break
                n -= 1
                c = src[p]
                p += 1
            if c == left[c]:
                out.append(c)
            else:
                stack.append(right[c])
                stack.append(left[c])
    return bytes(out)


BOM, ZERO2 = bytes((0xFF, 0xFE)), bytes(2)


def text_lines(t):
    """A game text file: u32 offsets (word 0 = count, word n+1 = line n) to U+FEFF-prefixed UTF-16LE strings."""
    first = struct.unpack_from('<I', t, 4)[0] & ~3
    out = []
    for i in range(first // 4 - 1):
        o = struct.unpack_from('<I', t, 4 * (i + 1))[0] & ~3
        if o == 0 or o + 2 > len(t) or t[o:o + 2] != BOM:
            out.append(None)
            continue
        e = o + 2
        while e + 1 < len(t) and t[e:e + 2] != ZERO2:
            e += 2
        out.append(t[o + 2:e].decode('utf-16-le', 'replace'))
    return out


# --- characters ---
CHARA_SLOTS = 250            # file 4 section 1 has 250 entries of 0x3C bytes; ids 161..164 are menu specials
CHARA_FILE = {               # per-character files: base id + chara (ELF references in brackets)
    'tex2':      0x0D48,     # + chara*2 + side, 1-part texture packs, 4 KB          [0x127A28, 0x127FFC]
    'data_f3c':  0x0F3C,     # BT3 model part 35 ("aC" table)                         [0x26BF20]
    'pack3':     0x1036,     # 3 parts (BT3 model parts 0, 17, 1?)                    [0x20095C, 0x1E1E28]
    'movelist':  0x1130,     # UTF-16 move-list text (BT3 model parts 43..51)         [0x200980]
    'lips_a':    0x122A,     # 100 LIPS (lip sync) parts, used when flag bit is 0      [0x127744, 0x2009B4]
    'lips_b':    0x1324,     # 100 LIPS parts, used when flag bit is 1                 [0x127734, 0x2009A4]
    'anim':      0x141E,     # 440-part animation pack (same as BT3 +8)               [0x249C84]
    'effect':    0x1518,     # 7-part effect pack (BT3 +9 had 6)                      [0x249C8C]
    'voice_a':   0x1D012,    # 3-part voice bank (pzs4us0), flag bit 0                 [0x19BDA4]
    'voice_b':   0x1D10C,    # 3-part voice bank (pzs4us0), flag bit 1                 [0x19BDA8]
}
MODEL_FIRST = 0x1612         # 0x20081C returns it; models are packed per character (see model_id)


class Characters:
    def __init__(self, disc):
        self.disc = disc
        f4 = disc.read_id(4)                      # common file 4 (chara table, like BT3)
        off = struct.unpack_from('<I', f4, 4)[0] & ~3
        self.entries = [f4[off + 0x3C * c: off + 0x3C * (c + 1)] for c in range(CHARA_SLOTS)]
        # BT4's 0x25D468 reads the costume count from u16 +0x28 of the entry (BT3 used +0x0A)
        self.costumes = [struct.unpack_from('<H', e, 0x28)[0] for e in self.entries]
        self.model_base = []
        s = MODEL_FIRST
        for n in self.costumes:
            self.model_base.append(s)
            s += 2 * n
        self.model_end = s
        self._names = None

    def model_id(self, chara, costume=0, damaged=False):
        """0x26BDFC: 0x1612 + sum(2*costumes[i] for i < chara) + costume (+ costumes[chara] when damaged)."""
        n = self.costumes[chara]
        if not 0 <= costume < n:
            raise IndexError('character %d has %d costumes' % (chara, n))
        return self.model_base[chara] + costume + (n if damaged else 0)

    def file_id(self, kind, chara, side=0):
        if kind == 'tex2':
            return CHARA_FILE[kind] + chara * 2 + side
        return CHARA_FILE[kind] + chara

    def names(self):
        """(name, form) per character id: menu archive file 0x1C1, section 2 (BPE), its sections 43 and 44."""
        if self._names is None:
            res = bpe_decode(word_at(self.disc.read_id(0x1C1), 2))
            nm, fm = text_lines(word_at(res, 43)), text_lines(word_at(res, 44))
            self._names = [(nm[c] if c < len(nm) else None, fm[c] if c < len(fm) else None)
                           for c in range(CHARA_SLOTS)]
        return self._names

    def display_name(self, chara):
        n, f = self.names()[chara]
        return (n or '') + (' (%s)' % f if f else '')


if __name__ == '__main__':
    import sys
    disc = BT4Disc(sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != '-v' else r'D:\Dragon ball budokai modz\SLUS_219.78.DBZBT4B14REV2ENG.iso')
    for num in sorted(disc.partitions):
        p = disc.partitions[num]
        lba, size = disc.archives[num]
        print('pt%d %s: %d files, ids %#x..%#x, data ends at sector %d of %d'
              % (num, p.fname, p.count, PT_FIRST_ID[num], PT_FIRST_ID[num] + p.count - 1, p.end_sector, size // SECTOR))
    ch = Characters(disc)
    print('characters: %d slots, %d with models, models %#x..%#x'
          % (CHARA_SLOTS, sum(1 for n in ch.costumes if n), MODEL_FIRST, ch.model_end - 1))
    if '-v' in sys.argv:
        for c in range(CHARA_SLOTS):
            n = ch.costumes[c]
            print('%3d %-45s costumes %d  models %s  anim %#x  effect %#x  movelist %#x'
                  % (c, ch.display_name(c), n, ('%#x..%#x' % (ch.model_base[c], ch.model_base[c] + 2 * n - 1)) if n else '-',
                     ch.file_id('anim', c), ch.file_id('effect', c), ch.file_id('movelist', c)))
