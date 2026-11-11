// Experiment 24: which SceneController / DocumentView members deal with typed text (root text flow, cursor, paste)?
// Run through rmpp-studio's add-on (no restart): rmpp-ctl ui "load /tmp/exp24.so" -> writes /tmp/exp24.txt
#include <functional>
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
QQuickItem *findAnywhere(const std::function<bool(QQuickItem *)> &pred) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), pred)) return f;
    return nullptr;
}
bool interesting(const QByteArray &s) {
    static const char *keys[] = {"ext", "oot", "aste", "ursor", "nsert", "aragraph", "ype", "onvert", "hwc", "Hwc", "lipboard", "ocument"};
    for (const char *k : keys)
        if (s.contains(k)) return true;
    return false;
}
void dump(QObject *o, const char *label, QTextStream &out, bool all) {
    if (!o) { out << label << ": null\n"; return; }
    out << "== " << label << " " << o->metaObject()->className() << "\n";
    for (const QMetaObject *m = o->metaObject(); m && qstrcmp(m->className(), "QObject"); m = m->superClass()) {
        out << "[class " << m->className() << "]\n";
        for (int i = m->propertyOffset(); i < m->propertyCount(); ++i) {
            const QMetaProperty p = m->property(i);
            if (all || interesting(p.name()))
                out << "  prop " << p.typeName() << " " << p.name() << " = " << p.read(o).toString().left(60) << "\n";
        }
        for (int i = m->methodOffset(); i < m->methodCount(); ++i) {
            const QMetaMethod mm = m->method(i);
            static const char *k[] = {"method", "signal", "slot", "ctor"};
            if (all || interesting(mm.methodSignature()))
                out << "  " << k[mm.methodType()] << " " << mm.typeName() << " " << mm.methodSignature() << "\n";
        }
    }
}
QByteArray g_ret;
}

extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *) {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "DocumentView_QMLTYPE", 20) == 0 && i->isVisible(); });
    if (!dv) return "error: no open notebook";
    QFile f(QStringLiteral("/tmp/exp24.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "error: cannot write /tmp/exp24.txt";
    QTextStream out(&f);
    QObject *sc = dv->property("sceneController").value<QObject *>();
    dump(sc, "sceneController (all)", out, true);
    dump(dv, "DocumentView", out, false);
    for (const char *p : {"penHandler", "textHandler", "textController", "keyboardHandler", "typingController"})
        if (QObject *o = dv->property(p).value<QObject *>()) dump(o, p, out, false);
    if (QQuickItem *h = findAnywhere([](QQuickItem *i) { return qstrncmp(i->metaObject()->className(), "HwcSelection", 12) == 0; }))
        dump(h, "HwcSelection (all)", out, true);
    f.close();
    g_ret = "ok wrote /tmp/exp24.txt";
    return g_ret.constData();
}
