// Lasso convert: recognize the lasso selection and either type the text into the page (typed text) or hand it to
// rmpp-textobj, which draws it as a text object (ink in the language's font). Included inside hwrui.cpp's namespace.
//
// Text objects are rmpp-textobj's (a separate add-on, loaded before this one: preload.d/55-rmpp-textobj.so): it owns
// their records, glyphs, edit box and selection rules. This file only talks to its C API (see TextObj below).
// Selected text objects are never recognized again: their text comes from rmpp-textobj.
//
// xochitl 3.29 internals used: SceneController::cloneSelectedItems(int layer, double) + getItemBoundingRect(items)
// (the selection's bounds), deleteSelectedItems(int layer), addSelectionRect(QRect, mode); the lasso's
// SelectionImage item (image() -> the selected ink at page scale).

// ---- rmpp-textobj's C API (resolved on each use: it may be installed or removed while xochitl runs) ----
struct TextObj {
    int (*selection)(char *, int) = nullptr;
    int (*editSelection)() = nullptr;
    int (*dropObjects)() = nullptr;
    int (*putLines)(const char *, double, double, double, double, int, const char *) = nullptr;
    bool ok() const { return selection && editSelection && dropObjects && putLines; }
};

TextObj textObj() {
    TextObj t;
    void *h = dlopen("/usr/lib/rmpp/rmpp-textobj/librmpp-textobj-ui.so", RTLD_NOW | RTLD_NOLOAD);
    auto sym = [&](const char *name) { void *p = h ? dlsym(h, name) : nullptr; return p ? p : dlsym(RTLD_DEFAULT, name); };
    auto api = reinterpret_cast<int (*)()>(sym("rmpp_textobj_api"));
    if (!api || api() < 1) return t;
    t.selection = reinterpret_cast<int (*)(char *, int)>(sym("rmpp_textobj_selection"));
    t.editSelection = reinterpret_cast<int (*)()>(sym("rmpp_textobj_edit_selection"));
    t.dropObjects = reinterpret_cast<int (*)()>(sym("rmpp_textobj_drop_objects_from_selection"));
    t.putLines = reinterpret_cast<int (*)(const char *, double, double, double, double, int, const char *)>(sym("rmpp_textobj_put_lines"));
    return t;
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

// ---- recognizing the selection from xochitl's own rendering of it ----
// The lasso's SelectionImage item holds the selected ink as an image at page scale (image() -> QImage, ARGB, ink in
// the alpha channel, same size as the selection bounds). Recognizing from it needs no saved page file: it includes
// strokes written a moment ago, and it is right on pages whose strokes are stored relative to typed text.
QImage selectionImage() {
    QQuickItem *img = findAnywhere([](QQuickItem *i) { return qstrcmp(i->metaObject()->className(), "SelectionImage") == 0 && i->isVisible(); });
    QImage im;
    if (img) QMetaObject::invokeMethod(img, "image", Qt::DirectConnection, Q_RETURN_ARG(QImage, im));
    return im;
}

struct InkLine {
    QRect box;      // in image pixels
    QString text;
};

// Split the ink into text lines like page.rs split_lines (connected components instead of strokes), render each line
// black on white with a 12 px margin and recognize it. Runs on a worker thread.
QList<InkLine> recognizeImage(const QImage &src) {
    QList<InkLine> out;
    if (src.isNull() || !g_hwr.recognize) return out;
    const QImage im = src.convertToFormat(QImage::Format_ARGB32);
    const int W = im.width(), H = im.height();
    std::vector<int> label(size_t(W) * H, -1);
    auto ink = [&](int x, int y) { return qAlpha(im.pixel(x, y)) > 60; };
    QList<QRect> comps;
    std::vector<int> stack;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (label[size_t(y) * W + x] >= 0 || !ink(x, y)) continue;
            const int id = int(comps.size());
            QRect r(x, y, 1, 1);
            stack.assign(1, y * W + x);
            label[size_t(y) * W + x] = id;
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                const int px = p % W, py = p / W;
                r = r.united(QRect(px, py, 1, 1));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H || label[size_t(ny) * W + nx] >= 0 || !ink(nx, ny)) continue;
                        label[size_t(ny) * W + nx] = id;
                        stack.push_back(ny * W + nx);
                    }
            }
            comps.append(r);
        }
    if (comps.isEmpty()) return out;
    // group into lines (port of split_lines in src/page.rs)
    QList<int> heights;
    for (const QRect &r : comps) if (r.height() > 2) heights << r.height();
    if (heights.isEmpty()) return out;
    std::sort(heights.begin(), heights.end());
    const double median = qMax(10, heights[heights.size() / 2]);
    const double tol = median * 0.3;
    QList<int> order;
    for (int i = 0; i < comps.size(); ++i) order << i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return comps[a].center().y() < comps[b].center().y(); });
    auto small = [&](int i) { return comps[i].height() < median * 0.35 && comps[i].width() < median * 0.6; };
    struct L { QList<int> ids; double y0, y1; };
    QList<L> lines;
    for (int i : order) {
        if (small(i)) continue;
        const QRect &b = comps[i];
        const double cy = (b.top() + b.bottom()) / 2.0;
        auto it = std::find_if(lines.begin(), lines.end(), [&](const L &l) { return cy >= l.y0 - tol && cy <= l.y1 + tol; });
        if (it != lines.end()) { it->y0 = qMin(it->y0, double(b.top())); it->y1 = qMax(it->y1, double(b.bottom())); it->ids << i; }
        else lines.append(L{{i}, double(b.top()), double(b.bottom())});
    }
    for (int i : order) {
        if (!small(i)) continue;
        const double cy = (comps[i].top() + comps[i].bottom()) / 2.0;
        auto dist = [&](const L &l) { return cy < l.y0 ? l.y0 - cy : (cy > l.y1 ? cy - l.y1 : 0.0); };
        auto best = std::min_element(lines.begin(), lines.end(), [&](const L &a, const L &b) { return dist(a) < dist(b); });
        if (best != lines.end() && dist(*best) < median) best->ids << i;
        else lines.append(L{{i}, double(comps[i].top()), double(comps[i].bottom())});
    }
    std::sort(lines.begin(), lines.end(), [](const L &a, const L &b) { return a.y0 < b.y0; });
    for (const L &l : lines) {
        QRect box;
        for (int i : l.ids) box = box.united(comps[i]);
        const int pad = 12;
        QImage g(box.width() + 2 * pad, box.height() + 2 * pad, QImage::Format_Grayscale8);
        g.fill(255);
        QSet<int> ids(l.ids.begin(), l.ids.end());
        for (int y = box.top(); y <= box.bottom(); ++y)
            for (int x = box.left(); x <= box.right(); ++x)
                if (ids.contains(label[size_t(y) * W + x])) g.setPixel(x - box.left() + pad, y - box.top() + pad, qRgb(0, 0, 0));
        QByteArray txt(4096, '\0');
        float conf = 0;
        if (g_hwr.recognize(g_hwr.rec, g.constBits(), unsigned(g.width()), unsigned(g.height()), unsigned(g.bytesPerLine()),
                            txt.data(), txt.size(), &conf) > 0)
            out.append(InkLine{box, QString::fromUtf8(txt.constData()).trimmed()});
    }
    return out;
}

