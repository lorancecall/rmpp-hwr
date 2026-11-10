# Fine-tuning a pretrained CTC/SVTR recognizer on small data

Last updated: 2026-11-10

Scope: how to adapt `korean_PP-OCRv5_mobile_rec` (PPLCNetV3 + SVTR neck, CTC head, NRTR auxiliary head) to ~100 real handwritten lines without destroying what it already knows. Entries are ordered by how directly actionable they are for rmpp-hwr.

Notes marked **[our inference]** are reasoning applied to our setup, not results reported by the cited paper.

---

## Fine-tuning Is a Surprisingly Effective Domain Adaptation Baseline in Handwriting Recognition
- Authors: Jan Kohút, Michal Hradiš
- Year / venue: 2023 (arXiv v1), revised 2025 (v2); arXiv preprint
- Link: https://arxiv.org/abs/2302.06308
- Key idea: For CTC-trained line recognizers, plain fine-tuning (with data augmentation) of a model pretrained on a large general dataset onto a small specialized one is a strong adaptation baseline and resists overfitting even with very few target lines.
- Evidence: On a large real-world dataset, fine-tuning on new writers gave about 25% average relative CER improvement with 16 lines and about 50% with 256 lines.
- Relevance to rmpp-hwr: This is the strongest external signal that our results (CER 0.257 -> 0.40-0.58 after fine-tuning, and worse even on training lines) are not "what fine-tuning on 100 lines does", but a pipeline bug. With ~100 lines we should expect a sizeable *improvement*. Use this as the expected-outcome baseline: if a correctly implemented, conservative fine-tune does not beat 0.257 on held-out lines, debug before trying anything fancier.

## Practical Fine-Tuning of Autoregressive Models on Limited Handwritten Texts
- Authors: Jan Kohút, Michal Hradiš
- Year / venue: 2025, arXiv (submitted to ICDAR 2025)
- Link: https://arxiv.org/abs/2503.19546
- Key idea: Studies fine-tuning of encoder-decoder (autoregressive) HTR models with very few lines: which part (encoder vs decoder) matters, how to choose stopping criteria without a big validation set, and how to choose which lines to annotate.
- Evidence: About 10% relative CER improvement from 16 lines, 40% from 256 lines; confidence-based selection of lines to annotate reached the same performance with roughly half the annotations.
- Relevance: (1) Stopping criteria with tiny validation sets are exactly our problem: use their trend-based early stopping ideas instead of a fixed epoch count. (2) Confidence-based selection maps directly onto the user-correction loop: prioritize asking the user to verify/correct lines where the CTC posterior confidence is lowest.

## Low-Rank Adaptation vs. Fine-Tuning for Handwritten Text Recognition
- Authors: Lukas Hüttner, Martin Mayr, Thomas Gorges, Fei Wu, Mathias Seuret, Andreas Maier, Vincent Christlein
- Year / venue: 2025, WACV 2025 Workshops (VisionDocs)
- Link: https://openaccess.thecvf.com/content/WACV2025W/VISIONDOCS/html/Huttner_Low-Rank_Adaptation_vs._Fine-Tuning_for_Handwritten_Text_Recognition_WACVW_2025_paper.html
- Key idea: Compares LoRA / DoRA against full fine-tuning of TrOCR for HTR, including few-shot regimes.
- Evidence: LoRA/DoRA did not outperform full fine-tuning and was not substantially faster for TrOCR; high rank (256) roughly matched full fine-tuning on IAM (per supplementary material).
- Relevance: LoRA is not a magic fix for forgetting/overfitting in HTR. For our 13 MB mobile model (mostly convolutions in PPLCNetV3), LoRA is also awkward to apply and would have to be merged before ONNX export anyway. **[our inference]** Prefer full fine-tuning with a low LR + replay data; consider LoRA/adapters only if we want per-user deltas stored separately from the base weights.

## LoRA: Low-Rank Adaptation of Large Language Models
- Authors: Edward J. Hu, Yelong Shen, Phillip Wallis, Zeyuan Allen-Zhu, Yuanzhi Li, Shean Wang, Lu Wang, Weizhu Chen
- Year / venue: 2022, ICLR (arXiv 2021)
- Link: https://arxiv.org/abs/2106.09685
- Key idea: Freeze pretrained weights and learn a low-rank additive update per linear layer; the update can be merged into the weights for zero inference overhead.
- Evidence: On-par-or-better quality than full fine-tuning on several NLP models with far fewer trainable parameters.
- Relevance: Reference for the mechanism only. Could be applied to the SVTR neck's attention/MLP linears and the CTC FC layer. Merge before ONNX export so on-device cost is unchanged.

