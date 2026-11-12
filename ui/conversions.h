// Saved conversions (Settings > Handwriting > Collect training data > Save conversions). Included inside hwrui.cpp's
// namespace after practicenb.h.
//
// After each conversion the converted page(s) are copied into the notebook hwr-db/<lang>/Conversions (lang = the
// language of the model that made it), one conversion after the other, so corrections happen there and the original
// page can change freely. Copies are made by xochitl itself (QML singletons of xofm.libs.library, kConvQml):
// DocumentController.copyPages(doc, [page indices], target, at) appends to Conversions, the first conversion creates
// it with LibraryController.createDocumentFromExisting(folder, "Conversions", doc, pages).
//   Convert to text, new page:    the handwritten page and the typed page after it (2 pages)
//   Convert to text, into page / lasso to typed text: the page after the text went in (1 page: text, and the ink
//                                 unless it was replaced; the recognized ink is kept as ink/<time>.png)
// conversions.jsonl, one line per conversion (read by `hwr pull`):
//   time, doc, src (page with the ink), dst (page with the typed text to correct), lines (recognized), ink (.rm or
//   .png of the recognized ink, relative to the rmpp-hwr folder), kind (page | page-replace | lasso |
//   lasso-replace), lang, model, orig_doc / orig_src / orig_dst (the pages converted), corrected (time, after Done
//   correcting). doc/src/dst name the copies in Conversions; without a copy (it failed) the original pages.

struct PendingConv {
    QJsonObject rec;
    QList<int> pages;
    QString conv;
    int before = 0, tries = 0;
};
QList<std::shared_ptr<PendingConv>> g_convQueue;   // one copy at a time

void appendRecord(const QJsonObject &o) {
    QDir().mkpath(QLatin1String(kLibDir));
    QFile f(recordsPath());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    const QString time = o.value(QStringLiteral("time")).toString();
    for (const char *k : {"dst", "orig_dst"}) {
        const QString p = o.value(QLatin1String(k)).toString();
        if (p.isEmpty()) continue;
        g_dstToTime[p] = time;
        g_dstDone[p] = false;
    }
}

void startNextConv();

// Copy requested (kConvQml answers "conv:copied:<1|0>:<conversions doc>:<pages before>").
void convCopied(bool ok, const QString &conv, int before) {
    if (g_convQueue.isEmpty()) return;
    auto c = g_convQueue.first();
    if (!ok || conv.isEmpty()) {
        qWarning("[rmpp-hwr] conversion: copy to Conversions failed, recorded with the original pages");
        appendRecord(c->rec);
        g_convQueue.removeFirst();
        startNextConv();
        return;
    }
    c->conv = conv;
    c->before = before;
    auto *t = new QTimer(qApp);
    QObject::connect(t, &QTimer::timeout, qApp, [t, c] {
        const QStringList ids = docPageIds(c->conv);
        if (ids.size() < c->before + c->pages.size()) {
            if (++c->tries < 40) return;
            qWarning("[rmpp-hwr] conversion: copied pages not found, recorded with the original pages");
            appendRecord(c->rec);
        } else {
            QJsonObject r = c->rec;
            const QString first = ids[c->before], last = ids[c->before + c->pages.size() - 1];
            r.insert(QStringLiteral("doc"), c->conv);
            r.insert(QStringLiteral("src"), first);
            r.insert(QStringLiteral("dst"), last);
            appendRecord(r);
            qWarning("[rmpp-hwr] conversion saved in Conversions (%lld pages)", (long long)c->pages.size());
        }
        t->deleteLater();
        if (!g_convQueue.isEmpty() && g_convQueue.first() == c) g_convQueue.removeFirst();
        startNextConv();
    });
    t->start(500);
}

