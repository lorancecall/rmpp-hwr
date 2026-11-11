# Convert to ink (prototype design notes)

Last updated: 2026-11-10

Goal: after recognition, put the recognized text back on the page as ordinary pen strokes (a lasso-selectable,
movable, erasable ink object that looks like handwriting) instead of reMarkable's typed text layer, starting from a
lasso selection.

## Status (prototype, branch `feature/convert-to-ink`, tested on OS 3.29.1)

Works on the tablet: lasso-select handwriting -> the lasso menu's convert button (now a pen-tip icon) -> the selection
is recognized (`rmpp_hwr_region`) and the text is written below it as ballpoint strokes, one line per recognized line,
at the size of the handwriting, and is left selected (rectangular lasso via `selectWithLine(Line)`) so it can be moved
at once. The new ink is ordinary page content: saved in the page `.rm` as normal lines (tool 15). The original strokes are kept (nothing is deleted unless `ink:replace` is used).

Verified: strokes added live via `addDrawingLine`; saved to the `.rm`; selection bounds; lasso menu takeover on the
first selection; immediate redraw. Not yet verified: undo of the added ink (each stroke should be its own undo step:
untested), `ink:replace` (deleteSelectedItems), pages with several layers, zoomed/landscape views, PDF/EPUB pages.

## 1. How ink gets into a live page

### xochitl internals used (no headers; found by walking meta-objects from the open `DocumentView`)

| Object / member | Use |
|---|---|
| `DocumentView.sceneController` (`SceneController`) `addDrawingLine(Line)` | adds one finished pen stroke to the open page. **Ignored while a selection is active**: deselect (`clearSelectedItems()`) or delete (`deleteSelectedItems(int layer)`) first, then add after ~400 ms |
| `SceneController.cloneSelectedItems(int layer, double)` + `getItemBoundingRect(QList<shared_ptr<SceneItem>>)` | bounds of the selection in page units. The double must be 1.0 (0 gives an empty rect). The item list is passed opaquely as `QList<std::shared_ptr<void>>` |
| `SceneController.selectionItemCountChanged()` | used to patch the lasso menu as soon as it is created |
| `DocumentView.updateAfterStroke()` | redraw after adding lines (called at 0, 300 and 1000 ms: the closing lasso menu repaints the old content) |
| `SceneController.selectWithLine(Line)` | select with a lasso polygon (Line with tool 11); used to leave the new ink selected |
| `DocumentView.penHandler` (`PenInputLineHandler`) `lineTool`, `lineColor`, `lineArgbCode`, `lineThickness` | current pen. With the lasso active `lineTool` is 11 (selection tool), so non-pen tools fall back to Ballpointv2 (15) |
| lasso menu `SelectionContextualMenu 'selectionHandlerMenu'` > `ButtonRow` > `Button` x4 (`iconSource` cut / copy / hwc / trashcan) | recreated for every selection; we put a `MouseArea` over the `hwc` button and set its icon to `ballpoint_tip` |
| `SceneSelectionHandler` (`showConvertToText`, `sceneSelectionRect`) | alternative source of the selection rect; not used yet |

Signals we expected but that do not fire for the lasso: `SceneController::areaSelected(int, QRectF)`.

### The `Line` value type

Registered with QMetaType (name `Line`, 88 bytes), read from a captured pen stroke (`ui/exp/exp1.cpp`, fault-safe
reads via `process_vm_readv`):

| Offset | Field |
|---|---|
| 0x00 | int tool (15 = Ballpointv2) |
| 0x04 | int color (0 = black) |
| 0x08 | uint ARGB (0xff000000) |
| 0x10 | `QList<Point>` (Qt 6: d, ptr, size) |
| 0x28 | double thickness scale (~2.0) |
| 0x30.. | other fields, zero in a default-constructed Line; left as default |

Point = 14 bytes, packed: float x, float y, u16 speed, u16 width, u8 direction, u8 pressure: the same as a `.rm` v6
point. Page coordinates: x centred on 0 (about -810..810), y from the top. We create the Line with `QMetaType::create()`
(default constructor), set the fields, assign our own `QList<Point>` and call the slot through `qt_metacall`. The add-on
checks the size (88) and refuses otherwise, so a future xochitl with a different layout fails safely instead of crashing.
A first version that followed guessed pointers while dumping crashed xochitl once; the dump code now never
dereferences unknown pointers directly.

### Rejected: writing the .rm file

rmscene can write v6 scene files, but xochitl keeps the open page in memory and writes it back, so edits to an open
page's file are lost or conflict, and there is no reload slot for one page. Only usable for a closed document.

### Edit text (tested 2026-11-10: edit, move + edit, resize + edit work)

- Every conversion is recorded in `/home/root/.local/share/rmpp-hwr/ink.json`: page id, text (lines joined with
  `
`), stroke count `n`, selection size `w`/`h` (measured from the selection right after the new ink is selected),
  em origin offset `ox`/`oy` from the selection's top-left, `em` size. Max 500 records.
- Lasso menu button: if the selection on this page has the same stroke count and aspect ratio (+-4%) as a record, it
  is our ink (moved and/or scaled) and the button opens an edit box instead of converting; the edit is laid out at the
  current scale (k = selection width / recorded width).