const char *kBusyQml = R"QML(
import QtQuick
Rectangle {
    property string message: "Converting…"
    width: label.implicitWidth + 80; height: 90
    x: ((parent ? parent.width : 1620) - width) / 2; y: 40
    z: 10000
    color: "white"; border.color: "black"; border.width: 3; radius: 8
    Text { id: label; anchors.centerIn: parent; text: parent.message; font.pixelSize: 36 }
}
)QML";

void showInkBusy(QQuickItem *dv) {
    if (g_inkBusy) g_inkBusy->deleteLater();
    QQmlEngine *eng = qmlEngine(dv);
    QQmlContext *ctx = qmlContext(dv);
    g_inkBusy = (eng && ctx) ? createInline(eng, ctx, kBusyQml, "InkBusy", dv) : nullptr;
}

// ---- typed text (lasso convert without Text object mode) ----
// OS 3.29 keeps a page's typed text in one text column (the root document) that starts at the top of the page; it
// cannot be placed at a free x/y. The recognized text goes to the line at the height of the selection, as
// reMarkable's own lasso convert does: below the existing text with blank lines up to that height, or, when the
// text already reaches that height, at the end of the line there. Measured on 3.29: line pitch 53.55 for the default
// paragraph style. Strokes below the text column are anchored to it, so inserted lines push them down (as typing
// does): kept handwriting ends up just below its text, and so does all handwriting further down the page. Moving
// them back failed (moveSelectedItems after addSelectionRect does nothing; clone + delete + cloneAddAndSelectItems
// only deletes), see exp28/exp29. Inserting text ends the lasso selection, so with replace the
// handwriting is deleted first (one undo step each).
constexpr double kTextLinePitch = 53.55;

