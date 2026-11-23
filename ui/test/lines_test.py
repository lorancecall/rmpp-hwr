#!/usr/bin/env python3
"""Unit test of the lasso line grouping (ui/ink.h recognizeImage), ported to Python so it runs on the PC.
Components are the connected ink parts as (x, y, w, h) in image pixels.

    python ui/test/lines_test.py          (exit code 0 = all passed)

Keep this in step with ui/ink.h.
"""
import sys


def group(comps):
    hs = sorted(h for _, _, _, h in comps if h > 2)
    med = max(10, hs[len(hs) // 2])
    tol = med * 0.3
    bottom = lambda c: c[1] + c[3] - 1
    order = sorted(range(len(comps)), key=lambda i: (comps[i][1] + bottom(comps[i])) / 2)
    small = lambda i: comps[i][3] < med * 0.35 and comps[i][2] < med * 0.6
    lines = []  # [ids, y0, y1]
    for i in order:
        if small(i):
            continue
        x, y, w, h = comps[i]
        cy = (y + bottom(comps[i])) / 2
        for l in lines:
            if l[1] - tol <= cy <= l[2] + tol:
                l[0].append(i); l[1] = min(l[1], y); l[2] = max(l[2], bottom(comps[i]))
                break
        else:
            lines.append([[i], y, bottom(comps[i])])
    merged = True
    while merged:  # overlapping lines
        merged = False
        for a in range(len(lines)):
            for b in range(a + 1, len(lines)):
                A, B = lines[a], lines[b]
                ov = min(A[2], B[2]) - max(A[1], B[1])
                if ov >= 0.3 * min(A[2] - A[1], B[2] - B[1]):
                    A[0] += B[0]; A[1] = min(A[1], B[1]); A[2] = max(A[2], B[2]); del lines[b]
                    merged = True
                    break
            if merged:
                break
    merged = True
    while merged:  # batchim under the vowel line
        merged = False
        lines.sort(key=lambda l: l[1])
        for a in range(len(lines) - 1):
            top, bot = lines[a], lines[a + 1]
            ht, hb, gap = top[2] - top[1], bot[2] - bot[1], bot[1] - top[2]
            if gap >= 0.35 * ht or hb >= 0.6 * ht:
                continue
            x0 = min(comps[i][0] for i in top[0]); x1 = max(comps[i][0] + comps[i][2] - 1 for i in top[0])
            if any(comps[i][0] < x0 or comps[i][0] + comps[i][2] - 1 > x1 for i in bot[0]):
                continue
            top[0] += bot[0]; top[2] = max(top[2], bot[2]); del lines[a + 1]
            merged = True
            break
    for i in order:  # small marks
        if not small(i):
            continue
        cy = (comps[i][1] + bottom(comps[i])) / 2
        d = lambda l: l[1] - cy if cy < l[1] else (cy - l[2] if cy > l[2] else 0)
        if lines and min(d(l) for l in lines) < 2 * med:
            min(lines, key=d)[0].append(i)
        else:
            lines.append([[i], comps[i][1], bottom(comps[i])])
    return len(lines)


CASES = [
    # name, components, lines wanted
    ("안녕? with batchim under the vowels", [(0, 0, 22, 22), (26, 0, 10, 40), (5, 46, 26, 16), (50, 0, 20, 24),
                                           (74, 0, 14, 40), (52, 44, 26, 20), (100, 4, 16, 30), (104, 38, 5, 5)], 1),
    ("안녕 batchim close", [(0, 0, 22, 22), (26, 0, 10, 40), (5, 41, 26, 16), (50, 0, 20, 24), (74, 0, 14, 40),
                          (52, 42, 26, 20)], 1),
    ("dot above a word", [(0, 0, 4, 4), (10, 30, 30, 30), (45, 32, 28, 28), (80, 30, 30, 30)], 1),
    ("tall stroke among small marks", [(0, 40, 20, 20), (25, 42, 20, 18), (60, 0, 40, 80), (110, 5, 40, 75)], 1),
    ("two written lines, close", [(0, 0, 30, 40), (35, 0, 30, 40), (70, 2, 28, 38), (0, 52, 30, 40),
                                  (35, 52, 30, 40), (70, 54, 28, 38)], 2),
    ("two lines, the second a short word", [(0, 0, 30, 40), (35, 0, 30, 40), (70, 2, 28, 38), (0, 56, 30, 22)], 2),
    ("second line wider than the first", [(20, 0, 30, 40), (0, 50, 30, 22), (40, 50, 60, 22)], 2),
]

fails = 0
for name, comps, want in CASES:
    got = group(comps)
    if got != want:
        fails += 1
        print(f"FAIL {name}: {got} line(s), want {want}")
print(f"{len(CASES) - fails}/{len(CASES)} passed")
sys.exit(1 if fails else 0)
