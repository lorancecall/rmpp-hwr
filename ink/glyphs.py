#!/usr/bin/env python3
"""Glyph strokes for "Convert to ink": text -> pen strokes (the lasso's Convert to ink writes these on the page).

Glyph sources:
  font  - centre lines of an outline font (rendered, thinned to 1 px, traced into a stroke graph). The release uses
          Noto Sans KR (SIL OFL 1.1, see ink/OFL-NotoSansKR.txt); `--font` defaults to the copy collect.py downloads.

Glyph format (glyphs.json): {"em": 1.0, "glyphs": {char: {"adv": float, "src": "font", "strokes": [[x, y, x, y, ...], ...]}}}
Coordinates are in em units, y down, 0 = top of the em box, 1 = bottom; the baseline sits at BASELINE.

  glyphs.py build  [--font F.ttf] [--chars ko2350,ascii] -o glyphs.json
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
SIZE = 160        # raster size for thinning


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


def skeleton_graph(sk):
    """Skeleton pixels -> (nodes, edges). Neighbouring junction/end pixels (degree != 2) form one node at their
    centroid; an edge is the chain of degree-2 pixels between two nodes, as a polyline that starts and ends at the
    node centres (so strokes meet at junctions instead of stopping short). Closed loops become a node-less edge."""
    pts = set(zip(*np.nonzero(sk)))
    nbrs = {p: [(p[0] + dy, p[1] + dx) for dy, dx in NB if (p[0] + dy, p[1] + dx) in pts] for p in pts}
    special = {p for p in pts if len(nbrs[p]) != 2}
    node_of, nodes = {}, []
    for p in special:   # group touching special pixels
        if p in node_of:
            continue
        stack, comp = [p], []
        node_of[p] = len(nodes)
        while stack:
            q = stack.pop()
            comp.append(q)
            for r in nbrs[q]:
                if r in special and r not in node_of:
                    node_of[r] = len(nodes)
                    stack.append(r)
        c = np.mean(comp, axis=0)
        nodes.append((c[1], c[0]))   # x, y
    edges, seen = [], set()
    for p in special:
        for q in nbrs[p]:
            if q in special:
                if node_of[q] != node_of[p] and (q, p) not in seen:   # two nodes directly adjacent
                    seen.add((p, q))
                    edges.append([node_of[p], node_of[q], [nodes[node_of[p]], nodes[node_of[q]]]])
                continue
            if (p, q) in seen:
                continue
            path, prev, cur = [nodes[node_of[p]]], p, q
            while cur not in special:
                path.append((cur[1], cur[0]))
                nxt = [r for r in nbrs[cur] if r != prev]
                if not nxt:
                    break
                prev, cur = cur, nxt[0]
            seen.add((cur, prev))   # the same edge walked from the other end
            seen.add((p, q))
            if cur in special:
                path.append(nodes[node_of[cur]])
                edges.append([node_of[p], node_of[cur], path])
    done = set(special) | {(int(round(y)), int(round(x))) for e in edges for x, y in e[2]}
    for p in sorted(pts - done):   # loops without any junction (O, ㅇ)
        if p in done:
            continue
        path, prev, cur = [(p[1], p[0])], None, p
        done.add(p)
        while True:
            nxt = [r for r in nbrs[cur] if r != prev and r not in done]
            if not nxt:
                break
            prev, cur = cur, nxt[0]
            done.add(cur)
            path.append((cur[1], cur[0]))
        path.append(path[0])
        edges.append([None, None, path])
    return nodes, edges


def trace(sk, size):
    """Skeleton -> strokes: drop short end spurs (thinning artefacts), then chain edges through every node where the
    stroke continues: always at a 2-way node (corner), and at junctions for the straightest pair."""
    nodes, edges = skeleton_graph(sk)
    deg = lambda n: sum((e[0] == n) + (e[1] == n) for e in edges)
    length = lambda pl: float(np.sum(np.hypot(*np.diff(np.array(pl), axis=0).T))) if len(pl) > 1 else 0.0
    spur = 0.04 * size
    changed = True
    while changed:   # remove spurs: an edge to a dead end that is short, when its other end is a junction
        changed = False
        for e in list(edges):
            a, b = e[0], e[1]
            if a is None:
                continue
            for end, other in ((a, b), (b, a)):
                if deg(end) == 1 and deg(other) >= 3 and length(e[2]) < spur:
                    edges.remove(e)
                    changed = True
                    break
            if changed:
                break
    strokes = [[e[0], e[1], [np.array(p) for p in e[2]]] for e in edges]

    def direction(path, at_start):
        p = np.array(path if at_start else path[::-1])
        k = min(len(p) - 1, 6)
        d = p[k] - p[0]
        n = np.hypot(*d)
        return d / n if n else d

    merged = True
    while merged:
        merged = False
        for n in range(len(nodes)):
            ends = [(i, s[0] == n) for i, s in enumerate(strokes) if s[0] == n] + \
                   [(i, False) for i, s in enumerate(strokes) if s[1] == n and s[0] != n]
            if len(ends) < 2:
                continue
            best = None
            for x in range(len(ends)):
                for y in range(x + 1, len(ends)):
                    (i, si), (j, sj) = ends[x], ends[y]
                    if i == j:
                        continue
                    c = float(np.dot(direction(strokes[i][2], si), direction(strokes[j][2], sj)))   # -1 = straight on
                    if best is None or c < best[0]:
                        best = (c, ends[x], ends[y])
            if best is None or (len(ends) > 2 and best[0] > -0.7):
                continue   # at a junction only join a clearly straight continuation
            (c, (i, si), (j, sj)) = best
            A = strokes[i][2][::-1] if si else strokes[i][2]          # ends at n
            B = strokes[j][2] if sj else strokes[j][2][::-1]          # starts at n
            a_other = strokes[i][1] if si else strokes[i][0]
            b_other = strokes[j][1] if sj else strokes[j][0]
            new = [a_other, b_other, A + B[1:]]
            for k in sorted((i, j), reverse=True):
                del strokes[k]
            strokes.append(new)
            merged = True
            break
    out = [np.array(s[2], dtype=float) for s in strokes]
    used = {s[0] for s in strokes} | {s[1] for s in strokes}
    for n, (x, y) in enumerate(nodes):   # isolated dots (a skeleton of one pixel cluster)
        if n not in used:
            out.append(np.array([(x - 0.5, y), (x + 0.5, y)]))
    return out


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


def font_glyph(font, ch):
    img = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    d = ImageDraw.Draw(img)
    # baseline at BASELINE * SIZE (+ SIZE/2 margin)
    d.text((SIZE // 2, SIZE // 2 + BASELINE * SIZE), ch, font=font, fill=255, anchor="ls")
    adv = font.getlength(ch) / SIZE
    a = np.array(img) > 110
    if not a.any():
        return {"adv": round(adv, 4), "src": "font", "strokes": []}
    out = []
    for l in trace(thin(a), SIZE):
        l = rdp(l, SIZE / 140)
        l = (l - SIZE / 2) / SIZE
        out.append([round(float(v), 4) for v in l.reshape(-1)])
    return {"adv": round(adv, 4), "src": "font", "strokes": out}


def bounds(g):
    p = np.concatenate([np.array(s, dtype=float).reshape(-1, 2) for s in g["strokes"]])
    return p.min(0), p.max(0)


FONT_NOTICE = ("Glyph centre lines derived from Noto Sans KR, Copyright 2014-2021 Adobe (http://www.adobe.com/), "
               "with Reserved Font Name 'Source'. SIL Open Font License 1.1: https://openfontlicense.org")


def cmd_build(a):
    if not a.font:   # the Noto Sans KR instance collect.py downloads (and uses for practice pages)
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "collect"))
        from collect import font_file
        a.font = str(font_file())
    font = ImageFont.truetype(a.font, SIZE)
    chars = charset(a.chars)
    glyphs = {}
    for n, ch in enumerate(chars):
        glyphs[ch] = font_glyph(font, ch)
        if n % 250 == 0:
            print(f"{n}/{len(chars)}", file=sys.stderr)
    glyphs[" "] = {"adv": round(font.getlength(" ") / SIZE, 4) or 0.3, "src": "font", "strokes": []}
    meta = {"em": 1.0, "baseline": BASELINE, "font": Path(a.font).name}
    if "notosanskr" in Path(a.font).name.lower():
        meta["notice"] = FONT_NOTICE
    Path(a.out).write_text(json.dumps({**meta, "glyphs": glyphs}, ensure_ascii=False), encoding="utf-8")
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
    b.add_argument("--font", help="TTF/OTF; default: Noto Sans KR from collect.py")
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
