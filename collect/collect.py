"""Turn handwriting on a reMarkable Paper Pro into line-image/text training pairs.

Commands: pull | practice | make-doc | render   (see README.md). Never writes to the tablet.
"""
import argparse, hashlib, io, json, logging, os, random, re, shlex, shutil, subprocess, sys, time, urllib.request, uuid
from pathlib import Path

import numpy as np
import rmscene
import rmscene.text
from PIL import Image

import random

sys.path.insert(0, str(Path(__file__).resolve().parent))   # hwr runs this with python -I (no script dir on the path)
import pages as pages_v2

KO = """오늘은 날씨가 정말 좋네요
내일 아침에 같이 운동할래요
회의 자료는 이메일로 보내 주세요
점심은 김치찌개로 먹을까요
이번 주말에는 가족과 여행을 가요
지하철역에서 십 분쯤 걸어야 해요
도서관에서 빌린 책을 반납해야 합니다
고양이가 창가에서 낮잠을 자고 있어요
비가 올 것 같으니 우산을 챙기세요
새로 산 노트북이 아주 빠릅니다
아브라카타브라
가는 말이 고우면 오는 말도 곱다
천 리 길도 한 걸음부터
작은 습관이 큰 변화를 만든다
바람이 불어서 나뭇잎이 떨어집니다
저는 한국어와 영어를 공부하고 있어요
커피 한 잔 하시겠어요
오늘 저녁 메뉴는 된장찌개입니다
다음 주 월요일까지 보고서를 제출하세요
창문을 열고 환기를 시켜 주세요
이 길로 쭉 가시면 은행이 나옵니다
생일 축하합니다 건강하세요
밤하늘에 별이 가득 떠 있었어요
아이들이 공원에서 신나게 뛰어놀아요
주문하신 음식이 곧 나옵니다
잠깐만 기다려 주시겠어요
회의실 예약을 취소했습니다
오랜만에 친구를 만나서 반가웠어요
이 문제는 생각보다 어렵지 않아요
따뜻한 차 한 잔이 생각나는 날이에요
버스가 늦게 와서 지각했습니다
주말에 영화를 보러 갈까요
책상 위에 있는 열쇠를 가져다 주세요
한글은 세종대왕이 만들었습니다
겨울에는 눈이 많이 내립니다
꽃이 피는 봄이 기다려져요
죄송하지만 다시 한번 말씀해 주세요
그는 매일 아침 일찍 일어납니다
우리 집 강아지는 산책을 좋아해요
감사합니다 좋은 하루 되세요
이번 프로젝트는 팀워크가 중요합니다
수업이 끝나면 도서관에서 만나요
냉장고에 우유가 하나도 없어요
읽기 쉬운 글씨로 또박또박 쓰세요
쌀국수와 볶음밥을 주문했어요
얼른 나아서 건강해지길 바랍니다
의사 선생님께 진료를 예약했어요
공책에 오늘 할 일을 적어 두었습니다
빨간 사과와 노란 바나나를 샀어요
읽다 만 소설을 마저 읽고 싶어요""".split("\n")

EN = """The quick brown fox jumps over the lazy dog
Please send me the report by Friday
Meeting at Zoom starts in ten minutes
I would like a cup of coffee with milk
Handwriting recognition works well today
See you at the station tomorrow morning
Can you help me with this problem
It is raining so take an umbrella
Thank you very much for your help
Let us review the schedule together
Oh yeah
works!
The weather is lovely this afternoon
Remember to save your work often
She sells seashells by the seashore
Where is the nearest pharmacy
Learning a new language takes patience
Turn left at the second traffic light
My favorite color is dark green
Dinner will be ready in half an hour
Please keep your voice down in the library
Practice makes perfect
Every problem has a simple solution
He forgot his keys on the kitchen table
We need to book a table for four
Quiet please the baby is sleeping
Do not forget to lock the door
Open the window and let in some air
A journey of a thousand miles begins with one step
Better late than never
Mix the flour and sugar then add two eggs
Check the battery level before you leave
The train to Seoul departs from platform nine
Write clearly and keep the letters apart
Jazz and blues are my favorite music
Virtual keyboards are slower than pens
Next week we will test the new release
Please review the pull request today
Zebras quickly vex jumpy wolves
Good morning everyone welcome aboard""".split("\n")

# confusable characters, written alone and in context
CONF = """ㅇ o 0 O 영 이응
ㅣ l 1 I | 일 이
ㄱ 7 ㄴ L 기역 니은
ㅁ 口 ㅂ ㅍ 미음
ㅗ ㅜ ㅏ ㅓ ㅑ ㅕ
오 영 공 0 O o
일 이 삼 1 2 3
사 오 육 4 5 6
칠 팔 구 7 8 9
l1I 0Oo ㅣ1l
ㅇ0o ㅣ1l ㄱ7 ㅁ口
ㅅ ㅈ ㅊ ㅆ ㅉ
ㅡ ㅜ - _ ㅡㅡ
ㅏㅣ ㅐ ㅔ ㅒ ㅖ
a o e c d q g 9
u v w y n m h r
2 Z z 5 S s 8 B
6 b G 4 A 7 1 l
이일 ㅣ1 ㅇㅣ 0l
영이 O1 ㅇㅣ o1
10 100 1000 0.01 O.1
Il1 Oo0 Ss5 Zz2
ㄷ ㅌ ㄸ ㄹ ㅎ ㅋ
ㅓ ㅕ ㅡ ㅜ ㅠ ㅛ""".split("\n")

