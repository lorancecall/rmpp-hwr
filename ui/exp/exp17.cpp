// Experiment 17: can we make xochitl store the open page now? Log linesStored, call DocumentWorker slots, watch mtime.
#include <functional>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
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
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0) qWarning("[rmpp-exp] linesStored page %d %s", *static_cast<int *>(a[1]), qPrintable(*static_cast<QString *>(a[2])));
        return -1;
    }
};
}

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    QQuickItem *dv = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((dv = findItem(qw->contentItem(), [](QQuickItem *i) {
                     return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible();
                 })))
                break;
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *wk = sc ? sc->property("worker").value<QObject *>() : nullptr;
    QObject *doc = dv ? dv->property("document").value<QObject *>() : nullptr;
    if (!wk || !doc) { qWarning("[rmpp-exp] no worker"); return; }
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        auto *h = new Hook;
        h->setParent(wk);
        QMetaObject::connect(wk, wk->metaObject()->indexOfSignal("linesStored(int,QString,qlonglong)"), h,
                             QObject::staticMetaObject.methodCount(), Qt::QueuedConnection, nullptr);
    }
    const QString pid = sc->property("pageId").toString();
    const QString path = QStringLiteral("/home/root/.local/share/remarkable/xochitl/%1/%2.rm").arg(doc->property("id").toString(), pid);
    auto mtime = [path] { return QFileInfo(path).lastModified().toString(QStringLiteral("hh:mm:ss.zzz")); };
    qWarning("[rmpp-exp] path %s", qPrintable(path));
    qWarning("[rmpp-exp] before: %s (now %s)", qPrintable(mtime()), qPrintable(QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"))));
    const QByteArray how(arg);
    bool ok = false;
    if (how == "page") ok = QMetaObject::invokeMethod(wk, "onModifiedPageId", Q_ARG(QString, pid));
    else if (how == "num") ok = QMetaObject::invokeMethod(wk, "onModifiedPageNumber", Q_ARG(int, dv->property("currentPage").toInt()));
    qWarning("[rmpp-exp] invoked %s: %d", how.constData(), ok);
    for (int ms : {300, 1000, 3000})
        QTimer::singleShot(ms, qApp, [mtime, ms] { qWarning("[rmpp-exp] +%d ms: %s", ms, qPrintable(mtime())); });
}
