"""Practice pages v2: structured layouts that let every character's strokes be extracted.

Page kinds (mixed in one notebook):
  grid     one Korean syllable (or space) per square cell, with a light cross guide; cell size varies by row
  digits   the same grid for digits, symbols and look-alike sets (0 O o ㅇ, 1 l I ㅣ, ...)
  english  English on four guide lines (ascender, x-height, baseline, descender); size varies by row
  free     a single baseline per line: natural spacing; half the lines ask for fast, everyday writing

The sidecar json lists every item with its box in .rm page coordinates (x centred on 0, y from the top,
1 unit = 1 px at 229 dpi), so `extract()` can assign strokes to cells/lines without any guessing.
Nothing personal goes into the sidecar: no names, document ids or times (those are added locally by pull).
"""
import json
import random
import re
from pathlib import Path

PX_W, PX_H, DPI = 1620, 2160, 229
PT = 72 / DPI
MM = DPI / 25.4                  # px per mm (~9.0)
MARGIN = 70
TOP = 150                        # first item below the page header
FORMAT = "rmpp-hwr/practice@2"

CELL_MM = {"S": 7, "M": 10, "L": 14}          # grid cell sizes
LINE_MM = {"S": 4.5, "M": 6, "L": 8}          # english: distance between two guide lines
TEXTS = Path(__file__).resolve().parent / "texts"

LOOKALIKE = ["0 O o ㅇ", "1 l I ㅣ |", "5 S s", "2 Z z", "7 ㄱ", "ㅌ E", "ㅂ B", "ㄹ 2 Z", "6 b", "9 g q",
             "ㅁ □ 口", "u v", "rn m", "cl d", "- ㅡ _", ". , ;", "( ) [ ]", "% & @"]
HANGUL = re.compile(r"[가-힣]")


def _ko_chunks(rng, n, width):
    """Korean text pieces (Hangul + spaces only) of at most `width` characters, from the built-in texts."""
    src = []
    for f in sorted(TEXTS.glob("*.txt")):
        if f.name == "english.txt":
            continue
        for line in f.read_text(encoding="utf-8").splitlines():
            s = re.sub(r"[^가-힣 ]", " ", line)
            s = re.sub(r"\s+", " ", s).strip()
            if len(HANGUL.findall(s)) >= 3:
                src.append(s)
    words = " ".join(src).split(" ")          # one running stream, so a row can be filled across lines
    out = []
    while len(out) < n:
        i, piece = rng.randrange(len(words)), ""
        while i < len(words) and len(piece) + len(words[i]) + (1 if piece else 0) <= width:
            piece += (" " if piece else "") + words[i]
            i += 1
        if len(piece) >= min(width, 4) * 0.6:
            out.append(piece)
    return out


