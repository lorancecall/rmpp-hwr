#!/bin/bash
# usage: bench.sh [model ...]   compare models on the personal lines and the stroke-dataset lines
# (needs bench_setup.sh once). Writes logs/bench.json (per-line outputs) and prints CER per set.
set -eo pipefail
W=$HWR_WORK; S=$W/code/scripts
export HF_HOME=$W/hf HF_HUB_OFFLINE=1
. $W/bench-env/bin/activate
# list: personal lines (eval lines marked separately) + other writers (stroke dataset, validation split)
python - <<'P'
import os
W = os.environ["HWR_WORK"]
held = set(open(f"{W}/data/personal/eval.txt", encoding="utf-8").read().split())
out = []
for l in open(f"{W}/data/personal/dataset.tsv", encoding="utf-8").read().splitlines():
    p, t = l.split("\t")
    out.append(f"data/personal/{p}\t{t}\t{'personal-eval12' if p in held else 'personal-other'}")
for l in open(f"{W}/data/val/real_val_5.txt", encoding="utf-8").read().splitlines():
    p, t = l.split("\t"); out.append(f"data/val/{p}\t{t}\tother-writers")
open(f"{W}/logs/bench_list.tsv", "w", encoding="utf-8").write("\n".join(out) + "\n")
print(len(out), "lines")
P
python $S/bench.py logs/bench_list.tsv logs/bench.json ${*:-paddle-base trocr-ko paddleocr-vl got-ocr2 qwen3-vl-4b qwen3-vl-8b}
