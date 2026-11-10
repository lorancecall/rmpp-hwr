# rmpp-hwr

Handwriting recognition (Korean, English, digits) for the reMarkable Paper Pro, running on the tablet's CPU,
plus the tools to collect your own handwriting and train a personal model.

> Status: experimental. rmpp-hwr is installed and updated with its own PC tool (`hwr install`), not as a Vellum
> package, because models change often. It only needs `rmpp-preload` from
> [rmpp-packages](https://github.com/lorancecall/rmpp-packages) on the tablet.

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

## Install

Requirements: reMarkable Paper Pro with developer mode and SSH (an `ssh rmpp` alias with a key), OS 3.29,
and `rmpp-preload` on the tablet (`vellum add rmpp-preload`, see rmpp-packages).

On the PC:

```sh
git clone https://github.com/lorancecall/rmpp-hwr && cd rmpp-hwr
uv venv ../.venv && uv pip install -p ../.venv -r collect/requirements.txt
cp .env.example .env              # tablet address etc.; never committed
tools/hwr install                 # UI add-on + recognizer + model onto the tablet
```

`hwr install` keeps a model that is already on the tablet (pass `--model DIR` to replace it). Run it again after
a reMarkable OS update. `hwr uninstall [--purge]` removes it.

Building from source needs the Arm GNU aarch64 toolchain, Qt 6.10 headers/device libs and onnxruntime for
linux-aarch64 in `../sdk` (see `.cargo/config.toml` and `ui/build.sh`):
`ui/build.sh` and `cargo build --release --target aarch64-unknown-linux-gnu`.

## Privacy

No personal data goes into this repository or into contributed data: machine-specific settings live only in the
gitignored `.env`, and `hwr contribute` sends random writer ids, practice templates and pen strokes only.

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
