// Convert to ink: recognize the lasso selection and write the text back as ordinary pen strokes (lasso-selectable,
// movable, erasable, undoable), next to the selection or in its place. Included inside hwrui.cpp's namespace.
//
// xochitl 3.29 internals used (no headers; found by walking meta-objects, see references/convert-to-ink.md):
//   SceneController::addDrawingLine(Line)                   adds one finished pen stroke to the open page
//   SceneController::cloneSelectedItems(int layer, double)  + getItemBoundingRect(items): the selection's bounds
//   SceneController::deleteSelectedItems(int layer)         removes the selection (one undo step)
//   PenInputLineHandler lineTool/lineColor/lineArgbCode/lineThickness: the current pen
//   Line (88 bytes): 0x00 int tool, 0x04 int color, 0x08 uint argb, 0x10 QList<Point>, 0x28 double thickness
//   Point (14 bytes, packed): float x, y; u16 speed, width; u8 direction, pressure  (same as .rm v6 points)
// The Line layout is checked (size 88) before use; anything else and the feature reports an error instead.

#pragma pack(push, 1)
struct InkPt {
    float x, y;
    quint16 speed, width;
    quint8 direction, pressure;
};
#pragma pack(pop)
static_assert(sizeof(InkPt) == 14, "Line point layout");

struct Glyph {
    double adv = 0.5;
    QList<QList<QPointF>> strokes;
};
QHash<QChar, Glyph> g_glyphs;

// glyphs.json (ink/glyphs.py build): {"glyphs": {char: {"adv", "strokes": [[x, y, ...]]}}}, em units, y down.
bool loadGlyphs() {
    if (!g_glyphs.isEmpty()) return true;
    QFile f{QLatin1String(kLibDir) + QStringLiteral("/glyphs.json")};
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject gs = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("glyphs")).toObject();
    for (auto it = gs.begin(); it != gs.end(); ++it) {
        if (it.key().size() != 1) continue;
        const QJsonObject o = it.value().toObject();
        Glyph g;
        g.adv = o.value(QStringLiteral("adv")).toDouble(0.5);
        for (const QJsonValue &s : o.value(QStringLiteral("strokes")).toArray()) {
            const QJsonArray a = s.toArray();
            QList<QPointF> p;
            for (int i = 0; i + 1 < a.size(); i += 2) p.append(QPointF(a[i].toDouble(), a[i + 1].toDouble()));
            g.strokes.append(p);
        }
        g_glyphs.insert(it.key().at(0), g);
    }
    return !g_glyphs.isEmpty();
}

// One line of text in em units (x from 0, y 0 = top of the em box). Returns the width in em.
double layoutLine(const QString &text, QList<QList<QPointF>> *out) {
    double x = 0;
    for (const QChar c : text) {
        auto it = g_glyphs.constFind(c);
        if (it == g_glyphs.constEnd()) { x += 0.5; continue; }
        for (const auto &s : it->strokes) {
            QList<QPointF> p;
            for (const QPointF &q : s) p.append(QPointF(q.x() + x, q.y()));
            out->append(p);
        }
        x += it->adv + 0.04;
    }
    return x;
}

QList<InkPt> inkPoints(const QList<QPointF> &xy, quint16 width) {
    QList<InkPt> out;
    const int n = int(xy.size());
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 < n ? i + 1 : i - 1;
        double a = 0;
        if (j >= 0 && j != i) {
            const QPointF d = j > i ? xy[j] - xy[i] : xy[i] - xy[j];
            a = std::atan2(d.y(), d.x());
        }
        if (a < 0) a += 2 * M_PI;
        out.append(InkPt{float(xy[i].x()), float(xy[i].y()), 4, width, quint8(a / (2 * M_PI) * 255), 110});
    }
    return out;
}

// Add strokes (page coordinates) with the current pen. Returns the number added, -1 if Line is not as expected.
int addInkStrokes(QObject *sc, QObject *ph, const QList<QList<QPointF>> &strokes) {
    const int add = sc->metaObject()->indexOfMethod("addDrawingLine(Line)");
    const QMetaType type = add >= 0 ? sc->metaObject()->method(add).parameterMetaType(0) : QMetaType();
    if (add < 0 || type.sizeOf() != 88) return -1;
    bool ok = false;
    int tool = ph->property("lineTool").toInt(&ok);
    // with the lasso active the "pen" is the selection tool (11), and erasers/highlighters make no sense for text
    static const QList<int> pens{1, 2, 3, 4, 7, 12, 13, 14, 15, 16, 17, 21};
    if (!ok || !pens.contains(tool)) tool = 15;   // Ballpointv2
    const int color = ph->property("lineColor").toInt();
    const uint argb = ph->property("lineArgbCode").toUInt();
    double thick = ph->property("lineThickness").toDouble();
    if (thick <= 0) thick = 2;
    for (const auto &s : strokes) {
        void *line = type.create();
        auto *b = static_cast<char *>(line);
        *reinterpret_cast<int *>(b + 0x00) = tool;
        *reinterpret_cast<int *>(b + 0x04) = color;
        *reinterpret_cast<uint *>(b + 0x08) = argb;
        *reinterpret_cast<QList<InkPt> *>(b + 0x10) = inkPoints(s, quint16(std::lround(thick * 6)));
        *reinterpret_cast<double *>(b + 0x28) = thick;
        void *argv[] = {nullptr, line};
        sc->qt_metacall(QMetaObject::InvokeMetaMethod, add, argv);
        type.destroy(line);
    }
    return int(strokes.size());
}

