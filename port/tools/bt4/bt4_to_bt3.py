"""BT4 (Team BT4 Beta 14 Rev2) <-> BT3 (USA) character file conversion.

    python bt4_to_bt3.py report [--bt3-iso X]      match rates over all characters present in both games
    python bt4_to_bt3.py export <bt4 chara> <costume> <out dir> [--damaged]   BT3-format files of a BT4 character
    python bt4_to_bt3.py map                         the part map

Where BT4 keeps each part of a BT3 252-part model pack (part i = game entry i + 1). Found by comparing the 161
characters of both games part by part (map_stats.py); `c` = BT4 character, k = costume:

  part (entry)            BT3: 0x590 + c*10 + k        BT4
  0      (0x01)           HUD face texture             0x1036 + c (3-part "pack3"), part 0   (per character)
  1..16  (0x02..0x11)     model, textures              model pack (52 parts, 0x1612 + ..., see bt4_disc.model_id)
                                                       part i (same index)
  17..27 (0x12..0x1C)     moveset data                 pack3 part 1: one 12496-byte blob, fixed offsets (BLOB)
                                                       - 21 (0x16, talk table): 1040 bytes in BT4 (252 opponents
                                                         instead of 162) -> remapped by opponent id
                                                       - 22 (0x17, chains) stays in the model pack, part 22
                                                       - 23 (0x18, techniques): the partner a rush brings in is
                                                         where BT4 keeps it -> put where BT3 reads it
                                                         (super_bt4_to_bt3)
                                                       - 25 (0x1A) is empty in BT3; BT4's model part 25 is a BT4 extra
  28..34, 36..39          model extras                 model pack part i
  35     (0x24)           "aC" table                   0xF3C + c (raw file)
  40..42 (0x29..0x2B)     CMAn camera animations       pack3 part 2 (a 3-part BT3-style pack)
  43..51 (0x2C..0x34)     move list: JP, US English,   0x1130 + c: ONE English UTF-16 text; BT3 entry 0x2D (US)
                          (PT), UK English, ES, DE,    and 0x2F (UK) are the English ones. bt3_model_pack writes it
                          FR, IT, KO                   into every language entry except 0x2E (empty in 132 BT3 chars)
  52..151 (0x35+line)     lip sync, Japanese           0x122A + c (100-part LIPS pack), part line
  152..251 (+0x64)        lip sync, English            0x1324 + c
  (BT4 model part 51 is a BT4 extra texture, like part 12/13; BT4 model part 0 is always empty)

Other files:
  animations  BT3 0x590+c*10+8 (440 parts)   = BT4 0x141E + c (440 parts, same layout)
  effects     BT3 +9 (6 parts)               = BT4 0x1518 + c (7 parts; the 7th is empty in 157 of 161: dropped)
  voice bank  BT3 0xC7B + c (US) / 0xBDA + c (JP)   = BT4 0x1D10C + c (US) / 0x1D012 + c (JP)     [ELF 0x19BD88]
  lines       BT3 0xCC32 / 0x8D4E + c*100 + line    = BT4 0x16E6A / 0x10CC2 + c*100 + line (PZS4US0) [ELF 0x265E74]
  portrait    BT3 0x2F9 + c (BPE texture)           = BT4 0x71C + c
  team picture BT3 8 + c*2 + side (3424 B)          ~ BT4 0xD48 + c*2 + side (4096 B, a different texture size:
                                                      not converted)
The flag bit at [gp-0x4FE4]+0x1608 picks the language: 1 = English (0x1324 lips, 0x1D10C bank, 0x16E6A lines).

BT3 packs are written exactly like the BT3 tools do (64-byte aligned header and parts, count + 1 offsets); BT4
packs are 16-byte aligned. A part copied from a BT4 pack keeps its true length to 16 bytes, so padding it to 64
gives BT3's bytes; parts from BT4 raw files (0xF3C, 0x1130) only have sector-rounded sizes: trailing zeros are
cut and the part padded to 64 again (BT3 has a few parts that end in zeros: those can differ in length).
"""
import os, struct, sys, json

from bt4_disc import BT4Disc, Characters

BT4_ISO = r'D:\Dragon ball budokai modz\SLUS_219.78.DBZBT4B14REV2ENG.iso'
BT3_ISO = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))),
                       'Dragon Ball Z - Budokai Tenkaichi 3 (USA) (En,Ja).iso')

