# Using rmpp-hwr

## Lasso convert

In the lasso menu: select handwriting with the lasso and tap the convert button (the "aa" icon in a dashed box).
The recognized text is typed into the page on the line where you wrote it, or, in text object mode, drawn as a
**text object**.

Text objects are drawn by [rmpp-textobj](https://github.com/lorancecall/rmpp-textobj): ordinary pen strokes in the
language's font that you can move, resize and copy like handwriting, and edit as text with a double tap (selection
tool). Text object mode needs rmpp-textobj and the language's [rmpp-lang](https://github.com/lorancecall/rmpp-lang)
add-on (its glyphs). Text objects in the selection are not recognized again: their text is used as it is, and only
the handwriting around them is recognized.

## Convert page

In the toolbar, next to Convert to text: recognizes the whole page and types the text on a new page, or into the
page itself (Settings > Handwriting > Convert).

## Settings > Handwriting

- **Handwriting converter**: reMarkable (the lasso button is reMarkable's own Convert to text, Convert page is
  hidden) or rmpp-hwr.
- **Language**: the language and model in use. Each language lists its models: the installed packages'
  (`rmpp-hwr-ko`, `rmpp-hwr-en`, ...; removed with `vellum del`) and models you trained yourself in
  `/home/root/.local/share/rmpp-hwr/models/<name>/` (`rec.onnx`, `dict.txt`, `model.txt`; listed by the first line of
  `model.txt`).
- **Convert**: Convert page on a new page or into the page; lasso convert to typed text or to a text object (needs
  rmpp-textobj).
- **Collect training data**: new practice notebooks (lines to write, for [rmpp-hwr-db](https://github.com/lorancecall/rmpp-hwr-db)
  or your own model) and Save conversions (keeps a copy of the pages you convert, off unless you turn it on).
