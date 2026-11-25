#!/bin/bash
# usage: selftest.sh <name> <step>   pipeline check for `hwr run NAME --selftest`: no GPU, no data, no Python env.
# Step 1 writes export/<name>/rec.onnx (1 MiB of random bytes, not a model); step 2 checks that step 1 ran first and
# writes summary.txt and DONE. FAIL=1 makes the job fail (to check the failure path).
set -eo pipefail
name=${1:?name}; step=${2:-1}; O=export/$name
echo "[selftest] step $step of $name"
if [ "${FAIL:-0}" = 1 ]; then echo "[selftest] failing on purpose (FAIL=1)"; exit 3; fi
mkdir -p "$O"
if [ "$step" = 1 ]; then
  head -c 1048576 /dev/urandom > "$O/rec.onnx.part" && mv "$O/rec.onnx.part" "$O/rec.onnx"
else
  test -s "$O/rec.onnx"
  echo "selftest $name: step 1 output $(wc -c < "$O/rec.onnx") bytes" > "$O/summary.txt"
  touch "$O/DONE"
fi
echo "[selftest] step $step done"
