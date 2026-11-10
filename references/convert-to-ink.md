# Convert to ink (prototype design notes)

Last updated: 2026-11-10

Goal: after recognition, put the recognized text back on the page as ordinary pen strokes (a lasso-selectable,
movable, erasable ink object that looks like handwriting) instead of reMarkable's typed text layer, starting from a
lasso selection.

## Status (prototype, branch `feature/convert-to-ink`, tested on OS 3.29.1)

Works on the tablet: lasso-select handwriting -> the lasso menu's convert button (now a pen-tip icon) -> the selection
is recognized (`rmpp_hwr_region`) and the text is written below it as ballpoint strokes, one line per recognized line,
at the size of the handwriting. The new ink is ordinary page content: drawn at once, saved in the page `.rm` as normal
lines (tool 15), lasso-movable. The original strokes are kept (nothing is deleted unless `ink:replace` is used).

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
| `DocumentView.updateAfterStroke()` | redraw after adding lines; without it the new ink only appears after the next interaction |
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

Developer feedback (2026-11-10): placement works, but the font strokes look typed. Next, in order: a handwriting-style
OFL font as the fallback (e.g. a Korean pen font); more own glyphs from practice pages; later a stroke model trained on
own strokes.

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
4. "Edit text" for converted ink (see above); group the strokes so one undo removes the whole conversion.
5. Read strokes from the live scene instead of the saved file (selected items), so very recent writing is included.
6. Remove the developer loader and `ui/exp` from release builds.
