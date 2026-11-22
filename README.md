# rmpp-hwr

![Lasso handwriting, tap Convert: typed text on its line, or a text object you can move and edit](docs/rmpp-hwr.gif)

*Lasso your handwriting and tap Convert: typed text on the line where you wrote it, or a text object you can move and edit (v0.3).*

**Open handwriting recognition for the reMarkable Paper Pro, running on the tablet itself**: no cloud, no account,
your notes never leave the device. Select handwriting with the lasso and turn it into clean text or tidy pen
strokes, recognize a whole page, and train a model on your own handwriting. Runs without xovi and keeps working
after a reboot.

Tested on reMarkable Paper Pro, OS 3.29.1.

> Part of the **rmpp add-ons**, built on [rmpp-packages](https://github.com/lorancecall/rmpp-packages): it is
> installed by rmpp setup, loaded into xochitl at every boot by rmpp-preload (no xovi), and kept up to date with
> Vellum. How that differs from xovi: [how it works](https://github.com/lorancecall/rmpp-packages/blob/main/docs/how-it-works.md).
>
> Status: experimental.

> **Help it read your language: join [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db).** The recognizer
> only gets better with the handwriting people share. rmpp-hwr-db is an open dataset of real pen strokes written on
> the Paper Pro; it is just starting, so the first writers shape it the most.
> [**Join as a writer**](https://lorancecall.github.io/rmpp-hwr-db/) · [see the dataset](https://github.com/lorancecall/rmpp-hwr-db)
> · How: write practice pages on the tablet (Settings > Handwriting > Collect training data) and share them with
> `hwr contribute`, anonymized (CC BY 4.0). Your language is missing? Ask for it there.

## Install

Start at **[rmpp-packages](https://github.com/lorancecall/rmpp-packages#install)**: it explains developer mode and
has **rmpp setup**, the installer for all rmpp add-ons.

- In rmpp setup (the desktop app or the script), tick **Handwriting recognition (beta)** (rmpp-hwr; not selected by
  default). The other languages' models are listed under it.
- It installs the Korean model `rmpp-hwr-ko`, `rmpp-textobj`, `rmpp-lang` and `rmpp-preload` with it
  ([all packages and dependencies](https://github.com/lorancecall/rmpp-packages/blob/main/docs/packages.md)).
- After a reMarkable OS update: **Restore** in rmpp setup.
- Without rmpp setup, removing it, next to other mods, building it: [docs/install.md](docs/install.md).

## Use

- **Lasso convert** (lasso menu): handwriting to typed text on its line, or to a movable, editable text object.
- **Convert page** (toolbar): the whole page to text, on a new page or into the page.
- **Models** (Settings > Handwriting): pick the language and model, or switch back to reMarkable's converter.

Details: [docs/usage.md](docs/usage.md).

## How it works

A Rust recognizer (PaddleOCR PP-OCRv5 models, ONNX) runs on the tablet; a small add-on in xochitl adds the buttons
and settings, and hands text objects to [rmpp-textobj](https://github.com/lorancecall/rmpp-textobj). Models are
fine-tuned on rmpp-hwr-db and your own practice pages. The code, training and references:
[docs/develop.md](docs/develop.md).

## Build

[docs/install.md#build-and-install-by-hand](docs/install.md#build-and-install-by-hand).

Privacy: nothing leaves the tablet when you convert; `hwr contribute` sends random writer ids and pen strokes only.

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
