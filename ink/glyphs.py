#!/usr/bin/env python3
"""Glyph strokes for "convert to ink": text -> pen strokes that look like handwriting.

Glyph sources, best first:
  1. own   - the writer's own characters from practice v2 grid cells (hwr-data/collected/samples/practice_*/strokes.json)
  2. font  - centre lines of an outline font (rendered, thinned to 1 px, traced into polylines)

Glyph format (glyphs.json): {"em": 1.0, "glyphs": {char: {"adv": float, "src": "own"|"font", "strokes": [[x, y, x, y, ...], ...]}}}
Coordinates are in em units, y down, 0 = top of the em box, 1 = bottom; the baseline sits at BASELINE.

  glyphs.py build  --font F.ttf [--own DIR] [--chars ko2350,ascii] -o glyphs.json
  glyphs.py layout glyphs.json "text" --x X --y Y --h H [--w W] -o strokes.json    (page units, for the tablet)
  glyphs.py preview glyphs.json "text" -o preview.png
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

BASELINE = 0.82   # em fraction where the baseline sits (font glyphs are placed so; own cells are scaled to match)
SIZE = 96         # raster size for thinning


# ---------------------------------------------------------------- character sets
def charset(spec):
    out = []
    for part in spec.split(","):
        if part == "ascii":
            out += [chr(c) for c in range(0x21, 0x7F)]
        elif part == "ko2350":   # KS X 1001 Hangul syllables (the common 2,350)
            for hi in range(0xB0, 0xC9):
                for lo in range(0xA1, 0xFF):
                    try:
                        out.append(bytes([hi, lo]).decode("euc-kr"))
                    except UnicodeDecodeError:
                        pass
        elif part:
            out += list(part)
    return list(dict.fromkeys(out))


# ---------------------------------------------------------------- thinning + tracing
def thin(img):
    """Zhang-Suen thinning of a boolean image (True = ink)."""
    im = np.pad(img.astype(np.uint8), 1)
    changed = True
    while changed:
        changed = False
        for step in (0, 1):
            p = [np.roll(np.roll(im, -dy, 0), -dx, 1) for dy, dx in
                 ((-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1), (0, -1), (-1, -1))]   # P2..P9
            b = sum(p)
            seq = p + [p[0]]
            a = sum(((seq[i] == 0) & (seq[i + 1] == 1)).astype(np.uint8) for i in range(8))
            if step == 0:
                c = (p[0] * p[2] * p[4] == 0) & (p[2] * p[4] * p[6] == 0)
            else:
                c = (p[0] * p[2] * p[6] == 0) & (p[0] * p[4] * p[6] == 0)
            m = (im == 1) & (b >= 2) & (b <= 6) & (a == 1) & c
            if m.any():
                im[m] = 0
                changed = True
    return im[1:-1, 1:-1].astype(bool)


NB = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]


def trace(sk):
    """Skeleton pixels -> polylines [(n,2) x,y]. Walks from endpoints/junctions, then closed loops."""
    pts = set(zip(*np.nonzero(sk)))
    nbrs = {p: [(p[0] + dy, p[1] + dx) for dy, dx in NB if (p[0] + dy, p[1] + dx) in pts] for p in pts}
    deg = {p: len(n) for p, n in nbrs.items()}
    used = set()   # undirected edges
    lines = []

    def walk(a, b):
        path = [a, b]
        used.add(frozenset((a, b)))
        while deg[path[-1]] == 2:
            nxt = [q for q in nbrs[path[-1]] if frozenset((path[-1], q)) not in used]
            if not nxt:
                break
            used.add(frozenset((path[-1], nxt[0])))
            path.append(nxt[0])
        return path

    starts = sorted([p for p in pts if deg[p] != 2], key=lambda p: (deg[p] != 1, p))
    for s in starts:
        for q in nbrs[s]:
            if frozenset((s, q)) not in used:
                lines.append(walk(s, q))
    for s in sorted(pts):   # loops (all degree 2)
        for q in nbrs[s]:
            if frozenset((s, q)) not in used:
                lines.append(walk(s, q))
    return [np.array([(x, y) for y, x in l], dtype=float) for l in lines]


def rdp(p, eps):
    if len(p) < 3:
        return p
    a, b = p[0], p[-1]
    d = b - a
    n = np.hypot(*d)
    dist = np.abs(d[0] * (p[:, 1] - a[1]) - d[1] * (p[:, 0] - a[0])) / n if n > 0 else np.hypot(*(p - a).T)
    i = int(np.argmax(dist))
    if dist[i] > eps:
        return np.vstack([rdp(p[:i + 1], eps)[:-1], rdp(p[i:], eps)])
    return np.array([a, b])


def join(lines, gap):
    """Merge polylines whose ends touch (junction splits) so a glyph has fewer, longer strokes."""
    lines = [l for l in lines if len(l) >= 2]
    merged = True
    while merged:
        merged = False
        for i in range(len(lines)):
            for j in range(i + 1, len(lines)):
                a, b = lines[i], lines[j]
                for ra, rb in ((False, False), (False, True), (True, False), (True, True)):
                    A = a[::-1] if ra else a
                    B = b[::-1] if rb else b
                    if np.hypot(*(A[-1] - B[0])) <= gap:
                        # only continue roughly straight on (keeps crossing strokes separate)
                        da, db = A[-1] - A[max(0, len(A) - 4)], B[min(3, len(B) - 1)] - B[0]
                        na, nb = np.hypot(*da), np.hypot(*db)
                        if na and nb and np.dot(da, db) / (na * nb) > 0.5:
                            lines[i] = np.vstack([A, B[1:]])
                            del lines[j]
                            merged = True
                            break
                if merged:
                    break
            if merged:
                break
    return lines


def font_glyph(font, ch):
    img = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    d = ImageDraw.Draw(img)
    # baseline at BASELINE * SIZE (+ SIZE/2 margin)
    d.text((SIZE // 2, SIZE // 2 + BASELINE * SIZE), ch, font=font, fill=255, anchor="ls")
    adv = font.getlength(ch) / SIZE
    a = np.array(img) > 110
    if not a.any():
        return {"adv": adv, "src": "font", "strokes": []}
    sk = thin(a)
    lines = trace(sk)
    lines = [l for l in lines if len(l) >= 4]   # drop spurs and junction stubs
    lines = join(lines, 3.0)
    out = []
    for l in lines:
        l = rdp(l, 0.6)
        if len(l) < 2 or np.hypot(*(l.max(0) - l.min(0))) < SIZE * 0.04:
            continue
        l = (l - SIZE / 2) / SIZE
        out.append([round(float(v), 4) for v in l.reshape(-1)])
    return {"adv": round(adv, 4), "src": "font", "strokes": out}


# ---------------------------------------------------------------- own handwriting (practice v2 cells)
def own_glyphs(root):
    """Characters from practice v2 grid cells: the cell box is the em box (square, with cross guides)."""
    best = {}
    for f in sorted(Path(root).glob("samples/practice_*/strokes.json")):
        j = json.loads(f.read_text(encoding="utf-8"))
        tpl = j.get("template", {})
        items = {it["id"]: it for it in tpl.get("items", [])}
        assign = {a["id"]: a for a in j.get("assign", [])}
        # chars.tsv holds the labels collect.py trusted (cells/<item>_<cell>.png<TAB>char)
        tsv = f.parent / "chars.tsv"
        labels = [l.split("\t") for l in tsv.read_text(encoding="utf-8").splitlines() if "\t" in l] if tsv.exists() else []
        for path, ch in labels:
            iid, ci = (int(v) for v in Path(path).stem.split("_"))
            it, a = items.get(iid), assign.get(iid)
            if not it or not a or len(ch) != 1 or ch.isspace():
                continue
            page = j["pages"].get(str(a["page"]))
            idx = a["cells"][ci] if ci < len(a["cells"]) else []
            if not page or not idx:
                continue
            cell = it["cells"][ci]
            x0, y0, x1, y1 = cell["box"]
            size = max(x1 - x0, y1 - y0)
            # keep the writer's size and height inside the cell (cell bottom = 0.9 em); horizontally the ink
            # starts at 0.04 and the advance is the ink width (handwriting is proportional, not monospaced)
            strokes = [(np.array(page[i]["points"], dtype=float)[:, :2] - [x0, y0]) / size * 0.9 for i in idx]
            lo = min(p[:, 0].min() for p in strokes)
            hi = max(p[:, 0].max() for p in strokes)
            strokes = [p - [lo - 0.04, 0] for p in strokes]
            best[ch] = {"adv": round(float(hi - lo + 0.08), 4), "src": "own",   # last sample wins (latest practice)
                        "strokes": [[round(float(v), 4) for v in rdp(p, 0.004).reshape(-1)] for p in strokes]}
    return best


def bounds(g):
    p = np.concatenate([np.array(s, dtype=float).reshape(-1, 2) for s in g["strokes"]])
    return p.min(0), p.max(0)


def fit_to(own, ref):
    """Scale and place an own glyph like the font glyph of the same character (cells come in S/M/L sizes and the
    writer does not fill them evenly; the font fixes size and vertical position, the writer keeps the shape)."""
    if not own["strokes"] or not ref or not ref["strokes"]:
        return own
    (ox0, oy0), (ox1, oy1) = bounds(own)
    (fx0, fy0), (fx1, fy1) = bounds(ref)
    ow, oh, fw, fh = max(ox1 - ox0, 1e-3), max(oy1 - oy0, 1e-3), max(fx1 - fx0, 1e-3), max(fy1 - fy0, 1e-3)
    k = min(fh / oh, 1.3 * fw / ow) if fh > 0.05 else fw / ow
    cy = (fy0 + fy1) / 2
    strokes = []
    for st in own["strokes"]:
        p = np.array(st, dtype=float).reshape(-1, 2)
        p = np.column_stack([(p[:, 0] - ox0) * k + fx0, (p[:, 1] - (oy0 + oy1) / 2) * k + cy])
        strokes.append([round(float(v), 4) for v in p.reshape(-1)])
    return {"adv": round(float(ow * k + (ref["adv"] - fw)), 4), "src": "own", "strokes": strokes}


def cmd_build(a):
    font = ImageFont.truetype(a.font, SIZE)
    chars = charset(a.chars)
    glyphs = {}
    for n, ch in enumerate(chars):
        glyphs[ch] = font_glyph(font, ch)
        if n % 250 == 0:
            print(f"{n}/{len(chars)}", file=sys.stderr)
    glyphs[" "] = {"adv": round(font.getlength(" ") / SIZE, 4) or 0.3, "src": "font", "strokes": []}
    if a.own:
        own = own_glyphs(a.own)
        for ch, g in own.items():
            glyphs[ch] = fit_to(g, glyphs.get(ch))
        print(f"own glyphs: {len(own)}", file=sys.stderr)
    Path(a.out).write_text(json.dumps({"em": 1.0, "baseline": BASELINE, "glyphs": glyphs}, ensure_ascii=False),
                           encoding="utf-8")
    print(f"{a.out}: {len(glyphs)} glyphs")


# ---------------------------------------------------------------- layout
def layout(g, text, h, track=0.04):
    """Text -> strokes in em*h units starting at (0,0) top-left; returns (strokes, width)."""
    glyphs = g["glyphs"]
    x = 0.0
    out = []
    for ch in text:
        gl = glyphs.get(ch) or glyphs.get("?")
        if gl is None:
            x += 0.5
            continue
        for s in gl["strokes"]:
            p = np.array(s, dtype=float).reshape(-1, 2)
            out.append(np.column_stack([(p[:, 0] + x) * h, p[:, 1] * h]))
        x += gl["adv"] + track
    return out, x * h


def cmd_layout(a):
    g = json.loads(Path(a.glyphs).read_text(encoding="utf-8"))
    lines = a.text.split("\\n")
    out = []
    for i, line in enumerate(lines):
        strokes, w = layout(g, line, a.h)
        if a.w and w > a.w:   # shrink to fit the selection width
            k = a.w / w
            strokes = [s * k for s in strokes]
        out += [(s + [a.x, a.y + i * a.h * 1.25]).round(2).reshape(-1).tolist() for s in strokes]
    Path(a.out).write_text(json.dumps({"strokes": out}), encoding="utf-8")
    print(f"{a.out}: {len(out)} strokes")


def cmd_preview(a):
    g = json.loads(Path(a.glyphs).read_text(encoding="utf-8"))
    h = 80
    rows = a.text.split("\\n")
    res = [layout(g, r, h) for r in rows]
    W = int(max(w for _, w in res)) + 40
    img = Image.new("L", (W, int(len(rows) * h * 1.25) + 40), 255)
    d = ImageDraw.Draw(img)
    for i, (strokes, _) in enumerate(res):
        oy = 20 + i * h * 1.25
        d.line([(20, oy + BASELINE * h), (W - 20, oy + BASELINE * h)], fill=210)
        for s in strokes:
            d.line([(float(x) + 20, float(y) + oy) for x, y in s], fill=0, width=3, joint="curve")
    img.save(a.out)
    print(a.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--font", required=True)
    b.add_argument("--own", help="collected data dir (with samples/practice_*/strokes.json)")
    b.add_argument("--chars", default="ko2350,ascii")
    b.add_argument("-o", "--out", required=True)
    l = sp.add_parser("layout")
    l.add_argument("glyphs")
    l.add_argument("text")
    l.add_argument("--x", type=float, required=True)
    l.add_argument("--y", type=float, required=True)
    l.add_argument("--h", type=float, required=True, help="em height in page units")
    l.add_argument("--w", type=float, default=0, help="max line width (shrinks to fit)")
    l.add_argument("-o", "--out", required=True)
    p = sp.add_parser("preview")
    p.add_argument("glyphs")
    p.add_argument("text")
    p.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    {"build": cmd_build, "layout": cmd_layout, "preview": cmd_preview}[a.cmd](a)


if __name__ == "__main__":
    main()
