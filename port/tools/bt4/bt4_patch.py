"""Put BT3-format character files into a COPY of the BT4 disc (Team BT4 Beta 14 Rev2, SLUS-21978).

    python bt4_patch.py --chara <BT4 id or name> --costume N  [sources...]  [--iso BT4.iso] [--out X.iso] [--fresh]

Sources (any subset; BT3 formats, as port/tools/custom_fighter.py and the BT3 disc use them):
    --model F       BT3 252-part model pack -> the costume's BT4 model (52 parts) and, unless --model-only, the
                    character's BT4 per-character files made from it: pack3 (HUD face, moveset 0x12..0x1C, camera
                    animations), 0xF3C table, move list text (BT3 entry 0x2D, US English), lip sync (both languages)
    --damaged F     BT3 model pack -> the costume's damaged model (model parts only)
    --anim F        BT3 animation pack (440 parts)      --effect F   BT3 effect pack (6 parts)
    --portrait F    BT3 portrait (0x2F9 + c format)
    --voice DIR     bank_us.bin, bank_jp.bin, us/00.adx .. us/99.adx, jp/00.adx .. jp/99.adx (any of them)
    --from-bt3 C    all of the above from BT3 character C (id or name) of the BT3 USA disc (--bt3-iso, --bt3-costume)
    --keep-extras   keep the target model's BT4-only parts 25 and 51 (default: empty, as in 122 / 38 BT4 models)
The copy is made next to the original as SLUS_219.78.DBZBT4B14REV2ENG.custom.iso (the original is only read) and
reused by later runs (--fresh makes it again). After writing, the copy is read back with bt4_disc.BT4Disc and every
written file and every other file of the archives that changed is compared (md5 of every unchanged file of those archives, taken before writing; --no-verify skips it).

How it writes (see the report in bt4_disc.py / README of this folder):
* The game finds a file as: id -> (archive, index) -> start sector = cumulative sum of the u16 sector counts of
  BIN/DBZ4.BIN (read at run time by the ELF loader at 0x200500 into the ADXF partition info) -> sector offset in the
  archive, whose LBA comes from the ISO 9660 directory (CRI opens it by name: sceCdSearchFile; the recomp's
  sceCdSearchFile / iso9660Resolve walks the image's directory). No LBA of an archive is stored in the ELF or DBZ4.
* A file that fits its old sector count is written in place (rest of its sectors zeroed); nothing else changes.
* A file that grows: its whole archive is rebuilt with the new sizes and written after the end of the image; the
  ISO 9660 directory record (LBA, size, both-endian), the volume size (both-endian), DBZ4.BIN's u16 table (same
  size, in place) and the UDF bridge (file entry extents, partition length, closing anchor) are updated. The old
  copy of the archive stays where it was, unused. If the archive already is the last one in the image (an earlier
  run), it is rebuilt in place there.
"""
import argparse, hashlib, os, shutil, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bt4_disc import BT4Disc, Characters, Iso9660, id_to_partition, partition_to_id, SECTOR, CHARA_FILE
import bt4_to_bt3 as cv

ORIG = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE)))), 'SLUS_219.78.DBZBT4B14REV2ENG.iso')
CHUNK = 16 << 20
MAX_AD = 0x3FFFF800           # longest UDF extent


