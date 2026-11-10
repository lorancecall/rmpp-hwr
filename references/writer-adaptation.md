# Writer adaptation and personalization for HTR

Last updated: 2026-11-10

Scope: making a recognizer better for one specific writer (the device owner) with few labeled lines, unlabeled lines, and user corrections over time. Notes marked **[our inference]** are our own reasoning, not the papers' claims.

---

## Fine-tuning Is a Surprisingly Effective Domain Adaptation Baseline in Handwriting Recognition
- Authors: Jan Kohút, Michal Hradiš
- Year / venue: 2023 (arXiv), revised 2025
- Link: https://arxiv.org/abs/2302.06308
- Key idea: Per-writer plain fine-tuning with augmentation of a CTC line recognizer is a strong writer-adaptation method.
- Evidence: ~25% relative CER reduction with 16 lines of a new writer, ~50% with 256 lines.
- Relevance: Our personalization path should start with simple, correctly implemented per-user fine-tuning. Also see `fine-tuning-small-data.md`.

## Towards Writing Style Adaptation in Handwriting Recognition
- Authors: Jan Kohút, Michal Hradiš, Martin Kišš
- Year / venue: 2023, arXiv (ICDAR 2023 submission), revised 2025
- Link: https://arxiv.org/abs/2302.06318
- Key idea: Adds a "Writer Style Block" (adaptive instance normalization conditioned on a learned writer-style embedding) so one model can be specialized by a small style vector instead of new weights.
- Evidence: Helps in a writer-dependent setting compared to a baseline without it, but in the writer-independent setting simple fine-tuning was more accurate at similar compute.
- Relevance: For a single-user device, writer codes add complexity without beating fine-tuning. **[our inference]** Only worth revisiting if we ship one model to many users and want tiny per-user state (a style vector) instead of per-user weights.

## MetaHTR: Towards Writer-Adaptive Handwritten Text Recognition
- Authors: Ayan Kumar Bhunia, Shuvozit Ghose, Amandeep Kumar, Pinaki Nath Chowdhury, Aneeshan Sain, Yi-Zhe Song
- Year / venue: 2021, CVPR
- Link: https://arxiv.org/abs/2104.01876
- Key idea: Meta-learning (MAML-style) so that a single gradient step on a few samples of a new writer adapts the model; also meta-learns per-character loss weights.
- Evidence: Average gains of about 5-7% from 16 or fewer samples of the new style.
- Relevance: Requires meta-training on a large multi-writer dataset, which we do not have for Korean lines. **[our inference]** Not a near-term option; the conceptual takeaway (few gradient steps on the user's own lines is enough when the base model is good) is consistent with the fine-tuning results above.

## Fast writer adaptation with style extractor network for handwritten text recognition
- Authors: Zi-Rui Wang, Jun Du
- Year / venue: 2022, Neural Networks vol. 147, pp. 42-52
- Link: https://doi.org/10.1016/j.neunet.2021.12.002 (code: https://github.com/Wukong90/Handwritten-Text-Recognition)
- Key idea: A style extractor network (trained with a writer-identification loss) produces a writer embedding from a text line, which is fed into a writer-independent recognizer.
- Evidence: Gains on Chinese and English offline HTR using a single writer-specific line as adaptation data, without extra decoding passes.
- Relevance: Relevant because it was validated on Chinese (large character set like Korean). **[our inference]** Same caveat as writer codes: needs a multi-writer training corpus; not a first step.

## Unsupervised (Writer) Adaptation for Synthetic-to-Real Handwritten Word Recognition
- Authors: Lei Kang, Marçal Rusiñol, Alicia Fornés, Pau Riba, Mauricio Villegas
- Year / venue: 2020, WACV
- Link: https://arxiv.org/abs/1909.08473 (code: https://github.com/omni-us/research-WriterAdaptation-HTR)
- Key idea: Train on synthetic font-rendered words, then adapt to a real writer's unlabeled images via adversarial domain adaptation of features.
- Evidence: Consistent gains over synthetic-only training across GW, IAM, RIMES, CVL, Esposalles.
- Relevance: Matches our synthetic-to-real gap. **[our inference]** With labeled real lines available (and more coming), supervised fine-tuning is simpler; adversarial DA is a fallback if we have many unlabeled pages but few corrected ones.

## Self-Training of Handwritten Word Recognition for Synthetic-to-Real Adaptation
- Authors: Fabian Wolf, Gernot A. Fink
- Year / venue: 2022, ICPR
- Link: https://arxiv.org/abs/2206.03149
- Key idea: Use a synthetic-trained model to pseudo-label unlabeled real data, then retrain on the pseudo-labels (iteratively), with confidence-based selection.
- Evidence: Narrows the gap to fully supervised training on four benchmarks without any manually labeled real data.
- Relevance: Direct fit for the device: every line the user writes and does *not* correct is a weak pseudo-label; lines the user *does* correct are gold labels. **[our inference]** Use only high-confidence pseudo-labels (e.g. mean max-posterior above a threshold), weight them below gold labels, and never evaluate on pseudo-labeled lines.

## Few Shots Are All You Need: A Progressive Learning Approach for Low Resource Handwritten Text Recognition
- Authors: Mohamed Ali Souibgui, Alicia Fornés, Yousri Kessentini, Beáta Megyesi
- Year / venue: 2022, Pattern Recognition Letters vol. 160 (arXiv 2021)
- Link: https://arxiv.org/abs/2107.10064
- Key idea: Few-shot symbol-matching recognizer pretrained on synthetic lines, then progressive pseudo-labeling of unannotated target lines.
- Relevance: Another instance of the synthetic-pretrain + pseudo-label recipe. Mainly for rare-alphabet manuscripts; lower priority for us.

## Tent: Fully Test-Time Adaptation by Entropy Minimization
- Authors: Dequan Wang, Evan Shelhamer, Shaoteng Liu, Bruno Olshausen, Trevor Darrell
- Year / venue: 2021, ICLR (spotlight)
- Link: https://arxiv.org/abs/2006.10726
- Key idea: At test time, update only BN statistics and BN affine parameters to minimize prediction entropy on the incoming test batches.
- Relevance: **[our inference]** Interesting as an offline (not per-inference) personalization step: periodically run AdaBN/Tent-style updates of BN parameters on the user's accumulated unlabeled lines, on the desktop, then re-export. Risky with tiny batches and CTC (entropy minimization can collapse to blanks); treat as an experiment, always gated by held-out CER.

## Practical Fine-Tuning of Autoregressive Models on Limited Handwritten Texts
- Authors: Jan Kohút, Michal Hradiš
- Year / venue: 2025, arXiv (ICDAR 2025 submission)
- Link: https://arxiv.org/abs/2503.19546
- Key idea / evidence: see `fine-tuning-small-data.md`; the part relevant here is confidence-based selection of lines to annotate, halving annotation cost.
- Relevance: Design the correction UI/loop to ask about low-confidence lines first.

---

## Suggested personalization loop for rmpp-hwr (**[our inference]**)
1. Base model + correctly implemented conservative fine-tune on the owner's labeled lines (with replay data).
2. On device, log every recognized line: image (or strokes), output, per-line confidence, and whether the user corrected it.
3. Periodically (desktop side): add corrected lines as gold labels, uncorrected high-confidence lines as weighted pseudo-labels, re-fine-tune from the *base* checkpoint (not from the last personalized one, to avoid drift), evaluate on a frozen held-out set, ship only if better.
4. Keep the held-out set separate from anything that ever enters training (including pseudo-labels).
