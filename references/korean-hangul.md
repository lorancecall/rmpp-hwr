# Korean / Hangul specifics

Last updated: 2026-11-10

Scope: output vocabulary (syllables vs jamo), Korean handwriting datasets and their licenses, mixed Korean+English lines. Notes marked **[our inference]** are our own reasoning.

---

## Background facts (Unicode)
- Modern Hangul has 11,172 precomposed syllables (U+AC00-U+D7A3), each deterministically composed of an initial consonant (19), a vowel (21) and an optional final (27 + none). Decomposition/composition is pure arithmetic on code points, so any jamo-level output can be recomposed losslessly.
- Our model's dict (`ppocrv5_korean_dict.txt`, 11,945 entries) is syllable-level and also contains Latin, digits, symbols and some Hanja.

## Character decomposition to resolve class imbalance problem in Hangul OCR
- Authors: Geonuk Kim, Jaemin Son, Kanghyu Lee, Jaesik Min
- Year / venue: 2022, arXiv; ECCV 2022 workshop (Text in Everything)
- Link: https://arxiv.org/abs/2208.06079
- Key idea: Predict graphemes (jamo, 52 symbols) instead of syllables, then recompose. This removes the long tail of rare syllable classes and makes every syllable recognizable without needing training examples of it.
- Evidence: The authors claim grapheme encoding is both more efficient and more accurate for Hangul OCR and introduce a Hangul OCR benchmark (see paper for numbers).
- Relevance: **[our inference]** Strong idea for the long term (handwriting of rare syllables, tiny personal datasets). But it changes the output layer, so the pretrained CTC head cannot be reused as-is; PaddleOCR's doc advises against changing the dict for fine-tuning. For CTC, jamo output also roughly triples the output length per syllable, so check that 48-px-high lines downsampled by the backbone still give enough time steps (CTC requires T >= label length + repeats). Not a first step; consider for a future retrain.

## A Sub-Character Architecture for Korean Language Processing
- Authors: Karl Stratos
- Year / venue: 2017, EMNLP
- Link: https://arxiv.org/abs/1707.06341
- Key idea: Decompose syllables into jamo deterministically via Unicode and build representations from jamo.
- Relevance: Standard reference for the decomposition scheme; NLP, not OCR. Useful if we add a jamo-aware language model for CTC decoding/rescoring.

## Datasets

### PE92 and SERI95 (isolated handwritten Hangul characters)
- PE92: collected by POSTECH with ETRI support (1992); 2,350 KS X 1001 (KSC5601) syllable classes, ~100 samples each. Known to contain mislabeled samples.
- SERI95: collected by Korea University for SERI (later merged into ETRI); 520 frequent syllables, ~1,000 samples each.
- Reference describing both and a merged "HanDB": https://github.com/callee2006/HangulDB
- Relevance: Isolated characters, not lines; old scanned offline data. **[our inference]** Could be composed into synthetic lines (paste characters with jitter), but domain (scanner, pen) differs from our rendered strokes. License/redistribution terms are unclear; check with the distributor before any use beyond private research.

### AI Hub "Korean font images" (한국어 글자체 이미지, NIA)
- Link: https://www.aihub.or.kr/aihubdata/data/view.do?dataSetSn=81
- Contents (from the dataset page, checked 2026-11-10): printed text in 50 fonts covering all 11,172 syllables; handwriting (about 2.5M characters) from writers of various ages and genders; "Text in the Wild" photos (~100k images).
- License/access: Application required, approval-based download. The page states that only Korean nationals can apply. It does not state commercial-use or redistribution terms on the page itself; the general AI Hub usage policy applies. **Check the policy before use; do not redistribute.**
- Relevance: The largest public-ish Korean handwriting image source. Mostly character/word crops, scanned, not online strokes. **[our inference]** Best use is as additional pretraining/replay data for "Korean handwriting in general" if access and license allow; not a substitute for the owner's own lines.

### Other AI Hub OCR sets
- AI Hub also hosts other OCR datasets (e.g. public administrative documents OCR, "various forms of text" OCR) under the same access regime. Not verified individually here.

### RanPil: New Dataset and Benchmark for Offline Handwritten Korean Text Recognition
- Authors: Hyon-Gwang O, Myong-Chol Kim, Il-Nam Pak, Un-Hyok Choe, Chol-Jun O
- Year / venue: 2025, International Journal on Data Science and Technology 11(2), pp. 27-34
- Link: https://doi.org/10.11648/j.ijdst.20251102.12
- Content: 8,600 pages, ~182k text lines, ~4.3M characters, 1,804 writers, writer-disjoint train/test split; 1,301 classes (1,198 Korean, 42 English letters, digits, symbols). TrOCR baseline CER 10.93% overall, rising from 5.3% (neat) to 28.5% (least legible).
- License: Article is CC BY 4.0; dataset available from the authors "on reasonable request", no dataset license stated. Authors are from DPR Korea; character set and orthography may differ from South Korean usage.
- Relevance: The only line-level Korean HTR benchmark found. Useful context for expected CER ranges (10-30% CER for line HTR on varied Korean handwriting with a strong model), which makes our 25.7% base CER on a single writer plausible as a starting point. Access is uncertain.

### jbaudanza/korean-handwriting-strokes (already used by the project)
- Source: Hugging Face dataset https://huggingface.co/datasets/jbaudanza/korean-handwriting-strokes (there is no GitHub repo of that name; the GitHub URL returns 404). License verified 2026-11-10 via the Hugging Face API: **CC-BY-SA-4.0**. Used as `data/recordings.parquet` by `gen.py real`.
- CC-BY-SA implies attribution and share-alike obligations for derived datasets (possibly for models trained on it, depending on interpretation); record attribution wherever trained models are shared.

### Commercial / other
- Nexdata sells Korean handwriting OCR data with line-level annotations (commercial license). Hugging Face hosts some small Korean handwritten-notes datasets with CC-BY-4.0 cards (quality and provenance not verified). Listed for completeness only.

## Mixed-script (Korean + English) lines
- No dedicated paper on Korean-English code-switched handwriting lines was found in this pass.
- RanPil includes 42 English letters among its classes, so mixed lines exist there.
- **[our inference]**
  - Our base model already covers both scripts in one dict, which is the right setup for a single-pass CTC recognizer.
  - Risks: Latin letters in Korean handwriting are often small/cursive and confused with jamo-like shapes (e.g. "o"/"ㅇ", "l"/"ㅣ", "T"/"ㅜ"-like). Make sure synthetic and real training data contain realistic proportions of English words, digits and punctuation in Korean sentences.
  - Spaces: Korean word spacing in handwriting is irregular; decide whether space errors count toward CER and report both CER with and without spaces.
  - A small character n-gram or word LM over mixed Korean/English text, used for CTC beam search on the desktop or device, may fix many confusions cheaply (Google's line-recognition work below found CTC + external LM a strong combination).

## Related: Rethinking Text Line Recognition Models
- Authors: Daniel Hernandez Diaz, Siyang Qin, Reeve Ingle, Yasuhisa Fujii, Alessandro Bissacco
- Year / venue: 2021, arXiv (Google Research)
- Link: https://arxiv.org/abs/2104.07787
- Key idea: Compares encoders and decoders for text line recognition across scene text and handwriting; a self-attention encoder + CTC decoder + external LM gave the best accuracy/cost trade-off and handles arbitrary-length lines.
- Relevance: Validates our architecture choice (CTC line recognizer) and suggests adding an external LM rather than switching to an autoregressive decoder.