# --- BT3 / BT4 numbers -----------------------------------------------------------------------------------------
BT3_CHARAS = 161
BT3_MODEL, BT3_PORTRAIT, BT3_BANK_US, BT3_BANK_JP, BT3_LINE_US, BT3_LINE_JP = 0x590, 0x2F9, 0xC7B, 0xBDA, 0xCC32, 0x8D4E
BT4_PORTRAIT, BT4_BANK_US, BT4_BANK_JP, BT4_LINE_US, BT4_LINE_JP = 0x71C, 0x1D10C, 0x1D012, 0x16E6A, 0x10CC2
LINES = 100

BLOB_SIZE = 12496                      # pack3 part 1: BT3 parts 17..27 (entries 0x12..0x1C) at fixed offsets
BLOB = {17: (0x0000, 256), 18: (0x0100, 8512), 19: (0x2240, 704), 20: (0x2500, 192), 21: (0x25C0, 1040),
        23: (0x29D0, 640), 24: (0x2C50, 256), 26: (0x2D50, 512), 27: (0x2F50, 384)}
BT3_SIZE = {17: 256, 18: 8512, 19: 704, 20: 192, 21: 704, 23: 640, 24: 256, 26: 512, 27: 384}
TALK_BT3, TALK_BT4 = 162, 252          # talk table entries (4 bytes per opponent id); the rest of the part is zeros
TALK_NONE = b'\xff\x00\xff\x00'
MODEL_SAME = [i for i in range(1, 52) if not (17 <= i <= 27 and i != 22) and i != 35 and not 40 <= i <= 51]
TEXT_PARTS = list(range(43, 52))       # entries 0x2C..0x34
TEXT_US, TEXT_SKIP = 44, 45            # entry 0x2D (US English); entry 0x2E left empty
LIPS_JP, LIPS_US = 52, 152

# BT3 character id -> BT4 character id (pairing.py: identical animation/model parts + names, one to one).
BT3_TO_BT4 = {int(k): v for k, v in json.loads(
    '{"109":109,"156":156,"11":11,"89":89,"62":62,"101":101,"112":112,"61":61,"137":137,"80":80,"124":124,"103":103,'
    '"47":47,"28":28,"111":111,"87":87,"155":155,"123":123,"27":27,"48":48,"121":121,"136":136,"141":141,"129":129,'
    '"88":88,"60":60,"25":25,"63":63,"26":26,"77":77,"37":37,"24":24,"138":138,"50":50,"46":46,"84":84,"56":56,'
    '"49":49,"140":140,"23":23,"130":130,"126":126,"86":86,"29":29,"66":66,"147":147,"12":12,"90":90,"65":65,'
    '"97":97,"55":55,"153":153,"152":152,"92":92,"93":93,"44":44,"117":117,"91":91,"58":58,"114":114,"82":82,"0":0,'
    '"120":120,"115":115,"142":142,"42":42,"39":39,"41":41,"43":43,"94":94,"32":32,"100":100,"127":127,"20":20,'
    '"45":45,"15":15,"21":21,"95":95,"22":22,"17":17,"18":18,"10":10,"116":116,"102":102,"159":159,"40":40,'
    '"79":184,"73":73,"139":195,"113":192,"160":160,"118":182,"69":179,"125":186,"146":197,"33":33,"144":144,'
    '"4":4,"119":183,"85":79,"57":165,"99":223,"135":166,"98":222,"104":185,"70":178,"68":177,"157":176,"154":180,'
    '"110":193,"132":189,"71":167,"8":8,"150":175,"64":196,"134":187,"158":194,"151":174,"59":191,"131":188,'
    '"81":170,"122":171,"74":173,"78":169,"14":14,"133":190,"72":168,"7":7,"16":16,"96":96,"51":51,"2":2,"19":19,'
    '"5":5,"148":148,"52":52,"1":1,"149":181,"128":128,"9":9,"3":3,"6":6,"106":106,"105":105,"75":75,"67":67,'
    '"83":83,"143":143,"53":53,"36":36,"107":224,"108":107,"30":30,"54":54,"76":172,"38":38,"34":31,"145":145,'
    '"35":218,"13":13,"31":217}').items()}
BT4_TO_BT3 = {v: k for k, v in BT3_TO_BT4.items()}


