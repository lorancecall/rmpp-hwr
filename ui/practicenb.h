// Practice notebooks on the tablet: Settings > Handwriting > Collect training data. Included inside hwrui.cpp's
// namespace after practice.h (layout, sidecar, PDF).
//
// New practice notebook: language (texts/<lang> installed and known to practice.h), topics, pages -> the PDF and its
// sidecar (rmpp-hwr/practice@2 + doc_id, name, topics, source "tablet") -> imported into the folder hwr-db/<lang>
// without restarting xochitl. The import itself runs in QML (kPracticeQml): the singletons of xochitl's QML module
// xofm.libs.library (OS 3.29): LibraryController.createCollection(parent, name) -> id, and
// DocumentImporter.importFromUrls([file url], parent id), the importer behind drag and drop; it copies the PDF and
// adds the notebook to the library at once (checked 2026-11-12, exp31/exp32). The new notebook is found by name and
// folder, then its sidecar is written to the registry practice/<doc_id>.json, where `hwr pull` reads it.
//
// The list: every registry sidecar whose notebook still exists, with the pages written so far (a page with at least
// one stroke). A sidecar whose notebook was deleted is dropped.

QString practiceDir() { return QLatin1String(kLibDir) + QStringLiteral("/practice"); }
QString textsDir() { return QLatin1String(kLibDir) + QStringLiteral("/texts"); }

QJsonObject readJsonFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

bool writeJsonFile(const QString &path, const QJsonObject &o) {
    QFile f(path + QStringLiteral(".new"));
    const QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size() || !f.flush()) return false;
    f.close();
    return ::rename(qPrintable(f.fileName()), qPrintable(path)) == 0;
}

// Id of the folder (CollectionType) `name` inside `parent` ("" = My files), not deleted, not in the trash.
QString findFolder(const QString &name, const QString &parent) {
    const QStringList metas = QDir(QLatin1String(kPages)).entryList({QStringLiteral("*.metadata")}, QDir::Files);
    for (const QString &m : metas) {
        const QJsonObject o = readJsonFile(QLatin1String(kPages) + QLatin1Char('/') + m);
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("CollectionType")
            && o.value(QStringLiteral("visibleName")).toString() == name
            && o.value(QStringLiteral("parent")).toString() == parent && !o.value(QStringLiteral("deleted")).toBool())
            return m.chopped(9);
    }
    return QString();
}

// A v6 .rm page with at least one stroke (a SceneLineItemBlock, type 5).
bool rmHasStrokes(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray b = f.readAll();
    if (!b.startsWith("reMarkable .lines file, version=6")) return b.size() > 100;   // older formats: any content
    qsizetype pos = 43;
    while (pos + 8 <= b.size()) {
        const quint32 len = quint8(b[pos]) | quint8(b[pos + 1]) << 8 | quint8(b[pos + 2]) << 16 | quint32(quint8(b[pos + 3])) << 24;
        if (quint8(b[pos + 7]) == 5) return true;
        pos += 8 + qsizetype(len);
    }
    return false;
}

// Page ids of a document in page order (deleted pages left out).
QStringList docPageIds(const QString &doc) {
    const QJsonObject c = readJsonFile(QStringLiteral("%1/%2.content").arg(QLatin1String(kPages), doc));
    QList<QPair<QString, QString>> pages;   // idx, id
    for (const QJsonValue &v : c.value(QStringLiteral("cPages")).toObject().value(QStringLiteral("pages")).toArray()) {
        const QJsonObject p = v.toObject();
        if (p.contains(QStringLiteral("deleted"))) continue;
        pages.append({p.value(QStringLiteral("idx")).toObject().value(QStringLiteral("value")).toString(),
                      p.value(QStringLiteral("id")).toString()});
    }
    std::stable_sort(pages.begin(), pages.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList ids;
    for (const auto &p : pages) ids << p.second;
    return ids;
}

// Languages offered for a new notebook: texts/<lang>/ with texts and an entry in practice.h.
void refreshPracticeLangs() {
    QJsonArray langs;
    const QStringList codes = QDir(textsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &code : codes) {
        practice::Lang L;
        if (!practice::langInfo(code, &L)) continue;
        const QJsonObject meta = readJsonFile(textsDir() + QLatin1Char('/') + code + QStringLiteral("/meta.json"));
        const QJsonObject mt = meta.value(QStringLiteral("topics")).toObject();
        QJsonArray topics;
        const QStringList files = QDir(textsDir() + QLatin1Char('/') + code).entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
        for (const QString &f : files) {
            const QString id = QFileInfo(f).completeBaseName();
            const QJsonObject t = mt.value(id).toObject();
            topics.append(QJsonObject{{QStringLiteral("id"), id},
                                      {QStringLiteral("name"), t.value(QStringLiteral("name_en")).toString(id)}});
        }
        if (!topics.isEmpty())
            langs.append(QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("name"), L.name}, {QStringLiteral("topics"), topics}});
    }
    g_state->insert(QStringLiteral("practiceLangs"), QString::fromUtf8(QJsonDocument(langs).toJson(QJsonDocument::Compact)));
}

