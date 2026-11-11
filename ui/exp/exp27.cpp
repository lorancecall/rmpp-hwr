// Experiment 27: the toolbar's tools (for rmpp-refresh): enums, properties and children of #settingsMenu and its loader.
#include <functional>
#include <QtCore/QFile>
#include <QtCore/QMetaEnum>
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
QQuickItem *byName(const char *n) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), [n](QQuickItem *i) { return i->objectName() == QLatin1String(n); })) return f;
    return nullptr;
}
void dump(QObject *o, const char *label, QTextStream &out) {
    if (!o) { out << label << ": null\n"; return; }
    out << "== " << label << " " << o->metaObject()->className() << "\n";
    for (const QMetaObject *m = o->metaObject(); m && qstrcmp(m->className(), "QQuickItem"); m = m->superClass()) {
        out << "[class " << m->className() << "]\n";
        for (int i = m->enumeratorOffset(); i < m->enumeratorCount(); ++i) {
            const QMetaEnum e = m->enumerator(i);
            out << "  enum " << e.name() << ":";
            for (int k = 0; k < e.keyCount(); ++k) out << " " << e.key(k) << "=" << e.value(k);
            out << "\n";
        }
        for (int i = m->propertyOffset(); i < m->propertyCount(); ++i)
            out << "  prop " << m->property(i).typeName() << " " << m->property(i).name() << " = " << m->property(i).read(o).toString().left(60) << "\n";
    }
    if (auto *it = qobject_cast<QQuickItem *>(o))
        for (QQuickItem *c : it->childItems()) out << "  child " << c->metaObject()->className() << " #" << c->objectName() << "\n";
}
QByteArray g_ret;
}
extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *) {
    QFile f(QStringLiteral("/tmp/exp27.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "error: write";
    QTextStream out(&f);
    QQuickItem *menu = byName("settingsMenu");
    dump(menu, "settingsMenu", out);
    dump(menu ? menu->parentItem() : nullptr, "settingsMenu parent", out);
    QQuickItem *layout = byName("toolbarLayout");
    dump(layout, "toolbarLayout", out);
    if (layout) for (QQuickItem *c : layout->childItems())
        out << "  layout child " << c->metaObject()->className() << " #" << c->objectName() << " y=" << c->y() << " h=" << c->height() << " vis=" << c->isVisible() << "\n";
    f.close();
    return "ok wrote /tmp/exp27.txt";
}
