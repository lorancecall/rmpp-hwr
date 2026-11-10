# Synthetic handwriting data and augmentation

Last updated: 2026-11-10

Scope: font rendering vs stroke-based rendering vs generative handwriting models, plus augmentation, and how much they help real HTR. Notes marked **[our inference]** are our own reasoning.

---

## Generating Synthetic Data for Text Recognition (IIIT-HWS)
- Authors: Praveen Krishnan, C. V. Jawahar
- Year / venue: 2016, arXiv
- Link: https://arxiv.org/abs/1608.04224
- Key idea: Render words with many open-source handwriting-style fonts plus augmentation to build a 9M-image synthetic corpus for pretraining.
- Relevance: The classical font-rendering approach, close to what we do now. Useful as pretraining, but font glyphs are perfectly consistent per character, unlike real handwriting.

## Data Augmentation for Recognition of Handwritten Words and Lines using a CNN-LSTM Network
- Authors: Curtis Wigington, Seth Stewart, Brian Davis, Bill Barrett, Brian Price, Scott Cohen
- Year / venue: 2017, ICDAR, pp. 639-645
- Link: https://dblp.org/rec/conf/icdar/WigingtonSDBPC17
- Key idea: Grid-based random elastic distortion for augmentation plus a profile normalization of handwriting images, used with a CNN-LSTM.
- Evidence: Significant WER/CER reductions on standard handwriting benchmarks.
- Relevance: Elastic distortion is a cheap way to break the "too regular" look of font/stroke-rendered synthetic lines. Apply it to synthetic data and (lightly) to the real lines.

## Learn to Augment: Joint Data Augmentation and Network Optimization for Text Recognition
- Authors: Canjie Luo, Yuanzhi Zhu, Lianwen Jin, Yongpan Wang
- Year / venue: 2020, CVPR
- Link: https://arxiv.org/abs/2003.06606
- Key idea: Geometric augmentation by moving fiducial points (similar to TPS warping), with an agent network that learns harder augmentations jointly with the recognizer.
- Evidence: Improvements on regular scene text, irregular scene text and handwriting.
- Relevance: The fixed (non-learned) version of this warp is what PaddleOCR's `RecAug` family approximates. **[our inference]** The learned agent is optional; random fiducial warps on our 48-px lines are a quick win.

## ScrabbleGAN: Semi-Supervised Varying Length Handwritten Text Generation
- Authors: Sharon Fogel, Hadar Averbuch-Elor, Sarel Cohen, Shai Mazor, Roee Litman
- Year / venue: 2020, CVPR
- Link: https://arxiv.org/abs/2003.10557
- Key idea: Fully convolutional generator that builds word images of arbitrary length from per-character filters; semi-supervised training that can use unlabeled images.
- Evidence: Generated data improves HTR, and the authors report that lexicon diversity matters as much as style diversity.
- Relevance: The lexicon point applies to us: synthetic lines should cover the real character distribution of Korean+English notes (syllable frequencies, digits, punctuation, code-switching), not only random syllables.

## GANwriting: Content-Conditioned Generation of Styled Handwritten Word Images
- Authors: Lei Kang, Pau Riba, Yaxing Wang, Marçal Rusiñol, Alicia Fornés, Mauricio Villegas
- Year / venue: 2020, ECCV
- Link: https://arxiv.org/abs/2003.02567 (code: https://github.com/omni-us/research-GANwriting)
- Key idea: Few-shot styled word generation conditioned on a writer's samples and target text.
- Relevance: Early writer-imitation model (word level, Latin script).

## Content and Style Aware Generation of Text-line Images for Handwriting Recognition
- Authors: Lei Kang, Pau Riba, Marçal Rusiñol, Alicia Fornés, Mauricio Villegas
- Year / venue: 2022, IEEE TPAMI 44(12)
- Link: https://arxiv.org/abs/2204.05539
- Key idea: Generates full text-line images conditioned on visual style and content; can adapt to a new target using unlabeled line images.
- Evidence: Generated lines improve HTR beyond font-based synthetic data, which the authors note suffers from style bias.
- Relevance: Line-level (not word-level) generation matches our line recognizer. Latin-script only as published.

## HiGAN: Handwriting Imitation Conditioned on Arbitrary-Length Texts and Disentangled Styles
- Authors: Ji Gan, Weiqiang Wang
- Year / venue: 2021, AAAI
- Link: https://ojs.aaai.org/index.php/AAAI/article/view/16917 (code: https://github.com/ganji15/HiGAN, MIT)
- Key idea: GAN that imitates disentangled calligraphic style from reference samples for arbitrary-length text; follow-up HiGAN+.
- Relevance: Candidate for imitating the owner's style once we have enough lines. Would need retraining for Hangul.

## Handwriting Transformers (HWT)
- Authors: Ankan Kumar Bhunia, Salman Khan, Hisham Cholakkal, Rao Muhammad Anwer, Fahad Shahbaz Khan, Mubarak Shah
- Year / venue: 2021, ICCV
- Link: https://arxiv.org/abs/2104.03964
- Key idea: Transformer encoder-decoder that generates styled handwritten text from a few style examples, with per-character attention to style features.
- Relevance: Few-shot style imitation; Latin-trained.

