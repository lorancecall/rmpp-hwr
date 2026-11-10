# rmpp-hwr references

Last updated: 2026-11-10

Literature and technical sources for improving personalized handwritten text-line recognition (Korean + English) on the reMarkable Paper Pro, starting from PaddleOCR `korean_PP-OCRv5_mobile_rec`. This folder is **cumulative**: future sessions append entries and update the ranked list; do not delete entries, mark them superseded instead.

## Project situation this was written against (2026-11-10)
- Model: `korean_PP-OCRv5_mobile_rec` (PPLCNetV3 + SVTR neck, CTC head, NRTR auxiliary head for training only, 11,945-char dict), ONNX on device CPU, ~0.4 s/line, ~13 MB.
- Input: strokes rendered to grayscale, height 48, aspect kept, zero-padded (after normalization) to width buckets 160..1920.
- Data: ~90-100 own handwritten lines (more coming, plus user corrections); ~400k synthetic lines (fonts + jbaudanza Korean strokes); online strokes available.
- Base CER on 12 eval lines: 0.257. Fine-tunes so far: 0.40-0.58, and worse than base even on the training lines.

## Index

| File | Topic |
|---|---|
| [fine-tuning-small-data.md](fine-tuning-small-data.md) | Fine-tuning pretrained CTC/SVTR recognizers on small data without forgetting: LR, BN, freezing, LoRA, replay, distillation, WiSE-FT, PaddleOCR fine-tune guidance and official config |
| [writer-adaptation.md](writer-adaptation.md) | Writer adaptation / personalization: per-writer fine-tuning, writer style codes, meta-learning, self-training, test-time adaptation, correction loop |
| [synthetic-data.md](synthetic-data.md) | Font vs stroke vs generative (GAN/diffusion) synthetic handwriting, augmentation (elastic, learned warps) |
| [korean-hangul.md](korean-hangul.md) | Jamo decomposition, Korean datasets (PE92, SERI95, AI Hub, RanPil, jbaudanza) and licenses, mixed Korean/English lines |
| [online-vs-offline.md](online-vs-offline.md) | Online (stroke) vs offline recognition, Google's on-device online HWR, hybrids, InkSight |
| [on-device-and-evaluation.md](on-device-and-evaluation.md) | Lightweight line-recognizer architectures, on-device notes, CER evaluation with small test sets, bootstrap CIs, leakage |

## What to try first for rmpp-hwr (ranked)

1. **Fix the fine-tuning pipeline before trying any new method.** Kohút & Hradiš (arXiv 2302.06308) report ~25% relative CER reduction from fine-tuning a CTC line recognizer on just 16 lines of a new writer and ~50% from 256. A fine-tune that ends up worse than the base on its *own training lines* is therefore almost certainly a bug, not a data limit. Concrete checks: (a) reproduce base CER 0.257 through the training code's eval path; (b) one epoch at LR = 0 with BN in train mode, then re-eval (detects BN running-stat corruption from 960-wide, mostly-padding batches; AdaBN, arXiv 1603.04779, shows how strongly BN stats encode the domain); (c) disable bf16 AMP O2, or at least keep BN and CTC loss in fp32; (d) train on width buckets near content width (official config uses 320 wide with multi-scale heights, see fine-tuning file), not fixed 960; (e) verify label encoding, dict index alignment and the NRTR `max_text_length` 25 behavior on long lines; (f) check NFC normalization of labels.
2. **Conservative full fine-tune with replay, then weight interpolation.** Freeze BN stats, Adam LR ~1e-5 to 1e-4 (PaddleOCR doc suggests [1e-4, 2e-5] at batch 128 on one GPU), drop RecConAug/NRTR for the small-data phase as the PaddleOCR fine-tune doc suggests, mix real handwriting with general/synthetic replay data roughly 1:1 per batch, early-stop on held-out real lines. Then try WiSE-FT interpolation (arXiv 2109.01903) between base and fine-tuned weights and pick the mix on held-out data. LoRA is not expected to beat full fine-tuning for HTR (Hüttner et al., WACVW 2025), so skip it unless per-user deltas are needed.
3. **Fix evaluation so decisions are trustworthy.** 12 lines is too few: grow to 50-100+ held-out lines written in separate sessions, split by session/page, never share sentence text between train and test, report total-char CER with bootstrap confidence intervals (Bisani & Ney 2004) and per-script CER, and always include the base model row. Without this, a 0.02-0.05 CER change is noise.
4. **Make synthetic data look like device input, from strokes.** We own the renderer, so render all stroke sources (jbaudanza strokes, own strokes, later generated strokes) through the exact inference renderer and normalization, with stroke-space augmentation (width/pressure, slant, baseline, spacing, jitter) and light elastic warps (Wigington et al., ICDAR 2017). Use realistic mixed Korean/English text. Keep font-rendered lines as a minority. Judge synthetic changes only by held-out real CER. Generative imitation (One-DM, DiffusionPen, HiGAN, VATr) is phase 2.
5. **Close the personalization loop with corrections and self-training.** Log every recognized line with strokes, output and confidence; corrected lines become gold labels, uncorrected high-confidence lines become down-weighted pseudo-labels (Wolf & Fink, ICPR 2022); prioritize low-confidence lines for user review (Kohút & Hradiš, arXiv 2503.19546, halved annotation cost this way). Re-fine-tune periodically from the base checkpoint and ship only on held-out improvement.

Later / longer-term: jamo-level output vocabulary for rare syllables (Kim et al., arXiv 2208.06079; needs head retrain); a small online BiLSTM-CTC stroke model (Carbune et al., IJDAR 2020) possibly ensembled with the offline model; an external character LM in CTC beam search (Diaz et al., arXiv 2104.07787); AI Hub Korean handwriting as replay data if access/license permits (Korean nationals only per dataset page).

## How to add an entry

1. Pick the topic file (or create a new one and add it to the Index table).
2. Verify the source exists: title, authors, year, venue and a working link (arXiv abs page, DOI, CVF open access, official docs). If it cannot be verified, do not add it.
3. Summarize in your own words; no long quotes.
4. Use this template:

```markdown
## <Title>
- Authors: <full list, or first authors + "et al." for long lists>
- Year / venue: <year>, <venue>
- Link: <URL>
- Key idea: <2-4 sentences>
- Evidence: <numbers/results that matter, as reported by the authors>
- Relevance to rmpp-hwr: <what we would concretely change or test>
- License / data notes: <if relevant>
```

5. Mark our own reasoning (not claimed by the source) with **[our inference]**.
6. Update the `Last updated:` line of the file you edited and of this README, and revise the ranked list if the new source changes priorities. Record results of experiments that confirm or refute an entry under that entry as `- rmpp-hwr result (<date>): ...`.

## Changelog
- 2026-11-10: Recorded the training-mode pooling bug (fine-tuning-small-data.md, "rmpp-hwr findings").
- 2026-11-10: Added IAM-OnDB dataset entry (online-vs-offline.md); jbaudanza source/license verified (Hugging Face, CC-BY-SA-4.0).
- 2026-11-10: Initial version. Six topic files, about 45 sources (papers, datasets, official docs/configs), each checked against arXiv/DOI/venue pages.
