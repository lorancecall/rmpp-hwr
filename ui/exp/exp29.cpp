// Experiment 29: like 28, but move by cloning: cloneSelectedItems(layer, 1.0) -> deleteSelectedItems(layer) ->
// cloneAddAndSelectItems(layer, items, 1.0, QPointF(0, -dy)). arg "<y> <dy> <mode> <x> <w> <h>".
#include <functional>
#include <memory>
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtCore/QPointer>
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
QByteArray g_ret;
}
extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *argc) {
    QQuickItem *dv = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((dv = findItem(qw->contentItem(), [](QQuickItem *i) {
                     return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible(); })))
                break;
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) return "error: no open notebook";
    const QString arg = QString::fromUtf8(argc);
    const int y = arg.section(QLatin1Char(' '), 0, 0).toInt();
    const double dy = arg.section(QLatin1Char(' '), 1, 1).toDouble();
    const int modeArg = arg.section(QLatin1Char(' '), 2, 2).toInt();
    const int x0 = arg.section(QLatin1Char(' '), 3, 3).isEmpty() ? -5000 : arg.section(QLatin1Char(' '), 3, 3).toInt();
    const int w = arg.section(QLatin1Char(' '), 4, 4).isEmpty() ? 10000 : arg.section(QLatin1Char(' '), 4, 4).toInt();
    const int h = arg.section(QLatin1Char(' '), 5, 5).isEmpty() ? 40000 : arg.section(QLatin1Char(' '), 5, 5).toInt();
    const int m = sc->metaObject()->indexOfMethod("addSelectionRect(QRect,SceneController::LineSelectionMode)");
    if (m < 0) return "error: no addSelectionRect";
    QMetaObject::invokeMethod(sc, "clearSelectedItems");
    QRect rect(x0, y, w, h);
    int mode = modeArg;
    void *a[] = {nullptr, &rect, &mode};
    sc->qt_metacall(QMetaObject::InvokeMetaMethod, m, a);
    const int layer = sc->property("currentLayer").toInt();
    QPointer<QObject> scp(sc);
    QTimer::singleShot(300, sc, [scp, layer, dy] {
        if (!scp) return;
        const int n = scp->property("selectionItemCount").toInt();
        qWarning("[rmpp-exp] selected %d", n);
        if (n > 0 && dy != 0) {
            const QMetaObject *mo = scp->metaObject();
            const int clone = mo->indexOfMethod("cloneSelectedItems(int,double)");
            const int add = mo->indexOfMethod("cloneAddAndSelectItems(int,QList<std::shared_ptr<SceneItem>>,double,QPointF)");
            qWarning("[rmpp-exp] clone %d add %d", clone, add);
            if (clone < 0 || add < 0) return;
            QList<std::shared_ptr<void>> items;
            int l = layer;
            double scale = 1.0;
            void *a1[] = {&items, &l, &scale};
            scp->qt_metacall(QMetaObject::InvokeMetaMethod, clone, a1);
            qWarning("[rmpp-exp] cloned %lld", (long long)items.size());
            QMetaObject::invokeMethod(scp, "deleteSelectedItems", Q_ARG(int, layer));
            QPointF off(0, -dy);
            void *a2[] = {nullptr, &l, &items, &scale, &off};
            scp->qt_metacall(QMetaObject::InvokeMetaMethod, add, a2);
        }
        QTimer::singleShot(300, scp, [scp] { if (scp) QMetaObject::invokeMethod(scp, "clearSelectedItems"); });
    });
    g_ret = "ok (see log)";
    return g_ret.constData();
}
