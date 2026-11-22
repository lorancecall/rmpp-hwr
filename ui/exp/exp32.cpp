// Experiment 32: a JavaScript probe inside xochitl's QML: evaluates ARG as a QML expression in the context of an item
// (default: the item with objectName or class matching "Navigator", else the window's first item) and returns the
// result (objects: class name and, with a leading "?", their invokable members).
//   rmpp-ctl ui "load /tmp/exp32.so typeof documentImporter"
//   rmpp-ctl ui "load /tmp/exp32.so ?documentImporter"
//   rmpp-ctl ui "load /tmp/exp32.so @DocumentView_QMLTYPE|<expr>"   (context of the first item whose class starts so)
#include <functional>
#include <QtCore/QMetaMethod>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQml/QJSValue>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlExpression>
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

QString members(QObject *o) {
    QString s;
    for (const QMetaObject *m = o->metaObject(); m && qstrcmp(m->className(), "QObject"); m = m->superClass())
        for (int i = m->methodOffset(); i < m->methodCount(); ++i) {
            const QMetaMethod mm = m->method(i);
            if (mm.methodType() == QMetaMethod::Signal) continue;
            s += QStringLiteral("\n  %1 %2").arg(QLatin1String(mm.typeName()), QLatin1String(mm.methodSignature()));
        }
    return s;
}
}

extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *arg) {
    QString expr = QString::fromUtf8(arg);
    QString where = QStringLiteral("Navigator");
    if (expr.startsWith(QLatin1Char('@'))) {
        const int bar = expr.indexOf(QLatin1Char('|'));
        where = expr.mid(1, bar - 1);
        expr = expr.mid(bar + 1);
    }
    bool listMembers = expr.startsWith(QLatin1Char('?'));
    if (listMembers) expr = expr.mid(1);
    QQuickItem *item = nullptr;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
            item = findItem(qw->contentItem(), [&](QQuickItem *i) {
                return QLatin1String(i->metaObject()->className()).startsWith(where) && qmlContext(i);
            });
            if (!item) item = findItem(qw->contentItem(), [](QQuickItem *i) { return qmlContext(i) != nullptr; });
            if (item) break;
        }
    if (!item) return "error: no QML item";
    QQmlExpression e(qmlContext(item), item, expr);
    QVariant v = e.evaluate();
    QString out = QStringLiteral("[ctx %1] ").arg(QLatin1String(item->metaObject()->className()));
    if (e.hasError()) out += QStringLiteral("error: ") + e.error().toString();
    else if (v.canConvert<QJSValue>() && v.value<QJSValue>().isQObject()) v = QVariant::fromValue(v.value<QJSValue>().toQObject());
    if (!e.hasError()) {
        if (QObject *o = v.value<QObject *>()) {
            out += QStringLiteral("object %1 '%2'").arg(QLatin1String(o->metaObject()->className()), o->objectName());
            if (listMembers) out += members(o);
        } else if (v.canConvert<QJSValue>()) {
            out += v.value<QJSValue>().toString();
        } else {
            out += QStringLiteral("%1: %2").arg(QLatin1String(v.typeName()), v.toString());
        }
    }
    g_ret = out.toUtf8();
    return g_ret.constData();
}
