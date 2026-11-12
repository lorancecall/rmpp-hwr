// Experiment 31: which xochitl objects can import a file or copy pages without a restart (importFromUrls, copyPages,
// duplicate...)? Walks every window's items, their QObject children and QObject* properties (also of the QML
// engine's root context) and lists, once per class, the methods and properties matching the keys.
// Run through rmpp-studio's add-on (no restart): rmpp-ctl ui "load /tmp/exp31.so [extra,keys]" -> /tmp/exp31.txt
#include <QtCore/QFile>
#include <QtCore/QMetaMethod>
#include <QtCore/QSet>
#include <QtCore/QTextStream>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {
QStringList g_keys = {"import", "Import", "copyPage", "CopyPage", "uplicate", "ibrary", "createDocument", "addDocument",
                      "newNotebook", "createNotebook", "reload", "rescan", "efresh"};
QSet<const QObject *> g_seen;
QSet<QByteArray> g_classes;
QStringList g_cls;   // "cls:A,B": every member of the classes whose name contains A or B

bool hit(const QByteArray &s) {
    for (const QString &k : g_keys)
        if (s.contains(k.toUtf8())) return true;
    return false;
}

bool allOf(const QByteArray &cls) {
    for (const QString &c : g_cls)
        if (cls.contains(c.toUtf8())) return true;
    return false;
}

void visit(QObject *o, const QString &path, QTextStream &out, int depth) {
    if (!o || depth > 60 || g_seen.contains(o)) return;
    g_seen.insert(o);
    const QMetaObject *mo = o->metaObject();
    const QByteArray cls = mo->className();
    if (!g_classes.contains(cls)) {
        g_classes.insert(cls);
        QString lines;
        for (const QMetaObject *m = mo; m && qstrcmp(m->className(), "QObject"); m = m->superClass()) {
            for (int i = m->methodOffset(); i < m->methodCount(); ++i) {
                const QMetaMethod mm = m->method(i);
                if (allOf(cls) || hit(mm.methodSignature()))
                    lines += QStringLiteral("  %1 %2 %3\n").arg(QLatin1String(m->className()), QLatin1String(mm.typeName()),
                                                                 QLatin1String(mm.methodSignature()));
            }
            for (int i = m->propertyOffset(); i < m->propertyCount(); ++i)
                if (allOf(cls) || hit(m->property(i).name()))
                    lines += QStringLiteral("  %1 prop %2 %3\n").arg(QLatin1String(m->className()),
                                                                      QLatin1String(m->property(i).typeName()),
                                                                      QLatin1String(m->property(i).name()));
        }
        if (!lines.isEmpty()) out << "== " << cls << "  at " << path << "\n" << lines;
    }
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!(p.metaType().flags() & QMetaType::PointerToQObject)) continue;
        QObject *c = p.read(o).value<QObject *>();
        if (c) visit(c, path + QLatin1Char('.') + QLatin1String(p.name()), out, depth + 1);
    }
    for (QObject *c : o->children())
        visit(c, path + QLatin1Char('/') + QLatin1String(c->metaObject()->className()), out, depth + 1);
}
QByteArray g_ret;
}

extern "C" __attribute__((visibility("default"))) const char *rmpp_remote_exp(const char *arg) {
    const QString extra = QString::fromUtf8(arg).trimmed();
    g_cls.clear();
    if (extra.startsWith(QLatin1String("cls:"))) g_cls = extra.mid(4).split(QLatin1Char(','));
    else if (!extra.isEmpty()) g_keys = extra.split(QLatin1Char(','));
    QFile f(QStringLiteral("/tmp/exp31.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "error: cannot write /tmp/exp31.txt";
    QTextStream out(&f);
    g_seen.clear();
    g_classes.clear();
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
            visit(qw, QStringLiteral("window"), out, 0);
            if (QQmlEngine *e = qmlEngine(qw->contentItem()->childItems().value(0)))
                visit(e->rootContext(), QStringLiteral("rootContext"), out, 0);
        }
    QObject *app = QCoreApplication::instance();
    visit(app, QStringLiteral("app"), out, 0);
    out << "-- " << g_seen.size() << " objects, " << g_classes.size() << " classes\n";
    f.close();
    g_ret = QByteArray("ok wrote /tmp/exp31.txt, ") + QByteArray::number(g_seen.size()) + " objects";
    return g_ret.constData();
}
