# Teaching the recognizer your handwriting

How handwriting goes from the tablet to a personal model and back. Every step on the PC is one `hwr`
command (`rmpp-hwr/tools/hwr`, or `hwr.cmd` on Windows).

```
 tablet                                  PC                                   training GPU (local or Slurm)
 ─────────────────────────────           ───────────────────────────          ─────────────────────────
 1 practice notebook  ◄── hwr practice
   write the sentences
 2 Convert page, fix the text,
   Done correcting
                      ──── hwr pull ───► 3 hwr-data/collected
                                           (line images + text)
                                                      ──── hwr train ───► 4 synthetic + your lines,
                                                                            fine-tune, export
                                         5 hwr fetch NAME   ◄──────────────    export/NAME/rec.onnx
                                           (error rate on your 12 eval lines)
 6 new model          ◄── hwr deploy NAME  (only if it beats the current one)
```

## Setup (once)

- The tablet is reachable as `ssh rmpp` (key `~/.ssh/id_ed25519_rmpp`). A VPN on the PC can block the
  local network: turn it off or allow LAN.
- Training: copy `rmpp-hwr/.env.example` to `rmpp-hwr/.env` and choose `HWR_BACKEND=local` (this PC's GPU)
  or `slurm` (a cluster over ssh); see `train/README.md`. Then `hwr setup` once.
- Python: `rmpp-dev/.venv` with `collect/requirements.txt` (`uv venv .venv && uv pip install -r rmpp-hwr/collect/requirements.txt`).

## 1. Practice notebooks: copy sentences by hand

```
hwr practice 100                     # rmpp-practice-100: 100 lines, mixed story / talk / notes
hwr practice 30 --text talk          # only conversations
hwr practice 60 --text my-notes.txt  # your own text
hwr practice --list                  # which texts exist, how many lines each
```

- The notebook appears in the tablet library. Write each sentence inside its box, one line per box.
- The sentence is the label, so nothing needs correcting. This is the fastest way to collect data.
- Text files: one line per box (long lines are wrapped at spaces), a blank line between passages.
  Built-in texts are in `collect/texts/`. Passages used in earlier notebooks are picked last.
- The name is `rmpp-practice-<lines>`; if that exists, `-2`, `-3`, ... is added. Only documents whose
  name starts with `rmpp-practice` are collected.

## 2. Convert pages you write anyway

1. On a page with handwriting: toolbar, Convert menu, **Convert page**. The text is typed on a new page
   right after it. A copy of the handwriting is kept on the tablet at that moment, so editing or deleting
   the handwritten page later does not change the sample.
2. Fix the mistakes on the typed page. Deleting a junk line (a stray mark read as "/") is fine.
3. Convert menu, **Done correcting**. Only conversions marked like this are used for training. You can
   keep editing afterwards: the text is read again at every `hwr pull`. Tap again to undo.

Check what is saved in **Settings > Handwriting > Collect training data**: each record is one handwritten
page and its typed page ("page 3 → 4"), with its status. **View** shows the recognized text next to the
text on the page now; **Delete** removes the record (the notebook pages stay). "Save conversions: Off"
stops recording; Convert page keeps working.

## 3. Collect on the PC

```
hwr pull           # practice notebooks + corrected conversions -> hwr-data/collected
hwr pull --keep    # same, but leave the conversions on the tablet
```

- Output: `hwr-data/collected/dataset.tsv` (`image path <TAB> text`) and `samples/`. For each conversion:
  `original.rm/png` (handwriting), `converted.txt` (recognized), `corrected.txt` (your text),
  `lines/NN.png`, `pairs.tsv`.
- Corrected conversions are removed from the tablet after they are on the PC; the PC copy stays.
- Waiting (not corrected) ones stay on the tablet. A record deleted on the tablet before it was pulled is
  removed on the PC too.
- If the handwriting has a different number of lines from the typed page, lines are matched by content;
  unmatched lines are left out. If even that is impossible, the sample says `needs_review.txt`.
- If the tablet falls asleep during a pull, earlier results are kept; wake it and run `hwr pull` again.

## 4. Train

```
hwr train              # upload hwr-data/collected, start retraining (name: my-YYYYMMDD-HHMM)
hwr train my-run        # pick the name
hwr status             # jobs and the last log lines
```

`hwr train` runs on the backend from `.env`: generate augmented copies of your lines (plus synthetic
lines so nothing is forgotten), fine-tune from the current best model (or the base model), export
`export/NAME/rec.onnx` and score it. A model only becomes the new best if it beats the old one.

## 5. Check the new model

```
hwr fetch my-run        # download and score it
hwr eval               # score the model that is on the tablet now
hwr models             # all downloaded models and their scores
```

The score is the character error rate on the first 12 lines of `rmpp-practice-1`. Those lines are never
used for training, so models are compared fairly. Lower is better. The base model scores 0.257.

## 6. Install it

```
hwr deploy my-run      # install training run my-run on the tablet (the old one is kept)
hwr rollback           # go back to the previous model
```

Each deployed model gets the next version name, `rmpp-hwr-korean-v0.1`, `-v0.2`, ...; the training run it came from
is recorded in `hwr-train/out/releases.json` (`hwr models` lists both). Settings > Handwriting > Model then shows
"rmpp-hwr-korean-v0.3 (error NN%)". The base model is `rmpp-hwr-korean-base`. Only deploy a model that scores lower
than the one on the tablet.

## Status (2026-11-10)

- Collected: 100 lines (rmpp-practice-1 and -2, one corrected page). `rmpp-practice-100` is on the
  tablet, not written yet.
- The first fine-tuned models were worse on your handwriting than the base model (0.32 and up vs 0.257),
  so the tablet still runs the base model. The training setup is being investigated (training-mode
  forward pass, mixed precision, BatchNorm, English forgetting).
- `fetch`, `deploy`, `rollback` and `train` have not been run end to end yet.
