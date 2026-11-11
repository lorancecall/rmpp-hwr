#!/usr/bin/env python3
""""My handwriting" glyphs for Convert to ink: glyphs_own.json from the writer's collected strokes.

For every character, best first:
  1. cell  - practice v2 grid cells (one character per cell; labels from each sample's chars.tsv)
  2. line  - characters cut out of practice lines: the line's strokes are grouped into blobs by horizontal overlap;
             when a word's blobs match its characters one to one (print-style writing), each blob is a character
  3. jamo  - Hangul syllables composed from parts of the writer's own syllables: the initial+medial part of one
             syllable and the final consonant of another (like 녕 = 념 without ㅁ + the ㅇ of 경)
  4. font  - the font glyph (glyphs.json) for anything still missing

  own.py build --data hwr-data/collected --font-glyphs build/glyphs.json -o build/glyphs_own.json \
               [--rec target/release/rmpp-hwr --model model]   (ORT_DYLIB_PATH = onnxruntime for the PC)
  own.py preview build/glyphs_own.json "안녕하세요 hello" -o preview.png
"""
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from glyphs import bounds, fit_to, rdp  # noqa: E402

HANGUL0 = 0xAC00


def jamo(ch):
    """(initial, medial, final) indices of a Hangul syllable, or None."""
    o = ord(ch) - HANGUL0
    if not 0 <= o < 11172:
        return None
    return o // 588, (o % 588) // 28, o % 28


def syllable(i, m, f):
    return chr(HANGUL0 + i * 588 + m * 28 + f)


def pack(strokes):
    return [[round(float(v), 4) for v in rdp(np.asarray(s, dtype=float), 0.004).reshape(-1)] for s in strokes]


# ---------------------------------------------------------------- 2. characters cut out of lines
def blobs(strokes, gap):
    """Group strokes whose x ranges overlap or nearly touch, left to right."""
    order = sorted(range(len(strokes)), key=lambda i: strokes[i][:, 0].min())
    out = []
    for i in order:
        x0, x1 = strokes[i][:, 0].min(), strokes[i][:, 0].max()
        if out and x0 < out[-1]["x1"] + gap:
            out[-1]["ids"].append(i)
            out[-1]["x1"] = max(out[-1]["x1"], x1)
        else:
            out.append({"ids": [i], "x0": x0, "x1": x1})
    return out


def line_chars(root):
    """{char: [strokes (n,2) arrays in page units]} from lines whose words split cleanly into characters."""
    found = defaultdict(list)
    for f in sorted(Path(root).glob("samples/*/strokes.json")):
        j = json.loads(f.read_text(encoding="utf-8"))
        items = {it["id"]: it for it in j.get("template", {}).get("items", [])}
        for a in j.get("assign", []):
            it = items.get(a["id"])
            page = j["pages"].get(str(a.get("page")))
            if not it or not page or it.get("kind") == "row" or not a.get("strokes"):
                continue
            text = it.get("text", "").strip()
            strokes = [np.array(page[i]["points"], dtype=float)[:, :2] for i in a["strokes"]]
            if not strokes or not text:
                continue
            h = np.median([s[:, 1].max() - s[:, 1].min() for s in strokes]) or 20
            words = text.split()
            # words: blobs separated by a gap of about a character height or more (tuned on the collected lines)
            wb = blobs(strokes, h * 1.2)
            if len(wb) != len(words):
                continue
            for w, b in zip(words, wb):
                sub = [strokes[i] for i in b["ids"]]
                cb = blobs(sub, 2.0)
                chars = [c for c in w]
                if len(cb) != len(chars):
                    continue
                for c, g in zip(chars, cb):
                    if c.isalnum() or jamo(c):
                        found[c].append([sub[i] for i in g["ids"]])
    return found


def normalize(strokes, ref):
    """Strokes in page units -> a glyph in em units, sized and placed like the font glyph `ref`."""
    allp = np.vstack(strokes)
    lo = allp.min(0)
    s = max(allp[:, 1].max() - lo[1], allp[:, 0].max() - lo[0], 1e-6)
    g = {"adv": 1.0, "src": "own", "strokes": [((p - lo) / s).reshape(-1).tolist() for p in strokes]}
    return fit_to(g, ref)


# ---------------------------------------------------------------- 3. jamo composition
def split_final(g):
    """(upper strokes, final strokes): the final consonant is the strokes that start in the lower part. A stroke
    that starts high and runs down (the long ㅣ of ㅏ, ㅓ, ㅣ...) belongs to the vowel even when its centre is low."""
    st = [np.array(s, dtype=float).reshape(-1, 2) for s in g["strokes"]]
    allp = np.vstack(st)
    y0, y1 = allp[:, 1].min(), allp[:, 1].max()
    cut = y0 + (y1 - y0) * 0.55
    return [s for s in st if s[:, 1].min() < cut], [s for s in st if s[:, 1].min() >= cut]


