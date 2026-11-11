// Experiment 22: send a command to the rmpp-hwr add-on as its QML would (hwr.command = "..."), e.g.
//   load /tmp/exp22.so set:lassoInk:0
// Finds an item the add-on created (objectName rmppHwr*) and evaluates the assignment in its QML context.
#include <functional>
#include <QtCore/QDateTime>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQml/QQmlPropertyMap>
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
}

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    QQuickItem *it = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((it = findItem(qw->contentItem(), [](QQuickItem *i) { return i->objectName().startsWith(QLatin1String("rmppHwr")); })))
                break;
    QQmlContext *ctx = it ? qmlContext(it) : nullptr;
    if (!ctx) { qWarning("[rmpp-exp] no rmpp-hwr item"); return; }
    const QByteArray a(arg);
    if (a == "dump") {
        auto *map = qobject_cast<QQmlPropertyMap *>(ctx->contextProperty(QStringLiteral("hwr")).value<QObject *>());
        if (!map) return;
        for (const char *k : {"hwrModel", "modelInUse", "modelName", "modelList", "lassoInk"})
            qWarning("[rmpp-exp] %s = %s", k, qPrintable(map->value(QString::fromLatin1(k)).toString()));
        return;
    }
    if (a.startsWith("insert ") || a.startsWith("clear ")) {   // insert KEY VALUE | clear KEY on the hwr map
        auto *map = qobject_cast<QQmlPropertyMap *>(ctx->contextProperty(QStringLiteral("hwr")).value<QObject *>());
        if (!map) { qWarning("[rmpp-exp] no hwr map"); return; }
        const QList<QByteArray> w = a.split(' ');
        if (w[0] == "insert" && w.size() >= 3) map->insert(QString::fromUtf8(w[1]), QString::fromUtf8(w[2]));
        else if (w.size() >= 2) map->clear(QString::fromUtf8(w[1]));
        qWarning("[rmpp-exp] map keys: %s", qPrintable(map->keys().join(QLatin1Char(','))));
        return;
    }
    const QString cmd = QString::fromUtf8(arg) + QLatin1Char('#') + QString::number(QDateTime::currentMSecsSinceEpoch());
    QQmlExpression e(ctx, it, QStringLiteral("hwr.command = \"%1\"").arg(cmd));
    e.evaluate();
    qWarning("[rmpp-exp] sent %s via %s, error: %s", qPrintable(cmd), qPrintable(it->objectName()),
             e.hasError() ? qPrintable(e.error().toString()) : "none");
}
