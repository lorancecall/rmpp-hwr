#!/bin/bash
# usage: gen_personal.sh <name>
# data/train/list_<name>.txt = augmented personal lines (all but the 12 eval lines of data/personal/dataset.tsv) + replay
# of the synthetic set (REPLAY lines, default 40000) and of real English lines (REPLAY_EN, default 20000) so the
# model does not forget them.
set -eo pipefail
S=$HWR_WORK/code/scripts; name=${1:?name}
ne=$( [ -f data/personal/eval.txt ] && wc -l < data/personal/eval.txt || echo 12 )
n=$(( ($(wc -l < data/personal/dataset.tsv) - ne) * ${PERS_X:-200} )); seed=$((RANDOM + 10))
python $S/gen.py personal $n $seed data/train/pers_$name
# the held-out personal lines as the validation list used by export.sh / eval.sh
python $S/gen.py personal 12 5 data/val --val
sed "s#^#pers_$name/#" data/train/pers_$name/personal_$seed.txt > data/train/pers_$name.list
shuf -n ${REPLAY:-40000} data/train/train_list.txt > data/train/replay_$name.list
# real English handwriting (IAM-OnDB, gen_iamon.sh) when available: keeps English from being forgotten
[ -f data/train/replay_extra.list ] && shuf -n ${REPLAY_EN:-20000} data/train/replay_extra.list >> data/train/replay_$name.list
cat data/train/pers_$name.list data/train/replay_$name.list > data/train/list_$name.txt
wc -l data/train/list_$name.txt