def compose(target, own, font):
    """Build a Hangul syllable from own syllables, or None."""
    t = jamo(target)
    if not t:
        return None
    ti, tm, tf = t
    bases = [c for c in own if jamo(c) and jamo(c)[:2] == (ti, tm)]
    if not bases:
        return None
    if tf == 0:   # no final: a base without one, else a base with its final removed (then re-fit to the font)
        plain = [c for c in bases if jamo(c)[2] == 0]
        if plain:
            return None   # would be the character itself
        top, _ = split_final(own[bases[0]])
        return fit_to({"adv": 1.0, "src": "jamo", "strokes": [s.reshape(-1).tolist() for s in top]}, font.get(target))
    finals = [c for c in own if jamo(c) and jamo(c)[2] == tf and jamo(c)[2] != 0]
    if not finals:
        return None
    withf = [c for c in bases if jamo(c)[2] != 0]
    _, fin = split_final(own[finals[0]])
    if not fin:
        return None
    if not withf:
        # only written without a final (스 for 습): squeeze it into the upper 62% and put the final below,
        # as wide as the vowel part and as tall as the finals in the writer's own syllables
        st = [np.array(x, dtype=float).reshape(-1, 2) for x in own[bases[0]]["strokes"]]
        bp = np.vstack(st)
        y0, h = bp[:, 1].min(), np.ptp(bp[:, 1]) or 1e-3
        top = [np.column_stack([x[:, 0], y0 + (x[:, 1] - y0) * 0.62]) for x in st]
        fp = np.vstack(fin)
        k = min(h * 0.36 / (np.ptp(fp[:, 1]) or 1e-3), np.ptp(bp[:, 0]) * 0.8 / (np.ptp(fp[:, 0]) or 1e-3))
        c_f = (fp.min(0) + fp.max(0)) / 2
        target = np.array([(bp[:, 0].min() + bp[:, 0].max()) / 2, y0 + h * 0.62 + h * 0.2])
        strokes = top + [(x - c_f) * k + target for x in fin]
        return {"adv": own[bases[0]]["adv"], "src": "jamo", "strokes": [x.reshape(-1).tolist() for x in strokes]}
    top, old = split_final(own[withf[0]])
    if not top or not old:
        return None
    op, fp = np.vstack(old), np.vstack(fin)
    k = (np.ptp(op[:, 1]) or 1e-3) / (np.ptp(fp[:, 1]) or 1e-3)
    k = min(k, (np.ptp(op[:, 0]) or 1e-3) / (np.ptp(fp[:, 0]) or 1e-3) * 1.3)
    c_o, c_f = (op.min(0) + op.max(0)) / 2, (fp.min(0) + fp.max(0)) / 2
    strokes = top + [(s - c_f) * k + c_o for s in fin]
    return {"adv": own[withf[0]]["adv"], "src": "jamo", "strokes": [s.reshape(-1).tolist() for s in strokes]}


def verify(cands, exe, model):
    """Render each (char, strokes) black on white (48 px ink height, 12 px margin, pen 4 like page.rs) and run the
    recognizer CLI (`rmpp-hwr <model-dir> <png>...`) on all of them; True where it reads the label."""
    import subprocess
    import tempfile
    from PIL import Image, ImageDraw
    with tempfile.TemporaryDirectory() as t:
        paths = []
        for n, (c, strokes) in enumerate(cands):
            p = np.vstack(strokes)
            lo, hi = p.min(0), p.max(0)
            k = 48 / max(hi[1] - lo[1], 1e-3)
            w, h = int((hi[0] - lo[0]) * k) + 25, 48 + 25
            img = Image.new("L", (max(w, 25), h), 255)
            d = ImageDraw.Draw(img)
            for s in strokes:
                q = [(float((x - lo[0]) * k + 12), float((y - lo[1]) * k + 12)) for x, y in s]
                d.line(q if len(q) > 1 else q * 2, fill=0, width=4, joint="curve")
            path = str(Path(t) / f"{n:05d}.png")
            img.save(path)
            paths.append(path)
        got = {}
        for i in range(0, len(paths), 200):   # keep the command line short
            out = subprocess.run([str(Path(exe).resolve()), str(Path(model).resolve()), *paths[i:i + 200]], capture_output=True, text=True, encoding="utf-8").stdout
            for line in out.splitlines():
                if ": " in line and "  (conf" in line:
                    path, rest = line.split(": ", 1)
                    got[Path(path).name] = rest.split("  (conf")[0].strip()
        return [same(got.get(Path(p).name), c) for p, (c, _) in zip(paths, cands)]


# a lone character is often read as a look-alike (l -> 1, o -> 0, case): those count as read back
LOOKALIKE = [set("lI1|i!"), set("oO0"), set("cC"), set("sS5"), set("vV"), set("wW"), set("xX"), set("zZ2"),
             set("uU"), set("pP"), set("kK"), set("yY"), set("a"), set("gq9")]


def same(read, label):
    if read is None:
        return False
    if read == label:
        return True
    return len(read) == 1 and any(read in g and label in g for g in LOOKALIKE)