void startNextConv() {
    if (g_convQueue.isEmpty()) { g_state->insert(QStringLiteral("convCopy"), QString()); return; }
    auto c = g_convQueue.first();
    const QString lang = c->rec.value(QStringLiteral("lang")).toString();
    const QString root = findFolder(QStringLiteral("hwr-db"), QString());
    const QString folder = root.isEmpty() ? QString() : findFolder(lang, root);
    QString conv;
    if (!folder.isEmpty()) {
        const QStringList metas = QDir(QLatin1String(kPages)).entryList({QStringLiteral("*.metadata")}, QDir::Files);
        for (const QString &m : metas) {
            const QJsonObject o = readJsonFile(QLatin1String(kPages) + QLatin1Char('/') + m);
            if (o.value(QStringLiteral("type")).toString() == QLatin1String("DocumentType")
                && o.value(QStringLiteral("visibleName")).toString() == QLatin1String("Conversions")
                && o.value(QStringLiteral("parent")).toString() == folder && !o.value(QStringLiteral("deleted")).toBool()) {
                conv = m.chopped(9);
                break;
            }
        }
    }
    // page positions from the saved page list (by id), else the indices the conversion gave
    const QStringList ids = docPageIds(c->rec.value(QStringLiteral("orig_doc")).toString());
    const int si = ids.indexOf(c->rec.value(QStringLiteral("orig_src")).toString());
    const int di = ids.indexOf(c->rec.value(QStringLiteral("orig_dst")).toString());
    if (si >= 0 && di >= 0) c->pages = si == di ? QList<int>{si} : QList<int>{si, di};
    QJsonArray pages;
    for (int p : c->pages) pages.append(p);
    g_state->insert(QStringLiteral("convCopy"), QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("doc"), c->rec.value(QStringLiteral("orig_doc"))}, {QStringLiteral("pages"), pages},
        {QStringLiteral("lang"), lang}, {QStringLiteral("root"), root}, {QStringLiteral("folder"), folder},
        {QStringLiteral("conv"), conv}, {QStringLiteral("before"), conv.isEmpty() ? 0 : int(docPageIds(conv).size())},
        {QStringLiteral("token"), QString::number(QDateTime::currentMSecsSinceEpoch())}}).toJson(QJsonDocument::Compact)));
}

// A finished conversion: pages = indices (0-based) of the pages to copy, src/dst their ids in `doc`.
void collectConversion(const QString &doc, const QList<int> &pages, const QString &src, const QString &dst,
                       const QStringList &lines, const QString &ink, const QString &kind) {
    if (!g_state->value(QStringLiteral("collect")).toBool() || doc.isEmpty() || pages.isEmpty()) {
        if (!ink.isEmpty()) QFile::remove(QLatin1String(kLibDir) + QLatin1Char('/') + ink);
        return;
    }
    const QString dir = currentModelDir().isEmpty() ? QLatin1String(kLibDir) : currentModelDir();
    const QString model = modelName(dir, QStringLiteral("rmpp-hwr-korean-base"));
    QString lang = modelLanguage(dir, model);
    if (lang.isEmpty()) lang = QStringLiteral("ko");
    auto c = std::make_shared<PendingConv>();
    c->pages = pages;
    c->rec = QJsonObject{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                         {QStringLiteral("doc"), doc}, {QStringLiteral("src"), src}, {QStringLiteral("dst"), dst},
                         {QStringLiteral("lines"), QJsonArray::fromStringList(lines)}, {QStringLiteral("kind"), kind},
                         {QStringLiteral("lang"), lang}, {QStringLiteral("model"), model},
                         {QStringLiteral("orig_doc"), doc}, {QStringLiteral("orig_src"), src}, {QStringLiteral("orig_dst"), dst}};
    if (!ink.isEmpty()) c->rec.insert(QStringLiteral("ink"), ink);
    g_convQueue.append(c);
    if (g_convQueue.size() == 1) QTimer::singleShot(1500, qApp, [] { startNextConv(); });   // let xochitl save the page
}

// The recognized ink of a lasso / page-replace conversion as a PNG (kept with the record).
QString saveInkImage(const QImage &img) {
    if (img.isNull() || !g_state->value(QStringLiteral("collect")).toBool()) return QString();
    QDir().mkpath(QLatin1String(kLibDir) + QStringLiteral("/ink"));
    const QString rel = QStringLiteral("ink/%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    return img.save(QLatin1String(kLibDir) + QLatin1Char('/') + rel) ? rel : QString();
}

const char *kConvQml = R"QML(
import QtQuick
import xofm.libs.library as XL
QtObject {
    property string req: hwr.convCopy || ""
    onReqChanged: {
        if (!req) return;
        let p;
        try { p = JSON.parse(req); } catch (e) { return; }
        let ok = false, conv = p.conv || "", before = p.before || 0;
        try {
            const root = p.root || String(XL.LibraryController.createCollection("", "hwr-db"));
            const folder = p.folder || String(XL.LibraryController.createCollection(root, p.lang));
            if (conv) {
                ok = XL.DocumentController.copyPages(p.doc, p.pages, conv, before);
            } else {
                conv = String(XL.LibraryController.createDocumentFromExisting(folder, "Conversions", p.doc, p.pages));
                before = 0;
                ok = !!conv;
            }
        } catch (e) {
            console.warn("[rmpp-hwr] conversion copy: " + e);
        }
        hwr.command = "conv:copied:" + (ok ? "1" : "0") + ":" + conv + ":" + before + "#" + Date.now();
    }
}
)QML";
