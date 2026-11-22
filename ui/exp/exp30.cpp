// Experiment 30: how to activate the selection tool from code. arg "a": emit selectionButton.clicked();
// "b": emit clicked() on its ToolButton child; "c": synthesized mouse press/release at its centre. Logs "selected".
#include <functional>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
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
QByteArray g_ret;
}
extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *argc) {
    QQuickItem *tool = nullptr;
    QQuickWindow *win = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if ((tool = findItem(qw->contentItem(), [](QQuickItem *i) { return i->objectName() == QLatin1String("selectionButton"); }))) { win = qw; break; }
    if (!tool) return "error: no selectionButton";
    const QString arg = QString::fromUtf8(argc);
    QString kids;
    for (QQuickItem *c : tool->childItems()) kids += QLatin1String(c->metaObject()->className()) + QLatin1Char(' ');
    bool ok = false;
    if (arg == QLatin1String("a")) ok = QMetaObject::invokeMethod(tool, "clicked");
    else if (arg == QLatin1String("b")) {
        QQuickItem *btn = findItem(tool, [](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "ToolButton", 10) == 0; });
        ok = btn && QMetaObject::invokeMethod(btn, "clicked");
    } else if (arg == QLatin1String("c")) {
        const QPointF p = tool->mapToScene(QPointF(tool->width() / 2, tool->height() / 2));
        QMouseEvent press(QEvent::MouseButtonPress, p, p, tool->mapToGlobal(QPointF(tool->width() / 2, tool->height() / 2)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, p, p, tool->mapToGlobal(QPointF(tool->width() / 2, tool->height() / 2)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &press);
        QCoreApplication::sendEvent(win, &release);
        ok = true;
    }
    QPointer<QQuickItem> tp(tool);
    QTimer::singleShot(600, qApp, [tp] { if (tp) qWarning("[rmpp-exp] selected after: %d", tp->property("selected").toBool()); });
    g_ret = QStringLiteral("ok %1 invoked=%2 selected before=%3 kids=%4").arg(arg).arg(ok).arg(tool->property("selected").toBool()).arg(kids).toUtf8();
    return g_ret.constData();
}