// The registry with progress; sidecars of deleted notebooks are removed.
void refreshPracticeList() {
    QJsonArray list;
    const QStringList files = QDir(practiceDir()).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const QString path = practiceDir() + QLatin1Char('/') + f;
        const QJsonObject s = readJsonFile(path);
        const QString doc = s.value(QStringLiteral("doc_id")).toString(f.chopped(5));
        const QString mpath = QStringLiteral("%1/%2.metadata").arg(QLatin1String(kPages), doc);
        const QJsonObject meta = readJsonFile(mpath);
        if (!QFileInfo::exists(mpath) || meta.value(QStringLiteral("deleted")).toBool()) {
            QFile::remove(path);
            qWarning("[rmpp-hwr] practice notebook %s is gone: sidecar removed", qPrintable(doc));
            continue;
        }
        if (meta.isEmpty()) continue;
        if (meta.value(QStringLiteral("parent")).toString() == QLatin1String("trash")) continue;
        const int pages = s.value(QStringLiteral("pages")).toInt();
        const QStringList ids = docPageIds(doc);
        int written = 0;
        for (int i = 0; i < pages && i < ids.size(); ++i)
            written += rmHasStrokes(QStringLiteral("%1/%2/%3.rm").arg(QLatin1String(kPages), doc, ids[i]));
        const QString lang = s.value(QStringLiteral("languages")).toArray().first().toString();
        practice::Lang L;
        list.append(QJsonObject{{QStringLiteral("doc"), doc},
                                {QStringLiteral("name"), meta.value(QStringLiteral("visibleName")).toString()},
                                {QStringLiteral("lang"), practice::langInfo(lang, &L) ? L.name : lang},
                                {QStringLiteral("pages"), pages}, {QStringLiteral("written"), written},
                                {QStringLiteral("modified"), meta.value(QStringLiteral("lastModified")).toString()}});
    }
    // newest first
    QList<QJsonObject> v;
    for (const QJsonValue &x : list) v << x.toObject();
    std::stable_sort(v.begin(), v.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("modified")).toString().toLongLong() > b.value(QStringLiteral("modified")).toString().toLongLong();
    });
    QJsonArray out;
    for (const QJsonObject &o : v) out.append(o);
    g_state->insert(QStringLiteral("practiceList"), QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
}

struct PendingPractice {
    QJsonObject sidecar;
    QString name, pdf, lang, folder;
    int tries = 0;
};
std::unique_ptr<PendingPractice> g_practice;

void practiceStatus(const QString &s, bool busy) {
    g_state->insert(QStringLiteral("practiceStatus"), s);
    g_state->insert(QStringLiteral("practiceBusy"), busy);
    if (!s.isEmpty()) qWarning("[rmpp-hwr] %s", qPrintable(s));
}

