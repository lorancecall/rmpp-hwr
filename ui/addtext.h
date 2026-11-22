// "Add text object" (included from hwrui.cpp after ink.h): a text object (ink in the glyph font) from typed text,
// without handwriting first. An entry in xochitl's typing (T) tool menu, under "Add text"; then a tap on the page
// marks where the text goes, the edit box opens empty, and on OK the text is drawn there (ink.h applyInkEdit,
// create mode). As xochitl does when the pen taps outside a selection, a selection on the page is cleared first.

const char *kAddTextItemQml = R"QML(
import QtQuick
import QtQuick.Layouts
import ark.controls as ArkControls
ArkControls.FoldoutItem {
    objectName: "rmppHwrAddTextObject"
    Layout.fillWidth: true
    label: "Add text object"
    focusPolicy: Qt.NoFocus   // keeps the edit box's text field the focus item (else typing goes nowhere)
    iconSource: "qrc:/ark/icons/ballpoint_tip"
    visible: hwr.converter === "rmpp"
    onClicked: hwr.command = "addtext#" + Date.now()
}
)QML";

const char *kPlaceQml = R"QML(
import QtQuick
import ark.tokens as ArkTokens
import ark.controls as ArkControls
import xofm.libs.peninput
Item {
    id: place
    property var penManager: null
    anchors.fill: parent
    z: 10000
    // the pen draws nothing while placing; a tap (pen or finger) anywhere on the page places the text
    PenInputBlocker { anchors.fill: parent; manager: place.penManager }
    MouseArea {
        anchors.fill: parent
        onClicked: (m) => hwr.command = "addtextat:" + m.x + ":" + m.y + "#" + Date.now()
    }
    Rectangle {
        width: row.width + 60; height: 110
        x: (parent.width - width) / 2; y: 40
        color: "white"; border.color: "black"; border.width: 3; radius: 8
        MouseArea { anchors.fill: parent }   // taps on the banner do not place
        Row {
            id: row
            anchors.centerIn: parent; spacing: 30
            Text { text: "Tap where the text object goes"; font.pixelSize: 34; anchors.verticalCenter: parent.verticalCenter }
            ArkControls.Button { text: "Cancel"; type: ArkTokens.Button.secondary; onClicked: hwr.command = "addtextcancel#" + Date.now() }
        }
    }
}
)QML";

struct AddPlace {
    QPointer<QObject> overlay;
    QPointer<QQuickItem> dv;
    QString page;
};
AddPlace g_place;

void closeAddPlace() {
    if (g_place.overlay) g_place.overlay->deleteLater();
    g_place = AddPlace{};
}

// Called from the 1.5 s timer: the notebook was closed or the page turned while placing.
void closeStaleAddPlace() {
    if (!g_place.overlay) return;
    if (!g_place.dv || !g_place.dv->isVisible() || g_place.dv->property("currentPageId").toString() != g_place.page)
        closeAddPlace();
}

// The view that draws the open page (DeviceSceneView, a child of the DocumentView; it also has viewToScene(),
// sceneToView() and the signal closeToolbarFoldout()).
QQuickItem *sceneViewOf(QQuickItem *dv) {
    return findItem(dv, [](QQuickItem *i) { return classStarts(i, "DeviceSceneView_QMLTYPE") && i->isVisible(); });
}

// A point in DocumentView coordinates -> page coordinates (scene units: x centred on 0, -810..810; y from the top),
// through the view's own viewToScene (follows zoom and scrolling).
bool viewToPage(QQuickItem *dv, const QPointF &p, QPointF *out) {
    QQuickItem *sv = sceneViewOf(dv);
    if (!sv) return false;
    QVariant r;
    if (!QMetaObject::invokeMethod(sv, "viewToScene", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, QVariant(sv->mapFromItem(dv, p)))))
        return false;
    if (r.canConvert<QJSValue>()) r = r.value<QJSValue>().toVariant();
    if (!r.canConvert<QPointF>()) return false;
    *out = r.toPointF();
    return true;
}

// The em size for a new object: the size the last new object was made at (its size in the edit box included), else
// about the size of xochitl's body text. Not converted handwriting's: that follows the handwriting and is big.
double defaultTextObjectEm() {
    const double em = g_state->value(QStringLiteral("addTextEm")).toDouble();
    return em > 0 ? qBound(20.0, em, 200.0) : 40.0;
}

