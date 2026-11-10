// Experiment 2: insert strokes from a JSON file into the open page through SceneController::addDrawingLine(Line).
//   load /tmp/exp2.so /path/strokes.json      {"strokes": [[x, y, x, y, ...], ...]} in page coordinates
// The Line value is built from a default-constructed Line (QMetaType) with the current pen's tool/colour/thickness and
// our own point list (layout found with exp1, xochitl 3.29):
//   0x00 int tool, 0x04 int color, 0x08 uint argb, 0x10 QList<Point>, 0x28 double thickness
//   Point (14 bytes, packed): float x, y; u16 speed, width; u8 direction, pressure
#include <cmath>
#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QMetaMethod>
#include <QtCore/QTextStream>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {
#pragma pack(push, 1)
struct Pt {
    float x, y;
    quint16 speed, width;
    quint8 direction, pressure;
};
#pragma pack(pop)
static_assert(sizeof(Pt) == 14, "point layout");

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

QString hex(const void *p, int n) {
    const auto *b = static_cast<const uchar *>(p);
    QString s;
    for (int i = 0; i < n; i += 16) {
        s += QStringLiteral("    %1:").arg(i, 4, 16, QLatin1Char('0'));
        for (int j = i; j < i + 16 && j < n; ++j) s += QStringLiteral(" %1").arg(b[j], 2, 16, QLatin1Char('0'));
        s += QLatin1Char('\n');
    }
    return s;
}

// Pen-like point attributes along a polyline: width from the pen, direction from the segment angle.
QList<Pt> toPoints(const QJsonArray &xy, quint16 width) {
    QList<Pt> out;
    const int n = xy.size() / 2;
    for (int i = 0; i < n; ++i) {
        const float x = float(xy[2 * i].toDouble()), y = float(xy[2 * i + 1].toDouble());
        const int j = i + 1 < n ? i + 1 : i - 1;
        float a = 0;
        if (j >= 0 && j != i) {
            const float dx = float(xy[2 * j].toDouble()) - x, dy = float(xy[2 * j + 1].toDouble()) - y;
            a = std::atan2(j > i ? dy : -dy, j > i ? dx : -dx);
        }
        if (a < 0) a += float(2 * M_PI);
        out.append(Pt{x, y, 4, width, quint8(a / float(2 * M_PI) * 255.f), 110});
    }
    return out;
}
} // namespace

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    QQuickItem *dv = documentView();
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *ph = dv ? dv->property("penHandler").value<QObject *>() : nullptr;
    if (!sc || !ph) { qWarning("[rmpp-exp] no open page"); return; }
    const int add = sc->metaObject()->indexOfMethod("addDrawingLine(Line)");
    const QMetaType type = sc->metaObject()->method(add).parameterMetaType(0);
    if (add < 0 || type.sizeOf() != 88) { qWarning("[rmpp-exp] unexpected Line (%d, %d)", add, int(type.sizeOf())); return; }

    QFile f{QString::fromUtf8(arg)};
    if (!f.open(QIODevice::ReadOnly)) { qWarning("[rmpp-exp] cannot read %s", arg); return; }
    const QJsonArray strokes = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("strokes")).toArray();

    bool ok = false;
    int tool = ph->property("lineTool").toInt(&ok);
    if (!ok) tool = 15;   // Ballpointv2
    const int color = ph->property("lineColor").toInt();
    const uint argb = ph->property("lineArgbCode").toUInt();
    const double thick = ph->property("lineThickness").toDouble();
    {   // what a default Line looks like, for the notes
        void *d = type.create();
        QFile o{QStringLiteral("/home/root/.local/share/rmpp-hwr/line_default.txt")};
        if (o.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&o) << hex(d, 88);
        type.destroy(d);
    }
    qWarning("[rmpp-exp] before: selection %d", sc->property("selectionItemCount").toInt());
    if (QByteArray(getenv("NOCLEAR") ? "" : "x").size()) QMetaObject::invokeMethod(sc, "clearSelectedItems");
    qWarning("[rmpp-exp] after clear: selection %d", sc->property("selectionItemCount").toInt());
    int n = 0;
    for (const QJsonValue &s : strokes) {
        void *line = type.create();
        auto *b = static_cast<char *>(line);
        *reinterpret_cast<int *>(b + 0x00) = tool;
        *reinterpret_cast<int *>(b + 0x04) = color;
        *reinterpret_cast<uint *>(b + 0x08) = argb;
        *reinterpret_cast<QList<Pt> *>(b + 0x10) = toPoints(s.toArray(), quint16(std::lround(thick * 6)));
        *reinterpret_cast<double *>(b + 0x28) = thick;
        void *argv[] = {nullptr, line};
        sc->qt_metacall(QMetaObject::InvokeMetaMethod, add, argv);
        type.destroy(line);
        ++n;
    }
    qWarning("[rmpp-exp] added %d strokes (tool %d colour %d thickness %.2f)", n, tool, color, thick);
}
