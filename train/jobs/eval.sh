#!/bin/bash
# usage: eval.sh <rec.onnx>   CER on every validation list (greedy CTC, same padding as the tablet)
set -eo pipefail
S=$HWR_WORK/code/scripts
for L in personal_val_5 real_val_5 mixed_7777 kor_7777 eng_7777 $( [ -f data/val/iamon_val_7777.txt ] && echo iamon_val_7777 ); do python $S/evalonnx.py ${1:?onnx} data/dict.txt data/val data/val/$L.txt; done
