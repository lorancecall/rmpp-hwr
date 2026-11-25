# Training (personal handwriting model)

Everything here runs in a **work dir** on one of two backends, chosen in `rmpp-hwr/.env`:

| `HWR_BACKEND` | Where | How jobs run |
|---|---|---|
| `local` | this computer's GPU | in the background with bash; logs in `<work>/logs/hwr-<name>-<step>.log` |
| `slurm` | a cluster reached with `ssh` | `sbatch` (GPU jobs on `HWR_SLURM_GPU_PARTITION`, data/eval on `HWR_SLURM_CPU_PARTITION`), chained with `--dependency=afterok` |

`.env` is never committed. Copy `.env.example` and fill in the block for your backend; nothing about
machines, accounts or paths lives in the code.

## Settings

| Variable | Meaning |
|---|---|
| `HWR_BACKEND` | `local` or `slurm` |
| `HWR_AMP`, `HWR_AMP_DTYPE`, `HWR_AMP_LEVEL` | mixed precision. `bfloat16` needs an Ampere+ GPU (A100, L40, RTX 30xx/40xx); RTX 20xx/V100: `float16` or `HWR_AMP=0` |
| `HWR_LOCAL_WORK` | local work dir (~30 GB for synthetic data, checkpoints, exports) |
| `HWR_LOCAL_SETUP` | bash run before every local job, e.g. `source ~/miniconda3/etc/profile.d/conda.sh && conda activate hwr` |
| `HWR_LOCAL_GPU` | `CUDA_VISIBLE_DEVICES` |
| `HWR_SLURM_HOST` | ssh alias of the login node (set up keys / ProxyCommand in `~/.ssh/config`) |
| `HWR_SLURM_WORK` | work dir on the cluster file system |
| `HWR_SLURM_SETUP` | bash run at the start of every job, e.g. `module load ... && conda activate $HWR_WORK/env` |
| `HWR_SLURM_GPU_PARTITION`, `HWR_SLURM_GPU_QOS`, `HWR_SLURM_GPU_GRES` | where training/export run |
| `HWR_SLURM_CPU_PARTITION`, `HWR_SLURM_CPU_QOS` | where data generation and evaluation run |
| `HWR_SLURM_ACCOUNT` | optional `-A` |
| `HWR_RUN_CLEAN` | what `hwr run` deletes after a verified download: `run` (default), `all` or `none` |

Resources per job kind (Slurm): `gpu` 10 CPUs/48 GB/6 h, `gpu-short` 4/16 GB/40 min, `cpu` 8/16 GB/1 h,
`cpu-big` 32/64 GB/3 h, `tiny` 1/1 GB/5 min (`RES` in `tools/hwr.py`).

## Layout

```
train/jobs/      one bash script per step; every job starts through jobs/run.sh
train/scripts/   gen.py (synthetic lines), make_cfg.py (PaddleOCR config), evalonnx.py, fix_onnx.py, ...
train/patches/   PaddleOCR patch: HWR_EVAL_POOL=1 (default) trains with the inference pooling; FREEZE_BN=1 keeps BatchNorm statistics fixed
```

`hwr sync` copies `train/` to `<work>/code/` and writes `<work>/code/site.sh` (work dir, AMP, GPU, the setup
line) from `.env`. `jobs/run.sh` sources it, so the same job scripts run on both backends.

Work dir contents: `PaddleOCR/` (pinned commit + patch), `pretrained/korean.pdparams` and `orig_rec.onnx`
(base model), `data/` (dict, corpora, fonts, synthetic `train/` and `val/`, `personal/` from `hwr upload`),
`ckpt/`, `export/<name>/rec.onnx`, `logs/`, `best.txt` + `best_cer.txt` (best checkpoint so far),
`base_cer.txt` (base model CER on the 12 eval lines).

## Commands

```sh
hwr setup                                  # once: PaddleOCR + packages (inside the env from *_SETUP)
hwr job gen_base.sh --res cpu-big          # once: synthetic train/val lines
hwr train [NAME]                           # upload personal data -> gen_personal -> train -> export + judge
hwr status                                 # queue / background state, newest log, exports, best
hwr fetch NAME                             # export/NAME/rec.onnx -> hwr-train/out/NAME, eval through the Paper Pro pipeline
hwr run NAME                               # all of the above hands-off, then clean the backend (below)
hwr job probe.sh --res gpu-short           # training-mode sanity check of the base weights
hwr job eval.sh export/NAME/rec.onnx --res cpu
```

## Hands-off runs (`hwr run`)

`hwr run NAME` does a whole training round without anyone watching:

1. checks that the work dir is set up (PaddleOCR, synthetic data, base weights), uploads your lines (`hwr upload`)
   and `train/` (`hwr sync`);
2. submits `gen_personal.sh` -> `train.sh` -> `export.sh` (as `hwr train`);
3. waits, checking the jobs every `--interval` seconds (default 120; Slurm: `sacct`/`squeue`). If the cluster cannot
   be reached it keeps trying with a growing pause (up to 30 minutes), so a network hiccup only delays it. When a step
   fails, the steps after it are cancelled;
4. downloads `export/NAME/` (`rec.onnx`, `dict.txt`, `summary.txt`, `personal_cer.txt`) to `hwr-train/out/NAME/` and
   the job logs to `hwr-train/out/NAME/logs/`, and compares every file's size and sha256 with the cluster's copy.
   A failed run downloads only its logs;
