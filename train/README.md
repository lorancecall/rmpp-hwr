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

Resources per job kind (Slurm): `gpu` 10 CPUs/48 GB/6 h, `gpu-short` 4/16 GB/40 min, `cpu` 8/16 GB/1 h,
`cpu-big` 32/64 GB/3 h (`RES` in `tools/hwr.py`).

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
hwr fetch NAME                             # export/NAME/rec.onnx -> hwr-train/out/NAME, eval on the tablet pipeline
hwr job probe.sh --res gpu-short           # training-mode sanity check of the base weights
hwr job eval.sh export/NAME/rec.onnx --res cpu
```

`train.sh` reads `EPOCHS`, `LR`, `WIDTH`, `FREEZE_BN`; `gen_personal.sh` reads `PERS_X` (augmented copies per
personal line) and `REPLAY` (synthetic lines mixed in). Set them in `HWR_*_SETUP` or export them in a custom job.

## Keeping English (and print) from being forgotten

Fine-tuning on Korean handwriting alone made English worse (synthetic English CER 0.140 -> 0.233 in the first
runs). Two measures are built in:

1. **Real English handwriting: IAM-OnDB.** The IAM On-Line Handwriting Database has pen strokes of ~200
   writers, so `gen.py iamon` renders them exactly like tablet input. Its terms allow **non-commercial research
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
