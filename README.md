# rmpp-hwr

![Lasso handwriting, tap Convert: typed text on its line, or a text object you can move and edit](docs/rmpp-hwr.gif)

*Lasso your handwriting and tap Convert: typed text on the line where you wrote it, or a text object you can move and edit (v0.3).*

**Open handwriting recognition for the reMarkable Paper Pro, running on the tablet itself**: no cloud, no account,
your notes never leave the device. Select handwriting with the lasso and turn it into clean text or tidy pen
strokes, recognize a whole page, and train a model on your own handwriting.

> **Help it read your language: join [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db).** The recognizer
> only gets better with the handwriting people share. rmpp-hwr-db is an open dataset of real pen strokes written on
> the Paper Pro; it is just starting, so the first writers shape it the most.
> [**Join as a writer**](https://lorancecall.github.io/rmpp-hwr-db/) · [see the dataset](https://github.com/lorancecall/rmpp-hwr-db)
> · How: write practice pages on the tablet (Settings > Handwriting > Collect training data) and share them with
> `hwr contribute`, anonymized (CC BY 4.0). Your language is missing? Ask for it there.

> Part of the **rmpp add-ons**, built on [rmpp-packages](https://github.com/lorancecall/rmpp-packages): it is
> installed by rmpp setup, loaded into xochitl at every boot by rmpp-preload (no xovi), and kept up to date with
> Vellum. How that differs from xovi: [how it works](https://github.com/lorancecall/rmpp-packages/blob/main/docs/how-it-works.md).
>
> Status: experimental.

On the tablet ([details](docs/usage.md)):

- **Lasso convert** (lasso menu): handwriting to typed text on its line, or to a movable, editable text object.
- **Convert page** (toolbar): the whole page to text, on a new page or into the page.
- **Models** (Settings > Handwriting): pick the language and model, or switch back to reMarkable's converter.

## Install

- Install **rmpp setup** (desktop app or one-line script) from
  [rmpp-packages](https://github.com/lorancecall/rmpp-packages#install) and tick **rmpp-hwr** (not preselected); the
  Korean model and rmpp-textobj (text objects) come with it, the other languages' models are listed under it.
  Developer mode is needed: the rmpp-packages page explains it.
- After a reMarkable OS update: **Restore** in rmpp setup.
- Other ways (Vellum on the command line, the release files, `hwr install` and building it yourself) and removal:
  [docs/install.md](docs/install.md).

## More

- [Using rmpp-hwr](docs/usage.md): lasso convert, text objects, Convert page, Settings > Handwriting.
- [Installing](docs/install.md): Vellum on the command line, release files, `hwr install`, removal.
- [Developing](docs/develop.md): the code, training your own model, references.
- Privacy: nothing leaves the tablet when you convert; `hwr contribute` sends random writer ids and pen strokes only.

## License

GPL-3.0-only (see LICENSE). Third-party components: see NOTICE.
