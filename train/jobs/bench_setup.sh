#!/bin/bash
# One-time: a separate Python env for model comparison (torch + transformers; not the PaddlePaddle env)
# and the candidate models in <work>/hf. Needs internet (run where downloads work, e.g. a login node).
set -eo pipefail
cd "${HWR_WORK:?}"
export HF_HOME=$HWR_WORK/hf UV_CACHE_DIR=$HWR_WORK/uv-cache
[ -d bench-env ] || uv venv -q bench-env --python 3.11
. bench-env/bin/activate
uv pip install -q torch torchvision --index-url https://download.pytorch.org/whl/cu124
uv pip install -q "transformers>=4.57" accelerate pillow opencv-python-headless onnxruntime numpy einops sentencepiece protobuf "huggingface_hub[cli]"
python -c "import torch, transformers; print('torch', torch.__version__, 'transformers', transformers.__version__)"
hf download team-lucid/trocr-small-korean --exclude "*.msgpack" > /dev/null && echo "downloaded trocr-small-korean"
hf download PaddlePaddle/PaddleOCR-VL --exclude "PP-DocLayoutV2/*" > /dev/null && echo "downloaded PaddleOCR-VL"
for m in stepfun-ai/GOT-OCR-2.0-hf Qwen/Qwen3-VL-4B-Instruct Qwen/Qwen3-VL-8B-Instruct; do
  hf download "$m" > /dev/null && echo "downloaded $m"
done
du -sh hf
