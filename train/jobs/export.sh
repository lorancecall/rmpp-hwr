#!/bin/bash
# usage: export.sh <name> [ckpt prefix (default ckpt/<name>/latest)]
# Paddle checkpoint -> ONNX (dynamic width, input 'x') -> export/<name>/rec.onnx, then judge on the 12 personal eval
# lines: best.txt is promoted only when the CER improves. Writes export/<name>/summary.txt and DONE.
set -eo pipefail
W=$HWR_WORK; S=$W/code/scripts; name=${1:?name}; ck=${2:-$W/ckpt/$name/latest}
OUT=$W/export/$name; mkdir -p $OUT
cfg=$W/cfg_$name.yml; [ -f "$cfg" ] || python $S/make_cfg.py 1 0.0001 "$cfg"
(cd PaddleOCR && python tools/export_model.py -c $cfg -o Global.pretrained_model=$ck Global.save_inference_dir=$OUT Global.use_amp=False 2>&1 | tail -n 3)
paddle2onnx --model_dir $OUT --model_filename inference.json --params_filename inference.pdiparams --save_file $OUT/raw.onnx --opset_version 14 2>&1 | tail -n 2
python $S/fix_onnx.py $OUT/raw.onnx $OUT/rec.onnx
cp data/dict.txt $OUT/dict.txt
CER=$(python $S/evalonnx.py $OUT/rec.onnx data/dict.txt data/val data/val/personal_val_5.txt | grep -o "CER [0-9.]*" | head -n 1 | cut -d" " -f2)
echo $CER > $OUT/personal_cer.txt
BEST=$(cat best_cer.txt 2>/dev/null || cat base_cer.txt 2>/dev/null || echo 9)
if python -c "import sys; sys.exit(0 if float('$CER') < float('$BEST') else 1)"; then echo $ck > best.txt; echo $CER > best_cer.txt; P=promoted; else P=kept-old-best; fi
{ echo "personal12 CER $CER (best before $BEST) -> $P"; for L in real_val_5 mixed_7777 kor_7777 eng_7777 $( [ -f data/val/iamon_val_7777.txt ] && echo iamon_val_7777 ); do python $S/evalonnx.py $OUT/rec.onnx data/dict.txt data/val data/val/$L.txt; done; } > $OUT/summary.txt
cat $OUT/summary.txt; touch $OUT/DONE
