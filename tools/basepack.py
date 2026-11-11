#!/usr/bin/env python3
"""Build a base language pack from an official PaddleOCR PP-OCRv5 recognition model.

    python tools/basepack.py zh            # -> build/packs/rmpp-hwr-chinese-base/
    python tools/basepack.py ja            # -> build/packs/rmpp-hwr-japanese-base/
    python tools/basepack.py zh --install  # also copy it to the tablet (models/<id>/), no restart needed

A pack is a folder models/<id>/ with rec.onnx, dict.txt, model.txt (display name) and pack.json {"language": ...};
Settings > Handwriting > Language lists it under its language.

Needs a Python environment with paddlepaddle>=3.1, paddle2onnx>=2.1, onnx, pyyaml (not the one that runs hwr):
    uv venv -p 3.11 .p5 && uv pip install -p .p5 paddlepaddle==3.1.0 paddle2onnx onnx pyyaml setuptools "numpy<2"
and is run with that interpreter. The model is downloaded from PaddlePaddle's official inference-model bucket
(Apache License 2.0, see NOTICE); its own inference.yml supplies the character list.
"""
import argparse
import json
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
BUCKET = "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/"
# PP-OCRv5_mobile_rec covers Simplified and Traditional Chinese, Japanese and English (and pinyin) in one model,
# including handwriting; the same model serves the zh and ja packs.
PACKS = {
    "zh": ("rmpp-hwr-chinese-base", "PP-OCRv5_mobile_rec", "PaddleOCR PP-OCRv5 mobile: Chinese, Japanese, English"),
    "ja": ("rmpp-hwr-japanese-base", "PP-OCRv5_mobile_rec", "PaddleOCR PP-OCRv5 mobile: Japanese, Chinese, English"),
}
TABLET = "rmpp"
MODELS = "/home/root/.local/share/rmpp-hwr/models"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("language", choices=sorted(PACKS))
    ap.add_argument("--install", action="store_true", help="copy the pack to the tablet")
    ap.add_argument("--tablet", default=TABLET, help="ssh host of the tablet (default rmpp)")
    a = ap.parse_args()
    pack_id, model, title = PACKS[a.language]
    work = ROOT / "build" / "packs"
    out = work / pack_id
    out.mkdir(parents=True, exist_ok=True)

    src = work / f"{model}_infer"
    if not src.exists():
        tar = work / f"{model}_infer.tar"
        print(f"downloading {model}")
        urllib.request.urlretrieve(f"{BUCKET}{model}_infer.tar", tar)
        with tarfile.open(tar) as t:
            t.extractall(work, filter="data")
        tar.unlink()

    chars = [str(c) for c in yaml.safe_load((src / "inference.yml").read_text(encoding="utf-8"))["PostProcess"]["character_dict"]]
    (out / "dict.txt").write_text("\n".join(chars) + "\n", encoding="utf-8", newline="\n")

    raw = work / f"{model}.raw.onnx"
    subprocess.run([str(Path(sys.executable).with_name("paddle2onnx")), "--model_dir", str(src),
                    "--model_filename", "inference.json", "--params_filename", "inference.pdiparams",
                    "--save_file", str(raw), "--opset_version", "14"], check=True)
    # same input/output names and dynamic dims as the Korean model (N, 3, 48, W) -> (N, T, C)
    subprocess.run([sys.executable, str(ROOT / "train" / "scripts" / "fix_onnx.py"), str(raw), str(out / "rec.onnx")], check=True)
    raw.unlink()

    (out / "model.txt").write_text(f"{pack_id} ({title})\n", encoding="utf-8", newline="\n")
    (out / "pack.json").write_text(json.dumps({"language": a.language}) + "\n", encoding="utf-8", newline="\n")
    print(f"pack: {out} ({len(chars)} characters)")

    if a.install:
        subprocess.run(["ssh", a.tablet, f"mkdir -p {MODELS}/{pack_id}"], check=True)
        subprocess.run(["scp", "-q", *[str(out / f) for f in ("rec.onnx", "dict.txt", "model.txt", "pack.json")],
                        f"{a.tablet}:{MODELS}/{pack_id}/"], check=True)
        print(f"installed on {a.tablet}: {MODELS}/{pack_id}/")


if __name__ == "__main__":
    main()