// Selects the strokes inside a scene rect (SceneController::addSelectionRect, mode 0); false if not available.
// Only works while the selection tool is active (tested: with the pen active it selects nothing).
bool selectRect(QObject *sc, const QRect &r) {
    const int m = sc->metaObject()->indexOfMethod("addSelectionRect(QRect,SceneController::LineSelectionMode)");
    if (m < 0) return false;
    QRect rect = r;
    int mode = 0;
    void *a[] = {nullptr, &rect, &mode};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, m, a);
    return true;
}

// The conversion being typed (for Save conversions): document, page index and id, the recognized ink.
struct ConvCtx { QString doc, pageId; int page = -1; QImage img; bool pageReplace = false; };
ConvCtx g_convCtx;
bool g_pageConvert = false;   // set by convertPageInPlace for the conversion it starts

// lines: each recognized line with its vertical centre (scene units), top to bottom.
void putText(QPointer<QObject> scp, int layer, bool replace, const QList<QPair<double, QString>> &lines) {
    if (replace) QMetaObject::invokeMethod(scp, "deleteSelectedItems", Q_ARG(int, layer));
    else QMetaObject::invokeMethod(scp, "clearSelectedItems");
    if (!scp->property("hasRootDocument").toBool()) QMetaObject::invokeMethod(scp, "createRootDocument");
    // the root document's properties update asynchronously
    QTimer::singleShot(300, scp, [=] {
        if (!scp) { setStatus(QStringLiteral("page closed"), false); return; }
        const QRectF root = scp->property("rootDocumentBoundingRect").toRectF();
        const int len = scp->property("rootDocumentLength").toInt();
        auto lineOf = [&](double y) { return qMax(0, int((y - root.top()) / kTextLinePitch)); };
        const double y = lines.first().first;   // middle of the first written line
        QString insert;
        if (len > 0 && y < root.bottom()) {
            QMetaObject::invokeMethod(scp, "setCursorPosition", Q_ARG(QPointF, QPointF(root.right() - 1, y)));
            insert = (len > 1 ? QStringLiteral(" ") : QString()) + lines.first().second;   // no space on an empty line
        } else {
            const int used = len > 0 ? qMax(1, qRound(root.height() / kTextLinePitch)) : 0;   // lines in use
            const int target = lineOf(y);                                                        // line of the handwriting
            const int newlines = used == 0 ? target : qMax(1, target - used + 1);
            QMetaObject::invokeMethod(scp, "setCursorIndex", Q_ARG(int, len));
            insert = QString(newlines, QLatin1Char('\n')) + lines.first().second;
        }
        // the next lines at their own heights (at least one line apart)
        for (int i = 1; i < lines.size(); ++i)
            insert += QString(qMax(1, lineOf(lines[i].first) - lineOf(lines[i - 1].first)), QLatin1Char('\n')) + lines[i].second;
        QStringList texts;
        for (const auto &l : lines) texts << l.second;
        const QString text = texts.join(QLatin1String(" | "));
        QMetaObject::invokeMethod(scp, "replaceText", Q_ARG(QString, insert));
        QTimer::singleShot(300, scp, [=] {
            if (!scp) return;
            const bool in = scp->property("rootDocumentLength").toInt() > len;
            qWarning("[rmpp-hwr] text: %s (%s)", qPrintable(text),
                     in ? "inserted" : "NOT inserted");
            if (in && g_convCtx.page >= 0)
                collectConversion(g_convCtx.doc, {g_convCtx.page}, g_convCtx.pageId, g_convCtx.pageId, texts,
                                  saveInkImage(g_convCtx.img),
                                  g_convCtx.pageReplace ? QStringLiteral("page-replace") : replace ? QStringLiteral("lasso-replace") : QStringLiteral("lasso"));
            g_convCtx = ConvCtx();
            setStatus(in ? QString() : QStringLiteral("the text could not be inserted (undo brings the handwriting back)"), false);
        });
    });
}