## Fine-Tuning can Distort Pretrained Features and Underperform Out-of-Distribution (LP-FT)
- Authors: Ananya Kumar, Aditi Raghunathan, Robbie Jones, Tengyu Ma, Percy Liang
- Year / venue: 2022, ICLR (oral)
- Link: https://arxiv.org/abs/2202.10054
- Key idea: Full fine-tuning with a poorly matched head distorts good pretrained features. Training the head first (linear probe), then fine-tuning everything (LP-FT), keeps features closer to the pretrained ones.
- Evidence: Across ten distribution-shift datasets, LP-FT was about 1% better in-distribution and about 10% better out-of-distribution than full fine-tuning.
- Relevance: Our CTC head is already trained and matches the dict, so the "random head" failure mode does not apply directly. But the principle applies if we ever change the dictionary or re-init the head: train the head (and maybe the neck) first with the backbone frozen, then unfreeze with a smaller LR. **[our inference]** A staged schedule (head+neck first, backbone later) is a cheap experiment.

## Robust Fine-Tuning of Zero-Shot Models (WiSE-FT)
- Authors: Mitchell Wortsman, Gabriel Ilharco, Jong Wook Kim, Mike Li, Simon Kornblith, Rebecca Roelofs, Raphael Gontijo-Lopes, Hannaneh Hajishirzi, Ali Farhadi, Hongseok Namkoong, Ludwig Schmidt
- Year / venue: 2022, CVPR
- Link: https://arxiv.org/abs/2109.01903
- Key idea: After fine-tuning, linearly interpolate weights between the original and fine-tuned models (weight-space ensembling). Gives most of the target-domain gain while keeping robustness of the original.
- Evidence: Large gains under distribution shift on ImageNet variants for CLIP and other pretrained models, at no extra inference cost.
- Relevance: Very cheap safety net for us: export `theta = (1-a) * theta_base + a * theta_ft` for a in {0.25, 0.5, 0.75, 1.0} and pick a on the held-out set. Note BN running statistics must be interpolated too (or recomputed). **[our inference]** If interpolation at small `a` already beats the base, the fine-tune direction is useful; if every `a` is worse, the fine-tuned weights are broken (points to a bug).

## Overcoming catastrophic forgetting in neural networks (EWC)
- Authors: James Kirkpatrick, Razvan Pascanu, Neil Rabinowitz, et al.
- Year / venue: 2017, PNAS
- Link: https://arxiv.org/abs/1612.00796
- Key idea: Add a quadratic penalty that keeps weights important for the old task (estimated by Fisher information) close to their old values.
- Relevance: A principled regularizer, but we do not have the original training set to estimate Fisher well. **[our inference]** A simpler L2-to-pretrained-weights penalty ("L2-SP"-style) plus replay of general-domain data is easier and probably sufficient.

## Learning without Forgetting (LwF)
- Authors: Zhizhong Li, Derek Hoiem
- Year / venue: ECCV 2016; journal version in IEEE TPAMI
- Link: https://arxiv.org/abs/1606.09282
- Key idea: When training on new data, also match the old model's outputs (distillation) on the new inputs, preserving old behavior without old data.
- Relevance: **[our inference]** For CTC we can distill per-frame posteriors of the frozen base model (KL on softmax outputs) on printed/synthetic/replay images, while training with real labels on handwriting. Useful to keep printed-text and English performance from regressing.

## Boosting offline handwritten text recognition in historical documents with few labeled lines
- Authors: José Carlos Aradillas, Juan José Murillo-Fuentes, Pablo M. Olmos
- Year / venue: 2021, IEEE Access vol. 9 (arXiv 2020)
- Link: https://arxiv.org/abs/2012.02544
- Key idea: Transfer learning from a large HTR dataset to a small one; analyzes which layers to fine-tune, combines transfer with augmentation, and proposes a way to reduce the impact of wrong labels.
- Evidence: CER reductions up to about 6% (in some cases) on ICFHR 2018 READ, Washington and Parzival.
- Relevance: Supports layer-wise experiments (freeze early conv blocks, tune later blocks + neck + head). Their noisy-label handling matters for us once user-corrected lines (which may contain mistakes) enter training.

