// Experiment 15: does a double tap on the lasso selection reach the selection area's MouseArea::doubleClicked?
#include <functional>
#include <QtCore/QMetaMethod>
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
struct Hook : QObject {
    const char *what = "";
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0) qWarning("[rmpp-exp] %s", what);
        return -1;
    }
};
}

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *h = findItem(qw->contentItem(), [](QQuickItem *i) {
                    return qstrncmp(i->metaObject()->className(), "SceneSelectionHandler", 21) == 0;
                })) {
                int n = 0;
                for (QQuickItem *k : h->childItems()) {
                    if (!k->inherits("QQuickMouseArea")) continue;
                    for (const char *sig : {"doubleClicked(QQuickMouseEvent*)", "clicked(QQuickMouseEvent*)", "pressAndHold(QQuickMouseEvent*)"}) {
                        const int i = k->metaObject()->indexOfSignal(sig);
                        if (i < 0) continue;
                        auto *hook = new Hook;
                        hook->setParent(k);
                        static QByteArray names[32];
                        names[n % 32] = QByteArray(sig) + " on area " + QByteArray::number(n) + " " +
                                        QByteArray::number(k->width()) + "x" + QByteArray::number(k->height());
                        hook->what = names[n % 32].constData();
                        QMetaObject::connect(k, i, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
                        ++n;
                    }
                }
                qWarning("[rmpp-exp] hooked %d signals", n);
                return;
            }
    qWarning("[rmpp-exp] no selection handler");
}
