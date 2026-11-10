// Experiment 4: convert the lasso selection to ink.
//   load /tmp/exp4.so arm             watch lasso selections (SceneController::areaSelected)
//   load /tmp/exp4.so go [replace]    recognize the strokes of the last selection and write the text as pen strokes:
//                                     below the selection (default) or in its place (replace: the selection is deleted
//                                     first, one undo step each)
// Needs librmpp_hwr_ink.so (librmpp_hwr.so with rmpp_hwr_region) and glyphs.json in the rmpp-hwr data folder.
#include <dlfcn.h>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QMetaMethod>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {
const char *kDir = "/home/root/.local/share/rmpp-hwr";
const char *kPages = "/home/root/.local/share/remarkable/xochitl";

#pragma pack(push, 1)
struct Pt {
    float x, y;
    quint16 speed, width;
    quint8 direction, pressure;
};
#pragma pack(pop)

QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred, int depth = 0) {
    if (!root || depth > 250) return nullptr;
    if (pred(root)) return root;
    for (QQuickItem *k : root->childItems())
        if (QQuickItem *f = findItem(k, pred, depth + 1)) return f;
    return nullptr;
}

QQuickItem *documentView() {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), [](QQuickItem *i) {
                    return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible();
                }))
                return f;
    return nullptr;
}

class RawSpy : public QObject {
public:
    std::function<void(void **)> fn;
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0 && fn) fn(a);
        return -1;
    }
};

QRectF g_sel;
int g_layer = 0;

// ---- glyphs ----
struct Glyph {
    double adv = 0.5;
    QList<QList<QPointF>> strokes;
};
QHash<QChar, Glyph> g_glyphs;
double g_baseline = 0.82;