// Convert to text (toolbar) with "Replace the handwriting": select every stroke of the current layer, then convert
// the selection to typed text in this page (deleting the strokes first), each line at its own height.
void convertSelectionToInk(bool replace, bool toText = false);
// A tap on an item through its window (emitting the tool's clicked() does not switch tools; a tap does, exp30).
void tapItem(QQuickItem *item) {
    QQuickWindow *win = item ? item->window() : nullptr;
    if (!win) return;
    const QPointF c(item->width() / 2, item->height() / 2);
    const QPointF p = item->mapToScene(c), g = item->mapToGlobal(c);
    QMouseEvent press(QEvent::MouseButtonPress, p, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, p, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &press);
    QCoreApplication::sendEvent(win, &release);
}

void convertPageInPlace() {
    if (g_state->value(QStringLiteral("busy")).toBool()) return;
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) return;
    // close the toolbar menu the button sits in, then make the selection tool active (rect selection needs it)
    if (QQuickItem *sv = findItem(dv, [](QQuickItem *i) { return i->metaObject()->indexOfSignal("closeToolbarFoldout()") >= 0; }))
        QMetaObject::invokeMethod(sv, "closeToolbarFoldout");
    QPointer<QObject> scp(sc);
    QTimer::singleShot(400, sc, [scp] {
        QQuickItem *tool = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("selectionButton"); });
        if (tool && !tool->property("selected").toBool()) tapItem(tool);
        QTimer::singleShot(600, scp, [scp] {
            if (!scp) return;
            QMetaObject::invokeMethod(scp, "clearSelectedItems");
            if (!selectRect(scp, QRect(-5000, -5000, 10000, 40000))) {   // scene units: more than any page
                setStatus(QStringLiteral("this xochitl version is not supported"), false);
                return;
            }
            QTimer::singleShot(500, scp, [scp] {
                if (!scp) return;
                if (scp->property("selectionItemCount").toInt() <= 0) { setStatus(QStringLiteral("no handwriting on this page"), false); return; }
                g_pageConvert = true;
                convertSelectionToInk(true, true);
                g_pageConvert = false;
            });
        });
    });
}

// The language of the recognition model in use ("ko", "zh", ...), rmpp-textobj's first place to look for glyphs.
QString modelLanguageInUse() {
    const QString dir = currentModelDir();
    return dir.isEmpty() ? QString() : modelLanguage(dir, modelName(dir, QString()));
}

// "x0 y0 x1 y1<TAB>text" lines -> (vertical centre, text), as putText takes them.
QList<QPair<double, QString>> centredLines(const QStringList &lines) {
    QList<QPair<double, QString>> out;
    for (const QString &l : lines) {
        const QStringList b = l.section(QLatin1Char('\t'), 0, 0).split(QLatin1Char(' '));
        const QString t = l.section(QLatin1Char('\t'), 1);
        if (b.size() == 4 && !t.trimmed().isEmpty()) out.append({(b[1].toDouble() + b[3].toDouble()) / 2, t});
    }
    return out;
}

void recognizeAndPut(QPointer<QQuickItem> dvp, QPointer<QObject> scp, int layer, const QRectF &sel, bool replace,
                     bool toText, const QStringList &objLines);

