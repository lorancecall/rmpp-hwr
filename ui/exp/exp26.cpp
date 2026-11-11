// Experiment 26: clearRootDocument() on the open page (test notebook only), to retest createRootDocument with strokes.
#include <functional>
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
}
extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *) {
    QQuickItem *dv = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((dv = findItem(qw->contentItem(), [](QQuickItem *i) {
                     return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible(); })))
                break;
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) return "error: no open notebook";
    return QMetaObject::invokeMethod(sc, "clearRootDocument") ? "ok cleared" : "error: invoke failed";
}
