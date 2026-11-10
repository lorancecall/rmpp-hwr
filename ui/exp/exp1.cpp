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
const char *kOut = "/home/root/.local/share/rmpp-hwr/line.txt";

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

bool safeRead(const void *p, void *dst, size_t n) {
    iovec local{dst, n}, remote{const_cast<void *>(p), n};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(n);
}

QString hex(const void *p, int n) {
    QByteArray b(n, 0);
    if (!safeRead(p, b.data(), n)) return QStringLiteral("    (unreadable)\n");
    QString s;
    for (int i = 0; i < n; i += 16) {
        s += QStringLiteral("    %1:").arg(i, 4, 16, QLatin1Char('0'));
        for (int j = i; j < i + 16 && j < n; ++j) s += QStringLiteral(" %1").arg(uchar(b[j]), 2, 16, QLatin1Char('0'));
        s += QStringLiteral("  |");
        for (int j = i; j < i + 16 && j < n; j += 4) {
            float f;
            memcpy(&f, b.constData() + j, 4);
            s += QStringLiteral(" %1").arg(double(f), 0, 'g', 6);
        }
        s += QLatin1Char('\n');
    }
    return s;
}

class RawSpy : public QObject {
public:
    std::function<void(void **)> fn;
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0 && fn) fn(a);
        return -1;
    }
};

QMetaType g_type;
int g_count = 0;

void dump(void *line) {
    QFile f{QString::fromLatin1(kOut)};
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    const int n = g_type.sizeOf();
    out << "type " << g_type.name() << " size " << n << " align " << g_type.alignOf() << " flags 0x" << Qt::hex
        << g_type.flags() << Qt::dec << "\n" << hex(line, n);
    quint64 w[64] = {};
    safeRead(line, w, qMin(n, 512));
    for (int i = 0; i < n / 8; ++i) {
        const quint64 v = w[i];
        if (v > 0x10000 && !(v & 3) && (v >> 48) == 0) {
            out << "  word @" << i * 8 << " = 0x" << Qt::hex << v << Qt::dec << " ->\n" << hex(reinterpret_cast<void *>(v - 16), 16 + 256);
        }
    }
    out.flush();
}
} // namespace

bool g_countOnly = false;

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    g_countOnly = qstrcmp(arg, "count") == 0;
    QQuickItem *dv = documentView();
    QObject *ph = dv ? dv->property("penHandler").value<QObject *>() : nullptr;
    if (!ph) { qWarning("[rmpp-exp] no pen handler"); return; }
    const int sig = ph->metaObject()->indexOfSignal("stroke(Line)");
    g_type = ph->metaObject()->method(sig).parameterMetaType(0);
    static RawSpy *spy = new RawSpy;
    spy->fn = [](void **a) {
        if (g_countOnly) { qWarning("[rmpp-exp] stroke %d seen", ++g_count); return; }
        if (g_count++) return;   // first stroke only
        void *copy = g_type.create(a[1]);   // copy in the emitting thread, dump on the main thread
        QMetaObject::invokeMethod(qApp, [copy] { dump(copy); qWarning("[rmpp-exp] stroke dumped"); }, Qt::QueuedConnection);
    };
    QMetaObject::connect(ph, sig, spy, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
    qWarning("[rmpp-exp] spy armed on %s, type %s size %d", ph->metaObject()->className(), g_type.name(), int(g_type.sizeOf()));
}
