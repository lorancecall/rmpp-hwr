"""Compare recognition models on the same handwritten line images.

  bench.py <list.tsv> <out.json> <model> [<model> ...]
  list.tsv: image path (relative to the work dir) TAB ground truth TAB set name

Models:
  paddle-base        model/rec.onnx: PaddleOCR korean_PP-OCRv5_mobile_rec (the on-device model), height-48 input
  paddle-onnx=PATH   any exported rec.onnx (e.g. export/fix1/rec.onnx)
  trocr-ko           team-lucid/trocr-small-korean
  paddleocr-vl       PaddlePaddle/PaddleOCR-VL (0.9B)
  got-ocr2           stepfun-ai/GOT-OCR-2.0-hf
  qwen3-vl-4b / qwen3-vl-8b   Qwen/Qwen3-VL-{4B,8B}-Instruct

CER = edit distance / ground-truth length after NFC normalization and whitespace collapsing; also without spaces.
"""
import json
import math
import os
import re
import sys
import time
import unicodedata

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prep import finalize  # noqa: E402

W = os.environ.get("HWR_WORK", os.getcwd())
PROMPT = ("Transcribe the handwritten text in this image exactly as written. It may mix Korean, English, "
          "digits and symbols. Output only the text, nothing else.")


def norm(s):
    s = unicodedata.normalize("NFC", s)
    s = s.replace("```", " ").strip().strip('"“”\'')
    return re.sub(r"\s+", " ", s).strip()


def lev(a, b):
    d = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        p, d[0] = d[0], i
        for j, cb in enumerate(b, 1):
            p, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, p + (ca != cb))
    return d[-1]


def load_gray(p):
    return cv2.imread(os.path.join(W, p), cv2.IMREAD_GRAYSCALE)