ENG_WORDS = ["meeting", "deadline", "email", "report", "Zoom", "schedule", "project", "coffee",
             "update", "team", "budget", "feedback", "release", "login", "password", "wifi"]
KO_NOUN = ["회의", "점심", "발표", "상담", "수업", "운동", "배송", "진료", "면접", "약속"]
UNITS = ["5kg", "350ml", "2.5km", "36.5도", "100%", "12cm", "3.5L", "80km/h", "1.5GB", "20분", "7명", "9개"]


def prompts_pool():
    r = random.Random(20240601)
    out = list(KO) + list(EN) + list(CONF)
    for _ in range(34):
        h, m = r.randint(1, 12), r.choice([0, 5, 10, 15, 20, 30, 40, 45, 50, 55])
        out.append(r.choice([f"{h}시 {m}분", f"오후 {h}시 {m}분에 만나요", f"{r.choice(KO_NOUN)}은 {h}시 {m}분입니다",
                             f"{h}:{m:02d} PM meeting", f"회의는 {h}시 {m}분 {r.choice(ENG_WORDS)}"]))
    for _ in range(34):
        y, mo, d = r.randint(2024, 2028), r.randint(1, 12), r.randint(1, 28)
        out.append(r.choice([f"{y}년 {mo}월 {d}일", f"{y}-{mo:02d}-{d:02d}", f"{mo}/{d} 약속 있음",
                             f"{mo}월 {d}일 {r.choice(KO_NOUN)}", f"Due {mo}/{d}/{y}"]))
    for _ in range(34):
        p = r.randint(1, 99) * 100 + r.choice([0, 50, 90])
        out.append(r.choice([f"{p:,}원", f"커피 {r.randint(2, 5)}잔 {p:,}원", f"${p / 100:.2f}",
                             f"합계 {p * 3:,}원", f"Total: ${p / 10:.2f}"]))
    for _ in range(34):
        a, b = r.randint(1000, 9999), r.randint(1000, 9999)
        out.append(r.choice([f"010-{a}-{b}", f"02-{a % 900 + 100}-{b}", f"전화 010-{a}-{b}",
                             f"Call 010-{a}-{b}", f"주문번호 {a}{b}", f"ext. {a % 9000 + 1000}"]))
    for _ in range(34):
        out.append(r.choice(["무게는 ", "거리는 ", "용량은 ", "속도는 ", "크기 "]) + r.choice(UNITS)
                   + r.choice(["", " 입니다", " 정도예요"]))
    for _ in range(44):
        w, n = r.choice(ENG_WORDS), r.choice(KO_NOUN)
        out.append(r.choice([f"오늘 {w} {n}이 있어요", f"{w} 파일을 보내 주세요", f"내일까지 {w} 확인 부탁해요",
                             f"{n} 전에 {w} 끝내기", f"hot {r.randint(100, 999)}일의 여정이야",
                             f"Oh yeah {n} {r.randint(1, 30)}번"]))
    seen, uniq = set(), []
    for s in out:
        if s not in seen:
            seen.add(s); uniq.append(s)
    return uniq


# ---------------------------------------------------------------- renderer (mirror of src/page.rs)
PEN, PAD = 4.0, 12.0
F = np.float32


def read_strokes(data):
    """Pen strokes as float32 arrays (n,2); erasers and deleted strokes skipped (like src/rm.rs)."""
    logging.getLogger("rmscene").setLevel(logging.ERROR)
    out = []
    for b in rmscene.read_blocks(io.BytesIO(data)):
        if isinstance(b, rmscene.SceneLineItemBlock) and b.item.value is not None:
            ln = b.item.value
            if int(ln.tool.value) in (6, 8, 18) or not ln.points:
                continue
            out.append(np.array([(p.x, p.y) for p in ln.points], dtype=F))
    return out


def read_strokes_full(data):
    """Pen strokes with every point attribute: [{"tool": int, "points": (n, 5) x, y, pressure, width, speed}]."""
    logging.getLogger("rmscene").setLevel(logging.ERROR)
    out = []
    for b in rmscene.read_blocks(io.BytesIO(data)):
        if isinstance(b, rmscene.SceneLineItemBlock) and b.item.value is not None:
            ln = b.item.value
            if int(ln.tool.value) in (6, 8, 18) or not ln.points:
                continue
            out.append({"tool": int(ln.tool.value),
                        "points": np.array([(p.x, p.y, p.pressure, p.width, p.speed) for p in ln.points], dtype=F)})
    return out


def bbox(s):
    return s[:, 0].min(), s[:, 1].min(), s[:, 0].max(), s[:, 1].max()