# --- packs ------------------------------------------------------------------------------------------------------
def pack_split(data):
    """count, count + 1 offsets (the last one = the pack's length): the parts, padding included."""
    n = struct.unpack_from('<I', data)[0]
    offs = struct.unpack_from('<%dI' % (n + 1), data, 4)
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def _join(parts, align):
    parts = [p + b'\0' * (-len(p) % align) for p in parts]
    head = -(-(4 + (len(parts) + 1) * 4) // align) * align
    pos, offs = head, []
    for p in parts:
        offs.append(pos)
        pos += len(p)
    offs.append(pos)
    h = struct.pack('<%dI' % (len(parts) + 2), len(parts), *offs)
    return h + b'\0' * (head - len(h)) + b''.join(parts)


def join_bt3(parts):           # BT3: header and parts 64-byte aligned (as port/tools/custom_fighter.join_pack)
    return _join(parts, 64)


def join_bt4(parts):           # BT4: 16-byte aligned
    return _join(parts, 16)


def pad64(b):
    return b + b'\0' * (-len(b) % 64)


def raw_part(b):
    """A part from a BT4 raw file (sector-rounded): trailing zeros cut, padded to 64 (UTF-16 text kept even)."""
    s = b.rstrip(b'\0')
    if len(s) % 2:
        s += b'\0'
    return pad64(s)


def is_pack(data, count=None):
    if len(data) < 8 or not any(data[:64]):
        return False
    n = struct.unpack_from('<I', data)[0]
    return (count is None and 0 < n < 1000) or n == count


# --- talk table ---------------------------------------------------------------------------------------------------
def talk_bt4_to_bt3(t4):
    out = bytearray()
    for b in range(TALK_BT3):
        c4 = BT3_TO_BT4.get(b, 250)          # 161 (the entry after the last character) <- BT4's entry 250 (guess)
        out += t4[c4 * 4:c4 * 4 + 4]
    return bytes(out) + b'\0' * (BT3_SIZE[21] - len(out))


def talk_bt3_to_bt4(t3):
    out = bytearray(TALK_NONE * TALK_BT4)
    for b in range(TALK_BT3):
        c4 = BT3_TO_BT4.get(b, 250)
        out[c4 * 4:c4 * 4 + 4] = t3[b * 4:b * 4 + 4]
    return bytes(out) + b'\0' * (BLOB[21][1] - len(out))


# --- technique table (part 23, entry 0x18: Blast 2 x 2 + Ultimate, arrays of 3; include/battle/btl_tech_b.h) ------
# The partner a rush sequence brings in (a character, or an object 0x100..: file 0xC1C + id) sits elsewhere in BT4.
# BT3 (BtlSuper_GetThrowChara / GetThrowCostume, 0x211718 / 0x2117C0): s16 throwChara[3] at 0x208 and
# throwCharaAlt[3] at 0x20E (the one taken when the user is that character), -1 = none. Team BT4 rewrote both
# functions: s16 [costume 0..3][slot] at 0x244 (past the end of BT3's table) and u8 costume [slot][costume] at 0x208.
# Left as it was, BT3 read 0 at 0x208 in nearly every BT4 technique: Goku (Early) as a partner of every rush, with
# no step of the sequence (+0x220, -2) to take him in or let him go, so the change request stayed open and time
# stood still for good once an Ultimate connected (the "freeze after BT4 Ultimates").
SUPER_PARTNER_BT3, SUPER_PARTNER_ALT_BT3, SUPER_PARTNER_BT4, SUPER_STEP, SUPER_STEPS = 0x208, 0x20E, 0x244, 0x220, 0x159
OBJECT_IDS = range(0x100, 0x12C)       # objects with a file (0xC1C + id: 0xD1C..0xD47, the first archive's last)


def super_partner_bt4_to_bt3(v):
    """A BT4 partner id as BT3's: a character both games have, an object, or -1 (none, or a BT4-only character)."""
    if v < 0:
        return -1
    if v < 0x100:
        return BT4_TO_BT3.get(v, -1)
    return v if v in OBJECT_IDS else -1


def super_bt4_to_bt3(t, costume):
    """BT4's technique table with the partners where BT3 reads them (this costume's); a partner that cannot be
    brought in (none in BT3, or no step of the sequence for it) is none, and so is that step. BT4's partner
    costumes have no place in BT3: the partner comes in its first costume (as BT3's own do)."""
    t = bytearray(t)
    if len(t) < SUPER_PARTNER_BT4 + 24:
        return bytes(t)
    for j in range(3):
        v = super_partner_bt4_to_bt3(struct.unpack_from('<h', t, SUPER_PARTNER_BT4 + ((costume & 3) * 3 + j) * 2)[0])
        step, steps = struct.unpack_from('<b', t, SUPER_STEP + j)[0], max(1, struct.unpack_from('<b', t, SUPER_STEPS + j)[0])
        if v < 0 or not 0 <= step < steps:
            v = -1
            struct.pack_into('<b', t, SUPER_STEP + j, -2)
        struct.pack_into('<h', t, SUPER_PARTNER_BT3 + j * 2, v)
        struct.pack_into('<h', t, SUPER_PARTNER_ALT_BT3 + j * 2, v)
    return bytes(t)


# --- BT4 -> BT3 ---------------------------------------------------------------------------------------------------
class BT4Chars:
    def __init__(self, disc):
        self.disc = disc
        self.ch = Characters(disc)
        self.cache = {}

    def raw(self, fid):
        if fid not in self.cache:
            self.cache[fid] = self.disc.read_id(fid)
        return self.cache[fid]


def _src(src):
    return src if isinstance(src, BT4Chars) else BT4Chars(src)


def bt3_model_pack(bt4disc, c, costume, damaged=False):
    """A BT3 252-part model pack built from BT4 character c's files."""
    s = _src(bt4disc)
    model = s.raw(s.ch.model_id(c, costume, damaged))
    if not is_pack(model):
        raise ValueError('BT4 character %d costume %d has no model (placeholder)' % (c, costume))
    m = pack_split(model)
    p3 = pack_split(s.raw(s.ch.file_id('pack3', c)))
    parts = [b''] * 252
    for i in MODEL_SAME:
        if i < len(m):
            parts[i] = pad64(m[i])
    parts[0] = pad64(p3[0])
    blob = p3[1]
    for i, (o, n) in BLOB.items():
        parts[i] = talk_bt4_to_bt3(blob[o:o + n]) if i == 21 else blob[o:o + n]
    parts[23] = super_bt4_to_bt3(parts[23], costume)
    parts[35] = raw_part(s.raw(s.ch.file_id('data_f3c', c)))
    cam = pack_split(p3[2])
    for j in range(3):
        parts[40 + j] = pad64(cam[j])
    text = raw_part(s.raw(s.ch.file_id('movelist', c)))
    for i in TEXT_PARTS:
        if i != TEXT_SKIP:
            parts[i] = text
    for base, kind in ((LIPS_JP, 'lips_a'), (LIPS_US, 'lips_b')):
        for j, p in enumerate(pack_split(s.raw(s.ch.file_id(kind, c)))[:LINES]):
            parts[base + j] = pad64(p)
    return join_bt3(parts)


def bt3_anim_pack(bt4disc, c):
    s = _src(bt4disc)
    return join_bt3([pad64(p) for p in pack_split(s.raw(s.ch.file_id('anim', c)))])


def bt3_effect_pack(bt4disc, c):
    """BT4's 7-part effect pack -> BT3's 6 parts (the 7th part is empty / 16 zero bytes in BT4)."""
    s = _src(bt4disc)
    return join_bt3([pad64(p) for p in pack_split(s.raw(s.ch.file_id('effect', c)))[:6]])


def bt3_voice_files(bt4disc, c, bt3_c):
    """{BT3 file id: bytes} of BT4 character c's voice, for BT3 character slot bt3_c: two banks, 2 x 100 lines.
    Lines and banks are plain copies (ADX / IECS banks, same formats); a placeholder (empty) line is skipped."""
    s = _src(bt4disc)
    out = {BT3_BANK_US + bt3_c: s.raw(BT4_BANK_US + c), BT3_BANK_JP + bt3_c: s.raw(BT4_BANK_JP + c)}
    for l in range(LINES):
        for b3, b4 in ((BT3_LINE_US, BT4_LINE_US), (BT3_LINE_JP, BT4_LINE_JP)):
            d = s.raw(b4 + c * LINES + l)
            if any(d[:16]):
                out[b3 + bt3_c * LINES + l] = d
    return out


def bt3_portrait(bt4disc, c):
    s = _src(bt4disc)
    return s.raw(BT4_PORTRAIT + c)


def bt3_ids(c3):
    """The BT3 file ids of BT3 character c3 (for writing the converted files into a BT3 data set)."""
    return {'model': [BT3_MODEL + c3 * 10 + k for k in range(4)], 'damaged': [BT3_MODEL + c3 * 10 + 4 + k for k in range(4)],
            'anim': BT3_MODEL + c3 * 10 + 8, 'effect': BT3_MODEL + c3 * 10 + 9, 'portrait': BT3_PORTRAIT + c3,
            'bank_us': BT3_BANK_US + c3, 'bank_jp': BT3_BANK_JP + c3,
            'lines_us': BT3_LINE_US + c3 * LINES, 'lines_jp': BT3_LINE_JP + c3 * LINES}


# --- BT3 -> BT4 (used by bt4_patch.py) ----------------------------------------------------------------------------
def bt4_files_from_bt3_model(pack, keep=None):
    """A BT3 252-part model pack -> the BT4 files it is spread over:
    {'model': 52-part pack, 'pack3', 'data_f3c', 'movelist', 'lips_a', 'lips_b'} (the last five are per character).
    keep: the target's current BT4 model pack; its BT4-only parts 25 and 51 are kept (else they stay empty)."""
    p = pack_split(pack)
    if len(p) != 252:
        raise ValueError('not a BT3 252-part model pack')
    m = [b''] * 52
    for i in MODEL_SAME:
        m[i] = p[i]
    if keep is not None and is_pack(keep):
        k = pack_split(keep)
        for i in (25, 51):
            if i < len(k):
                m[i] = k[i]
    blob = bytearray(BLOB_SIZE)
    for i, (o, n) in BLOB.items():
        d = talk_bt3_to_bt4(p[i]) if i == 21 else p[i][:n]
        blob[o:o + len(d)] = d
    pack3 = join_bt4([p[0], bytes(blob), join_bt3([p[40], p[41], p[42]])])
    return {'model': join_bt4(m), 'pack3': pack3, 'data_f3c': p[35].rstrip(b'\0') or p[35],
            'movelist': p[TEXT_US].rstrip(b'\0') + b'\0\0',
            'lips_a': join_bt4(p[LIPS_JP:LIPS_JP + LINES]), 'lips_b': join_bt4(p[LIPS_US:LIPS_US + LINES])}


def bt4_anim_from_bt3(pack):
    return join_bt4(pack_split(pack))


def bt4_effect_from_bt3(pack):
    p = pack_split(pack)
    return join_bt4(p[:6] + [b''])


# --- report -------------------------------------------------------------------------------------------------------
def report(bt3_iso=BT3_ISO):
    from bt3disc import BT3Disc
    d3, s = BT3Disc(bt3_iso), BT4Chars(BT4Disc(BT4_ISO))
    tot = {'packs': 0, 'packs same': 0, 'parts': 0, 'parts same': 0}
    per = {}
    for c3, c4 in sorted(BT3_TO_BT4.items()):
        for k in range(4):
            for dmg in (False, True):
                b3 = d3.read_id(BT3_MODEL + c3 * 10 + k + (4 if dmg else 0))
                if not is_pack(b3, 252) or k >= s.ch.costumes[c4]:
                    continue
                try:
                    b4 = bt3_model_pack(s, c4, k, dmg)
                except ValueError:
                    continue
                tot['packs'] += 1
                tot['packs same'] += b4 == b3
                for i, (x, y) in enumerate(zip(pack_split(b3), pack_split(b4))):
                    if not x and not y:
                        continue
                    key = 'lips' if i >= 52 else ('text' if 43 <= i <= 51 else i)
                    a = per.setdefault(key, [0, 0])
                    a[0] += 1
                    a[1] += x == y
                    tot['parts'] += 1
                    tot['parts same'] += x == y
        s.cache.clear()
    anim = eff = 0
    for c3, c4 in BT3_TO_BT4.items():
        anim += bt3_anim_pack(s, c4) == d3.read_id(BT3_MODEL + c3 * 10 + 8)
        eff += bt3_effect_pack(s, c4) == d3.read_id(BT3_MODEL + c3 * 10 + 9)
        s.cache.clear()
    print('model packs (character x costume x damaged, both games): %(packs)d, byte-identical %(packs same)d' % tot)
    print('parts: %d, identical %d (%.1f%%)' % (tot['parts'], tot['parts same'], 100.0 * tot['parts same'] / tot['parts']))
    for k in sorted(per, key=lambda x: (isinstance(x, str), x)):
        print('  part %-5s %4d / %4d identical (%5.1f%%)' % (k, per[k][1], per[k][0], 100.0 * per[k][1] / per[k][0]))
    print('anim packs identical %d / %d, effect packs identical %d / %d' % (anim, len(BT3_TO_BT4), eff, len(BT3_TO_BT4)))


def main(argv):
    if not argv or argv[0] == 'map':
        print(__doc__)
    elif argv[0] == 'report':
        report(argv[argv.index('--bt3-iso') + 1] if '--bt3-iso' in argv else BT3_ISO)
    elif argv[0] == 'export':
        c, k, out = int(argv[1], 0), int(argv[2], 0), argv[3]
        os.makedirs(out, exist_ok=True)
        s = BT4Chars(BT4Disc(BT4_ISO))
        files = {'model.bin': bt3_model_pack(s, c, k, '--damaged' in argv), 'anim.bin': bt3_anim_pack(s, c),
                 'effect.bin': bt3_effect_pack(s, c), 'portrait.bin': bt3_portrait(s, c)}
        for name, data in files.items():
            open(os.path.join(out, name), 'wb').write(data)
        print('wrote', ', '.join('%s (%d)' % (n, len(d)) for n, d in files.items()), 'to', out)
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