def _digit_items(rng, n, width):
    gens = [
        lambda: f"{rng.randint(1, 12)}/{rng.randint(1, 28)} {rng.randint(1, 12)}:{rng.choice([0, 15, 30, 45]):02d}",
        lambda: f"{rng.randint(1000, 99999):,}원",
        lambda: f"{rng.randint(0, 100)}% {rng.randint(1, 9)}.{rng.randint(0, 9)}kg",
        lambda: f"2026-{rng.randint(1, 12):02d}-{rng.randint(1, 28):02d}",
        lambda: "".join(str(rng.randint(0, 9)) for _ in range(rng.randint(width // 2, width))),
        lambda: rng.choice(LOOKALIKE),
        lambda: (lambda x, y: f"{x}+{y}={x + y}")(rng.randint(1, 99), rng.randint(1, 99)),
        lambda: (lambda x, y: f"{x}x{y}={x * y}")(rng.randint(2, 19), rng.randint(2, 19)),
    ]
    out = []
    while len(out) < n:
        s = rng.choice(gens)()
        if len(s) <= width:
            out.append(s)
    return out


_EN_POOL = []


def _english(rng, n):
    """English sentences without repeats until the list is used up."""
    if len(_EN_POOL) < n:
        lines = [l.strip() for l in (TEXTS / "english.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        rng.shuffle(lines)
        _EN_POOL.extend(lines)
    return [_EN_POOL.pop() for _ in range(n)]


def _free(rng, n):
    src = []
    for f in sorted(TEXTS.glob("*.txt")):
        src += [l.strip().lstrip("-").strip() for l in f.read_text(encoding="utf-8").splitlines()
                if 6 <= len(l.strip()) <= 34]
    return [rng.choice(src) for _ in range(n)]


def _rm(x, y):
    """page px -> .rm coordinates"""
    return x - PX_W / 2, y


def layout(kinds, seed):
    """Plan pages: list of (kind, [items]). Every item: kind, text, box (page px), plus guide details."""
    rng = random.Random(seed)
    pages = []
    for kind in kinds:
        items, y = [], TOP
        sizes = ["M", "L", "S"]
        rng.shuffle(sizes)
        k = 0
        while True:
            size = sizes[k % 3]
            if kind in ("grid", "digits"):
                cell = CELL_MM[size] * MM
                ncell = int((PX_W - 2 * MARGIN) // cell)
                h = 46 + cell
                if y + h > PX_H - 80:
                    break
                text = (_ko_chunks(rng, 1, ncell) if kind == "grid" else _digit_items(rng, 1, ncell))[0]
                x0 = MARGIN
                cells = []
                for i, ch in enumerate(text):
                    cx0 = x0 + i * cell
                    cells.append({"char": ch, "box": [cx0, y + 46, cx0 + cell, y + 46 + cell]})
                items.append({"kind": "row", "layout": kind, "size": size, "text": text, "prompt_y": y + 30,
                              "box": [x0, y + 46, x0 + len(text) * cell, y + 46 + cell], "cells": cells,
                              "cell_px": cell})
                y += h + 26
            elif kind == "english":
                s = LINE_MM[size] * MM
                h = 46 + 3 * s
                if y + h > PX_H - 80:
                    break
                text = _english(rng, 1)[0]
                top = y + 46
                items.append({"kind": "line", "layout": "english", "size": size, "text": text, "prompt_y": y + 30,
                              "box": [MARGIN, top, PX_W - MARGIN, top + 3 * s],
                              "guides": [top, top + s, top + 2 * s, top + 3 * s]})
                y += h + 30
            else:  # free
                h = 46 + 130
                if y + h > PX_H - 80:
                    break
                text = _free(rng, 1)[0]
                fast = k % 2 == 1
                items.append({"kind": "line", "layout": "free", "size": "M", "text": text, "prompt_y": y + 30,
                              "speed": "fast" if fast else "normal",
                              "box": [MARGIN, y + 46, PX_W - MARGIN, y + h], "guides": [y + 46 + 100]})
                y += h + 24
            k += 1
        pages.append((kind, items))
    return pages


HEADERS = {
    "grid": "흰 칸에 한 글자씩 쓰세요. 회색 칸은 띄어쓰기라 비워 둡니다. 평소 글씨체로.",
    "digits": "칸마다 한 글자(숫자, 기호)씩 쓰세요. 비슷한 글자는 서로 다르게 보이도록.",
    "english": "영어는 네 줄에 맞춰 쓰세요: 소문자 몸통은 가운데 칸, g y p 꼬리는 아래 칸.",
    "free": "줄 위에 자연스럽게 쓰세요. '빠르게' 표시가 있는 줄은 평소 메모하듯 빨리.",
}


def draw_pdf(pages, out, font_name, title="rmpp-practice"):
    from reportlab.pdfgen import canvas
    c = canvas.Canvas(str(out), pagesize=(PX_W * PT, PX_H * PT))
    c.setTitle(title)
    Y = lambda y: (PX_H - y) * PT
    for pno, (kind, items) in enumerate(pages):
        c.setFillGray(0.45)
        c.setFont(font_name, 28 * PT)
        c.drawString(MARGIN * PT, Y(70), f"{pno + 1}/{len(pages)}  {HEADERS[kind]}")
        for it in items:
            c.setFillGray(0.0)
            size = 34
            label = it["text"] + ("   (빠르게)" if it.get("speed") == "fast" else "")
            c.setFont(font_name, size * PT)
            c.drawString(MARGIN * PT, Y(it["prompt_y"]), label)
            x0, y0, x1, y1 = it["box"]
            if it["kind"] == "row":
                for cell in it["cells"]:
                    a, b, d, e = cell["box"]
                    if cell["char"] == " ":   # a space: shaded, no guide, so it is clearly left empty
                        c.setFillGray(0.9); c.setStrokeGray(0.9); c.setLineWidth(0.5); c.setDash()
                        c.rect(a * PT, Y(e), (d - a) * PT, (e - b) * PT, stroke=1, fill=1)
                        c.setFillGray(0.0)
                        continue
                    c.setStrokeGray(0.72); c.setLineWidth(1.2); c.setDash()
                    c.rect(a * PT, Y(e), (d - a) * PT, (e - b) * PT)
                    c.setStrokeGray(0.86); c.setLineWidth(0.8); c.setDash(3, 4)
                    c.line(((a + d) / 2) * PT, Y(b), ((a + d) / 2) * PT, Y(e))
                    c.line(a * PT, Y((b + e) / 2), d * PT, Y((b + e) / 2))
                c.setDash()
            else:
                for gi, gy in enumerate(it["guides"]):
                    dashed = it["layout"] == "english" and gi in (0, 1, 3)
                    c.setStrokeGray(0.6 if gi == 2 or it["layout"] == "free" else 0.82)
                    c.setLineWidth(1.0)
                    c.setDash(*((4, 4) if dashed else ()))
                    c.line(x0 * PT, Y(gy), x1 * PT, Y(gy))
                c.setDash()
        c.showPage()
    c.save()


def sidecar(pages, seed):
    """Template description in .rm coordinates; no personal data."""
    out = []
    n = 0
    for pno, (kind, items) in enumerate(pages):
        for it in items:
            conv = lambda b: [round(_rm(b[0], b[1])[0], 1), round(b[1], 1), round(_rm(b[2], b[3])[0], 1), round(b[3], 1)]
            d = {"id": n, "page": pno, "kind": it["kind"], "layout": it["layout"], "size": it["size"], "text": it["text"],
                 "box": conv(it["box"])}
            if it["kind"] == "row":
                d["cells"] = [{"char": cl["char"], "box": conv(cl["box"])} for cl in it["cells"]]
            else:
                d["guides"] = [round(g, 1) for g in it["guides"]]
            if it.get("speed"):
                d["speed"] = it["speed"]
            out.append(d)
            n += 1
    return {"format": FORMAT, "page_px": [PX_W, PX_H], "dpi": DPI, "pages": len(pages), "seed": seed,
            "page_kinds": [k for k, _ in pages], "items": out}


def plan_kinds(npages):
    cycle = ["grid", "english", "grid", "digits", "free", "grid", "english", "free"]
    return [cycle[i % len(cycle)] for i in range(npages)]


# ---------------------------------------------------------------- extraction
def _center(pts):
    return (pts[:, 0].min() + pts[:, 0].max()) / 2, (pts[:, 1].min() + pts[:, 1].max()) / 2


def _inside(x, y, box, slack):
    return box[0] - slack <= x <= box[2] + slack and box[1] - slack <= y <= box[3] + slack


def extract(side, page_strokes):
    """Assign strokes to items/cells by the centre of each stroke's bounding box.
    page_strokes: {page index: [ {"points": np.ndarray (n, 5) x,y,pressure,width,speed, "tool": int}, ... ]}
    Returns (items, qc): items carry stroke indices; qc lists problems per item."""
    res, qc = [], []
    for it in side["items"]:
        strokes = page_strokes.get(it["page"], [])
        box = it["box"]
        h = box[3] - box[1]
        slack_y = h * (0.15 if it["kind"] == "row" else 0.35)
        mine = [i for i, s in enumerate(strokes)
                if _inside(*_center(s["points"]), [box[0] - 20, box[1] - slack_y, box[2] + 20, box[3] + slack_y], 0)]
        r = {"id": it["id"], "page": it["page"], "strokes": mine}
        problems = []
        if it["kind"] == "row":
            r["cells"] = []
            for ci, cl in enumerate(it["cells"]):
                b = cl["box"]
                cs = [i for i in mine if b[0] <= _center(strokes[i]["points"])[0] < b[2]]
                r["cells"].append(cs)
                if cl["char"] != " " and not cs:
                    problems.append(f"cell {ci} '{cl['char']}' empty")
                if cl["char"] == " " and cs:
                    problems.append(f"cell {ci} should be blank")
                for i in cs:   # a stroke that reaches far into the next cell: ambiguous split
                    p = strokes[i]["points"]
                    w = b[2] - b[0]
                    if p[:, 0].min() < b[0] - 0.45 * w or p[:, 0].max() > b[2] + 0.45 * w:
                        problems.append(f"cell {ci} '{cl['char']}' crosses into a neighbour")
                        break
            # strokes in the row box but beyond the last cell
            extra = [i for i in mine if _center(strokes[i]["points"])[0] >= it["cells"][-1]["box"][2]] if it["cells"] else []
            if extra:
                problems.append(f"{len(extra)} stroke(s) after the last cell")
        elif not mine:
            problems.append("empty")
        res.append(r)
        if problems:
            qc.append({"id": it["id"], "page": it["page"], "text": it["text"], "problems": problems})
    used = {(r["page"], i) for r in res for i in r["strokes"]}
    stray = sum(1 for p, ss in page_strokes.items() for i in range(len(ss)) if (p, i) not in used)
    return res, qc, stray
