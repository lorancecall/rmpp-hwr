#!/bin/bash
# Synthetic training/validation lines (fonts, public stroke set, printed). Needs data/corpus_*.txt, fonts/, recordings.parquet.
set -eo pipefail
S=$HWR_WORK/code/scripts; O=data/train
[ -f data/corpus_ko.txt ] || python $S/prep_text.py data
python $S/gen.py real 239 5 data/val --val
python $S/gen.py personal 12 5 data/val --val
python $S/gen.py mixed 2000 7777 data/val
python $S/gen.py kor 1000 7777 data/val
python $S/gen.py eng 600 7777 data/val
python $S/gen.py real 100000 1 $O
python $S/gen.py personal 6000 1 $O
python $S/gen.py mixed 140000 1 $O
python $S/gen.py kor 70000 1 $O
python $S/gen.py eng 40000 1 $O
python $S/gen.py print 40000 1 $O
