#!/bin/bash
# English handwriting from IAM-OnDB strokes (download it yourself into data/iamondb, see train/README.md).
# Writes data/train/iamon_1.txt (+ val list) and appends it to data/train/replay_extra.list for gen_personal.sh.
set -eo pipefail
S=$HWR_WORK/code/scripts
[ -d data/iamondb/lineStrokes ] || { echo "data/iamondb/lineStrokes missing"; exit 1; }
python $S/gen.py iamon 600 7777 data/val --val
python $S/gen.py iamon ${N:-60000} 1 data/train
cp data/train/iamon_1.txt data/train/replay_extra.list
wc -l data/train/replay_extra.list data/val/iamon_val_7777.txt