// Bounds of the current selection (page coordinates); empty when nothing is selected. The item list is passed
// through opaquely: QList<std::shared_ptr<SceneItem>> has the layout of QList<std::shared_ptr<void>>.
QRectF selectionRect(QObject *sc, int layer) {
    const QMetaObject *mo = sc->metaObject();
    const int clone = mo->indexOfMethod("cloneSelectedItems(int,double)");
    const int bound = mo->indexOfMethod("getItemBoundingRect(QList<std::shared_ptr<SceneItem>>)");
    if (clone < 0 || bound < 0 || sc->property("selectionItemCount").toInt() <= 0) return {};
    QList<std::shared_ptr<void>> items;
    double scale = 1.0;   // 0 yields an empty rect
    void *a1[] = {&items, &layer, &scale};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, clone, a1);
    QRectF r;
    void *a2[] = {&r, &items};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, bound, a2);
    return r;
}

// Recognizer with rmpp_hwr_region. A separate copy (librmpp_hwr_ink.so) so the prototype does not replace the
// recognizer that Convert page uses; once rmpp_hwr_region is in the released librmpp_hwr.so this can use g_hwr.
struct InkRec {
    void *rec = nullptr;
    int (*region)(const void *, const char *, float, float, float, float, char *, size_t) = nullptr;
};
InkRec g_inkRec;

bool loadInkRecognizer(QString *err) {
    if (g_inkRec.rec) return true;
    void *lib = nullptr;
    for (const char *name : {"/librmpp_hwr_ink.so", "/librmpp_hwr.so"}) {
        lib = dlopen((QByteArray(kLibDir) + name).constData(), RTLD_NOW | RTLD_LOCAL);
        if (lib && dlsym(lib, "rmpp_hwr_region")) break;
        lib = nullptr;
    }
    if (!lib) { *err = QStringLiteral("recognizer without rmpp_hwr_region"); return false; }
    auto open = reinterpret_cast<void *(*)(const char *)>(dlsym(lib, "rmpp_hwr_open"));
    g_inkRec.region = reinterpret_cast<decltype(g_inkRec.region)>(dlsym(lib, "rmpp_hwr_region"));
    g_inkRec.rec = open ? open(kLibDir) : nullptr;
    if (!g_inkRec.rec) { *err = QStringLiteral("model failed to load"); return false; }
    return true;
}

// Place recognized lines ("x0 y0 x1 y1<TAB>text") as glyph strokes: each line at the size of its handwriting,
// either over the original line (replace) or stacked below the selection.
QList<QList<QPointF>> placeLines(const QStringList &lines, const QRectF &sel, bool replace) {
    QList<QList<QPointF>> all;
    double yNext = sel.bottom() + 20;
    for (const QString &l : lines) {
        const QStringList box = l.section(QLatin1Char('\t'), 0, 0).split(QLatin1Char(' '));
        const QString text = l.section(QLatin1Char('\t'), 1);
        if (box.size() != 4 || text.isEmpty()) continue;
        const QRectF b(QPointF(box[0].toDouble(), box[1].toDouble()), QPointF(box[2].toDouble(), box[3].toDouble()));
        QList<QList<QPointF>> em;
        const double w = layoutLine(text, &em);
        if (w <= 0) continue;
        // em size from the line's ink height (Hangul ink is ~0.85 em), kept within +-15% of the original width
        double h = b.height() / 0.85;
        if (w * h > b.width() * 1.15) h = b.width() * 1.15 / w;
        if (w * h < b.width() * 0.85) h = qMin(b.width() * 0.85 / w, b.height() / 0.6);
        const double top = replace ? b.top() - 0.05 * h : yNext;
        for (const auto &s : em) {
            QList<QPointF> p;
            for (const QPointF &q : s) p.append(QPointF(b.left() + q.x() * h, top + q.y() * h));
            all.append(p);
        }
        yNext = top + h * 1.15;
    }
    return all;
}