## Handwritten Text Generation from Visual Archetypes (VATr)
- Authors: Vittorio Pippi, Silvia Cascianelli, Rita Cucchiara
- Year / venue: 2023, CVPR
- Link: https://arxiv.org/abs/2303.15269 (code: https://github.com/aimagelab/VATr)
- Key idea: Represents each character by an image rendered from GNU Unifont (a "visual archetype") instead of a one-hot token, so visually similar characters share features and rare characters generate better.
- Relevance: **[our inference]** The archetype idea is attractive for Hangul: 11,172 syllables share jamo shapes, so archetype-based conditioning could generalize to unseen syllables far better than one-hot. Still requires training on Korean handwriting.

## One-DM: One-Shot Diffusion Mimicker for Handwritten Text Generation
- Authors: Gang Dai, Yifan Zhang, Quhui Ke, Qiangya Guo, Shuangping Huang
- Year / venue: 2024, ECCV
- Link: https://arxiv.org/abs/2409.04004 (code: https://github.com/dailenson/One-DM, MIT)
- Key idea: Diffusion model conditioned on one reference sample, with a style module that emphasizes high-frequency details.
- Evidence: Reported to outperform earlier methods that used ten or more style samples. The abstract says it works "in multiple languages" (check the paper for which scripts).
- Relevance: One of the more promising generators for large-charset scripts. **[our inference]** Heavy to train; consider only after the fine-tuning pipeline is fixed and if real data stays scarce.

## DiffusionPen: Towards Controlling the Style of Handwritten Text Generation
- Authors: Konstantina Nikolaidou, George Retsinas, Giorgos Sfikas, Marcus Liwicki
- Year / venue: 2024, ECCV
- Link: https://arxiv.org/abs/2409.06065 (code: https://github.com/koninik/DiffusionPen)
- Key idea: 5-shot styled generation with a latent diffusion model and a hybrid (metric-learning + classification) style extractor.
- Evidence: Generated data improved HTR on IAM.
- Relevance: Evidence that diffusion-generated, writer-styled data can help HTR training; Latin-only as published.

## Generating Sequences With Recurrent Neural Networks
- Authors: Alex Graves
- Year / venue: 2013, arXiv
- Link: https://arxiv.org/abs/1308.0850
- Key idea: LSTM with mixture density outputs generates pen trajectories; conditioned on text via a soft attention window, it synthesizes realistic cursive online handwriting.
- Relevance: The online-synthesis route. **[our inference]** Because we render device lines from strokes, an *online* generator (strokes) composes naturally with our existing renderer: generate strokes, render with the exact device brush, and the image domain matches inference by construction.

## Evaluating Synthetic Pre-Training for Handwriting Processing Tasks
- Authors: Vittorio Pippi, Silvia Cascianelli, Lorenzo Baraldi, Rita Cucchiara
- Year / venue: 2023, arXiv preprint
- Link: https://arxiv.org/abs/2304.01842
- Key idea: Pretraining on a large font-rendered synthetic handwriting dataset yields good handwriting style representations for writer-related tasks.
- Relevance: Supports font-rendered synthetic data as *pretraining*, not as a replacement for real data.

## Few-shot Compositional Font Generation with Dual Memory (DM-Font)
- Authors: Junbum Cha, Sanghyuk Chun, Gayoung Lee, Bado Lee, Seonghyeon Kim, Hwalsuk Lee
- Year / venue: 2020, ECCV
- Link: https://arxiv.org/abs/2005.10510 (code: https://github.com/clovaai/dmfont)
- Key idea: Uses the compositional (jamo/component) structure of Hangul to generate a complete font from a few reference glyphs.
- Relevance: **[our inference]** A route to a "personal font": generate all 11,172 syllables in the owner's style from a few dozen handwritten characters, then render synthetic lines with it. Glyph-level only (no inter-character variation), so combine with elastic/stroke augmentation.

---

## Practical guidance for rmpp-hwr (**[our inference]**)
- **Render synthetic data through the same path as inference.** Our device lines come from strokes rendered at height 48 with the device's brush. The strongest, cheapest synthetic source is therefore stroke data (jbaudanza strokes, our own strokes, later generated strokes) rendered through the *identical* renderer and normalization, with randomized stroke width, pressure curve, slant, baseline drift, inter-character spacing, and character-level jitter. Font rendering should be a minority of the mix.
- **Match the text distribution.** Build synthetic line text from a corpus resembling real notes (Korean sentences with embedded English words, digits, punctuation), not uniform random syllables.
- **Augment real lines lightly** (elastic distortion, small affine, width/pressure jitter), since 100 lines is very few.
- **Measure every synthetic change on held-out real lines only.** Synthetic validation CER is not informative about the real gap.
- **Generative imitation (GAN/diffusion) is phase 2**: the strong published models are mostly Latin-trained (One-DM claims multi-language support); training one for Hangul is a project in itself.