5. only after that check passes, cleans up (`--clean`, default `HWR_RUN_CLEAN` or `run`):
   - `run`: what this run left on the backend: `export/NAME`, its logs, `cfg_NAME.yml`, its training lists and
     augmented lines, `code/`, and your uploaded handwriting (`data/personal/` and the personal validation lines
     made from it). Its checkpoints go too, except `latest.pdparams`/`latest.states` when `export.sh` promoted the
     run to `best.txt` (the next run starts from them). The shared setup (Python env, PaddleOCR, base weights,
     synthetic data) stays, so the next run starts at once;
   - `all`: the whole work dir (`hwr clean --yes`), including a Python env inside it. Nothing is left on the
     backend, and the next run needs the full setup again (env, `hwr setup`, base weights, `gen_base.sh`);
   - `none`: nothing;
6. scores the model (`hwr eval NAME`) when the host recognizer is built here; otherwise run that later.

Every step is recorded in `hwr-train/out/runs/NAME.json`. Run the same command again after an interruption (closed
terminal, reboot, lost network) and it continues from where it stopped: it never submits a run twice and never
cleans before a verified download. A finished run only prints where its results are.

Run it so it survives logging out, e.g. on a server that reaches the cluster with its own ssh key (an entry for
`HWR_SLURM_HOST` in `~/.ssh/config`, key without a passphrase or loaded in an agent, since nobody types one):

```sh
tmux new -s hwr 'tools/hwr run my-run; read'          # detach: Ctrl-b d; back: tmux attach -t hwr
nohup tools/hwr run my-run > run-my-run.log 2>&1 &    # or without tmux; tail -f run-my-run.log
```

Check the pipeline itself (submit, chained jobs, wait, download, checksums, cleanup) without data, GPU or Python
env: `hwr run check-1 --selftest` runs two one-CPU jobs of a few seconds (`jobs/selftest.sh`) and keeps the
download in `hwr-train/out/runs/check-1-selftest/`. Try it in a separate test work dir first, e.g.
`HWR_SLURM_WORK=<new empty dir> hwr run check-1 --selftest --clean all`.

## Cleaning the backend (`hwr clean`)

```sh
hwr clean --run NAME      # one run's files and the uploaded handwriting (as `hwr run --clean run`)
hwr clean                 # the whole work dir: lists it with its size, then asks you to type its name
hwr clean --yes           # the same without asking (scripts)
```

Deleting is refused when the work dir path is empty, relative, contains `..`, is `/` or directly below it, is the
home dir or one of its parents, has no `.hwr-work` marker (`hwr sync` writes it, so only folders hwr has used
qualify), or while hwr jobs are still queued or running. Nothing outside `HWR_SLURM_WORK` (or `HWR_LOCAL_WORK`) is
ever deleted. A Python env counts as part of the work dir only if it lives inside it.

## Job settings

`train.sh` reads `EPOCHS`, `LR`, `WIDTH`, `FREEZE_BN`; `gen_personal.sh` reads `PERS_X` (augmented copies per
personal line) and `REPLAY` (synthetic lines mixed in). Set them in `HWR_*_SETUP` or export them in a custom job.

## Keeping English (and print) from being forgotten

Fine-tuning on Korean handwriting alone made English worse (synthetic English CER 0.140 -> 0.233 in the first
runs). Two measures are built in:

1. **Real English handwriting: IAM-OnDB.** The IAM On-Line Handwriting Database has pen strokes of ~200
   writers, so `gen.py iamon` renders them exactly like Paper Pro input. Its terms allow **non-commercial research
   use only**, ask for registration and a citation, and the data itself must not be redistributed. Every user
   registers and downloads it themselves (fki.tic.heia-fr.ch, "IAM On-Line Handwriting Database"):
   `lineStrokes-all.tar.gz` and `ascii-all.tar.gz`, unpacked into one folder with `lineStrokes/` and `ascii/`. Then

   ```sh
   hwr dataset <that folder> iamondb
   hwr job gen_iamon.sh --res cpu-big     # data/train/replay_extra.list + data/val/iamon_val_7777.txt
   ```

   `gen_personal.sh` mixes `REPLAY_EN` (default 20000) of these lines into every personal fine-tune, and
   `eval.sh`/`export.sh` report English handwriting CER (`iamon_val_7777`). Models trained with it are for
   personal/research use; they are not distributed with the packages.
2. **Weight-space blending (WiSE-FT).** `hwr job wise.sh <name> --res gpu-short` blends the base weights with
   `ckpt/<name>/latest` at alphas 0.2-0.8, exports and judges each; the best blend can become `best.txt`.

Cite IAM-OnDB in anything published from these models: M. Liwicki and H. Bunke, "IAM-OnDB - an On-Line
English Sentence Database Acquired from Handwritten Text on a Whiteboard", ICDAR 2005.

## Setting up the local backend

PaddlePaddle GPU builds are per CUDA version: install `paddlepaddle-gpu` matching the driver
(see paddlepaddle.org.cn install guide), in its own conda env, then point `HWR_LOCAL_SETUP` at that env.
On Windows, jobs run in Git Bash; use forward slashes in `HWR_LOCAL_WORK` (e.g. `D:/hwr-work`).
