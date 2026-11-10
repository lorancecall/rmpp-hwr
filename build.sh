#!/bin/sh
# Build everything that goes onto the tablet: the UI add-on (build/librmpp-hwr-ui.so) and the recognizer
# (target/aarch64-unknown-linux-gnu/release/librmpp_hwr.so).
# Rust embeds source paths in panic messages; --remap-path-prefix replaces the local ones (home folder, cargo
# registry, this checkout) with neutral paths so no user or machine names end up in released binaries.
set -e
cd "$(dirname "$0")"
CARGO_HOME=${CARGO_HOME:-$HOME/.cargo}
RUSTUP_HOME=${RUSTUP_HOME:-$HOME/.rustup}
abs() { (cd "$1" 2>/dev/null && (pwd -W 2>/dev/null || pwd)) || echo "$1"; }   # Windows form under Git Bash
here=$(abs .); cargo_home=$(abs "$CARGO_HOME"); rustup_home=$(abs "$RUSTUP_HOME")
flags=""
for pair in "$cargo_home=/cargo" "$rustup_home=/rustup" "$here=/rmpp-hwr"; do
  from=${pair%%=*}; to=${pair#*=}
  flags="$flags --remap-path-prefix=$from=$to"
  case "$from" in *:*) flags="$flags --remap-path-prefix=$(printf '%s' "$from" | tr '/' '\\')=$to";; esac
done
export RUSTFLAGS="${RUSTFLAGS}$flags"
cargo build --release --target aarch64-unknown-linux-gnu --lib
ui/build.sh
ls -la target/aarch64-unknown-linux-gnu/release/librmpp_hwr.so build/librmpp-hwr-ui.so
