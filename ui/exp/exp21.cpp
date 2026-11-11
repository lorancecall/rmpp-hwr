// Experiment 21: what does reMarkable's own lasso Convert to text produce, and can we read the text?
// Hooks SceneController::textPasted(int, qsizetype) and HwcSelection::conversionFinished(); on textPasted it selects
// the inserted range, copies it (copySelectedText) and logs the clipboard text.
#include <functional>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
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
struct Hook : QObject {
    std::function<void(void **)> fn;
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0 && fn) fn(a);
        return -1;
    }
};
void hook(QObject *o, const char *sig, std::function<void(void **)> fn) {
    const int i = o->metaObject()->indexOfSignal(sig);
    if (i < 0) { qWarning("[rmpp-exp] no signal %s on %s", sig, o->metaObject()->className()); return; }
    auto *h = new Hook;
    h->fn = std::move(fn);
    h->setParent(o);
    QMetaObject::connect(o, i, h, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
}
QPointer<QObject> g_sc;
}

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) { qWarning("[rmpp-exp] no scene"); return; }
    g_sc = sc;
    hook(sc, "textPasted(int,qsizetype)", [](void **a) {
        const int pos = *static_cast<int *>(a[1]);
        const qsizetype len = *static_cast<qsizetype *>(a[2]);
        qWarning("[rmpp-exp] textPasted pos %d len %lld rootLength %d", pos, (long long)len,
                 g_sc ? g_sc->property("rootDocumentLength").toInt() : -1);
        QTimer::singleShot(300, qApp, [pos, len] {
            if (!g_sc) return;
            const QString before = QGuiApplication::clipboard()->text();
            QMetaObject::invokeMethod(g_sc, "selectTextRange", Q_ARG(int, pos), Q_ARG(int, int(pos + len)));
            QMetaObject::invokeMethod(g_sc, "copySelectedText");
            QTimer::singleShot(200, qApp, [before] {
                const QString after = QGuiApplication::clipboard()->text();
                qWarning("[rmpp-exp] clipboard changed %d: '%s'", before != after, qPrintable(after.left(200).replace(QLatin1Char('\n'), QLatin1String(" / "))));
                if (g_sc) QMetaObject::invokeMethod(g_sc, "clearSelectedText");
            });
        });
    });
    hook(sc, "textCopied(SceneClipboardText)", [](void **) { qWarning("[rmpp-exp] textCopied"); });
    if (QQuickItem *h = findAnywhere([](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "HwcSelection_QMLTYPE", 20) == 0; })) {
        hook(h, "conversionFinished()", [](void **) { qWarning("[rmpp-exp] hwc conversionFinished"); });
        hook(h, "conversionFailed()", [](void **) { qWarning("[rmpp-exp] hwc conversionFailed"); });
        hook(h, "statusChanged()", [h = QPointer<QQuickItem>(h)](void **) { if (h) qWarning("[rmpp-exp] hwc status %d", h->property("status").toInt()); });
        qWarning("[rmpp-exp] hooked hwc");
    } else {
        // HwcSelection is not an item child: look among QObject children of the scene views
        qWarning("[rmpp-exp] no HwcSelection item found");
    }
    qWarning("[rmpp-exp] armed");
}
