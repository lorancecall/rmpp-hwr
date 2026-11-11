// Experiment 14: set the lasso menu's ink button icon to ARG (to find the form of path xochitl's Icon accepts).
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
            if (QQuickItem *menu = findItem(qw->contentItem(), [](QQuickItem *i) { return i->objectName() == QLatin1String("selectionHandlerMenu"); })) {
                QQuickItem *btn = findItem(menu, [](QQuickItem *i) { return i->property("_rmppHwrInk").toBool(); });
                if (!btn) { qWarning("[rmpp-exp] no ink button"); return; }
                btn->setProperty("iconSource", QString::fromUtf8(arg));
                qWarning("[rmpp-exp] icon set to %s", arg);
                return;
            }
    qWarning("[rmpp-exp] no lasso menu");
}