// Lasso convert. toText: typed text into the page's text column (see putText); otherwise a text object (rmpp-textobj).
// Text objects in the selection are not recognized again: only text objects -> their text is typed (toText) or the
// edit box opens (text object); text objects and handwriting -> only the handwriting is recognized (the objects
// are taken out of the selection first); typed text gets the objects' text too, in reading order.
void convertSelectionToInk(bool replace, bool toText) {
    if (g_state->value(QStringLiteral("busy")).toBool()) return;
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *doc = dv ? dv->property("document").value<QObject *>() : nullptr;
    if (!sc || !doc) { setStatus(QStringLiteral("no open page"), false); return; }
    const int layer = sc->property("currentLayer").toInt();
    const QRectF sel = selectionRect(sc, layer);
    if (sel.isEmpty()) { setStatus(QStringLiteral("select handwriting with the lasso first"), false); return; }
    const TextObj to = textObj();
    if (!toText && !to.ok()) { setStatus(QStringLiteral("text objects need rmpp-textobj"), false); return; }
    QByteArray buf(64 * 1024, '\0');
    const int kind = to.ok() ? to.selection(buf.data(), int(buf.size())) : 0;
    const QStringList objLines = QString::fromUtf8(buf.constData()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QPointer<QObject> scp(sc);
    QPointer<QQuickItem> dvp(dv);
    if (kind == 1) {   // only text objects: their text, no recognition
        if (!toText) { to.editSelection(); return; }
        qWarning("[rmpp-hwr] text objects -> typed text (not recognized again)");
        putText(scp, layer, replace, centredLines(objLines));
        return;
    }
    if (kind == 2) {   // text objects and handwriting: recognize the handwriting only
        const int out = to.dropObjects();
        qWarning("[rmpp-hwr] selection with text objects: %d of their strokes left out of recognition", out);
        QTimer::singleShot(700, sc, [=] {
            if (!scp || !dvp) return;
            const QRectF rest = selectionRect(scp, layer);
            if (rest.isEmpty()) { setStatus(QStringLiteral("only text objects selected"), false); return; }
            recognizeAndPut(dvp, scp, layer, rest, replace, toText, objLines);
        });
        return;
    }
    recognizeAndPut(dvp, scp, layer, sel, replace, toText, {});
}

void recognizeAndPut(QPointer<QQuickItem> dvp, QPointer<QObject> scp, int layer, const QRectF &sel, bool replace,
                     bool toText, const QStringList &objLines) {
    QObject *doc = dvp ? dvp->property("document").value<QObject *>() : nullptr;
    if (!dvp || !scp || !doc) return;
    const QString pageId = dvp->property("currentPageId").toString();
    const QImage img = selectionImage();
    if (img.isNull()) { setStatus(QStringLiteral("no selection image"), false); return; }
    if (toText) g_convCtx = ConvCtx{doc->property("id").toString(), pageId, dvp->property("currentPage").toInt(), img, g_pageConvert};
    setStatus(QStringLiteral("Converting…"), true);
    showInkBusy(dvp);
    const QString lang = modelLanguageInUse();
    std::thread([=] {
        QString err;
        QList<InkLine> found;
        if (loadRecognizer(&err)) {
            if (!g_hwr.recognize) err = QStringLiteral("recognizer without rmpp_hwr_recognize");
            else found = recognizeImage(img);
        }
        QMetaObject::invokeMethod(qApp, [=] {
            if (!err.isEmpty()) { setStatus(err, false); return; }
            if (!scp) { setStatus(QStringLiteral("page closed"), false); return; }
            if (found.isEmpty() && objLines.isEmpty()) { setStatus(QStringLiteral("no handwriting recognized in the selection"), false); return; }
            // image pixels -> page units (the image covers the selection bounds)
            const double kx = sel.width() / img.width(), ky = sel.height() / img.height();
            QStringList lines, texts;
            for (const InkLine &l : found) {
                if (l.text.isEmpty()) continue;
                const QRectF b(sel.left() + l.box.left() * kx, sel.top() + l.box.top() * ky, l.box.width() * kx, l.box.height() * ky);
                lines << QStringLiteral("%1 %2 %3 %4\t%5").arg(b.left()).arg(b.top()).arg(b.right()).arg(b.bottom()).arg(l.text);
                texts << l.text;
            }
            if (toText) {
                QList<QPair<double, QString>> typed = centredLines(objLines);
                for (const InkLine &l : found)
                    // skip empty lines and lone marks (a dot or dash recognized from a stray stroke)
                    if (!l.text.trimmed().isEmpty() && !(l.text.trimmed().size() == 1 && !l.text.trimmed().at(0).isLetterOrNumber()))
                        typed.append({sel.top() + l.box.center().y() * ky, l.text});
                if (typed.isEmpty()) { setStatus(QStringLiteral("no handwriting recognized in the selection"), false); return; }
                std::sort(typed.begin(), typed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
                putText(scp, layer, replace, typed);
                return;
            }
            if (lines.isEmpty()) { setStatus(QStringLiteral("no handwriting recognized in the selection"), false); return; }
            qWarning("[rmpp-hwr] ink: %s", qPrintable(texts.join(QLatin1String(" | "))));
            const TextObj to = textObj();
            const int r = to.ok() ? to.putLines(lines.join(QLatin1Char('\n')).toUtf8().constData(), sel.left(), sel.top(),
                                                sel.right(), sel.bottom(), replace ? 1 : 0, lang.toUtf8().constData()) : -3;
            setStatus(r == 1 ? QString() : r == -1 ? QStringLiteral("no glyphs for some characters (see the hint)")
                      : r == -3 ? QStringLiteral("text objects need rmpp-textobj") : QStringLiteral("nothing to draw"), false);
        }, Qt::QueuedConnection);
    }).detach();
}