// "practice:new:<lang>:<topic,topic>:<pages>": lay out, draw and hand the PDF to the QML importer.
void newPractice(const QString &lang, const QStringList &topics, int npages) {
    if (g_practice) { practiceStatus(QStringLiteral("A practice notebook is being added, wait a moment"), true); return; }
    practice::Lang L;
    if (!practice::langInfo(lang, &L) || npages < 1 || npages > 40) { practiceStatus(QStringLiteral("No practice texts for this language"), false); return; }
    // name: rmpp-practice-<lang>-<pages>p, then -2, -3 ... (registry names and the target folder)
    QSet<QString> taken;
    for (const QString &f : QDir(practiceDir()).entryList({QStringLiteral("*.json")}, QDir::Files))
        taken.insert(readJsonFile(practiceDir() + QLatin1Char('/') + f).value(QStringLiteral("name")).toString());
    const QString stem = QStringLiteral("rmpp-practice-%1-%2p").arg(lang).arg(npages);
    QString name = stem;
    for (int k = 2; taken.contains(name); ++k) name = QStringLiteral("%1-%2").arg(stem).arg(k);
    const uint64_t seed = uint64_t(QDateTime::currentSecsSinceEpoch() % 1000000);
    practice::Layout lay(seed, L, textsDir(), topics);
    const QList<practice::Page> pages = lay.layout(practice::planKinds(npages, L));
    QDir().mkpath(practiceDir() + QStringLiteral("/new"));
    const QString pdf = practiceDir() + QStringLiteral("/new/") + name + QStringLiteral(".pdf");
    QString err;
    if (!practice::drawPdf(pages, pdf, L, name, &err)) { practiceStatus(err, false); return; }
    QJsonObject side = practice::sidecar(pages, seed, lang);
    side.insert(QStringLiteral("name"), name);
    side.insert(QStringLiteral("topics"), QJsonArray::fromStringList(topics));
    side.insert(QStringLiteral("source"), QStringLiteral("tablet"));
    const QString root = findFolder(QStringLiteral("hwr-db"), QString());
    const QString folder = root.isEmpty() ? QString() : findFolder(lang, root);
    g_practice.reset(new PendingPractice{side, name, pdf, lang, QString()});
    practiceStatus(QStringLiteral("Adding %1…").arg(name), true);
    qWarning("[rmpp-hwr] practice: %s, %d pages, seed %llu", qPrintable(name), npages, (unsigned long long)seed);
    g_state->insert(QStringLiteral("practiceImport"), QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("pdf"), pdf}, {QStringLiteral("name"), name}, {QStringLiteral("lang"), lang},
        {QStringLiteral("root"), root}, {QStringLiteral("folder"), folder},
        {QStringLiteral("token"), QString::number(QDateTime::currentMSecsSinceEpoch())}}).toJson(QJsonDocument::Compact)));
}

void finishPractice(const QString &msg) {
    if (g_practice) QFile::remove(g_practice->pdf);
    g_practice.reset();
    g_state->insert(QStringLiteral("practiceImport"), QString());
    practiceStatus(msg, false);
    refreshPracticeList();
}

// "practice:importing:<1|0>:<folder id>" from the QML importer: wait for the notebook to appear in the folder.
void practiceImporting(bool ok, const QString &folder) {
    if (!g_practice) return;
    if (!ok || folder.isEmpty()) { finishPractice(QStringLiteral("The notebook could not be added")); return; }
    g_practice->folder = folder;
    auto *t = new QTimer(qApp);
    QObject::connect(t, &QTimer::timeout, qApp, [t] {
        if (!g_practice) { t->deleteLater(); return; }
        const QStringList metas = QDir(QLatin1String(kPages)).entryList({QStringLiteral("*.metadata")}, QDir::Files, QDir::Time);
        for (const QString &m : metas) {
            const QJsonObject o = readJsonFile(QLatin1String(kPages) + QLatin1Char('/') + m);
            if (o.value(QStringLiteral("visibleName")).toString() != g_practice->name
                || o.value(QStringLiteral("parent")).toString() != g_practice->folder || o.value(QStringLiteral("deleted")).toBool())
                continue;
            const QString doc = m.chopped(9);
            QJsonObject side = g_practice->sidecar;
            side.insert(QStringLiteral("doc_id"), doc);
            QDir().mkpath(practiceDir());
            const bool ok = writeJsonFile(practiceDir() + QLatin1Char('/') + doc + QStringLiteral(".json"), side);
            const QString where = QStringLiteral("hwr-db/") + g_practice->lang;
            const QString name = g_practice->name;
            t->deleteLater();
            finishPractice(ok ? QStringLiteral("Added %1 to %2").arg(name, where)
                              : QStringLiteral("%1 added, but its layout could not be saved").arg(name));
            return;
        }
        if (++g_practice->tries > 60) { t->deleteLater(); finishPractice(QStringLiteral("The notebook did not appear")); }
    });
    t->start(500);
}

// Runs the import with xochitl's own library objects (see the top of this file). Created with the settings page.
const char *kPracticeQml = R"QML(
import QtQuick
import xofm.libs.library as XL
QtObject {
    property string req: hwr.practiceImport || ""
    onReqChanged: {
        if (!req) return;
        let p;
        try { p = JSON.parse(req); } catch (e) { return; }
        let ok = false, folder = "";
        try {
            const root = p.root || String(XL.LibraryController.createCollection("", "hwr-db"));
            folder = p.folder || String(XL.LibraryController.createCollection(root, p.lang));
            ok = !!folder && XL.DocumentImporter.importFromUrls(["file://" + p.pdf], folder);
        } catch (e) {
            console.warn("[rmpp-hwr] practice import: " + e);
        }
        hwr.command = "practice:importing:" + (ok ? "1" : "0") + ":" + folder + "#" + Date.now();
    }
}
)QML";
