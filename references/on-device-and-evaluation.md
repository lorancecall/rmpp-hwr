# Lightweight on-device HTR architectures and evaluation practice

Last updated: 2026-11-10

Scope: what keeps the model small/fast on the Paper Pro CPU (onnxruntime, ~0.4 s/line, ~13 MB), and how to evaluate reliably with tiny test sets. Notes marked **[our inference]** are our own reasoning.

---

## Architectures

### PP-OCRv5 (mobile) and PP-OCRv3 SVTR-LCNet
- PP-OCRv5 paper: Cheng Cui et al., 2026, arXiv https://arxiv.org/abs/2603.24373 (listed as CVPR 2026 poster). PaddleOCR 3.0 report: https://arxiv.org/abs/2507.05595. PP-OCRv3: https://arxiv.org/abs/2206.03001.
- Key idea: A ~5M-parameter two-stage OCR pipeline (mobile variant) whose quality comes mainly from data curation; the recognizer is SVTR-LCNet style (PPLCNetV3 CNN + small SVTR neck + CTC).
- Relevance: Our current model is already in the right size/speed class for the device. Architecture change is not the bottleneck; data and fine-tuning correctness are.

### Rethinking Text Line Recognition Models
- Authors: Daniel Hernandez Diaz, Siyang Qin, Reeve Ingle, Yasuhisa Fujii, Alessandro Bissacco
- Year / venue: 2021, arXiv (Google Research)
- Link: https://arxiv.org/abs/2104.07787
- Key idea / evidence: Across scene text and handwriting, self-attention encoder + CTC + external LM was the best accuracy/cost trade-off, and CTC handles arbitrary line length better than autoregressive transformer decoders that degrade on long lines.
- Relevance: Keep CTC for on-device inference. The NRTR head is training-only, consistent with this. Adding a lightweight external LM (character n-gram) in CTC beam search is a plausible cheap gain.

### Are Multidimensional Recurrent Layers Really Necessary for Handwritten Text Recognition?
- Author: Joan Puigcerver
- Year / venue: 2017, ICDAR, pp. 67-72
- Link: https://doi.org/10.1109/ICDAR.2017.20 (reproduction: PyLaia / Laia)
- Key idea: CNN + 1D BiLSTM + CTC matches or beats MDLSTM models for line HTR, with simpler and faster computation.
- Relevance: The classic strong baseline for line HTR (PyLaia). Useful as a from-scratch comparison model trained only on our data + synthetic, to sanity-check whether the PP-OCR fine-tune behaves sensibly.

### Best Practices for a Handwritten Text Recognition System
- Authors: George Retsinas, Giorgos Sfikas, Basilis Gatos, Christophoros Nikou
- Year / venue: DAS 2022 (arXiv version 2024)
- Link: https://arxiv.org/abs/2404.11339 (code: https://github.com/georgeretsi/HTR-best-practices)
- Key idea: Simple, effective choices for CNN-BiLSTM-CTC line HTR: preserve aspect ratio with padding, column-wise max-pooling before the recurrent head, and an auxiliary "CTC shortcut" (a 1D conv + CTC branch used only in training).
- Evidence: Combining the modifications gave the best IAM line results; the CTC shortcut gave the largest gain and is dropped at inference.
- Relevance: The CTC shortcut is analogous in spirit to PP-OCR's auxiliary head (training-only guidance). The aspect-preserving padding practice matches ours. **[our inference]** If we ever train a small model from scratch, this paper is the recipe.

### Fast Multi-language LSTM-based Online Handwriting Recognition
- See `online-vs-offline.md` (Carbune et al., IJDAR 2020). Relevant as the reference design for a small, fast on-device *online* recognizer.

### On-device inference notes (**[our inference]**, not from a paper)
- Width bucketing (160..1920) is good for onnxruntime; keep training-time padding consistent with inference padding (same normalization so padding value = 0 after normalization, same buckets), especially because the SVTR neck mixes globally across width.
- Post-training dynamic/static INT8 quantization in onnxruntime can cut latency on ARM CPUs, but verify CER on the held-out set after quantization; convolution-heavy models usually need static quantization with a calibration set drawn from *real* rendered lines.
- Any weight change (fine-tune, LoRA merge, WiSE-FT interpolation) must be re-exported and re-validated on device; check parity between training framework outputs and ONNX outputs on the same images (max abs diff of logits).

---

## Evaluation practice

### Bootstrap Estimates for Confidence Intervals in ASR Performance Evaluation
- Authors: Maximilian Bisani, Hermann Ney
- Year / venue: 2004, ICASSP, vol. 1, pp. 409-412
- Link: https://doi.org/10.1109/ICASSP.2004.1326009
- Key idea: Resample test utterances with replacement to estimate confidence intervals of error rates, and use paired bootstrap to decide whether system A is really better than B.
- Relevance: With 12 eval lines, a CER of 0.257 has a very wide confidence interval. **[our inference]** Compute line-level bootstrap CIs (resample lines; recompute total edit distance / total reference chars) and paired-bootstrap "probability of improvement" between models. Note the later caveat (Liu et al., arXiv 1912.09508, blockwise bootstrap) that correlated samples (e.g. lines from the same page/session) make plain bootstrap too optimistic; resample by page/session if lines are grouped.

### RanPil benchmark (context for expected CER)
- See `korean-hangul.md`. TrOCR on a 1,000-page writer-disjoint Korean test set: 10.93% CER overall, up to 28.5% for least legible writing. Useful as a sanity range, not directly comparable (different writers, scanned pages).

### Practical evaluation protocol for rmpp-hwr (**[our inference]**)
1. **Grow the held-out set first**: aim for 50-100+ lines, written on different days/sessions, with natural content (Korean, English, mixed, digits). Freeze it, version it, never train on it (also never pseudo-label it).
2. **Split by session/page, not by random line**, so near-duplicate lines from one sitting do not leak across train and test. Never put the same sentence text in both sets (models memorize text).
3. **Report**: CER (total edits / total reference chars, not mean of per-line CER), CER without spaces, and per-script CER (Hangul vs Latin/digits), plus bootstrap 95% CI.
4. **Also track forgetting**: a small fixed set of printed/rendered-font lines and some synthetic lines; a fine-tune should not collapse there.
5. **Always include the base model** as a row in every comparison, evaluated through the *same* code path, so pipeline bugs show up as a base-model mismatch.
6. **Training-set CER is a diagnostic**: if a fine-tuned model is worse than the base model on its own training lines (as currently observed), suspect the pipeline (BN stats, precision, preprocessing mismatch, label encoding/filtering) before suspecting data.
7. **Normalize text consistently** (Unicode NFC for Hangul; decide on full-width vs half-width punctuation) in both references and predictions before computing CER; NFD vs NFC mismatches silently inflate Korean CER.
