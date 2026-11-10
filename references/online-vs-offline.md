# Online (stroke-based) vs offline (image-based) recognition, and hybrids

Last updated: 2026-11-10

Scope: we capture strokes (x, y, pressure, time per point) on the reMarkable Paper Pro, but currently render them to images and run an offline recognizer. Notes marked **[our inference]** are our own reasoning.

---

## Multi-Language Online Handwriting Recognition
- Authors: Daniel Keysers, Thomas Deselaers, Henry A. Rowley, Li-Lun Wang, Victor Carbune
- Year / venue: IEEE TPAMI 39(6), 2017 (online 2016)
- Link: https://research.google/pubs/multi-language-online-handwriting-recognition/
- Key idea: Google's production segment-and-decode online recognizer for 22 scripts / 97 languages, designed for fast on-device text entry.
- Relevance: Historical baseline; superseded by the LSTM system below.

## Fast Multi-language LSTM-based Online Handwriting Recognition
- Authors: Victor Carbune, Pedro Gonnet, Thomas Deselaers, Henry A. Rowley, Alexander Daryin, Marcos Calvo, Li-Lun Wang, Daniel Keysers, Sandro Feuz, Philippe Gervais
- Year / venue: 2020, IJDAR 23:89-102 (arXiv 2019)
- Link: https://arxiv.org/abs/1902.10525
- Key idea: End-to-end BiLSTM + CTC on stroke sequences, with input points encoded as Bézier-curve segments to shorten sequences; one architecture for ~100 languages, including Korean.
- Evidence: 20-40% relative error reduction over the previous system for most languages and up to 10x faster; state of the art on IAM-OnDB at the time.
- Relevance: Proof that a small online model can run fast on phones for Korean. **[our inference]** Online input is much smaller than a 48x(160..1920) image: a line is a few hundred (x, y, t, pen-up) points, and after Bézier fitting fewer still. An online BiLSTM/CTC model of a few MB could be faster than our 0.4 s/line image model. The cost: no pretrained online Korean model is available to us, so we would train from scratch on stroke data (jbaudanza strokes + our own + synthetic strokes). That is a bigger project than fixing the offline fine-tune.

## Combining On-line and Off-line Systems for Handwriting Recognition
- Authors: Marcus Liwicki, Horst Bunke
- Year / venue: 2007, ICDAR, pp. 372-376
- Link: https://dblp.uni-trier.de/rec/html/conf/icdar/LiwickiB07
- Key idea: Combine one offline and two online HMM recognizers for whiteboard notes by aligning their word outputs and voting.
- Evidence: About 2% improvement over the best single system; later BLSTM versions (Pattern Recognition 2009) report larger ensemble gains.
- Relevance: **[our inference]** A cheap hybrid for us later: keep the offline PP-OCR model and add a small online model; combine at the CTC-posterior or n-best level. Only worthwhile once both models are individually reasonable.

## InkSight: Offline-to-Online Handwriting Conversion by Learning to Read and Write
- Authors: Blagoj Mitrevski, Arina Rak, Julian Schnitzler, Chengkun Li, Andrii Maksai, Jesse Berent, Claudiu Musat
- Year / venue: 2024 arXiv; TMLR 2025 (retitled "... by Teaching Vision-Language Models to Read and Write")
- Link: https://arxiv.org/abs/2402.05804 (code/model: https://github.com/google-research/inksight)
- Key idea: Derenders photos of handwriting into digital ink (stroke sequences) using a ViT + mT5 model trained with reading and writing tasks, without paired data.
- Relevance: Mostly the reverse of our need. **[our inference]** Could, in principle, convert scanned Korean handwriting images (e.g. AI Hub) into strokes so they can be re-rendered with our device renderer, removing the scanner/pen domain gap. Unverified for Hangul; experimental.

## Representing Online Handwriting for Recognition in Large Vision-Language Models
- Authors: Anastasiia Fadeeva, Philippe Schlattner, Andrii Maksai, Mark Collier, Efi Kokiopoulou, Jesse Berent, Claudiu Musat
- Year / venue: 2024, arXiv
- Link: https://arxiv.org/abs/2402.15307
- Key idea: Feed digital ink to a VLM both as a rendered image and as a tokenized time-ordered stroke sequence (text); this matches or beats specialized online recognizers.
- Evidence: Comparable or better than state-of-the-art online recognizers on several public datasets, with fine-tuning or parameter-efficient tuning; ablations show both views help.
- Relevance: Too large for on-device use, but the finding that *image + trajectory together* beats either alone supports the hybrid direction. **[our inference]** Also usable off-device as a "teacher" to pseudo-label the owner's unlabeled lines if a suitable model were accessible.

## Generating Sequences With Recurrent Neural Networks
- Authors: Alex Graves
- Year / venue: 2013, arXiv
- Link: https://arxiv.org/abs/1308.0850
- Relevance: Online handwriting *synthesis*; see `synthetic-data.md`. Strokes generated this way can be rendered by our device renderer for perfectly matched synthetic images.

---

## IAM-OnDB - an On-Line English Sentence Database Acquired from Handwritten Text on a Whiteboard (dataset)
- Authors: Marcus Liwicki, Horst Bunke
- Year / venue: 2005, ICDAR
- Link: https://fki.tic.heia-fr.ch/databases/iam-on-line-handwriting-database
- Key idea: English handwritten text lines written on a whiteboard with a pen tracker, stored as pen strokes (x, y, time) with line transcriptions; roughly 200 writers.
- Evidence: Standard benchmark for online English HWR (used e.g. by Carbune et al. above).
- Relevance to rmpp-hwr: Real English handwriting as strokes, so `gen.py iamon` renders it through the same pipeline as tablet input. Used as English replay data during personal fine-tuning so English is not forgotten (first fine-tunes: synthetic English CER 0.140 -> 0.233).
- License / data notes: Non-commercial research use only; download requires registration; cite the paper in published work. The data is not redistributed: each user downloads it into the training work dir (`hwr dataset ... iamondb`). Models trained with it are kept for personal/research use, not shipped in packages. rmpp-hwr result (2026-11-10): loader added, not trained with yet.

## Takeaways for rmpp-hwr (**[our inference]**)
1. **Short term: stay offline, but exploit that we own the renderer.** Since inference images are rendered from strokes, every stroke source (public stroke datasets, our own lines, synthetic strokes) can be rendered *exactly* like inference. This closes much of the synthetic-to-real gap without a new model.
2. **Use stroke data for augmentation in stroke space** (per-stroke affine jitter, speed/pressure-dependent width, slant, baseline wobble), which is more realistic than image-space elastic warps alone.
3. **Store strokes for every collected and corrected line**, not just images; this keeps the online route open.
4. **Medium term: a small online BiLSTM/CTC model** (Carbune et al. style) trained on rendered-from-the-same-strokes labels is a candidate for both speed and accuracy, and could be ensembled with the offline model.