def big_rgb(img):
    """Higher-resolution RGB for the large models (small 48-px images are upscaled to 96 px height)."""
    from PIL import Image
    if img.shape[0] < 64:
        img = cv2.resize(img, (img.shape[1] * 96 // img.shape[0], 96), interpolation=cv2.INTER_CUBIC)
    pad = cv2.copyMakeBorder(img, 8, 8, 16, 16, cv2.BORDER_CONSTANT, value=255)
    return Image.fromarray(cv2.cvtColor(pad, cv2.COLOR_GRAY2RGB))


# ---------------------------------------------------------------- models
def paddle_onnx(path):
    import onnxruntime as ort
    chars = [""] + [l.rstrip("\r\n") for l in open(f"{W}/data/dict.txt", encoding="utf-8").read().split("\n")[:11945]] + [" "]
    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    buckets = [160, 320, 640, 960, 1280, 1920]

    def rec(img):
        if img.shape[0] != 48:
            img = finalize(img, maxw=1920)
            if img is None:
                return ""
        tw = img.shape[1]; b = next((x for x in buckets if x >= tw), buckets[-1]); tw = min(tw, b)
        x = np.zeros((1, 3, 48, b), np.float32)
        x[:, :, :, :tw] = ((img[:, :tw].astype(np.float32) / 255 - .5) / .5)[None, None]
        p = sess.run(None, {sess.get_inputs()[0].name: x})[0][0]
        used = min(p.shape[0], math.ceil(p.shape[0] * tw / b)); s = ""; prev = 0
        for t in range(used):
            k = int(p[t].argmax())
            if k != 0 and k != prev:
                s += chars[k]
            prev = k
        return s
    return rec


def trocr_ko():
    import torch
    from transformers import AutoImageProcessor, AutoTokenizer, VisionEncoderDecoderModel
    m = "team-lucid/trocr-small-korean"
    model = VisionEncoderDecoderModel.from_pretrained(m).cuda().eval()
    ip, tok = AutoImageProcessor.from_pretrained(m), AutoTokenizer.from_pretrained(m)

    def rec(img):
        px = ip(big_rgb(img), return_tensors="pt").pixel_values.cuda()
        with torch.no_grad():
            ids = model.generate(px, max_new_tokens=64)
        return tok.batch_decode(ids, skip_special_tokens=True)[0]
    return rec


def paddleocr_vl():
    import torch
    from transformers import AutoModelForCausalLM, AutoProcessor
    m = "PaddlePaddle/PaddleOCR-VL"
    model = AutoModelForCausalLM.from_pretrained(m, trust_remote_code=True, torch_dtype=torch.bfloat16).cuda().eval()
    proc = AutoProcessor.from_pretrained(m, trust_remote_code=True)

    def rec(img):
        msgs = [{"role": "user", "content": [{"type": "image", "image": big_rgb(img)}, {"type": "text", "text": "OCR:"}]}]
        inp = proc.apply_chat_template(msgs, tokenize=True, add_generation_prompt=True, return_dict=True,
                                       return_tensors="pt").to("cuda")
        with torch.no_grad():
            out = model.generate(**inp, max_new_tokens=128)
        return proc.batch_decode(out[:, inp["input_ids"].shape[1]:], skip_special_tokens=True)[0]
    return rec


def got_ocr2():
    import torch
    from transformers import AutoModelForImageTextToText, AutoProcessor
    m = "stepfun-ai/GOT-OCR-2.0-hf"
    model = AutoModelForImageTextToText.from_pretrained(m, torch_dtype=torch.bfloat16).cuda().eval()
    proc = AutoProcessor.from_pretrained(m)

    def rec(img):
        inp = proc(big_rgb(img), return_tensors="pt").to("cuda")
        with torch.no_grad():
            out = model.generate(**inp, do_sample=False, tokenizer=proc.tokenizer, stop_strings="<|im_end|>",
                                 max_new_tokens=128)
        return proc.decode(out[0, inp["input_ids"].shape[1]:], skip_special_tokens=True)
    return rec


def qwen3_vl(size):
    import torch
    from transformers import AutoModelForImageTextToText, AutoProcessor
    m = f"Qwen/Qwen3-VL-{size}-Instruct"
    model = AutoModelForImageTextToText.from_pretrained(m, torch_dtype=torch.bfloat16).cuda().eval()
    proc = AutoProcessor.from_pretrained(m)

    def rec(img):
        msgs = [{"role": "user", "content": [{"type": "image", "image": big_rgb(img)}, {"type": "text", "text": PROMPT}]}]
        inp = proc.apply_chat_template(msgs, tokenize=True, add_generation_prompt=True, return_dict=True,
                                       return_tensors="pt").to("cuda")
        with torch.no_grad():
            out = model.generate(**inp, max_new_tokens=128, do_sample=False)
        return proc.batch_decode(out[:, inp["input_ids"].shape[1]:], skip_special_tokens=True)[0]
    return rec


def make(name):
    if name == "paddle-base":
        return paddle_onnx(f"{W}/pretrained/orig_rec.onnx")
    if name.startswith("paddle-onnx="):
        return paddle_onnx(os.path.join(W, name.split("=", 1)[1]))
    if name == "trocr-ko":
        return trocr_ko()
    if name == "paddleocr-vl":
        return paddleocr_vl()
    if name == "got-ocr2":
        return got_ocr2()
    if name.startswith("qwen3-vl-"):
        return qwen3_vl(name.rsplit("-", 1)[1].upper())
    raise SystemExit(f"unknown model {name}")


def main():
    lst, out, models = sys.argv[1], sys.argv[2], sys.argv[3:]
    rows = [l.rstrip("\n").split("\t") for l in open(lst, encoding="utf-8") if l.count("\t") >= 2]
    res = json.load(open(out, encoding="utf-8")) if os.path.exists(out) else {}
    for name in models:
        print(f"== {name}", flush=True)
        try:
            rec = make(name)
        except Exception as e:  # noqa: BLE001
            print(f"   could not load: {type(e).__name__}: {e}", flush=True)
            res[name] = {"error": f"{type(e).__name__}: {e}"}
            continue
        per, t0 = [], time.time()
        for p, gt, st in rows:
            try:
                hy = rec(load_gray(p))
            except Exception as e:  # noqa: BLE001
                hy = f"<error {type(e).__name__}>"
            per.append({"path": p, "set": st, "gt": gt, "hyp": norm(hy)})
        dt = (time.time() - t0) / max(len(rows), 1)
        sets = {}
        for st in sorted({r["set"] for r in per}):
            rs = [r for r in per if r["set"] == st]
            e = sum(lev(norm(r["gt"]), r["hyp"]) for r in rs); n = sum(len(norm(r["gt"])) for r in rs)
            e2 = sum(lev(norm(r["gt"]).replace(" ", ""), r["hyp"].replace(" ", "")) for r in rs)
            n2 = sum(len(norm(r["gt"]).replace(" ", "")) for r in rs)
            sets[st] = {"n": len(rs), "cer": round(e / n, 4), "cer_nospace": round(e2 / n2, 4)}
            print(f"   {st:16} n={len(rs):4}  CER {e / n:.3f}  (no spaces {e2 / n2:.3f})", flush=True)
        res[name] = {"sets": sets, "sec_per_line": round(dt, 3), "lines": per}
        json.dump(res, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        del rec
        try:
            import gc, torch
            gc.collect(); torch.cuda.empty_cache()
        except Exception:  # noqa: BLE001
            pass


if __name__ == "__main__":
    main()
