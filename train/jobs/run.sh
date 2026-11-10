#!/bin/bash
# Entry point for every job, on Slurm or locally. Run from the work dir (sbatch's submit dir / hwr cd's there).
# usage: bash code/jobs/run.sh <job.sh> [args...]
# code/site.sh is written by `hwr sync` from rmpp-hwr/.env: HWR_WORK, HWR_AMP*, CUDA_VISIBLE_DEVICES, env setup.
set -eo pipefail
export HWR_WORK=${HWR_WORK:-$PWD}
export HWR_EVAL_POOL=${HWR_EVAL_POOL:-1}   # train with the inference pooling (patches/paddleocr-rmpp.patch)
export FREEZE_BN=${FREEZE_BN:-1}           # keep BatchNorm statistics: zero-padded wide batches corrupt them
. "$HWR_WORK/code/site.sh"
cd "$HWR_WORK"
echo "[hwr] $(date '+%F %T') $1 ${*:2} on $(hostname)"
job=$1; shift
exec bash "$HWR_WORK/code/jobs/$job" "$@"