- Edit box (QML over the window, top of the screen): multi-line `TextEdit` in a `Flickable`, Enter = new line, wraps,
  scrolls; xochitl's on-screen keyboard (with the rmpp-korean layout) comes up for it (tested with exp12: Korean typing
  works). OK: delete the selection, lay the text out at the recorded em size from the current position, word-wrapped at
  the original ink width (min 8 em), select the new ink, replace the record. Empty text deletes the ink.
- Icon: xochitl's own `hwc` ("aa") icon inside a dashed box, composed at run time from `:/ark/icons/hwc` into
  `ink_convert.svg` (no reMarkable artwork in this repo). xochitl's Icon needs a plain path; a `file://` URL shows
  nothing ("Could not find").
- While recognizing, a "Converting…" box is shown at the top of the screen (closed by `setStatus`; errors stay 2.5 s).

### Editable text instead of strokes?

OS 3.29 typed text is one root text flow per page (`createRootDocument`, `rootDocument*`); there is no floating text
item, and the only floating objects are images (`pasteClipboardContentAsFloatingSceneImage`, `insertImageAsSceneItem`).
So "movable and editable text" is not available natively. Plan instead: keep strokes (movable) and add an "edit text"
action for ink made by this feature: re-recognize (font strokes recognize almost perfectly) or look up the stored text,
edit it in a keyboard dialog, redraw the strokes in place.

## 2. Glyph source: text -> strokes

`ink/glyphs.py` builds `glyphs.json` (em units, y down, baseline at 0.82 em; ~1.2 MB for 2,445 glyphs, 30 s):

1. **own**: the writer's own characters from practice v2 grid cells (`samples/practice_*/strokes.json`, labels from
   each sample's `chars.tsv`, i.e. only cells collect.py trusted). Size and vertical position come from the font glyph
   of the same character (cells come in S/M/L and are not filled evenly); the shape is the writer's. 79 so far.
2. **font**: centre lines of an outline font: render at 96 px, Zhang-Suen thinning, trace the skeleton into polylines
   (walk from end points and junctions, merge pieces that continue straight across a junction, drop stubs),
   Douglas-Peucker. Default charset KS X 1001 Hangul (2,350) + ASCII; font Noto Sans KR.

On the tablet `ui/ink.h` lays the text out from `glyphs.json`: em height = line ink height / 0.85, then kept within
+-15% of the original line width.

Developer feedback (2026-11-10): the developer prefers a clean font look over their own handwriting, so the deployed
`glyphs.json` is font-only (no `--own`). The first tracer left gaps at junctions and split corners; it is now a graph:
touching junction/end pixels form one node, every edge runs to the node centres, end spurs shorter than 0.04 em are
dropped, and edges are chained through corners and straight across junctions (raster 160 px; 2,445 glyphs in ~95 s).
Converted text is drawn with the Fineliner (even width), black, thickness = em / 20 (2..6), whatever pen is current.

## 3. Files

- `ui/ink.h`: everything above in the add-on (glyph loading/layout, Line construction, selection bounds, flow).
- `ui/hwrui.cpp`: lasso menu takeover (`tryInjectInkMenu`, `watchSelection`), commands `ink` / `ink:replace`, and a
  developer loader: `echo "load /path/x.so ARG" > ~/.local/share/rmpp-hwr/cmd` dlopens an experiment and calls
  `rmpp_exp(ARG)` without restarting xochitl (`echo probe > .../cmd` dumps the meta-objects).
- `ui/exp/*.cpp` + `ui/exp/build.sh`: the experiments (1 capture/dump Line, 2 add strokes, 3 selection signal,
  4-7 selection + add, 8 lasso menu tree, 9 redraw).
- `src/lib.rs`: `rmpp_hwr_region(rec, path, x0, y0, x1, y1, out, len)`: recognize the strokes whose box centre is in
  the rectangle; output `x0 y0 x1 y1<TAB>text` per line.
- Tablet: `glyphs.json` and `librmpp_hwr_ink.so` (a copy of the recognizer with `rmpp_hwr_region`, so the released
  `librmpp_hwr.so` stays untouched) in `/home/root/.local/share/rmpp-hwr/`.

## 4. Limits and plan

Limits now: recognizes from the saved `.rm` (strokes written a moment ago may not be saved yet); font look; always
ballpoint black at thickness from the current pen; replaces the native convert-to-text in the lasso menu (no setting
yet); one undo step per stroke (to verify); the recognizer is loaded a second time (memory) because of the separate
library copy.

Plan:
1. Ship `rmpp_hwr_region` in the released `librmpp_hwr.so` and drop the `_ink` copy.
2. Handwriting-style fallback font; regenerate `glyphs.json` on `hwr pull` so new practice cells become glyphs.
3. Settings > Handwriting: lasso convert = ink / text (native); place below / replace.
4. Test and polish "edit text" (see above); group the strokes so one undo removes the whole conversion.
5. Read strokes from the live scene instead of the saved file (selected items), so very recent writing is included.
6. Remove the developer loader and `ui/exp` from release builds.
