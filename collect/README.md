# collect: handwriting to training data

PC-side tool. Reads (never writes) the tablet over ssh and turns your handwriting into
`line image <TAB> text` pairs for the Korean/English/digit recognizer. One file: `collect.py`.

```
python -m venv .venv && .venv/Scripts/pip install -r requirements.txt
python -I collect.py <command> ...
```

The line splitting and the 4 px disc-stamp / 12 px pad renderer mirror `src/page.rs`, so training
images look like what the model sees at inference.

## Two sources of data

1. **Conversions.** Use "Convert page" on the tablet, then fix the typed text on the new page. `pull`
   pairs each handwritten line with the corrected text line.
2. **Practice notebook ("보고 따라 쓰기").** A PDF with a prompt and an empty writing band per
   sentence; you copy the prompt by hand. The prompt is the label, so no correcting is needed.

## Commands

```
# pull everything new (read-only: ssh cat/grep only)
python -I collect.py pull --host 10.11.99.1 --out data [--user root] [--key ~/.ssh/id_ed25519_rmpp] [--sidecar practice.json]

# debug: render a page and its lines (page.png has red boxes for the detected lines)
python -I collect.py render page1.rm --out dbg

# practice notebook
python -I collect.py practice --out practice.pdf --count 30 --seed 1     # also writes practice.json
python -I collect.py make-doc --pdf practice.pdf --name rmpp-practice-1 --out stage
```

### pull output

```
data/dataset.tsv                       all pairs: samples/<..>/lines/NN.png <TAB> text (paths relative to data/)
data/samples/<time>_<srcPage>/         one conversion
    original.rm original.png lines/NN.png converted.txt corrected.txt meta.json
    pairs.tsv                          when original line count == corrected line count
    needs_review.txt                   otherwise (explains the mismatch)
data/samples/practice_<docId>/NNN.png, pairs.tsv, meta.json    practice bands, label = prompt text
data/practice/*.json                   sidecars remembered between pulls
```

- `meta.json` has `edited` (corrected != converted). Unedited samples are kept; filter on `edited`
  if you only want the recognizer's mistakes.
- Incremental: a conversion sample that already exists is skipped; a practice doc is re-processed only
  when its strokes changed. `dataset.tsv` is rebuilt from all `pairs.tsv` files each time, so you can
  hand-fix a `needs_review` sample (write its `pairs.tsv`) and run `pull` again.
- Blank lines are ignored on both sides when counting lines.
- Practice strokes are assigned to a band by the vertical centre of each stroke (60 px above to
  30 px below the band). `pull` reports strokes outside every band; if that number is large the
  page offset is wrong, try `--y-offset`.

## Practice PDF on the tablet

`practice` makes a 1620x2160 px page at 229 dpi (509.3 x 679.1 pt), 6 prompts per page. PDF
coordinates map to `.rm` coordinates as x = px - 810 (centred on 0), y = px from the top; the
sidecar `practice.json` stores each prompt text and its band rectangle in those `.rm` coordinates.
The font is Noto Sans KR (OFL), embedded as a subset; it is taken from `C:/Windows/Fonts` or
downloaded once into `collect/fonts/` (6 MB, not meant for git).

`make-doc` writes `<uuid>.pdf`, `<uuid>.metadata`, `<uuid>.content` into `stage/` and records the
uuid in `practice.json`. To put it on the tablet (you do this by hand):

```
scp stage/<uuid>.* root@10.11.99.1:/home/root/.local/share/remarkable/xochitl/
ssh root@10.11.99.1 systemctl restart xochitl     # the document list is reloaded
```

The document shows up as `rmpp-practice-1` (the name must start with `rmpp-practice`; `pull` finds
practice documents by that prefix). Write each prompt in its band, then run `pull`.
To make more, run `practice` with another `--seed` and a new `--name` (`rmpp-practice-2`).
