"""Error breakdown of one model in logs/bench.json: substitutions / deletions / insertions, and which characters.

  errors.py logs/bench.json <model> [set ...]
"""
import collections
import json
import sys

sys.stdout.reconfigure(encoding="utf-8")


def align(a, b):
    """Levenshtein alignment -> list of (op, gt_char, hyp_char)."""
    n, m = len(a), len(b)
    d = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n + 1): d[i][0] = i
    for j in range(m + 1): d[0][j] = j
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] != b[j - 1]))
    ops, i, j = [], n, m
    while i or j:
        if i and j and d[i][j] == d[i - 1][j - 1] + (a[i - 1] != b[j - 1]):
            if a[i - 1] != b[j - 1]: ops.append(("sub", a[i - 1], b[j - 1]))
            i, j = i - 1, j - 1
        elif i and d[i][j] == d[i - 1][j] + 1:
            ops.append(("del", a[i - 1], "")); i -= 1
        else:
            ops.append(("ins", "", b[j - 1])); j -= 1
    return ops[::-1]


d = json.load(open(sys.argv[1], encoding="utf-8"))
model, sets = sys.argv[2], set(sys.argv[3:])
kinds, dels, ins, subs, dbl = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter(), 0
examples = []
for l in d[model]["lines"]:
    if sets and l["set"] not in sets: continue
    gt, hy = " ".join(l["gt"].split()), l["hyp"]
    ops = align(gt, hy)
    for op, g, h in ops:
        kinds[op] += 1
        if op == "del": dels[g] += 1
        if op == "ins": ins[h] += 1
        if op == "sub": subs[f"{g}->{h}"] += 1
    # deletions of a character that is repeated in the ground truth (CTC merges repeats without a blank)
    for k in range(1, len(gt)):
        if gt[k] == gt[k - 1] and gt[k] != " " and gt.count(gt[k] * 2) > hy.count(gt[k] * 2):
            dbl += 1
    if ops and len(examples) < 12:
        examples.append((gt, hy, [o for o in ops if o[0] != "sub" or True][:4]))
print(model, dict(kinds), "| doubled letters merged:", dbl)
print("deleted :", dels.most_common(12))
print("inserted:", ins.most_common(12))
print("subst.  :", subs.most_common(12))
for gt, hy, ops in examples:
    print(f"  GT {gt}\n  HY {hy}\n     {ops}")