## Revisiting Batch Normalization For Practical Domain Adaptation (AdaBN)
- Authors: Yanghao Li, Naiyan Wang, Jianping Shi, Jiaying Liu, Xiaodi Hou
- Year / venue: 2016, arXiv (ICLR 2017 submission)
- Link: https://arxiv.org/abs/1603.04779
- Key idea: Domain shift is partly captured by BN statistics; re-estimating BN mean/variance on target-domain data (no labels, no gradients) adapts a model.
- Relevance: Two uses for us. (1) A zero-training baseline: recompute BN running stats of the base model on our real handwriting images (rendered exactly as at inference) and measure CER. (2) A warning: BN statistics are fragile. **[our inference]** If training batches are 960-wide images that are mostly zero padding, the batch statistics during training are dominated by padding, and the running stats drift away from what inference sees. This is a plausible cause of "worse even on the training data". Safer: keep BN in eval mode (frozen running stats, `use_global_stats`) during fine-tuning, or train at widths close to the content width as the official recipe does.

## PP-OCRv3: More Attempts for the Improvement of Ultra Lightweight OCR System
- Authors: Chenxia Li, Weiwei Liu, Ruoyu Guo, et al. (Baidu)
- Year / venue: 2022, arXiv
- Link: https://arxiv.org/abs/2206.03001
- Key idea: Introduces SVTR-LCNet (the lineage of our model), attention-guided CTC training (an attention branch used only during training; the GTC/NRTR auxiliary head in later versions), TextConAug (concatenating text images for context augmentation) and better pretraining.
- Relevance: Explains why our config has an NRTR head (training-only guidance) and RecConAug. The PaddleOCR fine-tuning doc (below) recommends removing GTC and RecConAug when fine-tuning on simple/small data, because the attention branch can overfit.

## PaddleOCR documentation: model fine-tuning guide (text recognition)
- Source: PaddleOCR official docs, "Model Fine-tuning"
- Link: https://www.paddleocr.ai/v2.10.0/en/ppocr/model_train/finetune.html
- Key points (as stated in the doc):
  - With unchanged dictionary, prepare at least ~5,000 recognition samples; changing the dictionary is not recommended.
  - Learning rate for single-GPU fine-tuning: piecewise [1e-4, 2e-5] at batch 128, or [5e-5, 1e-5] at batch 64 (defaults are for 8 GPUs).
  - Keep domain-specific and general real data at about 1:1 per epoch via `ratio_list`; when adding synthetic data, original:new around 10:1 to 5:1 with a small LR.
  - Consider removing GTC (attention branch) and RecConAug for simple datasets.
- Relevance: These are vendor recommendations for exactly our model family. Our ~100 real lines are far below 5,000, so heavy replay of general data and very low LR are essential. Use `ratio_list` style mixing instead of training on handwriting alone.

## PaddleOCR official config: korean_PP-OCRv5_mobile_rec.yml
- Link: https://github.com/PaddlePaddle/PaddleOCR/blob/main/configs/rec/PP-OCRv5/multi_language/korean_PP-OCRv5_mobile_rec.yml
- Key facts (as read on 2026-11-10): Adam, cosine LR base 5e-4 with 5 warmup epochs, L2 3e-5; MultiHead (CTCHead with SVTR neck + NRTRHead, `max_text_length` 25); MultiLoss (CTC + NRTR); train transforms DecodeImage -> RecConAug(prob 0.5, image shape [48,320,3]) -> RecAug -> MultiLabelEncode; MultiScaleSampler with scales [320,32],[320,48],[320,64], batch 128 per card; eval resize to [3,48,320]; 75 epochs.
- Relevance (all **[our inference]**, verify in code):
  - The official training never sees 960-wide padded inputs; it uses width 320 with multi-scale heights. Our 960-wide zero-padded training is a domain shift for BN and for the SVTR neck's global mixing.
  - `max_text_length` 25: in PaddleOCR the label encoders typically return `None` (sample dropped) when a label is longer than `max_text_length`. Long handwritten lines could be silently filtered or truncated in our own reimplementation; check that long lines are not mishandled by the NRTR branch.
  - Training uses fp32 by default; bf16 AMP O2 casts BN and the CTC loss inputs to low precision, which is a known source of instability. Keep BN and CTC loss in fp32 (or drop AMP entirely; the model is small).

