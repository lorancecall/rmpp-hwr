// Experiment 12: does a TextInput of ours bring up xochitl's on-screen keyboard (with the Korean layout)?
// Shows a small edit box over the open document; logs the text on OK. "close" removes it.
#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
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

const char *kQml = R"QML(
import QtQuick
import ark.tokens as ArkTokens
import ark.controls as ArkControls
Rectangle {
    id: box
    property string initial: "안녕하세요"
    signal done(string text)
    width: 900; height: 260
    x: (parent.width - width) / 2; y: 300
    z: 10000
    color: "white"; border.color: "black"; border.width: 3
    MouseArea { anchors.fill: parent }   // keep taps off the page
    Text { x: 30; y: 20; text: "Edit text"; font.pixelSize: 34 }
    Rectangle {
        x: 30; y: 80; width: parent.width - 60; height: 70
        border.color: "black"; border.width: 2
        TextInput {
            id: input
            anchors.fill: parent; anchors.margins: 12
            font.pixelSize: 40
            text: box.initial
            focus: true
            Component.onCompleted: { forceActiveFocus(); selectAll(); Qt.inputMethod.show(); }
        }
    }
    Row {
        x: parent.width - width - 30; y: 175; spacing: 20
        ArkControls.Button { text: "Cancel"; type: ArkTokens.Button.secondary; onClicked: box.done("") }
        ArkControls.Button { text: "OK"; onClicked: box.done(input.text) }
    }
}
)QML";

QPointer<QObject> g_box;
} // namespace

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    if (g_box) { g_box->deleteLater(); g_box = nullptr; }
    if (qstrcmp(arg, "close") == 0) return;
    QQuickItem *dv = documentView();
    QQmlEngine *eng = dv ? qmlEngine(dv) : nullptr;
    if (!eng) { qWarning("[rmpp-exp] no document"); return; }
    QQuickItem *root = dv->window()->contentItem();
    QQmlComponent comp(eng);
    comp.setData(QByteArray(kQml), QUrl(QStringLiteral("qrc:/rmpphwr/EditBox.qml")));
    QObject *o = comp.create(qmlContext(dv));
    if (!o) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-exp] %s", qPrintable(e.toString()));
        return;
    }
    auto *item = qobject_cast<QQuickItem *>(o);
    item->setParentItem(root);
    item->setParent(root);
    g_box = o;
    const int sig = o->metaObject()->indexOfSignal("done(QString)");
    struct Hook : QObject {
        int qt_metacall(QMetaObject::Call c, int id, void **a) override {
            id = QObject::qt_metacall(c, id, a);
            if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
            if (id == 0) {
                qWarning("[rmpp-exp] edit box result: '%s'", qPrintable(*static_cast<QString *>(a[1])));
                if (g_box) { g_box->deleteLater(); g_box = nullptr; }
            }
            return -1;
        }
    };
    auto *hook = new Hook;
    hook->setParent(o);
    QMetaObject::connect(o, sig, hook, QObject::staticMetaObject.methodCount(), Qt::QueuedConnection, nullptr);
    qWarning("[rmpp-exp] edit box shown");
}
