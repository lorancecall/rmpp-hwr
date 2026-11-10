#!/bin/bash
# One-time setup of the work dir (inside the activated Python env): PaddleOCR at a pinned commit + our patch,
# Python packages, base model weights and dict. Safe to re-run.
set -eo pipefail
W=$HWR_WORK; cd "$W"
PADDLEOCR_COMMIT=${PADDLEOCR_COMMIT:-dab3fe3}
if [ ! -d PaddleOCR ]; then git clone -q https://github.com/PaddlePaddle/PaddleOCR.git; fi
git -C PaddleOCR checkout -q "$PADDLEOCR_COMMIT"
git -C PaddleOCR apply --check code/patches/paddleocr-rmpp.patch 2>/dev/null && git -C PaddleOCR apply code/patches/paddleocr-rmpp.patch || echo "[setup] patch already applied"
pip install -q -r PaddleOCR/requirements.txt -r code/requirements.txt
mkdir -p pretrained data logs ckpt export
cp PaddleOCR/ppocr/utils/dict/ppocrv5_korean_dict.txt data/dict.txt
[ -f pretrained/korean.pdparams ] || { echo "[setup] put korean_PP-OCRv5_mobile_rec pretrained weights at pretrained/korean.pdparams"; }
python -c "import paddle; paddle.utils.run_check()" 2>&1 | tail -n 2
# Reference CER of the base model on the 12 personal eval lines: export.sh promotes only models that beat it.
if [ -f pretrained/orig_rec.onnx ] && [ -f data/val/personal_val_5.txt ] && [ ! -f base_cer.txt ]; then
  python code/scripts/evalonnx.py pretrained/orig_rec.onnx data/dict.txt data/val data/val/personal_val_5.txt | grep -o "CER [0-9.]*" | head -n 1 | cut -d" " -f2 > base_cer.txt
fi
