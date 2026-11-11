// Experiment 25: insert typed text into the page's root text flow at a scene position.
// arg: "probe" (only report geometry) or "<x> <y> <text>" (scene coordinates). Steps: createRootDocument() if the page
// has no text yet, setCursorPosition(QPointF), replaceText(QString); reports cursor index/position and lengths.
#include <functional>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {
QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred, int depth = 0) {
    if (!root || depth > 250) return nullptr;
    if (pred(root)) return root;
    for (QQuickItem *k : root->childItems())
        if (QQuickItem *f = findItem(k, pred, depth + 1)) return f;
    return nullptr;
}
QQuickItem *findAnywhere(const std::function<bool(QQuickItem *)> &pred) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), pred)) return f;
    return nullptr;
}
QString r2s(const QRectF &r) { return QStringLiteral("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }
QString p2s(const QPointF &p) { return QStringLiteral("(%1,%2)").arg(p.x()).arg(p.y()); }
QString state(QObject *sc, QObject *ph) {
    return QStringLiteral("hasRoot=%1 len=%2 rootRect=%3 col=%4 cursorIdx=%5 cursorPos=%6 lineH=%7")
        .arg(sc->property("hasRootDocument").toBool())
        .arg(sc->property("rootDocumentLength").toInt())
        .arg(r2s(sc->property("rootDocumentBoundingRect").toRectF()))
        .arg(ph ? r2s(ph->property("textColumnRect").toRectF()) : QStringLiteral("-"))
        .arg(sc->property("textCursorIndex").toInt())
        .arg(p2s(sc->property("textCursorPosition").toPointF()))
        .arg(sc->property("textLineHeight").toInt());
}
QByteArray g_ret;
}

extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *argc) {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) return "error: no open notebook";
    QObject *ph = dv->property("penHandler").value<QObject *>();
    const QString arg = QString::fromUtf8(argc);
    QString out = QStringLiteral("before: ") + state(sc, ph);
    if (arg != QLatin1String("probe")) {
        const QPointF pos(arg.section(QLatin1Char(' '), 0, 0).toDouble(), arg.section(QLatin1Char(' '), 1, 1).toDouble());
        const QString text = arg.section(QLatin1Char(' '), 2);
        if (!sc->property("hasRootDocument").toBool()) {
            const bool ok = QMetaObject::invokeMethod(sc, "createRootDocument");
            out += QStringLiteral("\ncreateRootDocument %1: ").arg(ok) + state(sc, ph);
        }
        const bool okf = QMetaObject::invokeMethod(sc, "focusRootDocument", Q_ARG(QPointF, pos));
        out += QStringLiteral("\nfocusRootDocument%1 %2: ").arg(p2s(pos)).arg(okf) + state(sc, ph);
        const bool okc = QMetaObject::invokeMethod(sc, "setCursorPosition", Q_ARG(QPointF, pos));
        out += QStringLiteral("\nsetCursorPosition%1 %2: ").arg(p2s(pos)).arg(okc) + state(sc, ph);
        const bool okr = QMetaObject::invokeMethod(sc, "replaceText", Q_ARG(QString, text));
        out += QStringLiteral("\nreplaceText '%1' %2: ").arg(text).arg(okr) + state(sc, ph);
    }
    g_ret = out.toUtf8();
    return g_ret.constData();
}
