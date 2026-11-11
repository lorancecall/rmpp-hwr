// Experiment 16: full e-ink refresh through DocumentView.ghostBuster (IGhostBuster::userRequestedClear()).
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

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *dv = findItem(qw->contentItem(), [](QQuickItem *i) {
                    return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible();
                })) {
                QObject *gb = dv->property("ghostBuster").value<QObject *>();
                const char *m = qstrlen(arg) ? arg : "userRequestedClear";
                const bool ok = gb && QMetaObject::invokeMethod(gb, m);
                qWarning("[rmpp-exp] ghostBuster %s: %d", m, ok);
                return;
            }
    qWarning("[rmpp-exp] no document");
}
