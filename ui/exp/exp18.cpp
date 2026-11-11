// Experiment 18: page geometry properties of the open page (to map scene coordinates to .rm coordinates).
#include <functional>
#include <QtCore/QRectF>
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
extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *) {
    QQuickItem *dv = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((dv = findItem(qw->contentItem(), [](QQuickItem *i) {
                     return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible();
                 })))
                break;
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc) return;
    for (const char *p : {"boundingRect", "itemsBoundingRect", "defaultNoteBounds", "defaultPortraitBounds", "primaryContentRect",
                          "rootDocumentBoundingRect", "explicitPaperSize", "paperPortraitBounds", "paperNoteBounds"}) {
        const QVariant v = sc->property(p);
        const QRectF r = v.toRectF();
        const QSizeF s = v.toSizeF();
        qWarning("[rmpp-exp] %s = rect(%.1f,%.1f %.1fx%.1f) size(%.1fx%.1f)", p, r.x(), r.y(), r.width(), r.height(), s.width(), s.height());
    }
    qWarning("[rmpp-exp] dv.currentPageId=%s dv.currentPage=%d sc.pageId=%s", qPrintable(dv->property("currentPageId").toString()),
             dv->property("currentPage").toInt(), qPrintable(sc->property("pageId").toString()));
    QObject *ph = dv->property("penHandler").value<QObject *>();
    if (ph) {
        const QRectF r = ph->property("pdfContentRect").toRectF();
        qWarning("[rmpp-exp] pdfContentRect = %.1f,%.1f %.1fx%.1f", r.x(), r.y(), r.width(), r.height());
    }
}
