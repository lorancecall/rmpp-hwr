// Experiment 1 (loaded with "load <so>" into a running xochitl): capture one pen stroke (type Line) and dump its
// bytes, following pointer-looking words with a fault-safe read (process_vm_readv), so a wrong guess cannot crash.
#include <sys/uio.h>
#include <unistd.h>
#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QMetaMethod>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtCore/QTextStream>
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

QQuickItem *documentView() {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), [](QQuickItem *i) {
                    return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible();
                }))
                return f;
    return nullptr;
}

bool safeRead(const void *p, void *dst, size_t n) {
    iovec local{dst, n}, remote{const_cast<void *>(p), n};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(n);
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
} // namespace

// Experiment 3: log lasso selections (SceneController::areaSelected(int, QRectF)) and selection counts.
extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    QQuickItem *dv = documentView();
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) { qWarning("[rmpp-exp] no scene"); return; }
    const int sig = sc->metaObject()->indexOfSignal("areaSelected(int,QRectF)");
    static RawSpy *spy = new RawSpy;
    static QPointer<QObject> scp;
    scp = sc;
    spy->fn = [](void **a) {
        const int layer = *static_cast<int *>(a[1]);
        const QRectF r = *static_cast<QRectF *>(a[2]);
        qWarning("[rmpp-exp] areaSelected layer %d rect %.1f %.1f %.1f x %.1f items %d strokes %d", layer, r.x(), r.y(),
                 r.width(), r.height(), scp ? scp->property("selectionItemCount").toInt() : -1,
                 scp ? scp->property("selectionContainsStroke").toBool() : -1);
    };
    QMetaObject::connect(sc, sig, spy, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
    qWarning("[rmpp-exp] selection spy armed (%d), undoAvailable %d", sig, sc->property("undoAvailable").toBool());
}
