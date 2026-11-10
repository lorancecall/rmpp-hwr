#!/bin/bash
# usage: train.sh <name> [list (default list_<name>.txt)]
# Fine-tune from best.txt (or the base weights when there is none, or FROM=base). Env: EPOCHS, LR, FROM, WIDTH, FREEZE_BN, HWR_AMP*.
set -eo pipefail
S=$HWR_WORK/code/scripts; name=${1:?name}; list=${2:-list_$name.txt}
if [ "${FROM:-best}" = base ]; then PRE=$HWR_WORK/pretrained/korean; else PRE=$(cat best.txt 2>/dev/null || echo "$HWR_WORK/pretrained/korean"); fi
RUN=$name LIST=$list PRE=$PRE EVAL_EVERY=${EVAL_EVERY:-100000} python $S/make_cfg.py ${EPOCHS:-2} ${LR:-0.00005} cfg_$name.yml
cd PaddleOCR
python tools/train.py -c $HWR_WORK/cfg_$name.yml
