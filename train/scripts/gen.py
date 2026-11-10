# Synthetic / real-stroke handwriting line generator matching rmpp-hwr preprocessing. Usage:
#   gen.py <kind> <n> <seed> <outdir> [--val]     kind: mixed kor eng print real iamon personal
import os, sys, re, glob, random, math, numpy as np, cv2, pandas as pd
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
from skimage.morphology import skeletonize
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prep import finalize

W = os.environ.get('HWR_WORK', os.getcwd())   # training work dir (see train/README.md)
D = f'{W}/data'; G = f'{W}/fonts/gf/ofl'
KDIRS = "blackandwhitepicture cutefont dokdo eastseadokdo gaegu gamjaflower himelody kiranghaerang nanumbrushscript nanumpenscript poorstory singleday yeonsung".split()
PDIRS = "notosanskr nanumgothic nanummyeongjo".split()
dic = set(l.rstrip('\n') for l in open(f'{D}/dict.txt', encoding='utf-8')) | {' '}
HANGUL = re.compile(r'[ᄀ-ᇿ㄰-㆏가-힯]')
_cm = {}
def cmap(p):
    if p not in _cm:
        try: _cm[p] = set(TTFont(p, lazy=True, fontNumber=0).getBestCmap().keys())
        except Exception: _cm[p] = set()
    return _cm[p]
def fonts_of(dirs, req):
    out = []
    for d in dirs:
        for p in sorted(glob.glob(f'{G}/{d}/*.ttf')):
            if 'Italic' in p: continue
            if req(cmap(p)): out.append(p)
    return out
latin_ok = lambda c: all(ord(x) in c for x in 'abcxyzABCXYZ0123456789.,!?:-/@%')
hang_ok = lambda c: sum(1 for x in c if 0xac00 <= x <= 0xd7a3) > 2000
KF = HLAT = PKF = PLAT = None
def init_fonts():
    global KF, HLAT, PKF, PLAT
    KF = fonts_of(KDIRS, hang_ok)
    hl = open(f'{W}/fonts/hand_latin.txt').read().split()
    HLAT = fonts_of(hl, latin_ok)
    PKF = fonts_of(PDIRS, hang_ok)
    PLAT = fonts_of(['notosans'], latin_ok) or PKF
_fc = {}
def pil_font(p, s):
    k = (p, s)
    if k not in _fc: _fc[k] = ImageFont.truetype(p, s)
    return _fc[k]

# ---------------- text ----------------
ko_sent = en_sent = ko_words = en_words = None
TECH = "meeting zoom todo ok yes no project deadline email call lunch review report test note plan team idea done check list work page book hello thanks sorry please free sale new Wi-Fi coffee ppt pdf app".split()
PART = "는 은 를 을 가 이 에서 에 도 와 과 의 로 으로 만 까지 부터 랑 이랑 하고".split()
UNITS = "kg g km cm mm % 원 개 명 분 시간 일 GB MB m 층 번 장 권 살 달 주 회".split()
DAYS = "월 화 수 목 금 토 일".split()
def init_text():
    global ko_sent, en_sent, ko_words, en_words
    rd = lambda f: open(f'{D}/{f}', encoding='utf-8').read().split('\n')
    ko_sent, en_sent, ko_words, en_words = rd('corpus_ko.txt'), rd('corpus_en.txt'), rd('words_ko.txt'), rd('words_en.txt')
def chunk(r, sents, maxlen=None):
    maxlen = maxlen or r.choice([8, 12, 16, 22, 30])
    w = r.choice(sents).split(' ')
    i = r.randrange(len(w)); out = ''
    while i < len(w) and len(out) + len(w[i]) + (1 if out else 0) <= maxlen:
        out += (' ' if out else '') + w[i]; i += 1
    return out
def num(r):
    k = r.random()
    if k < .5: return str(r.randint(0, 99))
    if k < .7: return str(r.randint(100, 9999))
    if k < .8: return f'{r.randint(1, 999)},{r.randint(0, 999):03d}'
    if k < .9: return f'{r.randint(0, 99)}.{r.randint(0, 99)}'
    return str(r.randint(10000, 99999999))