def cmd_build(a):
    from glyphs import own_glyphs
    font = json.loads(Path(a.font_glyphs).read_text(encoding="utf-8"))
    fg = font["glyphs"]
    own = {c: g for c, g in own_glyphs(a.data).items()}
    for c, g in own.items():
        own[c] = fit_to(g, fg.get(c))
    n_cell = len(own)
    lines = line_chars(a.data)
    cands = [(c, s) for c, samples in lines.items() if c not in own and c in fg for s in samples]

    def aspect(strokes):
        p = np.vstack([np.asarray(x, dtype=float).reshape(-1, 2) for x in strokes])
        return np.ptp(p[:, 0]) / max(np.ptp(p[:, 1]), 1e-3)

    # a cut-out with a stroke of its neighbour is too wide for its character (ㅣ갑): compare with the font's shape
    cands = [(c, s) for c, s in cands
             if not fg[c]["strokes"] or 0.5 <= aspect(s) / max(aspect(fg[c]["strokes"]), 0.05) <= 1.45]
    if a.rec:   # keep only cut-outs the recognizer reads as their label (a word whose blobs matched by count only)
        ok = verify(cands, a.rec, a.model)
        print(f"line cut-outs: {sum(ok)} of {len(cands)} read back as their label", file=sys.stderr)
        cands = [x for x, k in zip(cands, ok) if k]
        # a stroke of the neighbouring syllable can ride along (ㅣ갑): drop the leftmost / rightmost stroke of a
        # Hangul cut-out while the rest is still read as the same syllable
        for _ in range(2):
            trials, where = [], []
            for n, (c, s) in enumerate(cands):
                if jamo(c) and len(s) >= 3:
                    xs = [np.asarray(x)[:, 0].mean() for x in s]
                    for drop in (int(np.argmin(xs)), int(np.argmax(xs))):
                        trials.append((c, [x for i, x in enumerate(s) if i != drop]))
                        where.append(n)
            if not trials:
                break
            ok = verify(trials, a.rec, a.model)
            trimmed = set()
            for (c, s), n, k in zip(trials, where, ok):
                if k and n not in trimmed:
                    cands[n] = (c, s)
                    trimmed.add(n)
            print(f"trimmed a neighbour's stroke from {len(trimmed)} cut-outs", file=sys.stderr)
            if not trimmed:
                break
    by_char = defaultdict(list)
    for c, s in cands:
        by_char[c].append(s)
    n_line = 0
    for c, samples in by_char.items():
        # the sample whose aspect is closest to the median of this character's samples
        asp = [np.ptp(np.vstack(s)[:, 0]) / max(np.ptp(np.vstack(s)[:, 1]), 1e-3) for s in samples]
        best = samples[int(np.argmin(np.abs(np.array(asp) - np.median(asp))))]
        own[c] = normalize(best, fg.get(c))
        own[c]["src"] = "line"
        n_line += 1
    composed = {}
    for c in fg:
        if c not in own and jamo(c):
            g = compose(c, own, fg)
            if g:
                composed[c] = g
    out = dict(fg)
    out.update({c: {**g, "strokes": pack([np.array(s).reshape(-1, 2) for s in g["strokes"]])} for c, g in own.items()})
    out.update({c: {**g, "strokes": pack([np.array(s).reshape(-1, 2) for s in g["strokes"]])} for c, g in composed.items()})
    meta = {k: v for k, v in font.items() if k != "glyphs"}
    meta["style"] = "own"
    meta["own"] = {"cell": n_cell, "line": n_line, "jamo": len(composed)}
    Path(a.out).write_text(json.dumps({**meta, "glyphs": out}, ensure_ascii=False), encoding="utf-8")
    print(f"{a.out}: {len(out)} glyphs; own: {n_cell} from cells, {n_line} from lines, {len(composed)} composed")


def cmd_preview(a):
    from PIL import Image, ImageDraw
    g = json.loads(Path(a.glyphs).read_text(encoding="utf-8"))["glyphs"]
    h = 120
    img = Image.new("RGB", (int(h * len(a.text) * 0.9) + 40, h + 60), "white")
    d = ImageDraw.Draw(img)
    x = 20
    col = {"own": (0, 0, 0), "line": (0, 0, 170), "jamo": (170, 0, 0), "font": (150, 150, 150)}
    for ch in a.text:
        gl = g.get(ch)
        if not gl:
            x += h * 0.4
            continue
        for s in gl["strokes"]:
            p = np.array(s).reshape(-1, 2)
            d.line([(x + px * h, 20 + py * h) for px, py in p], fill=col.get(gl.get("src"), (0, 0, 0)), width=4, joint="curve")
        x += (gl["adv"] + 0.04) * h
    img.save(a.out)
    print(a.out, "(black: cell, blue: line, red: composed, grey: font)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--data", required=True)
    b.add_argument("--font-glyphs", required=True)
    b.add_argument("-o", "--out", required=True)
    b.add_argument("--rec", help="recognizer CLI (target/release/rmpp-hwr) to check line cut-outs")
    b.add_argument("--model", default="model", help="model folder for --rec (rec.onnx, dict.txt)")
    p = sp.add_parser("preview")
    p.add_argument("glyphs")
    p.add_argument("text")
    p.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    {"build": cmd_build, "preview": cmd_preview}[a.cmd](a)


if __name__ == "__main__":
    main()
