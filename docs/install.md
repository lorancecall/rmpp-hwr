# Installing rmpp-hwr: other ways and removal

The usual way is **rmpp setup** (see the README's [Install](../README.md#install)). This page is for Vellum on the
command line, the release files, and building and installing it yourself.

## Packages

| Package | What |
|---|---|
| `rmpp-hwr` | the add-on: lasso convert, Convert page, Settings > Handwriting, the recognizer and onnxruntime (needs `rmpp-preload`) |
| `rmpp-hwr-ko` | Korean model (the one trained on [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db)) |
| `rmpp-hwr-zh`, `rmpp-hwr-ja`, `rmpp-hwr-en`, `rmpp-hwr-latin` | base models of other languages |

Text object mode (recognized handwriting drawn as a movable, editable text object) also needs
[rmpp-textobj](https://github.com/lorancecall/rmpp-textobj) and the language's add-on of
[rmpp-lang](https://github.com/lorancecall/rmpp-lang) (its glyphs).

Files: the packages put theirs under `/home/root/.vellum/share/<package>/`; the add-on's UI library goes to
`/usr/lib/rmpp/rmpp-hwr/` and is loaded through `/usr/lib/rmpp/preload.d/60-rmpp-hwr.so`. Your own data (records,
practice pages, models you trained) stays in `/home/root/.local/share/rmpp-hwr/`: no package writes or deletes
there.

## With Vellum on the command line

On the tablet (SSH, developer mode), if Vellum is not there yet:

```sh
wget -qO- https://lorancecall.github.io/rmpp-packages/install.sh | sh
```

Then:

```sh
vellum add rmpp-hwr rmpp-hwr-ko          # add the other languages' models as you like: rmpp-hwr-en ...
vellum add rmpp-textobj                  # optional: text object mode
vellum upgrade                           # later: updates
vellum reenable                          # after a reMarkable OS update
```

## From the release files

Up to 0.3 rmpp-hwr was installed from its GitHub release: rmpp setup copied the release assets into one folder on
the tablet and ran `rmpp-install.sh` there as root (Korean model to `/home/root/.local/share/rmpp-hwr/`, other
languages as `pack-*.tar`). The script is still in the repository and in the releases, for an install by hand:
copy all assets of a release into one folder on the tablet and run `sh rmpp-install.sh` there. Packages are the
way from 0.4 on; installing them moves the release install's files aside.

## Building and installing it yourself (developers)

Requirements: reMarkable Paper Pro with developer mode and SSH (an `ssh rmpp` alias with a key), OS 3.29, and
`rmpp-preload` on the tablet. On the PC:

```sh
git clone https://github.com/lorancecall/rmpp-hwr && cd rmpp-hwr
uv venv ../.venv && uv pip install -p ../.venv -r collect/requirements.txt
cp .env.example .env              # tablet address etc.; never committed
tools/hwr install                 # UI add-on + recognizer + model onto the tablet
```

`hwr install` keeps a model that is already on the tablet (pass `--model DIR` to replace it); a model you trained
yourself goes to `models/<name>/` and is listed in Settings > Handwriting. `hwr uninstall [--purge]` removes a
`hwr install`.

Building needs the Arm GNU aarch64 toolchain, Qt 6.10 headers/device libraries and onnxruntime for linux-aarch64 in
`../sdk` (see `.cargo/config.toml` and `ui/build.sh`): `./build.sh` (or `ui/build.sh` and
`cargo build --release --target aarch64-unknown-linux-gnu`).

Restarting xochitl after copying files by hand: `systemctl reset-failed xochitl` first, then
`systemctl restart xochitl`. Never `systemctl stop xochitl` (the screen stays blank until the next restart).

## Removal

- Packages: `vellum del rmpp-hwr` (and the model packages, e.g. `vellum del rmpp-hwr-ko`). Your data in
  `/home/root/.local/share/rmpp-hwr/` stays; delete that folder to remove it too.
- Everything the rmpp add-ons installed: **Remove everything** in rmpp setup.
- A `hwr install`: `tools/hwr uninstall` (`--purge` also deletes the data folder).
