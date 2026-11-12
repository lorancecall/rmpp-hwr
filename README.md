# rmpp-hwr

**Open handwriting recognition for the reMarkable Paper Pro, running on the tablet itself**: no cloud, no account,
your notes never leave the device. Select handwriting with the lasso and turn it into clean text or tidy pen
strokes, recognize a whole page, and train a model on your own handwriting.

It starts with Korean, English and digits, and grows with the reMarkable community: the open dataset
[rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db) collects handwriting in many languages from people who
write on the tablet, `hwr contribute` shares your practice pages to it (anonymized, CC BY 4.0), and the public
models are trained on it regularly. Every language that gathers writers there can be recognized here next.

On the tablet:

- **Convert to ink** (lasso menu): select handwriting with the lasso and tap the convert button (the "aa" icon in a
  dashed box). The recognized text is written below the selection as ordinary pen strokes (Fineliner, clean
  Noto Sans KR glyphs), already selected so you can move or resize it at once. Double tap it (or tap the button
  again) to **edit the text** in a keyboard box (Korean works with rmpp-korean); OK redraws it in place, wrapped at
  its width. Settings > Handwriting switches the lasso button back to reMarkable's own Convert to text.
- **Convert page** (toolbar, next to Convert to text): recognizes the whole page and types it on a new page.
- **Models** (Settings > Handwriting): pick the recognition model. The deployed model is `rec.onnx`/`dict.txt`/
  `model.txt` in `/home/root/.local/share/rmpp-hwr/`; more models go in `models/<name>/` with the same three files
  and are listed by the first line of their `model.txt`. "Off" turns rmpp-hwr's recognizer off: the lasso button is
  reMarkable's own Convert to text and Convert page is hidden.

> Status: experimental. rmpp-hwr is installed and updated with its own PC tool (`hwr install`), not as a Vellum
> package, because models change often. It only needs `rmpp-preload` from
> [rmpp-packages](https://github.com/lorancecall/rmpp-packages) on the tablet.

## What is here

| Path | What |
|---|---|
| `src/` | Rust recognizer: reads `.rm` pages, renders lines, runs the PaddleOCR PP-OCRv5 Korean recognition model (ONNX, onnxruntime). Also exports the typed text of a page. |
| `ui/` | xochitl add-on (LD_PRELOAD): lasso Convert to ink and text editing (`ui/ink.h`), "Convert page" in the toolbar, Settings > Handwriting (records, corrections, model info). `ui/exp/`: experiments for the developer loader (`DEV=1 ui/build.sh`). |
| `ink/` | `glyphs.py`: builds `glyphs.json` (text -> pen strokes) from the centre lines of Noto Sans KR (OFL, see NOTICE). `own.py`: builds `glyphs_own.json`, your own handwriting (practice cells, characters cut out of practice lines and checked by the recognizer, Hangul composed from your own jamo), for "Ink handwriting: My handwriting"; it stays on your PC and tablet. |
| `model/` | base model `rec.onnx` + `dict.txt` (PaddleOCR `korean_PP-OCRv5_mobile_rec`, Apache-2.0, see NOTICE). |
| `collect/` | practice notebooks (v1 lines, v2 structured pages with per-character cells), pulling handwriting from the tablet, line/character images. |
| `tools/hwr` | one command for the whole loop: `practice`, `pull`, `qc`, `upload`, `train`, `fetch`, `eval`, `deploy`, `contribute`, ... |
| `train/` | fine-tuning jobs that run on a local GPU or a Slurm cluster, configured by `.env` (see `train/README.md`). |
| `references/` | literature notes on improving personal handwriting recognition (cumulative). |

## Guides

- [WORKFLOW.md](WORKFLOW.md): from practice notebook to personal model on the tablet.
- [train/README.md](train/README.md): training backends (`.env`: local GPU or Slurm), jobs, known pitfalls.
- [references/README.md](references/README.md): what to try next and why.
- [references/convert-to-ink.md](references/convert-to-ink.md): how Convert to ink works inside xochitl (findings,
  internals used, limits).
- Open dataset: [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db), the community's handwriting in many
  languages (how to take part, in every supported language: https://lorancecall.github.io/rmpp-hwr-db/).
  `hwr contribute` shares structured practice pages, anonymized; your language is missing? Ask for it there.

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
`./build.sh` (or `ui/build.sh` and `cargo build --release --target aarch64-unknown-linux-gnu`), and
`python ink/glyphs.py build -o build/glyphs.json` (about 90 s; downloads Noto Sans KR the first time).

## Privacy

No personal data goes into this repository or into contributed data: machine-specific settings live only in the
gitignored `.env`, and `hwr contribute` sends random writer ids, practice templates and pen strokes only.

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
