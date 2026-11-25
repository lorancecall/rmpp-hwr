# rmpp-hwr

![Lasso handwriting, tap Convert: typed text on its line, or a text object you can move and edit](docs/rmpp-hwr.gif)

*Lasso your handwriting and tap Convert: typed text on the line where you wrote it, or a text object you can move and edit.*

**Open handwriting recognition for the reMarkable Paper Pro, running on the device itself**: no cloud, no account,
your notes never leave the device. Select handwriting with the lasso and turn it into typed text, recognize a whole
page, and train a model on your own handwriting. It works with [rmpp-textobj](https://github.com/lorancecall/rmpp-textobj)'s
text objects (typed text drawn as pen strokes): convert straight to a text object if you like, and text objects
inside a selection keep their text. Runs without xovi and keeps working after a reboot.

Made for and tested on the reMarkable Paper Pro with OS 3.29. Other reMarkable models are not supported.
Updates for newer OS versions may take some time to follow.

> Part of the **rmpp add-ons**, built on [rmpp-packages](https://github.com/lorancecall/rmpp-packages): it is
> installed by rmpp setup, loaded into xochitl at every boot by rmpp-preload (no xovi), and kept up to date with
> Vellum. How that differs from xovi: [how it works](https://github.com/lorancecall/rmpp-packages/blob/main/docs/how-it-works.md).
>
> Status: beta.

> **Help it read your language: join [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db).** Every language
> needs handwriting: the Korean model is fine-tuned on a still small set and gets better with each notebook shared,
> and the other languages have none yet ([languages and models](#languages-and-models)). rmpp-hwr-db is an open
> dataset of real pen strokes written on the Paper Pro; it is just starting, so the first writers shape it the most.
> [**Join as a writer**](https://lorancecall.github.io/rmpp-hwr-db/) · [see the dataset](https://github.com/lorancecall/rmpp-hwr-db)
> · How: write practice pages on the Paper Pro (Settings > Handwriting > Collect training data) and share them with
> `hwr contribute`, anonymized (CC BY 4.0). Your language is missing? Ask for it there.

## Install

Start at **[rmpp-packages](https://github.com/lorancecall/rmpp-packages#install)**: it explains developer mode and
has **rmpp setup**, the installer for all rmpp add-ons.

- In rmpp setup (the desktop app or the script), tick **Handwriting recognition (beta)** (rmpp-hwr; not selected by
  default). The other languages' models are listed under it.
- The Korean model `rmpp-hwr-ko` is ticked by default; `rmpp-textobj`, `rmpp-lang` and `rmpp-preload` are
  installed with it ([all packages and dependencies](https://github.com/lorancecall/rmpp-packages/blob/main/docs/packages.md)).
- After a reMarkable OS update: **Restore** in rmpp setup.
- Installing without rmpp setup, uninstalling, using it next to other mods, and building: [docs/install.md](docs/install.md).

## Use

- **Lasso convert** (lasso menu): handwriting to typed text on its line. It works with rmpp-textobj's text objects
  too: in text object mode the text comes out as a text object you can move and edit, and text objects already in the
  selection keep their text while the handwriting around them is recognized.
- **Convert page** (toolbar): the whole page to text, on a new page or into the page.
- **Models** (Settings > Handwriting): pick the language and model, or switch back to reMarkable's converter.

Details: [docs/usage.md](docs/usage.md).

## Languages and models

| Language | Package | Model |
|---|---|---|
| Korean | `rmpp-hwr-ko` | PP-OCRv5, **fine-tuned on a small handwriting set**, improving with more ([model card](MODEL_CARD.md)) |
| English, Chinese, Japanese, Latin-script languages | `rmpp-hwr-en`, `-zh`, `-ja`, `-latin` | PaddleOCR PP-OCRv5 base, **no handwriting data yet** |

Every language gets better with more handwriting, and that is what
[rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db) collects. The Korean model is fine-tuned on it, still from
a few writers, and each new writer makes it read more hands. The other languages use base models, general text
recognizers that read neat handwriting but miss more of it; they get fine-tuned once there is handwriting in that
language. Write a few practice pages in your language and share them:
[**join as a writer**](https://lorancecall.github.io/rmpp-hwr-db/). Meanwhile, you can also train a model on your
own handwriting ([docs/develop.md](docs/develop.md)).

## How it works

A Rust recognizer (PaddleOCR PP-OCRv5 models, ONNX) runs on the Paper Pro; a small add-on in xochitl adds the buttons
and settings, and hands text objects to [rmpp-textobj](https://github.com/lorancecall/rmpp-textobj). The Korean
model is fine-tuned on rmpp-hwr-db, still small and growing; you can also fine-tune one on your own practice pages. The code, training and references:
[docs/develop.md](docs/develop.md).

Privacy: nothing leaves the Paper Pro when you convert; `hwr contribute` sends random writer ids and pen strokes only.

## Build

[docs/install.md#build-and-install-by-hand](docs/install.md#build-and-install-by-hand).

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
