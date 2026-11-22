# Installing rmpp-hwr

The usual way is **rmpp setup** from [rmpp-packages](https://github.com/lorancecall/rmpp-packages#install), as the
desktop app or the script: tick **Handwriting recognition (beta)**. This page covers the rest. Steps that are the
same for every rmpp add-on live in rmpp-packages and are linked here, not repeated.

## Packages

| Package | Installs with it |
|---|---|
| `rmpp-hwr` (lasso convert, Convert page, Settings > Handwriting, the recognizer) | `rmpp-hwr-ko`, `rmpp-textobj`, `rmpp-lang`, `rmpp-preload` |
| `rmpp-hwr-ko` (Korean model, trained on [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db)) | `rmpp-hwr` |
| `rmpp-hwr-zh`, `rmpp-hwr-ja`, `rmpp-hwr-en`, `rmpp-hwr-latin` (base models of other languages) | `rmpp-hwr` |
| `rmpp-textobj` (text objects, used for text object mode) | `rmpp-lang`, `rmpp-preload` |

Dependencies are installed automatically, in rmpp setup and with `vellum add`. In rmpp setup the other languages'
models are listed under Handwriting recognition. All packages and what needs what:
[packages.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/packages.md).

## Without rmpp setup

Follow [manual-install.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/manual-install.md) (Vellum and
the rmpp package repository), then on the Paper Pro:

```sh
vellum add rmpp-hwr            # with rmpp-hwr-ko, rmpp-textobj, rmpp-lang, rmpp-preload
vellum add rmpp-hwr-en         # other languages' models as you like: rmpp-hwr-zh, -ja, -latin
```

Older ways, for developers and old installs:

- **`hwr install`** from a clone of this repository (your own models, development builds): see
  [Build and install by hand](#build-and-install-by-hand).
- **Release files** (how rmpp setup installed rmpp-hwr up to 0.3): the GitHub releases carry the files and
  `rmpp-install.sh`; copy all assets of a release into one folder on the Paper Pro and run `sh rmpp-install.sh` there
  as root. Installing the packages moves the files of such an install aside.

## Uninstall

- **Just this add-on:** untick it in rmpp setup and press **Remove unticked installed**, or
  `vellum del rmpp-hwr rmpp-hwr-ko` (and any other model packages). `rmpp-textobj`, `rmpp-lang` and `rmpp-preload`
  stay while other add-ons use them; remove them the same way if nothing else does.
- **What stays:** your data in `/home/root/.local/share/rmpp-hwr/` (conversion records, practice pages, models you
  trained) and the settings in `/home/root/.config/rmpp-hwr/`. Text objects already on your pages stay as pen
  strokes, with their records in `/home/root/.local/share/rmpp-textobj/objects.json`. Delete these by hand to start
  fresh, or use step 3 of [uninstall.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/uninstall.md).
- **A `hwr install`:** `tools/hwr uninstall` (`--purge` also deletes the data folder).
- **Everything rmpp, and before a factory reset:** [uninstall.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/uninstall.md).

## Next to other mods

It runs next to xovi and other Vellum packages: [how-it-works.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/how-it-works.md#next-to-other-mods).
reMarkable's own Convert to text stays available: Settings > Handwriting switches the lasso button back to it.

## Build and install by hand

For development and your own models. Requirements: developer mode and SSH (an `ssh rmpp` alias with a key), and the
packages installed (above, or rmpp setup). On the PC:

```sh
git clone https://github.com/lorancecall/rmpp-hwr && cd rmpp-hwr
uv venv ../.venv && uv pip install -p ../.venv -r collect/requirements.txt
cp .env.example .env              # Paper Pro address etc.; never committed
tools/hwr install                 # UI add-on + recognizer + model onto the Paper Pro
```

`hwr install` keeps a model that is already on the Paper Pro (pass `--model DIR` to replace it); a model you trained
yourself goes to `models/<name>/` and is listed in Settings > Handwriting.

Building needs the Arm GNU aarch64 toolchain, Qt 6.10 headers/device libraries and onnxruntime for linux-aarch64 in
`../sdk` (see `.cargo/config.toml` and `ui/build.sh`): `./build.sh` (or `ui/build.sh` and
`cargo build --release --target aarch64-unknown-linux-gnu`). Training and the code: [develop.md](develop.md).

The package recipes (VELBUILD) are in
[rmpp-packages/packages](https://github.com/lorancecall/rmpp-packages/tree/main/packages); the next `vellum upgrade`
or reinstall puts the released files back.

On the Paper Pro, never run `systemctl stop xochitl` (it powers the Paper Pro off); run `systemctl reset-failed xochitl`
before `systemctl restart xochitl` (repeated restarts otherwise hit systemd's limit). The root file system is
read-only: `mount -o remount,rw /` before changing files under `/usr/lib/rmpp`, `mount -o remount,ro /` after.