def sectors(n):
    return -(-n // SECTOR)


class Fail(Exception):
    pass


# ------------------------------------------------------------------------------------------------- the image
class Image:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'r+b')

    def rd(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def wr(self, off, data):
        self.f.seek(off)
        self.f.write(data)

    def size(self):
        self.f.seek(0, 2)
        return self.f.tell()

    # ISO 9660 directory record of a path: (byte offset of the record, lba, size)
    def iso_record(self, path):
        pvd = self.rd(16 * SECTOR, SECTOR)
        lba, size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
        rec = None
        for part in path.strip('/').split('/'):
            d = self.rd(lba * SECTOR, size)
            p, found = 0, None
            while p < len(d):
                ln = d[p]
                if ln == 0:
                    p = (p // SECTOR + 1) * SECTOR
                    continue
                name = d[p + 33:p + 33 + d[p + 32]].decode('latin1').split(';')[0]
                if name.upper() == part.upper():
                    found = p
                    break
                p += ln
            if found is None:
                raise Fail(path + ' not in the ISO 9660 directory')
            rec = lba * SECTOR + found
            lba, size = struct.unpack_from('<I', d, found + 2)[0], struct.unpack_from('<I', d, found + 10)[0]
        return rec, lba, size

    def set_iso_record(self, rec, lba, size):
        self.wr(rec + 2, struct.pack('<I', lba) + struct.pack('>I', lba) + struct.pack('<I', size) + struct.pack('>I', size))

    def set_volume_size(self, nsect):
        for s in range(16, 32):
            d = self.rd(s * SECTOR, 8)
            if d[1:6] != b'CD001':
                break
            if d[0] == 255:
                break
            if d[0] in (1, 2):
                self.wr(s * SECTOR + 80, struct.pack('<I', nsect) + struct.pack('>I', nsect))


# ------------------------------------------------------------------------------------------------- UDF bridge
def udf_crc(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def udf_retag(d, loc=None, crclen=None):
    d = bytearray(d)
    if loc is not None:
        struct.pack_into('<I', d, 12, loc)
    if crclen is None:
        crclen = struct.unpack_from('<H', d, 10)[0]
    struct.pack_into('<HH', d, 8, udf_crc(bytes(d[16:16 + crclen])), crclen)
    d[4] = sum(d[i] for i in range(16) if i != 4) & 0xFF
    return bytes(d)


class Udf:
    """Just enough UDF (the bridge of this DVD image) to repoint a file of /DATA."""

    def __init__(self, img):
        self.img = img
        a = img.rd(256 * SECTOR, SECTOR)
        if struct.unpack_from('<H', a)[0] != 2:
            raise Fail('no UDF anchor')
        self.vds = [struct.unpack_from('<II', a, 16), struct.unpack_from('<II', a, 24)]
        self.pstart = self.pd_locs = None
        self.pd_locs, fsd = [], None
        for ln, loc in self.vds:
            for i in range(ln // SECTOR):
                d = img.rd((loc + i) * SECTOR, SECTOR)
                tid = struct.unpack_from('<H', d)[0]
                if tid == 5:
                    self.pstart = struct.unpack_from('<I', d, 188)[0]
                    self.pd_locs.append(loc + i)
                elif tid == 6 and fsd is None:
                    fsd = struct.unpack_from('<I', d, 252)[0]
                elif tid == 8:
                    break
        fs = img.rd((self.pstart + fsd) * SECTOR, SECTOR)
        self.root = struct.unpack_from('<I', fs, 404)[0]

    def _fe(self, lbn):
        return bytearray(self.img.rd((self.pstart + lbn) * SECTOR, SECTOR))

    def _entries(self, lbn):
        d = self._fe(lbn)
        lea, lad = struct.unpack_from('<II', d, 168)
        data = b''
        for o in range(176 + lea, 176 + lea + lad, 8):
            l, p = struct.unpack_from('<II', d, o)
            l &= 0x3FFFFFFF
            data += self.img.rd((self.pstart + p) * SECTOR, l)
        o, out = 0, {}
        while o + 38 <= len(data) and struct.unpack_from('<H', data, o)[0] == 257:
            lfi, liu = data[o + 19], struct.unpack_from('<H', data, o + 36)[0]
            name = data[o + 38 + liu:o + 38 + liu + lfi]
            name = name[1:].decode('latin1') if name[:1] == b'\x08' else name[1:].decode('utf-16-be', 'replace')
            out[name.upper()] = struct.unpack_from('<I', data, o + 24)[0]
            o += (38 + liu + lfi + 3) & ~3
        return out

    def repoint(self, path, lba, size, new_end):
        lbn = self.root
        for part in path.strip('/').split('/'):
            lbn = self._entries(lbn)[part.upper()]
        d = self._fe(lbn)
        if struct.unpack_from('<H', d)[0] != 261 or struct.unpack_from('<H', d, 34)[0] & 7 != 0:
            raise Fail('unexpected UDF file entry for ' + path)
        lea = struct.unpack_from('<I', d, 168)[0]
        ads, pos, left = b'', lba - self.pstart, size
        while left > 0:
            n = min(left, MAX_AD)
            ads += struct.pack('<II', n, pos)
            pos += sectors(n)
            left -= n
        if 176 + lea + len(ads) > SECTOR:
            raise Fail('UDF file entry too small for %d extents' % (len(ads) // 8))
        d[176 + lea:] = ads + b'\0' * (SECTOR - 176 - lea - len(ads))
        struct.pack_into('<I', d, 172, len(ads))
        struct.pack_into('<QQ', d, 56, size, sectors(size))
        self.img.wr((self.pstart + lbn) * SECTOR, udf_retag(bytes(d), crclen=176 + lea + len(ads) - 16))
        for loc in self.pd_locs:                         # partition length: up to the new end of the image
            pd = bytearray(self.img.rd(loc * SECTOR, SECTOR))
            struct.pack_into('<I', pd, 192, max(struct.unpack_from('<I', pd, 192)[0], new_end - self.pstart))
            self.img.wr(loc * SECTOR, udf_retag(bytes(pd)))

    def write_end_anchor(self, lba):
        a = self.img.rd(256 * SECTOR, SECTOR)
        self.img.wr(lba * SECTOR, udf_retag(a, loc=lba))


# ------------------------------------------------------------------------------------------------- patching
class Patcher:
    def __init__(self, path, log=print):
        self.path, self.log = path, log
        self.disc = BT4Disc(path)
        self.img = Image(path)

    def write_files(self, files):
        """files: {file id: bytes}. Returns {file id: (pt, index)} written."""
        by_pt = {}
        for fid, data in files.items():
            pt, idx = id_to_partition(fid)
            if pt not in self.disc.partitions or not 0 <= idx < self.disc.partitions[pt].count:
                raise Fail('file id %#x does not exist on this disc' % fid)
            if sectors(len(data)) > 0xFFFF:
                raise Fail('file %#x is too big (%d bytes; 65535 sectors at most)' % (fid, len(data)))
            by_pt.setdefault(pt, {})[idx] = data
        for pt, rep in sorted(by_pt.items()):
            info = self.disc.partitions[pt]
            grow = [i for i, d in rep.items() if sectors(len(d)) > info.fsize[i + 1]]
            if not grow:
                lba = self.disc.archives[pt][0]
                for i, d in rep.items():
                    start, n = info.range(i)
                    self.img.wr((lba + start) * SECTOR, d + b'\0' * (n * SECTOR - len(d)))
                self.log('  %s: %d file(s) written in place' % (info.fname, len(rep)))
            else:
                self.rebuild(pt, rep)
        self.img.f.flush()
        os.fsync(self.img.f.fileno())

    def rebuild(self, pt, rep):
        info = self.disc.partitions[pt]
        name = '/DATA/' + info.fname.upper()
        rec, old_lba, old_size = self.img.iso_record(name)
        img_sect = self.img.size() // SECTOR
        old_end = old_lba + sectors(old_size)
        at_end = old_end in (img_sect, img_sect - 1)       # an earlier run put it last (then the anchor follows)
        new_lba = old_lba if at_end else img_sect
        # new sector counts
        fsize = list(info.fsize)
        for i, d in rep.items():
            fsize[i + 1] = max(1, sectors(len(d)))
        tail = sectors(old_size) - info.end_sector         # unused sectors after the last file: kept
        total = info.ofst + sum(fsize) + max(0, tail)
        self.log('  %s: rebuilding (%d file(s) grow) at LBA %d, %d -> %d sectors%s'
                 % (info.fname, len([1 for i, d in rep.items() if sectors(len(d)) > info.fsize[i + 1]]), new_lba,
                    sectors(old_size), total, ' (in place: it is the last archive)' if at_end else ''))
        if at_end and total < sectors(old_size):
            total = sectors(old_size)
        src = open(self.path, 'rb')            # reads the old copy while the new one is written
        if at_end:                              # rebuilding over itself: stage the old archive in memory-sized pieces
            src = None
        out = self.img.f
        pos = new_lba * SECTOR
        t0 = time.time()

        def copy(from_off, n):
            nonlocal pos
            if src is None:
                raise Fail('internal: in-place rebuild needs a staged copy')
            while n:
                k = min(n, CHUNK)
                src.seek(from_off)
                buf = src.read(k)
                out.seek(pos)
                out.write(buf)
                pos += k
                from_off += k
                n -= k

        if at_end:
            # read every file that stays (they are moved within the same region): via a temporary file
            tmp = self.path + '.rebuild.tmp'
            with open(tmp, 'wb') as t:
                src2 = open(self.path, 'rb')
                n = sectors(old_size) * SECTOR
                src2.seek(old_lba * SECTOR)
                while n:
                    k = min(n, CHUNK)
                    t.write(src2.read(k))
                    n -= k
                src2.close()
            src = open(tmp, 'rb')
            base_old = 0
        else:
            base_old = old_lba * SECTOR
        run = None                              # (old byte offset, length) of consecutive unchanged files
        out.seek(pos)
        if info.ofst + fsize[0]:
            copy(base_old, (info.ofst + fsize[0]) * SECTOR)
        for i in range(info.count):
            start, n = info.range(i)
            if i in rep:
                if run:
                    copy(*run)
                    run = None
                d = rep[i]
                out.seek(pos)
                out.write(d + b'\0' * (fsize[i + 1] * SECTOR - len(d)))
                pos += fsize[i + 1] * SECTOR
            else:
                o = base_old + start * SECTOR
                if run and run[0] + run[1] == o:
                    run = (run[0], run[1] + n * SECTOR)
                else:
                    if run:
                        copy(*run)
                    run = (o, n * SECTOR)
        if run:
            copy(*run)
        if tail > 0:
            copy(base_old + info.end_sector * SECTOR, tail * SECTOR)
        end = new_lba + total
        if pos < end * SECTOR:
            out.seek(end * SECTOR - 1)
            out.write(b'\0')
        src.close()
        if at_end:
            os.remove(self.path + '.rebuild.tmp')
        out.truncate(end * SECTOR)
        # the closing UDF anchor, the volume size, the directory records, DBZ4.BIN
        try:
            udf = Udf(self.img)
            udf.repoint(name, new_lba, total * SECTOR, end + 1)
            udf.write_end_anchor(end)
            new_sect = end + 1
        except Fail as e:
            self.log('  (UDF bridge not updated: %s)' % e)
            new_sect = end
        self.img.set_iso_record(rec, new_lba, total * SECTOR)
        self.img.set_volume_size(new_sect)
        _, dlba, dsize = self.img.iso_record('/BIN/DBZ4.BIN')
        table = struct.pack('<%dH' % info.nfile, *fsize)
        self.img.wr(dlba * SECTOR + info.base + 0x118, table)
        self.log('  %s: %.0f s, image now %d sectors' % (info.fname, time.time() - t0, new_sect))


# ------------------------------------------------------------------------------------------------- sources
def chara_arg(s, names):
    try:
        return int(s, 0)
    except ValueError:
        low = [n.lower() for n in names]
        if s.lower() in low:
            return low.index(s.lower())
        close = [n for n in names if s.lower() in n.lower()]
        if len(close) == 1:
            return names.index(close[0])
        raise Fail('unknown character %r%s' % (s, ('; did you mean ' + ', '.join(close[:6])) if close else ''))


def bt4_files(disc, c, costume, a):
    """The BT4 file ids and bytes for the given BT3-format sources."""
    ch = Characters(disc)
    if not 0 <= c < 250:
        raise Fail('BT4 character id out of range (0..249)')
    if not 0 <= costume < ch.costumes[c]:
        raise Fail('BT4 character %d (%s) has %d costumes' % (c, ch.display_name(c), ch.costumes[c]))
    src = {}
    if a.from_bt3 is not None:
        sys.path.insert(0, r'D:\Dragon ball budokai modz\Tenkaichi3Decomp\port\tools')
        from bt3disc import BT3Disc
        from custom_fighter import NAMES
        d3 = BT3Disc(a.bt3_iso)
        c3 = chara_arg(a.from_bt3, NAMES)
        k3 = a.bt3_costume
        ids = cv.bt3_ids(c3)
        src['model'] = d3.read_id(ids['model'][k3])
        if not cv.is_pack(src['model'], 252):
            raise Fail('BT3 %s has no costume %d' % (NAMES[c3], k3))
        dmg = d3.read_id(ids['damaged'][k3])
        src['damaged'] = dmg if cv.is_pack(dmg, 252) else src['model']
        src['anim'], src['effect'] = d3.read_id(ids['anim']), d3.read_id(ids['effect'])
        src['portrait'] = d3.read_id(ids['portrait'])
        src['voice'] = {'bank_us': d3.read_id(ids['bank_us']), 'bank_jp': d3.read_id(ids['bank_jp'])}
        for l in range(cv.LINES):
            for lang in ('us', 'jp'):
                d = d3.read_id(ids['lines_' + lang] + l)
                if any(d[:16]):
                    src['voice']['%s/%02d' % (lang, l)] = d
        a.log('source: BT3 %s, costume %d' % (NAMES[c3], k3 + 1))
    for k in ('model', 'damaged', 'anim', 'effect', 'portrait'):
        if getattr(a, k):
            src[k] = open(getattr(a, k), 'rb').read()
    if a.voice:
        v = src.setdefault('voice', {})
        for name in ['bank_us', 'bank_jp'] + ['%s/%02d' % (l, i) for l in ('us', 'jp') for i in range(cv.LINES)]:
            for ext in ('.bin', '.adx', ''):
                p = os.path.join(a.voice, name + ext)
                if os.path.isfile(p):
                    v[name] = open(p, 'rb').read()
                    break
    out = {}
    if 'model' in src:
        keep = disc.read_id(ch.model_id(c, costume)) if a.keep_extras else None
        f = cv.bt4_files_from_bt3_model(src['model'], keep)
        out[ch.model_id(c, costume)] = f['model']
        if not a.model_only:
            for kind in ('pack3', 'data_f3c', 'movelist', 'lips_a', 'lips_b'):
                out[ch.file_id(kind, c)] = f[kind]
    if 'damaged' in src:
        keep = disc.read_id(ch.model_id(c, costume, True)) if a.keep_extras else None
        out[ch.model_id(c, costume, True)] = cv.bt4_files_from_bt3_model(src['damaged'], keep)['model']
    if 'anim' in src:
        out[ch.file_id('anim', c)] = cv.bt4_anim_from_bt3(src['anim'])
    if 'effect' in src:
        out[ch.file_id('effect', c)] = cv.bt4_effect_from_bt3(src['effect'])
    if 'portrait' in src:
        out[cv.BT4_PORTRAIT + c] = src['portrait']
    for name, d in src.get('voice', {}).items():
        if name == 'bank_us':
            out[cv.BT4_BANK_US + c] = d
        elif name == 'bank_jp':
            out[cv.BT4_BANK_JP + c] = d
        else:
            lang, l = name.split('/')
            out[(cv.BT4_LINE_US if lang == 'us' else cv.BT4_LINE_JP) + c * cv.LINES + int(l)] = d
    return out


# ------------------------------------------------------------------------------------------------- verify
def archive_hashes(path, pts, skip):
    """md5 of every file of archives `pts` except the ids in `skip`, read in long runs of consecutive files."""
    d = BT4Disc(path)
    f = open(path, 'rb')
    out = {}
    for pt in pts:
        info, lba = d.partitions[pt], d.archives[pt][0]
        i = 0
        while i < info.count:
            if partition_to_id(pt, i) in skip:
                i += 1
                continue
            j, start = i, info.range(i)[0]          # a run of files that are contiguous and not skipped
            size = 0
            while j < info.count and partition_to_id(pt, j) not in skip and size < CHUNK:
                size += info.range(j)[1]
                j += 1
            f.seek((lba + start) * SECTOR)
            buf = f.read(size * SECTOR)
            o = 0
            for k in range(i, j):
                n = info.range(k)[1] * SECTOR
                out[partition_to_id(pt, k)] = hashlib.md5(buf[o:o + n]).digest()
                o += n
            i = j
    f.close()
    return out


def verify(path, written, before, log=print):
    """Every written file reads back as written (rest of its sectors zero); every other file of the touched
    archives is unchanged (its md5 taken from the same image before writing)."""
    n = BT4Disc(path)
    bad = 0
    for fid, d in written.items():
        got = n.read_id(fid)
        if got[:len(d)] != d or any(got[len(d):]):
            log('  MISMATCH written file %#x' % fid)
            bad += 1
    after = archive_hashes(path, sorted({id_to_partition(f)[0] for f in written}), set(written))
    diff = [fid for fid in before if after.get(fid) != before[fid]]
    for fid in diff[:20]:
        log('  MISMATCH unchanged file %#x' % fid)
    bad += len(diff)
    log('verify: %d written files read back, %d other files of the touched archives unchanged, %d mismatches'
        % (len(written), len(before) - len(diff), bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--chara', required=True)
    ap.add_argument('--costume', type=int, default=1, help='1-based')
    for k in ('model', 'damaged', 'anim', 'effect', 'portrait', 'voice'):
        ap.add_argument('--' + k)
    ap.add_argument('--from-bt3')
    ap.add_argument('--bt3-costume', type=int, default=1)
    ap.add_argument('--bt3-iso', default=cv.BT3_ISO)
    ap.add_argument('--model-only', action='store_true')
    ap.add_argument('--keep-extras', action='store_true')
    ap.add_argument('--iso', default=ORIG)
    ap.add_argument('--out')
    ap.add_argument('--fresh', action='store_true')
    ap.add_argument('--no-verify', action='store_true')
    a = ap.parse_args()
    a.log = print
    a.bt3_costume -= 1
    orig = os.path.abspath(a.iso)
    out = os.path.abspath(a.out or os.path.join(os.path.dirname(orig), 'SLUS_219.78.DBZBT4B14REV2ENG.custom.iso'))
    if os.path.normcase(out) == os.path.normcase(orig):
        raise Fail('the output must not be the original disc image')
    if a.fresh or not os.path.isfile(out):
        free = shutil.disk_usage(os.path.dirname(out)).free
        if free < os.path.getsize(orig) * 1.5:
            raise Fail('not enough free space next to the original for a copy')
        print('copying %s -> %s ...' % (orig, out))
        t = time.time()
        shutil.copyfile(orig, out)
        print('  %.0f s' % (time.time() - t))
    base = BT4Disc(orig)
    chs = Characters(base)
    try:
        c = int(a.chara, 0)
    except ValueError:                  # names need the menu archive (BPE, slow in Python): only when asked by name
        c = chara_arg(a.chara, [chs.display_name(i) for i in range(250)])
    names = {c: chs.display_name(c)}
    p = Patcher(out)
    files = bt4_files(p.disc, c, a.costume - 1, a)
    if not files:
        raise Fail('nothing to write: give --model / --anim / ... or --from-bt3')
    print('target: BT4 %d %s, costume %d: %d file(s): %s' % (c, names[c], a.costume, len(files),
          ', '.join('%#x' % f for f in sorted(files)[:12]) + (' ...' if len(files) > 12 else '')))
    before = None if a.no_verify else archive_hashes(out, sorted({id_to_partition(f)[0] for f in files}), set(files))
    p.write_files(files)
    p.img.f.close()
    if before is not None and verify(out, files, before):
        raise Fail('verification failed')
    print('done:', out)


if __name__ == '__main__':
    try:
        main()
    except Fail as e:
        print('error:', e)
        sys.exit(1)
