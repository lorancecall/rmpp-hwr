# Developing rmpp-hwr

Building and installing by hand: [install.md](install.md#build-and-install-by-hand).

## What is here

| Path | What |
|---|---|
| `src/` | Rust recognizer: reads `.rm` pages, renders lines, runs the PaddleOCR PP-OCRv5 Korean recognition model (ONNX, onnxruntime). Also exports the typed text of a page. |
| `ui/` | xochitl add-on (LD_PRELOAD): lasso convert (`ui/ink.h`: typed text, or a text object through rmpp-textobj's C API), "Convert page" in the toolbar, Settings > Handwriting (records, corrections, model info). `ui/exp/`: experiments for the developer loader (`DEV=1 ui/build.sh`). |
| `model/` | base model `rec.onnx` + `dict.txt` (PaddleOCR `korean_PP-OCRv5_mobile_rec`, Apache-2.0, see NOTICE). |
| `collect/` | practice notebooks (v1 lines, v2 structured pages with per-character cells), pulling handwriting from the tablet, line/character images. |
| `tools/hwr` | one command for the whole loop: `practice`, `pull`, `qc`, `upload`, `train`, `fetch`, `eval`, `deploy`, `contribute`, ... |
| `train/` | fine-tuning jobs that run on a local GPU or a Slurm cluster, configured by `.env` (see `train/README.md`). |
| `references/` | literature notes on improving personal handwriting recognition (cumulative). |

## Guides

- [WORKFLOW.md](../WORKFLOW.md): from practice notebook to personal model on the tablet.
- [train/README.md](../train/README.md): training backends (`.env`: local GPU or Slurm), jobs, known pitfalls.
- [references/README.md](../references/README.md): what to try next and why.
- [references/convert-to-ink.md](../references/convert-to-ink.md): how Convert to ink works inside xochitl (findings,
  internals used, limits).
- Open dataset: [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db), the community's handwriting in many
  languages (how to take part, in every supported language: https://lorancecall.github.io/rmpp-hwr-db/).
  `hwr contribute` shares structured practice pages, anonymized; your language is missing? Ask for it there.

## Privacy

No personal data goes into this repository or into contributed data: machine-specific settings live only in the
gitignored `.env`, and `hwr contribute` sends random writer ids, practice templates and pen strokes only.
