# rmpp-hwr

Handwriting recognition (Korean, English, digits) for the reMarkable Paper Pro, running on the tablet's CPU,
plus the tools to collect your own handwriting and train a personal model.

> Status: experimental, not packaged yet. The tablet UI is installed by hand for now; a Vellum package will
> follow in [rmpp-packages](https://github.com/lorancecall/rmpp-packages).

## What is here

| Path | What |
|---|---|
| `src/` | Rust recognizer: reads `.rm` pages, renders lines, runs the PaddleOCR PP-OCRv5 Korean recognition model (ONNX, onnxruntime). Also exports the typed text of a page. |
| `ui/` | xochitl add-on (LD_PRELOAD): "Convert page" in the toolbar, Settings > Handwriting (records, corrections, model info). |
| `model/` | base model `rec.onnx` + `dict.txt` (PaddleOCR `korean_PP-OCRv5_mobile_rec`, Apache-2.0, see NOTICE). |
| `collect/` | practice notebooks (v1 lines, v2 structured pages with per-character cells), pulling handwriting from the tablet, line/character images. |
| `tools/hwr` | one command for the whole loop: `practice`, `pull`, `qc`, `upload`, `train`, `fetch`, `eval`, `deploy`, `contribute`, ... |
| `train/` | fine-tuning jobs that run on a local GPU or a Slurm cluster, configured by `.env` (see `train/README.md`). |
| `references/` | literature notes on improving personal handwriting recognition (cumulative). |

## Guides

- [WORKFLOW.md](WORKFLOW.md): from practice notebook to personal model on the tablet.
- [train/README.md](train/README.md): training backends (`.env`: local GPU or Slurm), jobs, known pitfalls.
- [references/README.md](references/README.md): what to try next and why.
- Open dataset: [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db) (Korean guide:
  https://lorancecall.github.io/rmpp-hwr-db/). `hwr contribute` shares structured practice pages, anonymized.

## Setup (PC)

```sh
uv venv .venv && uv pip install -r rmpp-hwr/collect/requirements.txt
cp rmpp-hwr/.env.example rmpp-hwr/.env      # tablet address, training backend; never committed
rmpp-hwr/tools/hwr --help
```

The cross-compile toolchain and onnxruntime are expected in `../sdk` (see `.cargo/config.toml` and `ui/build.sh`).

## Privacy

No personal data goes into this repository or into contributed data: machine-specific settings live only in the
gitignored `.env`, and `hwr contribute` sends random writer ids, practice templates and pen strokes only.

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
