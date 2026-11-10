#!/bin/bash
# usage: wise.sh <name> [alphas, default "0.2 0.4 0.6 0.8"]
# WiSE-FT sweep: blend base and ckpt/<name>/latest at each alpha, export + judge each as <name>-w<alpha>.
# export.sh promotes a blend to best.txt only if it beats the current best on the personal eval lines.
set -eo pipefail
W=$HWR_WORK; S=$W/code/scripts; name=${1:?name}; shift || true
for a in ${*:-0.2 0.4 0.6 0.8}; do
  mkdir -p ckpt/$name-w$a
  python $S/wise_ft.py $W/pretrained/korean $W/ckpt/$name/latest $a $W/ckpt/$name-w$a/latest
  cp cfg_$name.yml cfg_$name-w$a.yml
  bash $W/code/jobs/export.sh $name-w$a $W/ckpt/$name-w$a/latest
done
