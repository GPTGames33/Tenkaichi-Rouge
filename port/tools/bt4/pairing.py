"""BT3 character id -> BT4 character id, by identical anim / model parts (corr_cache.json from build_corr.py) and names."""
import json, re, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
E = 'd41d8cd98f00'
ALIAS = {'taopaipai': 'tao pai pai', 'chichi': 'chi chi', 'paikuhan': 'pikkon', 'hildegarn': 'hirudegarn',
         'sauza': 'salza', 'akuman': 'devilman', 'namu': 'nam', 'wheelo': 'dr wheelo', 'android 2': 'dr gero',
         'hatchan': 'android 8', 'kibito kai': 'kibitokai', 'shin shenron': 'syn shenron', 'goten': 'kid goten',
         'goten super saiyan': 'kid goten super saiyan', 'evil buu': 'majin buu pure evil', 'demon king piccolo': 'king piccolo'}
def norm(s):
    s = ALIAS.get(s.lower(), s)
    s = s.lower().replace('(', ' ').replace(')', ' ').replace('.', ' ').replace('#', ' ').replace('-', ' ')
    s = s.replace('med', 'mid').replace('100% power', 'full power').replace('great ape', 'greatape')
    return ' '.join(sorted(s.split()))
def pair(bt3_names, bt4_names, cache=None):
    j = json.load(open(cache or os.path.join(HERE, 'corr_cache.json')))
    b3, b4 = j['bt3'], j['bt4']
    cand = []
    for c3, v3 in b3.items():
        s3a, s3m = set(v3['anim']) - {E}, set(v3['model']) - {E}
        for c4, v4 in b4.items():
            sa = len(s3a & set(v4['anim'])) / max(1, len(s3a)); sm = len(s3m & set(v4['model'])) / max(1, len(s3m))
            nm = 1.0 if norm(bt3_names[int(c3)]) == norm(bt4_names[int(c4)]) else 0.0
            cand.append((2 * sa + sm + nm + (0.1 if c4 == c3 else 0), int(c3), int(c4)))
    cand.sort(reverse=True)
    res, used = {}, set()
    for s, c3, c4 in cand:              # greedy one-to-one assignment, best scores first
        if c3 in res or c4 in used:
            continue
        res[c3] = (c4, s)
        used.add(c4)
    return res
if __name__ == '__main__':
    sys.path.insert(0, r'D:\Dragon ball budokai modz\Tenkaichi3Decomp\port\tools')
    from custom_fighter import NAMES
    from bt4_disc import BT4Disc, Characters
    ch = Characters(BT4Disc(r'D:\Dragon ball budokai modz\SLUS_219.78.DBZBT4B14REV2ENG.iso'))
    n4 = [ch.display_name(c) for c in range(250)]
    r = pair(NAMES, n4)
    for c3, (c4, s) in sorted(r.items()):
        if c3 != c4 or s < 3.5 or '-a' in sys.argv:
            print('%3d %-33s -> %3d %-40s %.2f' % (c3, NAMES[c3], c4, n4[c4], s))
    json.dump({str(k): v[0] for k, v in r.items()}, open(os.path.join(HERE, 'bt3_to_bt4_chara.json'), 'w'), indent=0)
