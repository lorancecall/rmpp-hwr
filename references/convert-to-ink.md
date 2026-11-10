# Convert to ink (prototype design notes)

Last updated: 2026-11-10

Goal: after recognition, put the recognized text back on the page as ordinary pen strokes (a lasso-selectable,
movable, erasable ink object that looks like handwriting) instead of reMarkable's typed text layer. Ideally it works on
a lasso selection: the new ink replaces or sits next to the selected strokes at the same position and size.

## 1. How ink can get into a live page

### What xochitl exposes (OS 3.29, found by walking the meta-objects from the open `DocumentView`)

`DocumentView.sceneController` is a `SceneController` (C++). Its slots are callable from our add-on through
`qt_metacall`, even with argument types we do not have headers for. The ones that matter:

| Slot / signal | Use |
|---|---|
| `addDrawingLine(Line)` | adds one finished pen stroke to the current page (the same call the pen path ends in) |
| `areaSelected(int layer, QRectF rect)` (signal), `selectionCleared()` | lasso selection made / cleared, with its bounding rect |
| `selectionItemCount`, `selectionContainsStroke` (props) | what is selected |
| `cloneSelectedItems(int, double)`, `getItemBoundingRect(QList<shared_ptr<SceneItem>>)` | selected items and their bounds |
| `deleteSelectedItems(int layer)` | remove the selection (undoable like the eraser) |
| `undo()`, `redo()`, `undoAvailable` | the normal undo stack |
| `currentLayer`, `pageId` | where we are |

The pen side: `DocumentView.penHandler` (`ScenePenInputHandler` -> `PenInputLineHandler`) emits `stroke(Line)` for each
finished pen stroke and has the current pen (`lineTool` e.g. `Ballpointv2`, `lineColor`, `lineThickness`,
`lineArgbCode`).

`Line` is xochitl's own value type (registered with QMetaType, no headers). We get its size and copy/destroy functions
from `QMetaType` (the signal's parameter type), capture a real pen stroke as a template, and fill in our points.

### Rejected / fallback: writing the .rm file

rmscene can write v6 scene files, but xochitl keeps the open page in memory and writes it back, so editing the file of
an open page is lost or conflicts; there is no reload slot for a single page. It would only work for a closed document
(close, write, reopen), with a risk of CRDT id clashes. Kept as an offline fallback only.

## 2. Glyph source: text -> strokes

`ink/glyphs.py` builds `glyphs.json` (em units, y down, baseline at 0.82 em):

1. **own**: the writer's own characters from practice v2 grid cells (`samples/practice_*/strokes.json`, labels from
   each sample's `chars.tsv`, i.e. only the cells collect.py trusted). Size and vertical position are taken from the
   font glyph of the same character (cells come in S/M/L and are not filled evenly); the shape is the writer's.
2. **font**: centre lines of an outline font: render at 96 px, Zhang-Suen thinning, trace the 1-px skeleton into
   polylines (walk from end points and junctions, merge pieces that continue straight across a junction, drop stubs),
   Douglas-Peucker simplify. Works for any script the font has; default charset = KS X 1001 Hangul (2,350) + ASCII.

`layout` places a line of text at a given top-left and em height (shrinking to a maximum width); on the tablet the same
layout runs in C++ from `glyphs.json`.

Status of glyph quality: font strokes are clean but look typed (uniform, geometric); own glyphs look like the writer
but only ~80 characters exist yet. Better options, in order: more practice cells (every new practice page adds
glyphs); a handwriting-style font (e.g. an OFL pen font) as the fallback instead of Noto Sans; later a stroke-level
generative model trained on own strokes.
