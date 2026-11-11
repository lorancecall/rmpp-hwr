// Experiment 19: the lasso's SelectionImage: its properties, and grab it to /tmp/sel.png.
#include <functional>
#include <QtCore/QMetaProperty>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
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
extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    QQuickItem *img = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((img = findItem(qw->contentItem(), [](QQuickItem *i) { return qstrcmp(i->metaObject()->className(), "SelectionImage") == 0 && i->isVisible(); })))
                break;
    if (!img) { qWarning("[rmpp-exp] no SelectionImage"); return; }
    QImage im;
    const bool ok = QMetaObject::invokeMethod(img, "image", Qt::DirectConnection, Q_RETURN_ARG(QImage, im));
    qWarning("[rmpp-exp] image(): %d %dx%d format %d item %.0fx%.0f save %d", ok, im.width(), im.height(), int(im.format()),
             img->width(), img->height(), im.isNull() ? 0 : im.save(QStringLiteral("/tmp/sel.png")));
}