// Closes the typing tool's menu (foldout), as picking one of xochitl's own entries does.
void closeTypingMenu(QQuickItem *dv) {
    QQuickItem *sv = dv ? sceneViewOf(dv) : nullptr;
    if (sv && QMetaObject::invokeMethod(sv, "closeToolbarFoldout")) return;
    QQuickItem *menu = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("typingMenu"); });
    QQuickItem *fold = menu ? findItem(menu, [](QQuickItem *i) { return classStarts(i, "ToolbarFoldout"); }) : nullptr;
    if (fold && fold->metaObject()->indexOfMethod("close()") >= 0 && QMetaObject::invokeMethod(fold, "close")) return;
    QQuickItem *btn = fold ? fold->parentItem() : nullptr;   // the T button; its foldout is open while it is checked
    if (btn && btn->property("checked").toBool()) btn->setProperty("checked", false);
}

// T menu > Add text object: close the menu, then wait for the tap that places the text.
void startAddTextObject() {
    if (g_state->value(QStringLiteral("busy")).toBool() || g_edit.box) return;
    if (!glyphsAvailable()) { setStatus(QStringLiteral("no ink font installed"), false); return; }
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    closeTypingMenu(dv);
    QQmlEngine *eng = dv ? qmlEngine(dv) : nullptr;
    QQmlContext *base = dv ? qmlContext(dv) : nullptr;
    if (!eng || !base) { setStatus(QStringLiteral("no open page"), false); return; }
    closeAddPlace();
    auto *ctx = new QQmlContext(base);
    ctx->setContextProperty(QStringLiteral("hwr"), g_state);
    QObject *pin = dv->property("penInput").value<QObject *>();
    QObject *mgr = pin ? pin->property("surfaceManager").value<QObject *>() : nullptr;
    QObject *o = createInline(eng, ctx, kPlaceQml, "AddTextPlace", dv, {{QStringLiteral("penManager"), QVariant::fromValue(mgr)}});
    if (!o) { delete ctx; return; }
    ctx->setParent(o);
    g_place = AddPlace{o, dv, dv->property("currentPageId").toString()};
}

// The tap: x, y in DocumentView coordinates.
void placeTextObject(double x, double y) {
    QPointer<QQuickItem> dv = g_place.dv;
    closeAddPlace();
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    QObject *ph = dv ? dv->property("penHandler").value<QObject *>() : nullptr;
    QPointF at;
    if (!sc || !ph || !viewToPage(dv, QPointF(x, y), &at)) { setStatus(QStringLiteral("no open page"), false); return; }
    qWarning("[rmpp-hwr] add text object at view %.0f,%.0f = page %.1f,%.1f", x, y, at.x(), at.y());
    InkEdit e;
    e.sc = sc; e.ph = ph; e.layer = sc->property("currentLayer").toInt();
    e.create = true;
    e.em = defaultTextObjectEm();
    e.origin = at - QPointF(0, e.em * 0.5);   // the tap marks the middle of the first line
    e.maxW = qMax(810.0 - 40.0 - e.origin.x(), e.em * 8);
    g_state->insert(QStringLiteral("inkMissing"), QString());
    showInkEditBox(dv, e, QStringLiteral("Add text object"), QString(), 1);
}

// Characters of the edit box's text that the ink font has no strokes for (shown in the box, left out of the ink).
void updateInkMissing(const QString &text) {
    QString missing;
    useGlyphLanguage(text);
    for (const uint c : text.toUcs4()) {
        const QString ch = QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1);
        if (!QChar::isSpace(c) && !glyphOf(char32_t(c)) && !missing.contains(ch)) missing += ch;
    }
    if (g_state->value(QStringLiteral("inkMissing")).toString() != missing) g_state->insert(QStringLiteral("inkMissing"), missing);
}

// The typing (T) tool menu: our entry right under xochitl's "Add text" (labelled "Edit text" on a page with text).
void tryInjectAddText() {
    QQuickItem *menu = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("typingMenu"); });
    QQuickItem *add = menu ? findItem(menu, [](QQuickItem *i) { const QString l = i->property("label").toString();
        return l == QLatin1String("Add text") || l == QLatin1String("Edit text"); }) : nullptr;
    QQuickItem *col = add ? add->parentItem() : nullptr;
    if (!col || col->property("_rmppHwr").toBool()) return;
    QQmlEngine *eng = qmlEngine(add);
    QQmlContext *base = qmlContext(add);
    if (!eng || !base) return;
    col->setProperty("_rmppHwr", true);
    auto *ctx = new QQmlContext(base, col);
    ctx->setContextProperty(QStringLiteral("hwr"), g_state);
    auto *item = qobject_cast<QQuickItem *>(createInline(eng, ctx, kAddTextItemQml, "AddTextObject", col));
    if (item) item->stackAfter(add);
    qWarning("[rmpp-hwr] typing menu: add text object %s", item ? "added" : "FAILED");
}