def tm(r):
    h = r.randint(1, 12); m = r.choice([0, 10, 15, 20, 30, 40, 45, 50, r.randint(0, 59)])
    return r.choice([f'{h}시', f'{h}시 {m}분', f'{h}:{m:02d}', f'오후 {h}시', f'오전 {h}시 {m}분', f'{h}pm', f'{h}:{m:02d} PM', f'{h}시 반', f'{h}시 {m}분'])
def dt(r):
    mo, d = r.randint(1, 12), r.randint(1, 28)
    return r.choice([f'2026-{mo:02d}-{d:02d}', f'{mo}/{d}', f'{mo}월 {d}일', f'2026.{mo}.{d}', f'{mo}/{d}({r.choice(DAYS)})', f'{mo}월 {d}일 {r.choice(DAYS)}요일'])
def eng(r):
    if r.random() < .3: w = r.choice(TECH)
    else: w = r.choice(en_words[:8000])
    k = r.random()
    return w.capitalize() if k < .25 else w.upper() if k < .32 else w
def engp(r): return ' '.join(eng(r) for _ in range(r.choice([1, 1, 2, 2, 3])))
def kor(r): return r.choice(ko_words[:15000])
def email(r):
    u = r.choice(en_words[:3000]).lower() + r.choice(['', '.', '_', str(r.randint(1, 99))]) + r.choice(['', r.choice(en_words[:3000]).lower()])
    return f'{u}@{r.choice(["gmail","school","naver","daum","mail"])}.{r.choice(["com","edu","net","kr"])}'
def phone(r): return f'010-{r.randint(1000, 9999)}-{r.randint(1000, 9999)}'
def mixed(r):
    T = [lambda: f'회의는 {tm(r)} {engp(r)}', lambda: f'{tm(r)}에 {eng(r)}', lambda: f'{eng(r)}{r.choice(PART)} {kor(r)}',
         lambda: f'{kor(r)} {eng(r)} {kor(r)}', lambda: f'{dt(r)} {kor(r)}', lambda: f'{kor(r)} {num(r)}{r.choice(UNITS)}',
         lambda: f'{engp(r)}: {num(r)}', lambda: email(r), lambda: f'TODO: {kor(r)} {kor(r)}', lambda: f'{r.randint(1, 20)}. {kor(r)} {eng(r)}',
         lambda: f'{kor(r)}({eng(r)})', lambda: f'{eng(r)} ({num(r)})', lambda: f'{tm(r)} {engp(r)} {kor(r)}', lambda: tm(r), lambda: dt(r),
         lambda: num(r) + r.choice(UNITS), lambda: phone(r), lambda: f'{num(r)} {kor(r)}', lambda: f'{engp(r)} {tm(r)}',
         lambda: f'오늘 {engp(r)}{r.choice(PART)} {tm(r)}에 {kor(r)}', lambda: f'${num(r)}', lambda: f'{engp(r)}, {engp(r)}!',
         lambda: f'{kor(r)}{r.choice(PART)} {num(r)}개 {eng(r)}', lambda: f'hot {num(r)}일의 {kor(r)}']
    if r.random() < .35:   # free random concatenation of segments
        segs = [r.choice([lambda: kor(r), lambda: eng(r), lambda: num(r), lambda: tm(r), lambda: kor(r)])() for _ in range(r.randint(2, 5))]
        return ''.join(s + (' ' if r.random() < .75 else '') for s in segs).strip() + r.choice(['', '', '.', '!', '?'])
    s = r.choice(T)()
    return s + r.choice(['', '', '', '.', '!', '?'])
def text_of(kind, r):
    if kind == 'kor': t = chunk(r, ko_sent)
    elif kind == 'eng':
        t = chunk(r, en_sent)
        if r.random() < .15: t = t.lower()
    else: t = mixed(r)
    t = t.strip()
    if r.random() < .08: t = t.rstrip('.')
    return t if 1 <= len(t) <= 40 and all(c in dic for c in t) else None

# ---------------- rendering ----------------
S = 96
def font_for(ch, kf, lf):
    if ch == ' ': return 'sp'
    o = ord(ch)
    if HANGUL.match(ch): return kf if o in cmap(kf) else None
    for f in (lf, kf):
        if o in cmap(f): return f
    return None
def covered(t, kf, lf): return all(font_for(c, kf, lf) for c in t)