def split_lines(strokes):
    """Group strokes into text lines, top to bottom -> list of [stroke indices, y0, y1]."""
    boxes = [bbox(s) for s in strokes]
    heights = sorted(b[3] - b[1] for b in boxes if b[3] - b[1] > 2.0)
    if not heights:
        return []
    median = max(heights[len(heights) // 2], F(10.0))
    tol = median * F(0.3)
    order = sorted(range(len(strokes)), key=lambda i: (boxes[i][1] + boxes[i][3]) / F(2))
    small = lambda i: (boxes[i][3] - boxes[i][1]) < median * F(0.35) and (boxes[i][2] - boxes[i][0]) < median * F(0.6)
    lines = []
    for i in (i for i in order if not small(i)):
        y0, y1 = boxes[i][1], boxes[i][3]
        cy = (y0 + y1) / F(2)
        for l in lines:
            if l[1] - tol <= cy <= l[2] + tol:
                l[1], l[2] = min(l[1], y0), max(l[2], y1)
                l[0].append(i)
                break
        else:
            lines.append([[i], y0, y1])
    for i in (i for i in order if small(i)):
        cy = (boxes[i][1] + boxes[i][3]) / F(2)
        dist = lambda l: l[1] - cy if cy < l[1] else (cy - l[2] if cy > l[2] else F(0))
        best = min(lines, key=dist) if lines else None
        if best is not None and dist(best) < median:
            best[0].append(i)
        else:
            lines.append([[i], boxes[i][1], boxes[i][3]])
    lines.sort(key=lambda l: l[1])
    return lines


def stamps(s):
    """Disc-stamp centres along one stroke: every 0.5 px, like render() in page.rs."""
    if len(s) == 1:
        return s.copy()
    a, b = s[:-1], s[1:]
    steps = np.maximum(np.ceil(np.hypot(b[:, 0] - a[:, 0], b[:, 1] - a[:, 1]) / F(0.5)), 1).astype(np.int64)
    n = steps + 1
    seg = np.repeat(np.arange(len(a)), n)
    k = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
    t = (k / steps[seg]).astype(F)
    return a[seg] + (b[seg] - a[seg]) * t[:, None]


def render(strokes, idx, pen=PEN):
    """Black ink on white, 8-bit grayscale PIL image; same size/padding/stamps as the Rust renderer."""
    idx = list(idx)
    bs = np.array([bbox(strokes[i]) for i in idx], dtype=F)
    b = (bs[:, 0].min(), bs[:, 1].min(), bs[:, 2].max(), bs[:, 3].max())
    ox, oy = b[0] - F(PAD), b[1] - F(PAD)
    w, h = int(b[2] - b[0] + F(2 * PAD)) + 1, int(b[3] - b[1] + F(2 * PAD)) + 1
    img = np.full((h, w), 255, np.uint8)
    pts = np.concatenate([stamps(strokes[i]) for i in idx]) - np.array([ox, oy], dtype=F)
    r = F(pen / 2)
    R = int(np.ceil(r))
    for c in range(0, len(pts), 40000):
        p = pts[c:c + 40000]
        fx, fy = np.floor(p[:, 0]).astype(np.int64), np.floor(p[:, 1]).astype(np.int64)
        for dy in range(-R, R + 2):
            for dx in range(-R, R + 2):
                xx, yy = fx + dx, fy + dy
                d2 = (xx.astype(F) - p[:, 0]) ** 2 + (yy.astype(F) - p[:, 1]) ** 2
                ok = (d2 <= r * r) & (xx >= 0) & (yy >= 0) & (xx < w) & (yy < h)
                img[yy[ok], xx[ok]] = 0
    return Image.fromarray(img, "L")


def save_lines(strokes, lines, folder):
    folder.mkdir(parents=True, exist_ok=True)
    names = []
    for n, l in enumerate(lines):
        render(strokes, l[0]).save(folder / f"{n:02d}.png")
        names.append(folder / f"{n:02d}.png")
    return names


# ---------------------------------------------------------------- reading typed text
def page_text(data):
    """Text typed on a page (RootTextBlock), one entry per paragraph."""
    logging.getLogger("rmscene").setLevel(logging.ERROR)
    paras = []
    for b in rmscene.read_blocks(io.BytesIO(data)):
        if isinstance(b, rmscene.RootTextBlock):
            doc = rmscene.text.TextDocument.from_scene_item(b.value)
            paras += ["".join(str(span) for span in p.contents) for p in doc.contents]
    return paras


def clean(lines):
    return [l.strip() for l in lines if l.strip()]


# ---------------------------------------------------------------- tablet access (read-only: ssh cat/grep)
XO = "/home/root/.local/share/remarkable/xochitl"
JSONL = "/home/root/.local/share/rmpp-hwr/conversions.jsonl"


class Tablet:
    def __init__(self, a):
        key = os.path.expanduser(a.key)
        self.cmd = ["ssh", "-i", key, "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", f"{a.user}@{a.host}"]

    def run(self, remote):
        p = subprocess.run(self.cmd + [remote], capture_output=True)
        return p.stdout if p.returncode == 0 else None

    def cat(self, path):
        return self.run("cat " + shlex.quote(path))

    def cat_opt(self, path):
        """b"" when the file does not exist, None when ssh failed."""
        return self.run("cat " + shlex.quote(path) + " 2>/dev/null; true")


# ---------------------------------------------------------------- pull
def sample_name(c):
    return re.sub(r"[^0-9A-Za-z]", "", c["time"]) + "_" + c["src"]


META_V = 2   # bump to re-process pulled conversions after a pairing change


def lev(a, b):
    d = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        p, d[0] = d[0], i
        for j, cb in enumerate(b, 1):
            p, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, p + (ca != cb))
    return d[-1]


def align(rec, cor, gap=0.6, worst=0.8):
    """Monotonic alignment of recognized lines to corrected lines (DP over normalized edit distance).
    Returns (rec index, cor index) pairs; a line skipped on either side is left unpaired."""
    cost = lambda a, b: lev(a, b) / max(len(a), len(b), 1)
    n, m = len(rec), len(cor)
    D = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1): D[i][0] = i * gap
    for j in range(1, m + 1): D[0][j] = j * gap
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            D[i][j] = min(D[i - 1][j - 1] + cost(rec[i - 1], cor[j - 1]), D[i - 1][j] + gap, D[i][j - 1] + gap)
    pairs, i, j = [], n, m
    while i and j:
        c = cost(rec[i - 1], cor[j - 1])
        if abs(D[i][j] - (D[i - 1][j - 1] + c)) < 1e-9:
            if c <= worst: pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif abs(D[i][j] - (D[i - 1][j] + gap)) < 1e-9:
            i -= 1
        else:
            j -= 1
    return pairs[::-1]


