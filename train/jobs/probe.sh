#!/bin/bash
# usage: probe.sh [variant...]   variant = name:width:amp(0|1):amp_dtype:freeze_bn(0|1), e.g. A:960:1:bfloat16:0
# Sanity check of the TRAINING-mode forward pass: base weights, lr ~0, one short epoch (PROBE_LINES lines,
# default 6400 = 100 steps of 64), prints acc/CTC loss. A healthy setup shows non-zero acc and a low CTC loss
# from the first steps. The epoch ends by itself (killing paddle mid-run can hang the job).
set -eo pipefail
W=$HWR_WORK; S=$W/code/scripts
[ $# -gt 0 ] || set -- B:960:0:float16:0 C:960:0:float16:1 D:320:0:float16:0 E:960:1:float16:0
shuf -n ${PROBE_LINES:-6400} --random-source=<(yes) data/train/${LIST:-train_list.txt} > data/train/probe_list.txt
for v in "$@"; do
  IFS=: read name width amp dt fbn <<< "$v"
  RUN=probe_$name WIDTH=$width HWR_AMP=$amp HWR_AMP_DTYPE=$dt HWR_AMP_LEVEL=O2 LIST=probe_list.txt EVAL_EVERY=1000000000 \
    python $S/make_cfg.py 1 1e-9 probe_$name.yml > /dev/null
  python - probe_$name.yml <<'P'
import sys, yaml
c = yaml.safe_load(open(sys.argv[1])); c["Optimizer"]["lr"] = {"name": "Const", "learning_rate": 1e-9}
c["Global"]["print_batch_step"] = 10; c["Global"]["save_epoch_step"] = 1000
yaml.safe_dump(c, open(sys.argv[1], "w"), allow_unicode=True)
P
  echo "=== PROBE $name width=$width amp=$amp/$dt freeze_bn=$fbn"
  (cd PaddleOCR && FREEZE_BN=$fbn python tools/train.py -c $W/probe_$name.yml > $W/logs/probe_$name.txt 2>&1) || echo "(exit $?)"
  grep -E "global_step: (10|20|30|50|70|100)," $W/logs/probe_$name.txt \
    | sed -E "s/.*global_step: ([0-9]+).*acc: ([0-9.]+).*CTCLoss: ([0-9.]+), NRTRLoss: ([0-9.]+).*/step \1 acc \2 ctc \3 nrtr \4/" \
    || tail -n 5 $W/logs/probe_$name.txt
  rm -rf $W/ckpt/probe_$name $W/probe_$name.yml
done