## SVTR: Scene Text Recognition with a Single Visual Model
- Authors: Yongkun Du, Zhineng Chen, Caiyan Jia, Xiaoting Yin, Tianlun Zheng, Chenxia Li, Yuning Du, Yu-Gang Jiang
- Year / venue: 2022, IJCAI
- Link: https://arxiv.org/abs/2205.00159
- Key idea: Replaces the CNN+RNN pipeline with patch-wise "character component" mixing (local and global mixing blocks) followed by a linear CTC readout.
- Relevance: The global mixing blocks attend over the whole width, so the amount of padding at training time vs inference time changes what the neck sees. **[our inference]** Pad consistently between training and inference (same buckets), or use attention masks if retraining the neck.

## PaddleOCR 3.0 Technical Report / PP-OCRv5 paper
- PaddleOCR 3.0 Technical Report: Cheng Cui et al., 2025, arXiv https://arxiv.org/abs/2507.05595
- PP-OCRv5: A Specialized 5M-Parameter Model Rivaling Billion-Parameter Vision-Language Models on OCR Tasks: Cheng Cui et al., 2026, arXiv https://arxiv.org/abs/2603.24373 (listed as a CVPR 2026 poster)
- Key idea: PP-OCRv5's gains are attributed mainly to data (difficulty, accuracy, diversity) rather than architecture.
- Relevance: Reinforces that our biggest lever is data (real handwriting, realistic synthetic rendering), not a new architecture. Licence: PaddleOCR is Apache-2.0.

---

## Checklist distilled for rmpp-hwr (all **[our inference]**)
1. Reproduce the base CER (0.257) through the *training* code path (same preprocessing, eval mode) before training. If the training-side eval does not reproduce it, the bug is in preprocessing/decoding, not in training.
2. Run a "LR = 0" epoch (BN in train mode) and re-evaluate. If CER degrades, BN statistic updates on padded batches are the culprit.
3. Freeze BN statistics (eval mode for all BN layers) for small-data fine-tuning.
4. Disable AMP (or keep fp32 BN + fp32 CTC loss).
5. Use width buckets close to the content width (e.g. 320/480/640) instead of a fixed 960, matching the official recipe.
6. LR around 1e-5 to 1e-4 (Adam), short warmup, early stopping on held-out real lines.
7. Mix: real handwriting + replay of general/printed or synthetic data (roughly 1:1 per batch), optionally a distillation term to the frozen base model.
8. Drop or down-weight the NRTR branch for fine-tuning (PaddleOCR doc advice), or ensure its 25-token limit does not interact badly with long lines.
9. Try WiSE-FT interpolation of the result against the base weights before export.

## rmpp-hwr findings (our experiments)

- rmpp-hwr result (2026-11-10): **the fine-tuning bug was the backbone's training-mode pooling, not AMP.**
  PaddleOCR's `PPLCNetV3` (rec backbone) pools to a fixed 40 time steps while training
  (`F.adaptive_avg_pool2d(x, [1, 40])`) but to width/8 at inference (`F.avg_pool2d(x, [3, 2])`). The two agree
  only for width 320, which is what the official config trains at. We trained at width 960, so every line was
  squeezed into 40 steps in training and read with 120 steps at inference. Probe with the base weights at
  lr ~0 (`train/jobs/probe.sh`, training-mode acc / CTC loss on synthetic lines; the base model's eval-mode acc is 0.24):
  width 960 bf16 O2 -> 0.00 / 180; width 960 fp32 -> 0.00 / 160; width 960 fp32 + frozen BN -> 0.03 / 105;
  width 960 fp16 O2 -> 0.00 / 160; **width 320 fp32 -> 0.20 / 25**. Fix: `train/patches/paddleocr-rmpp.patch`
  (`HWR_EVAL_POOL=1`, the default in `train/jobs/run.sh`) trains with the inference pooling.
  With that patch alone (width 960, fp32) the probe still gave 0.00 / 66: the second cause is **BatchNorm batch
  statistics** on zero-padded wide images. **Pooling patch + frozen BN (`FREEZE_BN=1`) -> 0.25-0.30 / 15**, better than
  width 320 (0.20 / 25). Both are now the defaults in `run.sh`. bf16 O2 AMP with the pooling patch alone: 0.00 / 68
  (AMP with frozen BN not tested yet).
- Also found: the collected personal data had 3 lines longer than `max_text_length` 40; PaddleOCR drops them
  silently (`hwr qc` flags them).