def render_font_line(t, kf, lf, r):
    sp = r.uniform(0.8, 1.08); wob = r.uniform(0, 0.05); ph = r.uniform(0, 6.28)
    Hc = int(S * 2.6); base = int(S * 1.7)
    items = []; x = S * 0.3
    for i, ch in enumerate(t):
        f = font_for(ch, kf, lf)
        if f == 'sp': x += S * r.uniform(0.3, 0.6); continue
        sz = int(S * r.uniform(0.88, 1.12)) // 2 * 2
        fo = pil_font(f, sz)
        adv = fo.getlength(ch)
        dy = int(S * (wob * math.sin(ph + i * 0.7) + r.gauss(0, 0.025)))
        items.append((ch, fo, x, dy)); x += adv * sp * r.uniform(0.9, 1.05)
    Wc = int(x + S)
    canvas = np.zeros((Hc, Wc), np.uint8)
    for ch, fo, x0, dy in items:
        cw = int(S * 2); tmp = Image.new('L', (cw, Hc), 0)
        ImageDraw.Draw(tmp).text((S * 0.4, base + dy), ch, font=fo, fill=255, anchor='ls')
        a = np.array(tmp)
        if r.random() < .7:
            M = cv2.getRotationMatrix2D((cw / 2, base), r.gauss(0, 3.5), r.uniform(0.95, 1.05)); a = cv2.warpAffine(a, M, (cw, Hc))
        xi = int(x0 - S * 0.4); xs0, xs1 = max(0, xi), min(Wc, xi + cw)
        if xs1 > xs0: canvas[:, xs0:xs1] = np.maximum(canvas[:, xs0:xs1], a[:, xs0 - xi:xs1 - xi])
    # global slant / rotation / elastic
    k = r.uniform(-0.18, 0.35) if r.random() < .8 else r.uniform(-0.05, 0.05); ang = math.radians(r.gauss(0, 1.5))
    M = np.float32([[math.cos(ang), -math.sin(ang) - k, 0], [math.sin(ang), math.cos(ang), 0]]); M[0, 2] = k * base
    M[:, 2] += [S * 0.2, 0]
    Wn = Wc + int(S * 0.6)
    canvas = cv2.warpAffine(canvas, M, (Wn, Hc), flags=cv2.INTER_LINEAR)
    A = r.uniform(0, 2.8)
    if A > .5:
        rng = np.random.RandomState(r.randrange(1 << 30))
        fx = cv2.resize(rng.rand(Hc // 24 + 2, Wn // 24 + 2).astype(np.float32) * 2 - 1, (Wn, Hc), interpolation=cv2.INTER_CUBIC) * A
        fy = cv2.resize(rng.rand(Hc // 24 + 2, Wn // 24 + 2).astype(np.float32) * 2 - 1, (Wn, Hc), interpolation=cv2.INTER_CUBIC) * A
        gx, gy = np.meshgrid(np.arange(Wn, dtype=np.float32), np.arange(Hc, dtype=np.float32))
        canvas = cv2.remap(canvas, gx + fx, gy + fy, cv2.INTER_LINEAR)
    ys, xs = np.where(canvas > 100)
    if len(xs) < 20: return None
    sk = skeletonize(canvas[ys.min():ys.max() + 1, xs.min():xs.max() + 1] > 100)
    yy, xx = np.where(sk)
    return strokes_to_img(xx, yy, r, src_scale=S)

def pen_w(r): return r.choice([3, 4, 4, 4, 5])
def strokes_to_img(xs, ys, r, src_scale):
    E = r.uniform(50, 92); f = E / src_scale
    p = pen_w(r); pad = 14
    x = np.rint(xs * f).astype(int); y = np.rint(ys * f).astype(int)
    img = np.zeros((y.max() + 2 * pad + 1, x.max() + 2 * pad + 1), np.uint8)
    img[y + pad, x + pad] = 255
    img = cv2.dilate(img, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (p, p)))
    out = 255 - img
    if r.random() < .25:  # occasional stroke breaks
        iy, ix = np.where(img > 0)
        for _ in range(r.randint(1, 3)):
            j = r.randrange(len(ix)); cv2.circle(out, (int(ix[j]), int(iy[j])), r.choice([2, 2, 3]), 255, -1)
    return out

REAL = None
def init_real(val):
    global REAL
    df = pd.read_parquet(f'{D}/recordings.parquet', columns=['recording_id', 'sentence_id', 'text', 'strokes'])
    hold = (df.sentence_id % 20 == 7)   # ~5% of sentences held out (single writer: split by sentence)
    df = df[hold if val else ~hold]
    REAL = []
    for t, st in zip(df.text, df.strokes):
        if not t or not all(c in dic for c in t) or len(t) > 40: continue
        REAL.append((t, [np.stack([np.asarray(s['x']), np.asarray(s['y'])], 1) for s in st if len(s['x']) > 0]))
def init_iamon(val):
    """IAM On-Line Handwriting Database (English, ~200 writers, pen strokes). Non-commercial research use only;
    each user downloads it after registering, into data/iamondb/{lineStrokes,ascii}. Never redistributed."""
    global REAL
    import xml.etree.ElementTree as ET, zlib
    root = f'{D}/iamondb'; REAL = []
    for txt in sorted(glob.glob(f'{root}/ascii/*/*/*.txt')):
        form = os.path.splitext(os.path.basename(txt))[0]          # e.g. a01-000u
        if (zlib.crc32(form[:7].encode()) % 20 == 7) != val: continue   # ~5% of text pages held out
        lines = open(txt, encoding='latin-1').read().split('CSR:', 1)[-1].strip('\n').split('\n')
        lines = [l.strip() for l in lines if l.strip()]
        d = os.path.join(root, 'lineStrokes', os.path.relpath(os.path.dirname(txt), f'{root}/ascii'))
        for k, t in enumerate(lines, 1):
            p = f'{d}/{form}-{k:02d}.xml'
            if not os.path.exists(p) or len(t) > 40 or not all(c in dic for c in t): continue
            st = []
            for s in ET.parse(p).getroot().iter('Stroke'):
                pts = [(float(q.get('x')), float(q.get('y'))) for q in s.iter('Point')]
                if pts: st.append(np.asarray(pts))
            if st: REAL.append((t, st))
def render_real(item, r, aug=True):
    t, strokes = item
    allp = np.concatenate(strokes)
    mx = np.median(allp, 0)
    sh = r.uniform(-0.2, 0.3) if aug else 0; ang = math.radians(r.gauss(0, 2)) if aug else 0
    sx = r.uniform(0.85, 1.2) if aug else 1
    R = np.array([[math.cos(ang), -math.sin(ang)], [math.sin(ang), math.cos(ang)]])
    a_w, ph, lam = (r.uniform(0, 0.05), r.uniform(0, 6.28), r.uniform(3, 12)) if aug else (0, 0, 1)
    pts = []
    for s in strokes:
        q = s - mx; q = np.stack([q[:, 0] + sh * q[:, 1], q[:, 1]], 1) @ R.T
        q[:, 0] *= sx; q[:, 1] += a_w * np.sin(q[:, 0] / lam * 2 * math.pi + ph) * 2.5
        pts.append(q)
    a = np.concatenate(pts); lo, hi = np.percentile(a[:, 1], [0.5, 99.5])
    hh = max(hi - lo, 1e-3)
    f = (r.uniform(58, 105) if aug else 80) / hh
    p = pen_w(r) if aug else 4; pad = 14
    mn = a.min(0)
    img = np.full((int((a[:, 1].max() - mn[1]) * f) + 2 * pad + 2, int((a[:, 0].max() - mn[0]) * f) + 2 * pad + 2), 255, np.uint8)
    for q in pts:
        q = np.rint((q - mn) * f + pad).astype(np.int32)
        if len(q) > 1: cv2.polylines(img, [q.reshape(-1, 1, 2)], False, 0, p)
        for (x, y) in q[::max(1, len(q) // 40)]: cv2.circle(img, (int(x), int(y)), p // 2, 0, -1)
        cv2.circle(img, tuple(int(v) for v in q[0]), p // 2, 0, -1)
        cv2.circle(img, tuple(int(v) for v in q[-1]), p // 2, 0, -1)
    return img

def render_print(t, r):
    kf, lf = r.choice(PKF), r.choice(PLAT)
    if not covered(t, kf, lf): return None
    E = int(r.uniform(44, 80)); Wt = int(E * (len(t) + 3) * 1.3)
    im = Image.new('L', (Wt, int(E * 2)), 255); d = ImageDraw.Draw(im); x = E * 0.5
    for ch in t:
        f = font_for(ch, kf, lf)
        if f == 'sp': x += E * 0.3; continue
        fo = pil_font(f, E); d.text((x, E * 1.3), ch, font=fo, fill=0, anchor='ls'); x += fo.getlength(ch)
    return np.array(im)

def make(kind, i, seed, real_items=None):
    r = random.Random(seed * 10_000_019 + i)
    for _ in range(30):
        if kind == 'personal':
            it = real_items[i % len(real_items)]
            img = render_personal(it, r, aug=not VAL); t = it[0] if False else it[1]
        elif kind in ('real', 'iamon'):
            it = real_items[i % len(real_items)]
            img = render_real(it, r, aug=not VAL); t = it[0]
        else:
            t = text_of(kind, r)
            if not t: continue
            if kind == 'print': img = render_print(t, r)
            else:
                kf = r.choice(KF); lf = kf if (r.random() < .45 and latin_ok(cmap(kf))) else r.choice(HLAT)
                if not covered(t, kf, lf): continue
                img = render_font_line(t, kf, lf, r)
        if img is None: continue
        o = finalize(img)
        if o is not None and o.shape[1] >= 8: return o, t
    return None

PERS = None
def init_personal(val):
    global PERS
    root = f'{D}/personal'; PERS = []
    # eval.txt (written by `hwr upload`) lists the held-out lines; without it, the first 12 lines (old behaviour)
    ev = f'{root}/eval.txt'
    held = set(open(ev, encoding='utf-8').read().split()) if os.path.exists(ev) else None
    for k, l in enumerate(open(f'{root}/dataset.tsv', encoding='utf-8').read().splitlines()):
        p, t = l.split('	')
        if len(t) > 40: continue                       # longer than max_text_length: PaddleOCR would drop it
        is_eval = (p in held) if held is not None else (k < 12)
        if is_eval == val: PERS.append((cv2.imread(f'{root}/{p}', 0), t))
def render_personal(item, r, aug=True):
    img, t = item
    if not aug: return img
    h, w = img.shape; pad = 20
    big = cv2.copyMakeBorder(img, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=255)
    k = r.uniform(-0.15, 0.15); ang = math.radians(r.gauss(0, 1.5)); sx = r.uniform(0.9, 1.12); sy = r.uniform(0.92, 1.08)
    M = np.float32([[sx * math.cos(ang), -math.sin(ang) + k, 0], [math.sin(ang), sy * math.cos(ang), 0]])
    M[:, 2] = [-k * h / 2, 0]
    big = cv2.warpAffine(big, M, (int((w + 2 * pad) * sx + 40), int((h + 2 * pad) * sy + 20)), borderValue=255)
    q = r.random()
    if q < .25: big = 255 - cv2.dilate(255 - big, np.ones((2, 2), np.uint8))
    elif q < .45: big = 255 - cv2.erode(255 - big, np.ones((2, 2), np.uint8))
    return big

VAL = False
def work(a):
    kind, i, seed, out = a
    res = make(kind, i, seed, REAL)
    if res is None: return None
    o, t = res
    rel = f'{kind}{"_val" if VAL else ""}/{i // 1000:03d}/{seed}_{i}.png'
    os.makedirs(f'{out}/{os.path.dirname(rel)}', exist_ok=True)
    cv2.imwrite(f'{out}/{rel}', o)
    return f'{rel}\t{t}'

if __name__ == '__main__':
    import multiprocessing as mp
    kind, n, seed, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    VAL = '--val' in sys.argv
    init_fonts(); init_text()
    if kind == 'real': init_real(VAL)
    if kind == 'iamon': init_iamon(VAL)
    if kind == 'personal': init_personal(VAL); REAL = PERS
    print(kind, 'KF', len(KF), 'HLAT', len(HLAT), 'PKF', len(PKF), 'PLAT', len(PLAT), 'real', len(REAL or []), flush=True)
    nproc = int(os.environ.get('SLURM_CPUS_PER_TASK', 8))
    with mp.Pool(nproc) as pool:
        res = [x for x in pool.imap(work, [(kind, i, seed, out) for i in range(n)], chunksize=200) if x]
    os.makedirs(out, exist_ok=True)
    open(f'{out}/{kind}{"_val" if VAL else ""}_{seed}.txt', 'w', encoding='utf-8').write('\n'.join(res) + '\n')
    print('done', len(res))