bool loadGlyphs() {
    if (!g_glyphs.isEmpty()) return true;
    QFile f{QString::fromLatin1(kDir) + QStringLiteral("/glyphs.json")};
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    g_baseline = root.value(QStringLiteral("baseline")).toDouble(0.82);
    const QJsonObject gs = root.value(QStringLiteral("glyphs")).toObject();
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

// Lay out one line in em units (x from 0, y: 0 = top of em box). Returns the advance width in em.
double layout(const QString &text, QList<QList<QPointF>> *out) {
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

// ---- adding lines ----
QList<Pt> toPoints(const QList<QPointF> &xy, quint16 width) {
    QList<Pt> out;
    const int n = xy.size();
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 < n ? i + 1 : i - 1;
        double a = 0;
        if (j >= 0 && j != i) {
            const QPointF d = j > i ? xy[j] - xy[i] : xy[i] - xy[j];
            a = std::atan2(d.y(), d.x());
        }
        if (a < 0) a += 2 * M_PI;
        out.append(Pt{float(xy[i].x()), float(xy[i].y()), 4, width, quint8(a / (2 * M_PI) * 255), 110});
    }
    return out;
}

int addStrokes(QObject *sc, QObject *ph, const QList<QList<QPointF>> &strokes) {
    const int add = sc->metaObject()->indexOfMethod("addDrawingLine(Line)");
    const QMetaType type = add >= 0 ? sc->metaObject()->method(add).parameterMetaType(0) : QMetaType();
    if (add < 0 || type.sizeOf() != 88) { qWarning("[rmpp-exp] unexpected Line type"); return 0; }
    bool ok = false;
    int tool = ph->property("lineTool").toInt(&ok);
    if (!ok) tool = 15;
    const int color = ph->property("lineColor").toInt();
    const uint argb = ph->property("lineArgbCode").toUInt();
    const double thick = ph->property("lineThickness").toDouble();
    for (const auto &s : strokes) {
        void *line = type.create();
        auto *b = static_cast<char *>(line);
        *reinterpret_cast<int *>(b + 0x00) = tool;
        *reinterpret_cast<int *>(b + 0x04) = color;
        *reinterpret_cast<uint *>(b + 0x08) = argb;
        *reinterpret_cast<QList<Pt> *>(b + 0x10) = toPoints(s, quint16(std::lround(thick * 6)));
        *reinterpret_cast<double *>(b + 0x28) = thick;
        void *argv[] = {nullptr, line};
        sc->qt_metacall(QMetaObject::InvokeMetaMethod, add, argv);
        type.destroy(line);
    }
    return int(strokes.size());
}

// ---- recognizer (separate copy of librmpp_hwr.so with rmpp_hwr_region) ----
void *g_rec = nullptr;
int (*g_region)(const void *, const char *, float, float, float, float, char *, size_t) = nullptr;

bool loadRec() {
    if (g_rec) return true;
    void *lib = dlopen((QByteArray(kDir) + "/librmpp_hwr_ink.so").constData(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { qWarning("[rmpp-exp] %s", dlerror()); return false; }
    auto open = reinterpret_cast<void *(*)(const char *)>(dlsym(lib, "rmpp_hwr_open"));
    g_region = reinterpret_cast<decltype(g_region)>(dlsym(lib, "rmpp_hwr_region"));
    if (!open || !g_region) return false;
    g_rec = open(kDir);
    return g_rec != nullptr;
}

void go(bool replace) {
    QQuickItem *dv = documentView();
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *ph = dv ? dv->property("penHandler").value<QObject *>() : nullptr;
    QObject *doc = dv ? dv->property("document").value<QObject *>() : nullptr;
    if (!sc || !ph || !doc || g_sel.isEmpty()) { qWarning("[rmpp-exp] nothing selected"); return; }
    if (!loadGlyphs()) { qWarning("[rmpp-exp] glyphs.json missing"); return; }
    const QString path = QStringLiteral("%1/%2/%3.rm").arg(QLatin1String(kPages), doc->property("id").toString(),
                                                          sc->property("pageId").toString());
    const QRectF sel = g_sel;
    QPointer<QObject> scp(sc), php(ph);
    std::thread([=] {
        QByteArray out(64 * 1024, '\0');
        int n = -1;
        if (loadRec())
            n = g_region(g_rec, path.toUtf8().constData(), float(sel.left()), float(sel.top()), float(sel.right()),
                         float(sel.bottom()), out.data(), out.size());
        const QString res = QString::fromUtf8(out.constData());
        QMetaObject::invokeMethod(qApp, [=] {
            if (!scp || !php) return;
            qWarning("[rmpp-exp] recognized %d lines: %s", n, qPrintable(QString(res).replace(QLatin1Char('\n'), QLatin1String(" | "))));
            if (n <= 0) return;
            QList<QList<QPointF>> all;
            const double below = sel.bottom() + 20;
            double yNext = below;
            for (const QString &l : res.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList box = l.section(QLatin1Char('\t'), 0, 0).split(QLatin1Char(' '));
                const QString text = l.section(QLatin1Char('\t'), 1);
                if (box.size() != 4) continue;
                const QRectF b(QPointF(box[0].toDouble(), box[1].toDouble()), QPointF(box[2].toDouble(), box[3].toDouble()));
                // em from the line's ink height (font Hangul ink ~0.85 em); keep the original width within +-15%
                QList<QList<QPointF>> em;
                const double w = layout(text, &em);
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
            if (replace) {
                const int del = scp->metaObject()->indexOfMethod("deleteSelectedItems(int)");
                QMetaObject::invokeMethod(scp, "deleteSelectedItems", Q_ARG(int, g_layer));
                Q_UNUSED(del);
            }
            qWarning("[rmpp-exp] added %d strokes", addStrokes(scp, php, all));
        }, Qt::QueuedConnection);
    }).detach();
}
} // namespace

// Current selection bounds: getItemBoundingRect(cloneSelectedItems(layer, 0)). The item list is handled opaquely
// (QList<std::shared_ptr<SceneItem>> has the same layout as QList<std::shared_ptr<void>>).
QRectF selectionRect(QObject *sc, int layer) {
    const QMetaObject *mo = sc->metaObject();
    const int clone = mo->indexOfMethod("cloneSelectedItems(int,double)");
    const int bound = mo->indexOfMethod("getItemBoundingRect(QList<std::shared_ptr<SceneItem>>)");
    if (clone < 0 || bound < 0) { qWarning("[rmpp-exp] selection slots missing %d %d", clone, bound); return {}; }
    QList<std::shared_ptr<void>> items;
    double scale = qEnvironmentVariableIsSet("X") ? 0 : 1.0;
    void *a1[] = {&items, &layer, &scale};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, clone, a1);
    QRectF r;
    void *a2[] = {&r, &items};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, bound, a2);
    for (int k = 0; k < 2 && k < items.size(); ++k) {
        QList<std::shared_ptr<void>> one{items[k]};
        QRectF r1;
        void *a3[] = {&r1, &one};
        sc->qt_metacall(QMetaObject::InvokeMetaMethod, bound, a3);
        qWarning("[rmpp-exp] item %d rect %.0f,%.0f %.0fx%.0f", k, r1.x(), r1.y(), r1.width(), r1.height());
    }
    qWarning("[rmpp-exp] selection: %lld items, rect %.0f,%.0f %.0fx%.0f", (long long)items.size(), r.x(), r.y(), r.width(), r.height());
    return r;
}

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    const QByteArray a(arg);
    if (a.startsWith("sel")) {
        QQuickItem *dv = documentView();
        QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
        if (!sc) return;
        g_layer = sc->property("currentLayer").toInt();
        qWarning("[rmpp-exp] selectionItemCount %d", sc->property("selectionItemCount").toInt());
        const QRectF r = selectionRect(sc, g_layer);
        if (!r.isEmpty()) g_sel = r;
        if (a.contains("go")) go(a.contains("replace"));
        return;
    }
    if (a.startsWith("go")) { go(a.contains("replace")); return; }
    QQuickItem *dv = documentView();
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) { qWarning("[rmpp-exp] no scene"); return; }
    static RawSpy *spy = nullptr;
    if (!spy) {
        spy = new RawSpy;
        spy->fn = [](void **a) {
            g_layer = *static_cast<int *>(a[1]);
            g_sel = *static_cast<QRectF *>(a[2]);
            qWarning("[rmpp-exp] selection %.0f,%.0f %.0fx%.0f", g_sel.x(), g_sel.y(), g_sel.width(), g_sel.height());
        };
        const int sig = sc->metaObject()->indexOfSignal("areaSelected(int,QRectF)");
        QMetaObject::connect(sc, sig, spy, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
        auto *cleared = new RawSpy;
        cleared->fn = [](void **) { g_sel = QRectF(); };
        Q_UNUSED(cleared);   // (not connected: a cleared selection keeps the last rect for "go" right after)
    }
    qWarning("[rmpp-exp] armed; glyphs %s", loadGlyphs() ? "ok" : "missing");
}
