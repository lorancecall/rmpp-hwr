// Experiment 1 (loaded with "load <so>" into a running xochitl): capture one pen stroke (type Line) and dump its
// bytes, following pointer-looking words with a fault-safe read (process_vm_readv), so a wrong guess cannot crash.
#include <sys/uio.h>
#include <unistd.h>
#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QMetaMethod>
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


void dumpTree(QObject *o, int depth, QTextStream &out) {
    if (!o || depth > 30) return;
    const QMetaObject *mo = o->metaObject();
    out << QString(depth * 2, QLatin1Char(' ')) << mo->className() << " '" << o->objectName() << "'";
    for (const char *p : {"iconSource", "icon", "text", "label", "visible", "enabled", "x", "y", "width", "height", "action", "type"}) {
        const int i = mo->indexOfProperty(p);
        if (i >= 0) out << " " << p << "=" << mo->property(i).read(o).toString().left(60);
    }
    // signals with "click"/"trigger" and own slots (helps find handlers)
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QByteArray sig = mo->method(i).methodSignature();
        if (sig.contains("lick") || sig.contains("rigger") || sig.contains("onvert")) out << " [" << sig << "]";
    }
    out << "\n";
    if (auto *it = qobject_cast<QQuickItem *>(o))
        for (QQuickItem *c : it->childItems()) dumpTree(c, depth + 1, out);
}
} // namespace

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    QFile f{QStringLiteral("/home/root/.local/share/rmpp-hwr/selmenu.txt")};
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *h = findItem(qw->contentItem(), [](QQuickItem *i) {
                    return qstrncmp(i->metaObject()->className(), "SceneSelectionHandler", 21) == 0 && i->isVisible();
                })) {
                out << "parent chain:";
                for (QQuickItem *p = h->parentItem(); p; p = p->parentItem()) out << " < " << p->metaObject()->className();
                out << "\n";
                dumpTree(h, 0, out);
            }
    qWarning("[rmpp-exp] selection menu dumped");
}