// Select strokes with a rectangular lasso around them (SceneController::selectWithLine(Line), what the lasso tool
// calls). Selecting the new ink lets the user move it at once and also makes xochitl redraw that area.
void selectInk(QObject *sc, const QList<QList<QPointF>> &strokes) {
    const int sel = sc->metaObject()->indexOfMethod("selectWithLine(Line)");
    const QMetaType type = sel >= 0 ? sc->metaObject()->method(sel).parameterMetaType(0) : QMetaType();
    if (sel < 0 || type.sizeOf() != 88 || strokes.isEmpty()) return;
    QRectF r;
    for (const auto &s : strokes)
        for (const QPointF &p : s) r = r.isNull() ? QRectF(p, QSizeF(0.1, 0.1)) : r.united(QRectF(p, QSizeF(0.1, 0.1)));
    r.adjust(-15, -15, 15, 15);
    const QList<QPointF> loop{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()};
    void *line = type.create();
    auto *b = static_cast<char *>(line);
    *reinterpret_cast<int *>(b + 0x00) = 11;   // selection tool
    *reinterpret_cast<uint *>(b + 0x08) = 0xff000000u;
    *reinterpret_cast<QList<InkPt> *>(b + 0x10) = inkPoints(loop, 12);
    *reinterpret_cast<double *>(b + 0x28) = 2.0;
    void *argv[] = {nullptr, line};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, sel, argv);
    type.destroy(line);
}

void convertSelectionToInk(bool replace) {
    if (g_state->value(QStringLiteral("busy")).toBool()) return;
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *ph = dv ? dv->property("penHandler").value<QObject *>() : nullptr;
    QObject *doc = dv ? dv->property("document").value<QObject *>() : nullptr;
    if (!sc || !ph || !doc) { setStatus(QStringLiteral("no open page"), false); return; }
    const int layer = sc->property("currentLayer").toInt();
    const QRectF sel = selectionRect(sc, layer);
    if (sel.isEmpty()) { setStatus(QStringLiteral("select handwriting with the lasso first"), false); return; }
    if (!loadGlyphs()) { setStatus(QStringLiteral("glyphs.json missing"), false); return; }
    const QString path = QStringLiteral("%1/%2/%3.rm").arg(QLatin1String(kPages), doc->property("id").toString(),
                                                          sc->property("pageId").toString());
    setStatus(QStringLiteral("Converting…"), true);
    QPointer<QObject> scp(sc), php(ph);
    QPointer<QQuickItem> dvp(dv);
    std::thread([=] {
        QString err;
        QByteArray out(64 * 1024, '\0');
        int n = -1;
        if (loadInkRecognizer(&err))
            n = g_inkRec.region(g_inkRec.rec, path.toUtf8().constData(), float(sel.left()), float(sel.top()),
                                float(sel.right()), float(sel.bottom()), out.data(), out.size());
        const QStringList lines = QString::fromUtf8(out.constData()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QMetaObject::invokeMethod(qApp, [=] {
            if (!err.isEmpty()) { setStatus(err, false); return; }
            if (!scp || !php) { setStatus(QStringLiteral("page closed"), false); return; }
            if (n <= 0) { setStatus(QStringLiteral("no handwriting recognized in the selection"), false); return; }
            qWarning("[rmpp-hwr] ink: %s", qPrintable(lines.join(QLatin1String(" | "))));
            const QList<QList<QPointF>> strokes = placeLines(lines, sel, replace);
            // addDrawingLine is ignored while a selection is active: delete (replace) or deselect it first, then add
            // once the scene has settled
            if (replace) QMetaObject::invokeMethod(scp, "deleteSelectedItems", Q_ARG(int, layer));
            else QMetaObject::invokeMethod(scp, "clearSelectedItems");
            QTimer::singleShot(400, scp, [scp, php, dvp, strokes] {
                if (!scp || !php) { setStatus(QStringLiteral("page closed"), false); return; }
                const int added = addInkStrokes(scp, php, strokes);
                if (added > 0) selectInk(scp, strokes);
                // lines added this way are not drawn until the next redraw (updateAfterStroke is what the pen path
                // calls); repeat once the lasso menu has closed, which repaints the old screen content
                for (int ms : {0, 300, 1000})
                    QTimer::singleShot(ms, qApp, [dvp] { if (dvp) QMetaObject::invokeMethod(dvp, "updateAfterStroke"); });
                setStatus(added < 0 ? QStringLiteral("this xochitl version is not supported") : QString(), false);
            });
        }, Qt::QueuedConnection);
    }).detach();
}