def pull_conversion(tab, c, out):
    d = out / "samples" / sample_name(c)
    dst = tab.cat_opt(f"{XO}/{c['doc']}/{c['dst']}.rm")
    if dst is None:
        return "ssh failed, kept the previous result"
    dst = dst or None
    if (d / "meta.json").exists() and dst is not None and (d / "corrected.txt").exists() and \
            (d / "corrected.txt").read_text(encoding="utf-8") == "\n".join(clean(page_text(dst))) + "\n" and \
            json.loads((d / "meta.json").read_text()).get("confirmed") == bool(c.get("corrected")) and \
            json.loads((d / "meta.json").read_text()).get("v") == META_V:
        m = json.loads((d / "meta.json").read_text())
        return "unchanged (corrected)" if m["pairs"] else ("needs review" if m["confirmed"] else "waiting for your correction")
    # the copy of the recognized ink (newer records) wins over the page, which may have been edited or deleted since
    src = tab.cat(f"{JSONL.rsplit('/', 1)[0]}/{c['ink']}") if c.get("ink") else None
    src = src or tab.cat(f"{XO}/{c['doc']}/{c['src']}.rm")
    if src is None:
        return "src page missing on tablet"
    shutil.rmtree(d / "lines", ignore_errors=True)
    for f in ("pairs.tsv", "needs_review.txt", "waiting.txt"):
        (d / f).unlink(missing_ok=True)
    d.mkdir(parents=True, exist_ok=True)
    (d / "original.rm").write_bytes(src)
    strokes = read_strokes(src)
    converted = clean(c.get("lines", []))
    (d / "converted.txt").write_text("\n".join(converted) + "\n", encoding="utf-8")
    meta = {"time": c["time"], "doc": c["doc"], "src": c["src"], "dst": c["dst"], "pairs": False, "edited": False,
            "confirmed": bool(c.get("corrected")), "v": META_V}
    if not strokes:
        (d / "needs_review.txt").write_text("no strokes on the handwritten page\n")
    else:
        render(strokes, range(len(strokes))).save(d / "original.png")
        lines = split_lines(strokes)
        imgs = save_lines(strokes, lines, d / "lines")
        if dst is None:
            (d / "needs_review.txt").write_text("typed page (dst) not found on the tablet\n")
        else:
            corrected = clean(page_text(dst))
            (d / "corrected.txt").write_text("\n".join(corrected) + "\n", encoding="utf-8")
            meta["edited"] = corrected != converted
            if not meta["confirmed"]:
                # Only pages marked "Done correcting" on the tablet become training data.
                (d / "waiting.txt").write_text("text not corrected yet: fix it on the tablet and pull again\n", encoding="utf-8")
            elif len(corrected) == len(lines) or len(converted) == len(lines):
                # Same count: pair in order. Otherwise (a stray line deleted, two lines merged...) align the
                # recognized lines, one per handwriting line, with the corrected lines by edit distance.
                pairs = list(zip(range(len(lines)), range(len(lines)))) if len(corrected) == len(lines) \
                    else align(converted, corrected)
                rows = [f"{imgs[i].relative_to(out).as_posix()}\t{corrected[j]}" for i, j in pairs]
                (d / "pairs.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
                meta["pairs"] = True
                meta["unpaired_lines"] = sorted(set(range(len(lines))) - {i for i, _ in pairs})
            else:
                (d / "needs_review.txt").write_text(
                    f"handwriting split into {len(lines)} lines but typed page has {len(corrected)} lines.\n"
                    "Check lines/NN.png against corrected.txt, then write pairs.tsv by hand "
                    "(line image path TAB text) and re-run `pull` (dataset.tsv is rebuilt from pairs.tsv files).\n",
                    encoding="utf-8")
    (d / "meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    return "corrected" if meta["pairs"] else ("waiting for your correction" if not meta["confirmed"] else "needs review")


def load_sidecars(out, extra):
    pd = out / "practice"
    pd.mkdir(exist_ok=True)
    for f in extra or []:
        shutil.copy(f, pd / Path(f).name)
    return [json.loads(f.read_text(encoding="utf-8")) for f in sorted(pd.glob("*.json"))]


def pull_practice(tab, out, sidecars, y_off):
    names = tab.run(f"grep -l rmpp-practice {XO}/*.metadata 2>/dev/null") or b""
    for path in names.decode().split():
        doc = path.rsplit("/", 1)[1].removesuffix(".metadata")
        meta = json.loads(tab.cat(path) or b"{}")
        if not meta.get("visibleName", "").startswith("rmpp-practice") or meta.get("deleted"):
            continue
        content = json.loads(tab.cat(f"{XO}/{doc}.content") or b"{}")
        pages = [p["id"] for p in content.get("cPages", {}).get("pages", []) if "deleted" not in p] \
            or content.get("pages", [])
        side = next((s for s in sidecars if s.get("doc_id") == doc), None) or \
            (sidecars[0] if len(sidecars) == 1 else None)
        if side is None:
            print(f"practice {doc}: no sidecar json (pass --sidecar FILE.json written by make-doc)")
            continue
        d = out / "samples" / f"practice_{doc}"
        rms = [tab.cat_opt(f"{XO}/{doc}/{pid}.rm") for pid in pages]
        if any(r is None for r in rms):   # a dropped connection must not overwrite earlier results
            print(f"practice {doc}: ssh failed while reading pages, kept the previous result")
            continue
        digest = hashlib.sha1(b"".join(r or b"" for r in rms)).hexdigest()
        if (d / "meta.json").exists() and json.loads((d / "meta.json").read_text())["sha1"] == digest:
            print(f"practice {doc}: unchanged")
            continue
        d.mkdir(parents=True, exist_ok=True)
        if side.get("format") == pages_v2.FORMAT:
            pull_practice2(d, doc, side, rms, digest)
            continue
        rows, lost = [], 0
        for pi, rm in enumerate(rms):
            if not rm:
                continue
            strokes = read_strokes(rm)
            cy = [(bbox(s)[1] + bbox(s)[3]) / 2 + y_off for s in strokes]
            used = set()
            for pr in (p for p in side["prompts"] if p["page"] == pi):
                bd = pr["band"]
                idx = [i for i, y in enumerate(cy) if bd["y0"] - BAND_UP <= y <= bd["y1"] + BAND_DOWN]
                used.update(idx)
                if idx:
                    f = d / f"{pr['id']:03d}.png"
                    render(strokes, idx).save(f)
                    rows.append(f"{f.relative_to(out).as_posix()}\t{pr['text']}")
            lost += len(strokes) - len(used)
        (d / "pairs.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
        (d / "meta.json").write_text(json.dumps({"kind": "practice", "doc": doc, "sha1": digest,
                                                 "bands": len(rows), "unassigned_strokes": lost}), encoding="utf-8")
        print(f"practice {doc}: {len(rows)} bands, {lost} strokes outside any band")


def pull_practice2(d, doc, side, rms, digest):
    """Practice pages v2: keep the raw strokes, assign them to rows/cells/lines, render line and cell images.
    Writes strokes.json (raw pen data + assignment), pairs.tsv (line images), chars.tsv (cell images), qc.json."""
    page_strokes = {pi: read_strokes_full(rm) for pi, rm in enumerate(rms) if rm}
    items, qc, stray = pages_v2.extract(side, page_strokes)
    bad = {q["id"]: q["problems"] for q in qc}
    tmpl = {k: v for k, v in side.items() if k not in ("doc_id", "name")}
    (d / "strokes.json").write_text(json.dumps({
        "format": "rmpp-hwr/strokes@1", "template": tmpl,
        "pages": {str(p): [{"tool": s["tool"], "points": np.round(s["points"], 2).tolist()} for s in ss]
                  for p, ss in page_strokes.items()},
        "assign": items}, ensure_ascii=False), encoding="utf-8")
    rows, chars = [], []
    for it, r in zip(side["items"], items):
        ss = page_strokes.get(it["page"], [])
        if not r["strokes"]:
            continue
        xy = [s["points"][:, :2] for s in ss]
        probs = bad.get(it["id"], [])
        line_ok = not any("empty" in p or "blank" in p or "after the last" in p for p in probs)
        if line_ok:
            f = d / f"{it['id']:03d}.png"
            render(xy, r["strokes"]).save(f)
            rows.append(f"{f.relative_to(d.parent.parent).as_posix()}\t{it['text'].strip()}")
        if it["kind"] == "row":
            for ci, (cl, cs) in enumerate(zip(it["cells"], r["cells"])):
                if cl["char"] == " " or not cs or any(p.startswith(f"cell {ci} ") for p in probs):
                    continue
                f = d / "cells" / f"{it['id']:03d}_{ci:02d}.png"
                f.parent.mkdir(exist_ok=True)
                render(xy, cs).save(f)
                chars.append(f"{f.relative_to(d.parent.parent).as_posix()}\t{cl['char']}")
    (d / "pairs.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    (d / "chars.tsv").write_text("\n".join(chars) + "\n", encoding="utf-8")
    (d / "qc.json").write_text(json.dumps({"stray_strokes": stray, "items": qc}, ensure_ascii=False, indent=1), encoding="utf-8")
    (d / "meta.json").write_text(json.dumps({"kind": "practice2", "doc": doc, "sha1": digest, "lines": len(rows),
                                             "chars": len(chars), "qc_items": len(qc), "stray_strokes": stray}), encoding="utf-8")
    print(f"practice {doc}: v2, {len(rows)} lines, {len(chars)} characters, {len(qc)} items to check, {stray} stray strokes")


def cmd_pull(a):
    out = Path(a.out)
    (out / "samples").mkdir(parents=True, exist_ok=True)
    tab = Tablet(a)
    raw = tab.run(f"cat {JSONL} 2>/dev/null; true")   # b"" = no records, None = ssh failed
    reachable = raw is not None
    if not reachable:
        print("ssh failed; skipping conversions")
        raw = b""
    live, counts, moved = set(), {}, []
    for line in raw.decode("utf-8", "replace").splitlines():
        try:
            c = json.loads(line)
            c["time"], c["doc"], c["src"], c["dst"]
        except (ValueError, KeyError, TypeError):
            continue
        live.add(sample_name(c))
        r = pull_conversion(tab, c, out)
        counts[r] = counts.get(r, 0) + 1
        print(sample_name(c), r)
        if r in ("corrected", "unchanged (corrected)"):
            moved.append(c)
    # Corrected conversions now on the PC are removed from the tablet (record + ink copy), unless --keep.
    if moved and not a.keep:
        jl = shlex.quote(JSONL)
        pats = " ".join("-e " + shlex.quote('"time":"%s"' % c["time"]) for c in moved)
        inks = " ".join(shlex.quote(JSONL.rsplit("/", 1)[0] + "/" + c["ink"]) for c in moved if c.get("ink"))
        ok = tab.run(f"grep -vF {pats} {jl} > {jl}.new; mv {jl}.new {jl}" + (f"; rm -f {inks}" if inks else "")
                     + "; echo ok")
        if ok:
            for c in moved:
                m = out / "samples" / sample_name(c) / "meta.json"
                meta = json.loads(m.read_text())
                meta["moved"] = True
                m.write_text(json.dumps(meta, indent=1), encoding="utf-8")
            print(f"{len(moved)} corrected conversions moved to the PC and removed from the tablet")
    # conversions deleted on the tablet (Settings > Handwriting) are removed here too; moved ones stay
    if reachable:
        for d in (out / "samples").iterdir():
            if d.is_dir() and not d.name.startswith("practice_") and d.name not in live and \
                    not json.loads((d / "meta.json").read_text()).get("moved"):
                shutil.rmtree(d)
                print(d.name, "removed (deleted on the tablet)")
    if counts:
        print("conversions:", ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))
    pull_practice(tab, out, load_sidecars(out, a.sidecar), a.y_offset)
    rows = []
    for p in sorted((out / "samples").glob("*/pairs.tsv")):
        rows += [l for l in p.read_text(encoding="utf-8").splitlines() if l.strip()]
    (out / "dataset.tsv").write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")
    print(f"dataset.tsv: {len(rows)} labelled line images")
    chars = []
    for p in sorted((out / "samples").glob("*/chars.tsv")):
        chars += [l for l in p.read_text(encoding="utf-8").splitlines() if l.strip()]
    (out / "chars.tsv").write_text("\n".join(chars) + ("\n" if chars else ""), encoding="utf-8")
    if chars:
        print(f"chars.tsv: {len(chars)} labelled character images")


# ---------------------------------------------------------------- practice PDF
PX_W, PX_H, DPI = 1620, 2160, 229
PT = 72 / DPI
TOP, PITCH, BAND_OFF, BAND_H = 120, 300, 80, 190  # px; item = prompt line + empty writing band
MARGIN = 70
BAND_UP, BAND_DOWN = 60, 30  # px slack when assigning strokes to a band
PER_PAGE = (PX_H - TOP - 100) // PITCH
FONT_URL = "https://github.com/google/fonts/raw/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf"


def font_file():
    here = Path(__file__).parent / "fonts"
    dst = here / "NotoSansKR-Regular.ttf"
    if dst.exists():
        return dst
    here.mkdir(exist_ok=True)
    var = here / "NotoSansKR-VF.ttf"
    sys_vf = Path("C:/Windows/Fonts/NotoSansKR-VF.ttf")
    if sys_vf.exists():
        var = sys_vf
    elif not var.exists():
        print("downloading Noto Sans KR (OFL) ...")
        urllib.request.urlretrieve(FONT_URL, var)
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
    instancer.instantiateVariableFont(TTFont(var), {"wght": 400}).save(dst)
    return dst


TEXTS = Path(__file__).resolve().parent / "texts"
MAX_LINE = 26   # characters per handwriting band


def text_files(names):
    """--text values: a built-in name (story, talk, notes) or a path; none = every built-in text."""
    if not names:
        return sorted(TEXTS.glob("*.txt"))
    files = []
    for n in names:
        p = Path(n)
        p = p if p.exists() else TEXTS / (n if n.endswith(".txt") else n + ".txt")
        if not p.exists():
            sys.exit(f"text not found: {n} (built-in: {', '.join(f.stem for f in sorted(TEXTS.glob('*.txt')))})")
        files.append(p)
    return files


def wrap(line):
    """Split a line longer than one band at spaces."""
    out, cur = [], ""
    for w in line.split():
        if cur and len(cur) + 1 + len(w) > MAX_LINE:
            out.append(cur); cur = w
        else:
            cur = f"{cur} {w}" if cur else w
    return out + ([cur] if cur else [])


def passages(path):
    """Blank-line separated passages; one line = one band. A file without blank lines is one line per passage."""
    raw = path.read_text(encoding="utf-8").replace("\r", "")
    blocks = [b for b in re.split(r"\n\s*\n", raw) if b.strip()]
    if len(blocks) == 1:
        blocks = blocks[0].split("\n")
    return [[w for l in b.split("\n") if l.strip() for w in wrap(l.strip())] for b in blocks]


def pick_from_texts(files, count, avoid, seed):
    """Whole passages in order, alternating between files, unused ones first (lines in --avoid sidecars count as used)."""
    used = set()
    for f in avoid:
        try:
            used.update(p["text"] for p in json.loads(Path(f).read_text(encoding="utf-8"))["prompts"])
        except (OSError, ValueError, KeyError):
            pass
    r = random.Random(seed)
    queues = []
    for f in files:
        ps = passages(f)
        fresh = [p for p in ps if not used.intersection(p)]
        old = [p for p in ps if used.intersection(p)]
        r.shuffle(fresh); r.shuffle(old)
        queues.append(fresh + old)
    chosen = []
    while len(chosen) < count and any(queues):
        for q in queues:
            if q and len(chosen) < count:
                chosen += q.pop(0)[:count - len(chosen)]
    if len(chosen) < count:
        print(f"only {len(chosen)} lines available in {', '.join(f.name for f in files)}")
    return chosen


def cmd_texts(a):
    for f in text_files(a.text):
        ps = passages(f)
        print(f"{f.stem:12} {len(ps):4} passages {sum(map(len, ps)):5} lines   {f}")


def cmd_practice(a):
    from reportlab.pdfgen import canvas
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    pdfmetrics.registerFont(TTFont("NotoKR", str(font_file())))
    if a.text == ["builtin"]:
        pool = prompts_pool()
        if a.count > len(pool):
            sys.exit(f"only {len(pool)} built-in prompts")
        chosen = random.Random(a.seed).sample(pool, a.count)
    else:
        chosen = pick_from_texts(text_files(a.text), a.count, a.avoid or [], a.seed)
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(out), pagesize=(PX_W * PT, PX_H * PT))
    c.setTitle("rmpp-practice")
    items, npages = [], (len(chosen) + PER_PAGE - 1) // PER_PAGE
    Y = lambda y: (PX_H - y) * PT  # px from top -> PDF pt from bottom
    for n, text in enumerate(chosen):
        page, slot = divmod(n, PER_PAGE)
        y0 = TOP + slot * PITCH
        if slot == 0:
            c.setFillGray(0.6)
            c.setFont("NotoKR", 30 * PT)
            c.drawString(MARGIN * PT, Y(70), f"보고 따라 쓰기  {page + 1}/{npages}")
        size = 40
        while pdfmetrics.stringWidth(text, "NotoKR", size * PT) > (PX_W - 2 * MARGIN - 80) * PT and size > 24:
            size -= 2
        c.setFillGray(0.0)
        c.setFont("NotoKR", size * PT)
        c.drawString((MARGIN + 80) * PT, Y(y0 + 45), text)
        c.setFillGray(0.55)
        c.setFont("NotoKR", 28 * PT)
        c.drawString(MARGIN * PT, Y(y0 + 45), f"{n + 1}")
        top, bot = y0 + BAND_OFF, y0 + BAND_OFF + BAND_H
        c.setStrokeGray(0.8)
        c.setLineWidth(1)
        c.rect(MARGIN * PT, Y(bot), (PX_W - 2 * MARGIN) * PT, BAND_H * PT)
        c.setDash(4, 4)
        c.line(MARGIN * PT, Y(top + BAND_H * 0.72), (PX_W - MARGIN) * PT, Y(top + BAND_H * 0.72))
        c.setDash()
        # .rm coordinates: x centred on 0, y from the top of the page, 1 unit = 1 px at 229 dpi
        items.append({"id": n, "page": page, "text": text,
                      "band": {"x0": MARGIN - PX_W / 2, "x1": PX_W / 2 - MARGIN, "y0": top, "y1": bot}})
        if slot == PER_PAGE - 1 or n == len(chosen) - 1:
            c.showPage()
    c.save()
    side = {"page_px": [PX_W, PX_H], "dpi": DPI, "pages": npages, "seed": a.seed, "prompts": items}
    out.with_suffix(".json").write_text(json.dumps(side, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{out}: {npages} pages, {len(items)} prompts; sidecar {out.with_suffix('.json')}")


def cmd_practice2(a):
    """Structured practice pages (v2): grid cells with cross guides, digits/look-alikes, English four-line rows,
    free lines; varied sizes. Writes the PDF and a sidecar json with every box (see pages.py)."""
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    pdfmetrics.registerFont(TTFont("NotoKR", str(font_file())))
    kinds = a.kinds.split(",") if a.kinds else pages_v2.plan_kinds(a.pages)
    plan = pages_v2.layout(kinds, a.seed)
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    pages_v2.draw_pdf(plan, out, "NotoKR")
    side = pages_v2.sidecar(plan, a.seed)
    out.with_suffix(".json").write_text(json.dumps(side, ensure_ascii=False, indent=1), encoding="utf-8")
    n_cells = sum(len(i.get("cells", [])) for i in side["items"])
    print(f"{out}: {len(plan)} pages ({', '.join(kinds)}), {len(side['items'])} rows/lines, {n_cells} cells")


def cmd_make_doc(a):
    pdf = Path(a.pdf).read_bytes()
    side_path = Path(a.pdf).with_suffix(".json")
    side = json.loads(side_path.read_text(encoding="utf-8")) if side_path.exists() else {}
    n = side.get("pages") or len(re.findall(rb"/Type\s*/Page[^s]", pdf))
    if not a.name.startswith("rmpp-practice"):
        print("warning: pull only collects documents whose name starts with 'rmpp-practice'")
    doc = str(uuid.uuid4())
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    meta = {"deleted": False, "lastModified": str(int(time.time() * 1000)), "lastOpened": "", "lastOpenedPage": 0,
            "metadatamodified": False, "modified": False, "parent": "", "pinned": False, "synced": False,
            "type": "DocumentType", "version": 0, "visibleName": a.name}
    content = {"coverPageNumber": 0, "documentMetadata": {}, "dummyDocument": False, "extraMetadata": {},
               "fileType": "pdf", "fontName": "", "formatVersion": 1, "lineHeight": -1, "margins": 125,
               "orientation": "portrait", "originalPageCount": n, "pageCount": n, "pageTags": [],
               "pages": [str(uuid.uuid4()) for _ in range(n)], "redirectionPageMap": list(range(n)),
               "sizeInBytes": str(len(pdf)), "tags": [], "textAlignment": "left", "textScale": 1,
               "zoomMode": "bestFit"}
    (out / f"{doc}.pdf").write_bytes(pdf)
    (out / f"{doc}.metadata").write_text(json.dumps(meta, indent=4))
    (out / f"{doc}.content").write_text(json.dumps(content, indent=4))
    if side:  # remember which tablet document this sidecar belongs to
        side.update(doc_id=doc, name=a.name)
        side_path.write_text(json.dumps(side, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"wrote {doc}.pdf/.metadata/.content in {out} ({n} pages); sidecar {side_path} updated with doc_id")


# ---------------------------------------------------------------- render (debug)
def cmd_render(a):
    from PIL import ImageDraw
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    strokes = read_strokes(Path(a.rm).read_bytes())
    if not strokes:
        sys.exit("no strokes")
    lines = split_lines(strokes)
    page = render(strokes, range(len(strokes))).convert("RGB")
    oy = min(bbox(s)[1] for s in strokes) - PAD
    dr = ImageDraw.Draw(page)
    for n, l in enumerate(lines):  # red boxes = the y-range of each detected line
        dr.rectangle([0, float(l[1] - oy), page.width - 1, float(l[2] - oy)], outline=(220, 0, 0))
        dr.text((4, float(l[1] - oy)), str(n), fill=(220, 0, 0))
    page.save(out / "page.png")
    for n, p in enumerate(save_lines(strokes, lines, out / "lines")):
        print(p.name, Image.open(p).size, f"strokes={len(lines[n][0])}")
    print(f"{len(strokes)} strokes, {len(lines)} lines -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("pull", help="fetch conversions + practice docs from the tablet (read-only)")
    p.add_argument("--host", required=True)
    p.add_argument("--user", default="root")
    p.add_argument("--key", default="~/.ssh/id_ed25519_rmpp")
    p.add_argument("--out", required=True)
    p.add_argument("--sidecar", action="append", help="practice FILE.json (repeatable; remembered in OUT/practice)")
    p.add_argument("--y-offset", type=float, default=0.0, help="shift stroke y when matching practice bands")
    p.add_argument("--keep", action="store_true", help="leave corrected conversions on the tablet after pulling")
    p.set_defaults(f=cmd_pull)
    p = sp.add_parser("practice", help="write a practice PDF and sidecar json")
    p.add_argument("--out", required=True)
    p.add_argument("--count", type=int, default=30)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--text", action="append", help="story / talk / notes / a .txt path (repeatable; default all built-in; "
                                                  "'builtin' = the old short-prompt mix)")
    p.add_argument("--avoid", action="append", help="earlier practice json: prefer passages not used there (repeatable)")
    p.set_defaults(f=cmd_practice)

    p = sp.add_parser("practice2", help="structured practice pages v2 (cells, guides, sizes) + sidecar json")
    p.add_argument("--out", required=True)
    p.add_argument("--pages", type=int, default=8)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--kinds", help="comma list of page kinds: grid,digits,english,free (default: a mix)")
    p.set_defaults(f=cmd_practice2)

    p = sp.add_parser("texts", help="list the texts practice notebooks can use")
    p.add_argument("--text", action="append")
    p.set_defaults(f=cmd_texts)
    p = sp.add_parser("make-doc", help="write <uuid>.pdf/.metadata/.content for copying to the tablet")
    p.add_argument("--pdf", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(f=cmd_make_doc)
    p = sp.add_parser("render", help="render a .rm page and its lines (debug)")
    p.add_argument("rm")
    p.add_argument("--out", required=True)
    p.set_defaults(f=cmd_render)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
