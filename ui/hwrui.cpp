// rmpp-hwr UI glue, loaded into xochitl with LD_PRELOAD (via rmpp-preload).
// Adds "Convert page" (and Settings > Handwriting) next to xochitl's own "Convert to text" tool. On click it reads the
// current page file, recognizes it with librmpp_hwr.so (Rust, loaded only on first use), adds a page after the
// current one and types the text there.
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <thread>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <cstdio>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSet>
#include <QtCore/QTextStream>
#include <QtCore/QMetaMethod>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlPropertyMap>
#include <QtGui/QPainter>
#include <QtQuick/QQuickImageProvider>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {

const char *kLibDir = "/home/root/.local/share/rmpp-hwr";   // librmpp_hwr.so, libonnxruntime.so, rec.onnx, dict.txt
const char *kPages = "/home/root/.local/share/remarkable/xochitl";

const char *kConf = "/home/root/.config/rmpp-hwr/settings.conf";

// busy, status, command (written by QML), settings converter / collect / hwrModel / inkStyle, info modelName / conversions
QQmlPropertyMap *g_state = nullptr;
bool g_confLoaded = false;

// ---- settings (Settings > Handwriting) ----
// /home is mounted after xochitl starts: load once it is there, defaults until then.
void loadSettings() {
    if (g_confLoaded || !QFileInfo::exists(QStringLiteral("/home/root/.local"))) return;
    g_confLoaded = true;
    QSettings s(QLatin1String(kConf), QSettings::IniFormat);
    // converter: "rmpp" (rmpp-hwr replaces reMarkable's Convert to text) or "remarkable" (rmpp-hwr changes nothing).
    // Older settings: model=builtin meant reMarkable's converter.
    QString conv = s.value(QStringLiteral("converter")).toString();
    if (conv.isEmpty()) conv = s.value(QStringLiteral("model")).toString() == QLatin1String("builtin")
        ? QStringLiteral("remarkable") : QStringLiteral("rmpp");
    g_state->insert(QStringLiteral("converter"), conv);
    g_state->insert(QStringLiteral("inkStyle"), s.value(QStringLiteral("inkStyle"), QStringLiteral("own")).toString());
    g_state->insert(QStringLiteral("collect"), s.value(QStringLiteral("collect"), true).toBool());
    const QString model = s.value(QStringLiteral("model"), QStringLiteral("deployed")).toString();
    g_state->insert(QStringLiteral("hwrModel"), model == QLatin1String("builtin") ? QStringLiteral("deployed") : model);
}

void saveSetting(const QString &key, const QVariant &v) {
    g_state->insert(key, v);
    QDir().mkpath(QFileInfo(QLatin1String(kConf)).path());
    QSettings s(QLatin1String(kConf), QSettings::IniFormat);
    s.setValue(key, v);
    s.sync();
}

// ---- recognizer library (dlopen on first use; the model loads only for Convert page) ----
struct Hwr {
    void *lib = nullptr;
    void *rec = nullptr;
    void *(*open)(const char *) = nullptr;
    int (*page)(const void *, const char *, char *, size_t) = nullptr;
    int (*text)(const char *, char *, size_t) = nullptr;
    int (*recognize)(const void *, const unsigned char *, unsigned, unsigned, unsigned, char *, size_t, float *) = nullptr;
    void (*close)(void *) = nullptr;
    QString recDir;   // model directory the loaded recognizer came from
};
Hwr g_hwr;

bool loadLib(QString *err) {
    if (g_hwr.lib) return true;
    // the recognizer loads onnxruntime next to the model unless ORT_DYLIB_PATH is set; models in models/<name>/ have
    // no copy of it, so point it at the one installed in kLibDir
    setenv("ORT_DYLIB_PATH", (QByteArray(kLibDir) + "/libonnxruntime.so").constData(), 0);
    const QByteArray so = QByteArray(kLibDir) + "/librmpp_hwr.so";
    g_hwr.lib = dlopen(so.constData(), RTLD_NOW | RTLD_LOCAL);
    if (!g_hwr.lib) { *err = QString::fromLocal8Bit(dlerror()); return false; }
    g_hwr.open = reinterpret_cast<void *(*)(const char *)>(dlsym(g_hwr.lib, "rmpp_hwr_open"));
    g_hwr.page = reinterpret_cast<int (*)(const void *, const char *, char *, size_t)>(dlsym(g_hwr.lib, "rmpp_hwr_page"));
    g_hwr.text = reinterpret_cast<int (*)(const char *, char *, size_t)>(dlsym(g_hwr.lib, "rmpp_hwr_text"));
    g_hwr.recognize = reinterpret_cast<decltype(g_hwr.recognize)>(dlsym(g_hwr.lib, "rmpp_hwr_recognize"));
    g_hwr.close = reinterpret_cast<void (*)(void *)>(dlsym(g_hwr.lib, "rmpp_hwr_close"));
    return true;
}

// ---- models (Settings > Handwriting > Recognition) ----
// "deployed": rec.onnx / dict.txt / model.txt in kLibDir (what `hwr deploy` and the installer write);
// models/<folder>/ with the same three files: more models to pick from (grouped by language). With the converter set to
// "remarkable" the recognizer is off and
// reMarkable's own recognizer is used. A model is named by the first line of its model.txt (else its folder).
QString modelName(const QString &dir, const QString &fallback) {
    QFile f(dir + QStringLiteral("/model.txt"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.isEmpty()) return l;
    }
    return fallback;
}

struct ModelEntry { QString id, name, dir; };

// Language of a model: pack.json {"language": "ko"} in its folder, else guessed from its name.
QString modelLanguage(const QString &dir, const QString &name) {
    QFile f(dir + QStringLiteral("/pack.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QString l = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("language")).toString();
        if (!l.isEmpty()) return l;
    }
    const QString n = name.toLower();
    if (n.contains(QLatin1String("korean")) || n.contains(QLatin1String("-ko"))) return QStringLiteral("ko");
    if (n.contains(QLatin1String("english")) || n.contains(QLatin1String("-en"))) return QStringLiteral("en");
    if (n.contains(QLatin1String("japan"))) return QStringLiteral("ja");
    if (n.contains(QLatin1String("chinese"))) return QStringLiteral("zh");
    if (n.contains(QLatin1String("latin"))) return QStringLiteral("latin");
    return QStringLiteral("other");
}

QString languageName(const QString &code) {
    static const QHash<QString, QString> names{{QStringLiteral("ko"), QStringLiteral("Korean (with English and digits)")},
        {QStringLiteral("en"), QStringLiteral("English")}, {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("zh"), QStringLiteral("Chinese")}, {QStringLiteral("latin"), QStringLiteral("Latin languages")}};
    return names.value(code, QStringLiteral("Other"));
}

QList<ModelEntry> listModels() {
    QList<ModelEntry> out;
    const QString top = QLatin1String(kLibDir);
    QString deployedName;
    if (QFileInfo::exists(top + QStringLiteral("/rec.onnx"))) {
        deployedName = modelName(top, QStringLiteral("rmpp-hwr-korean-base"));
        out.append({QStringLiteral("deployed"), deployedName, top});
    }
    const QDir models(top + QStringLiteral("/models"));
    for (const QString &d : models.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString dir = models.filePath(d);
        if (!QFileInfo::exists(dir + QStringLiteral("/rec.onnx")) || !QFileInfo::exists(dir + QStringLiteral("/dict.txt"))) continue;
        const QString name = modelName(dir, d);
        if (!deployedName.isEmpty() && name == deployedName) continue;   // the deployed copy is listed once
        out.append({d, name, dir});
    }
    return out;
}

// The model directory to use now ("" = builtin). A selected model that is gone falls back to the deployed one.
QString currentModelDir() {
    if (g_state->value(QStringLiteral("converter")).toString() != QLatin1String("rmpp")) return QString();
    const QString sel = g_state->value(QStringLiteral("hwrModel")).toString();
    for (const ModelEntry &m : listModels())
        if (m.id == sel) return m.dir;
    return QLatin1String(kLibDir);
}

bool builtinRecognizer() { return g_state->value(QStringLiteral("converter")).toString() != QLatin1String("rmpp"); }

// Called on a worker thread while busy; a different model selected since the last load is loaded instead.
bool loadRecognizer(QString *err) {
    const QString dir = currentModelDir();
    if (dir.isEmpty()) { *err = QStringLiteral("rmpp-hwr recognition is off (Settings > Handwriting)"); return false; }
    if (g_hwr.rec && g_hwr.recDir == dir) return true;
    if (!loadLib(err)) return false;
    if (!g_hwr.open || !g_hwr.page) { *err = QStringLiteral("symbols missing"); return false; }
    if (g_hwr.rec && g_hwr.close) g_hwr.close(g_hwr.rec);
    g_hwr.rec = nullptr;
    g_hwr.recDir = dir;
    g_hwr.rec = g_hwr.open(dir.toUtf8().constData());
    qWarning("[rmpp-hwr] model %s: %s", g_hwr.rec ? "loaded" : "failed to load", qPrintable(dir));
    if (!g_hwr.rec) { *err = QStringLiteral("model failed to load"); return false; }
    return true;
}

// Typed text of a page file, non-empty paragraphs only.
QStringList pageText(const QString &doc, const QString &page) {
    QString err;
    if (!loadLib(&err) || !g_hwr.text) return {};
    const QString path = QStringLiteral("%1/%2/%3.rm").arg(QLatin1String(kPages), doc, page);
    QByteArray out(64 * 1024, '\0');
    if (g_hwr.text(path.toUtf8().constData(), out.data(), out.size()) < 0) return {};
    QStringList lines;
    for (const QString &l : QString::fromUtf8(out.constData()).split(QLatin1Char('\n')))
        if (!l.trimmed().isEmpty()) lines << l.trimmed();
    return lines;
}

// ---- conversion records (conversions.jsonl, one JSON object per Convert page) ----
// fields: time, doc, src (handwritten page), dst (typed page), lines (recognized), corrected (time, when the
// user pressed "Done correcting"; only those records are used for training).
QString recordsPath() { return QLatin1String(kLibDir) + QStringLiteral("/conversions.jsonl"); }

QList<QJsonObject> readRecords() {
    QList<QJsonObject> out;
    QFile f(recordsPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        if (!o.isEmpty()) out << o;
    }
    return out;
}

void writeRecords(const QList<QJsonObject> &recs) {
    if (recs.isEmpty()) { QFile::remove(recordsPath()); return; }
    QByteArray all;
    for (const QJsonObject &o : recs) all += QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    QFile w(recordsPath() + QStringLiteral(".new"));
    if (w.open(QIODevice::WriteOnly) && w.write(all) == all.size() && w.flush()) {
        w.close();
        ::rename(qPrintable(w.fileName()), qPrintable(recordsPath()));
    }
}

QHash<QString, QString> g_dstToTime;   // typed page id -> record time, for the toolbar button
QHash<QString, bool> g_dstDone;

void indexRecords(const QList<QJsonObject> &recs) {
    g_dstToTime.clear();
    g_dstDone.clear();
    for (const QJsonObject &o : recs) {
        const QString dst = o.value(QStringLiteral("dst")).toString();
        g_dstToTime[dst] = o.value(QStringLiteral("time")).toString();
        g_dstDone[dst] = o.contains(QStringLiteral("corrected"));
    }
}

// Notebook name and the 1-based page number of a page id.
QPair<QString, int> docPage(const QString &doc, const QString &page) {
    QFile mf(QStringLiteral("%1/%2.metadata").arg(QLatin1String(kPages), doc));
    const QString name = mf.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(mf.readAll()).object().value(QStringLiteral("visibleName")).toString() : QString();
    QFile cf(QStringLiteral("%1/%2.content").arg(QLatin1String(kPages), doc));
    int n = 0;
    if (cf.open(QIODevice::ReadOnly)) {
        const QJsonObject c = QJsonDocument::fromJson(cf.readAll()).object();
        const QJsonArray pages = c.value(QStringLiteral("cPages")).toObject().value(QStringLiteral("pages")).toArray();
        int i = 0;
        for (const QJsonValue &p : pages) {
            const QJsonObject po = p.toObject();
            if (po.contains(QStringLiteral("deleted"))) continue;
            ++i;
            if (po.value(QStringLiteral("id")).toString() == page) { n = i; break; }
        }
    }
    return {name, n};
}

void refreshInfo() {
    QJsonArray models;
    for (const ModelEntry &e : listModels()) {
        QFileInfo f(e.dir + QStringLiteral("/rec.onnx"));
        models.append(QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name},
                                  {QStringLiteral("info"), QStringLiteral("%1 MB, %2").arg(f.size() / 1048576.0, 0, 'f', 1)
                                      .arg(f.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                                      + (e.id == QLatin1String("deployed") ? QStringLiteral(", deployed") : QString())}});
    }
    g_state->insert(QStringLiteral("hasOwnGlyphs"), QFileInfo::exists(QLatin1String(kLibDir) + QStringLiteral("/glyphs_own.json")));
    g_state->insert(QStringLiteral("modelList"), QString::fromUtf8(QJsonDocument(models).toJson(QJsonDocument::Compact)));
    // the same models grouped by language, for the Language section
    QJsonArray langs;
    QStringList order;
    QHash<QString, QJsonArray> byLang;
    for (const QJsonValue &v : models) {
        const QJsonObject m = v.toObject();
        QString dir = QLatin1String(kLibDir);
        for (const ModelEntry &e : listModels()) if (e.id == m.value(QStringLiteral("id")).toString()) dir = e.dir;
        const QString l = modelLanguage(dir, m.value(QStringLiteral("name")).toString());
        if (!order.contains(l)) order << l;
        byLang[l].append(m);
    }
    for (const QString &l : order)
        langs.append(QJsonObject{{QStringLiteral("code"), l}, {QStringLiteral("name"), languageName(l)}, {QStringLiteral("models"), byLang[l]}});
    g_state->insert(QStringLiteral("languageList"), QString::fromUtf8(QJsonDocument(langs).toJson(QJsonDocument::Compact)));
    // the model in use (id as resolved: a vanished selection shows as the deployed one)
    const QString cur = currentModelDir();
    QString curId = QStringLiteral("builtin");
    for (const ModelEntry &e : listModels()) if (e.dir == cur) curId = e.id;
    g_state->insert(QStringLiteral("modelInUse"), curId);
    const QString dir = cur.isEmpty() ? QLatin1String(kLibDir) : cur;
    QFileInfo m(dir + QStringLiteral("/rec.onnx"));
    const QString name = modelName(dir, QStringLiteral("rmpp-hwr-korean-base"));
    g_state->insert(QStringLiteral("modelName"), cur.isEmpty() ? QStringLiteral("reMarkable built-in")
                                                 : m.exists() ? name : QStringLiteral("Model not installed"));
    g_state->insert(QStringLiteral("modelInfo"), m.exists()
        ? QStringLiteral("%1 MB, installed %2").arg(m.size() / 1048576.0, 0, 'f', 1)
              .arg(m.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
        : QStringLiteral("Put rec.onnx and dict.txt in %1").arg(dir));
    // Saved conversions, newest first: notebook, page, status.
    const QList<QJsonObject> recs = readRecords();
    indexRecords(recs);
    QJsonArray items;
    int done = 0;
    for (const QJsonObject &o : recs) {
        const QString doc = o.value(QStringLiteral("doc")).toString();
        const auto [nb, pg] = docPage(doc, o.value(QStringLiteral("src")).toString());
        const QDateTime t = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODate).toLocalTime();
        const bool corrected = o.contains(QStringLiteral("corrected"));
        done += corrected;
        items.prepend(QJsonObject{{QStringLiteral("time"), o.value(QStringLiteral("time"))},
                                  {QStringLiteral("when"), t.toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
                                  {QStringLiteral("name"), nb.isEmpty() ? QStringLiteral("(deleted notebook)") : nb},
                                  {QStringLiteral("page"), pg},
                                  {QStringLiteral("dpage"), docPage(doc, o.value(QStringLiteral("dst")).toString()).second},
                                  {QStringLiteral("corrected"), corrected},
                                  {QStringLiteral("lines"), o.value(QStringLiteral("lines")).toArray().size()}});
    }
    g_state->insert(QStringLiteral("conversions"), int(items.size()));
    g_state->insert(QStringLiteral("correctedCount"), done);
    g_state->insert(QStringLiteral("items"), QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact)));
}

// What one record holds: recognized lines and the current text of its typed page.
void showRecord(const QString &time) {
    for (const QJsonObject &o : readRecords()) {
        if (o.value(QStringLiteral("time")).toString() != time) continue;
        QJsonObject d{{QStringLiteral("time"), time},
                      {QStringLiteral("recognized"), o.value(QStringLiteral("lines"))},
                      {QStringLiteral("text"), QJsonArray::fromStringList(
                          pageText(o.value(QStringLiteral("doc")).toString(), o.value(QStringLiteral("dst")).toString()))}};
        g_state->insert(QStringLiteral("detail"), QString::fromUtf8(QJsonDocument(d).toJson(QJsonDocument::Compact)));
        return;
    }
    g_state->insert(QStringLiteral("detail"), QString());
}

// Mark a record corrected (on) or back to waiting (off).
void markRecord(const QString &time, bool on) {
    QList<QJsonObject> recs = readRecords();
    for (QJsonObject &o : recs)
        if (o.value(QStringLiteral("time")).toString() == time) {
            if (on) o.insert(QStringLiteral("corrected"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            else o.remove(QStringLiteral("corrected"));
        }
    writeRecords(recs);
    refreshInfo();
}

// Drop saved conversion records (all when time is empty). The notebook pages themselves are not touched.
void deleteConversions(const QString &time) {
    QList<QJsonObject> recs;
    for (const QJsonObject &o : readRecords()) {
        if (time.isEmpty() || o.value(QStringLiteral("time")).toString() == time) {
            const QString ink = o.value(QStringLiteral("ink")).toString();
            if (!ink.isEmpty()) QFile::remove(QLatin1String(kLibDir) + QLatin1Char('/') + ink);
        } else {
            recs << o;
        }
    }
    writeRecords(recs);
    refreshInfo();
}

// ---- item tree helpers ----
QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred, int depth = 0) {
    if (!root || depth > 250) return nullptr;
    if (pred(root)) return root;
    const auto kids = root->childItems();
    for (QQuickItem *k : kids)
        if (QQuickItem *f = findItem(k, pred, depth + 1)) return f;
    return nullptr;
}

QQuickItem *findAnywhere(const std::function<bool(QQuickItem *)> &pred) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), pred)) return f;
    return nullptr;
}

bool classStarts(QObject *o, const char *prefix) {
    return qstrncmp(o->metaObject()->className(), prefix, qstrlen(prefix)) == 0;
}

// "Converting…" box shown while converting a selection to ink (ink.h); setStatus closes it, or shows the message.
QPointer<QObject> g_inkBusy;

void setStatus(const QString &s, bool busy) {
    g_state->insert(QStringLiteral("status"), s);
    g_state->insert(QStringLiteral("busy"), busy);
    if (!s.isEmpty()) qWarning("[rmpp-hwr] %s", qPrintable(s));
    if (!busy && g_inkBusy) {
        if (s.isEmpty()) {
            g_inkBusy->deleteLater();
        } else {   // keep the box a moment with the message
            g_inkBusy->setProperty("message", s);
            QTimer::singleShot(2500, g_inkBusy, [o = g_inkBusy] { if (o) o->deleteLater(); });
        }
        g_inkBusy = nullptr;
    }
}

// ---- typing the result on the new page ----
void typeText(const QStringList &lines) {
    QObject *focus = QGuiApplication::focusObject();
    if (!focus) { setStatus(QStringLiteral("no text field to type into"), false); return; }
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(focus, &press);
            QCoreApplication::sendEvent(focus, &release);
        }
        QInputMethodEvent im;
        im.setCommitString(lines[i]);
        QCoreApplication::sendEvent(focus, &im);
    }
    setStatus(QString(), false);
}

// One JSON line per conversion: the handwritten source page (original), the recognized lines (converted) and
// the page they were typed on, whose text the user may correct later (corrected). Collected over SSH for training.
void logConversion(const QString &doc, const QString &src, const QString &dst, const QStringList &lines,
                   const QString &ink) {
    if (!g_state->value(QStringLiteral("collect")).toBool()) {
        if (!ink.isEmpty()) QFile::remove(QLatin1String(kLibDir) + QLatin1Char('/') + ink);
        return;
    }
    QDir().mkpath(QLatin1String(kLibDir));
    QFile f(QStringLiteral("%1/conversions.jsonl").arg(QLatin1String(kLibDir)));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    const QString time = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QJsonObject o{{QStringLiteral("time"), time},
                  {QStringLiteral("doc"), doc}, {QStringLiteral("src"), src}, {QStringLiteral("dst"), dst},
                  {QStringLiteral("lines"), QJsonArray::fromStringList(lines)}};
    if (!ink.isEmpty()) o.insert(QStringLiteral("ink"), ink);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    g_dstToTime[dst] = time;
    g_dstDone[dst] = false;
}

// Toolbar state: is the open page a typed page made by Convert page, and is it marked corrected?
void updateCurrentPage() {
    static bool indexed = false;
    if (!indexed && g_confLoaded) { indexRecords(readRecords()); indexed = true; }
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    const QString pid = dv ? dv->property("currentPageId").toString() : QString();
    const QString t = g_dstToTime.value(pid);
    if (g_state->value(QStringLiteral("textPage")).toString() != t) g_state->insert(QStringLiteral("textPage"), t);
    const bool done = g_dstDone.value(pid);
    if (g_state->value(QStringLiteral("textPageDone")).toBool() != done) g_state->insert(QStringLiteral("textPageDone"), done);
}

void insertOnNextPage(QPointer<QQuickItem> dv, int fromPage, const QStringList &lines) {
    if (!dv) { setStatus(QStringLiteral("document closed"), false); return; }
    QVariant doc = dv->property("document");
    QMetaObject::invokeMethod(dv, "addPage", Q_ARG(QVariant, doc), Q_ARG(QVariant, QVariant()));
    // wait for the new page to open, then enter typing mode and type
    auto *timer = new QTimer(dv);
    auto tries = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, dv, [dv, timer, tries, fromPage, lines] {
        if (!dv) return;
        if (dv->property("currentPage").toInt() != fromPage + 1 || dv->property("addingPage").toBool()) {
            if (++*tries > 40) { timer->deleteLater(); setStatus(QStringLiteral("new page did not open"), false); }
            return;
        }
        timer->deleteLater();
        if (QObject *d = dv->property("document").value<QObject *>())
            logConversion(d->property("id").toString(), dv->property("_rmppHwrSrc").toString(),
                          dv->property("currentPageId").toString(), lines, dv->property("_rmppHwrInk").toString());
        QMetaObject::invokeMethod(dv, "enterTextMode", Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant()));
        QTimer::singleShot(400, dv, [lines] { typeText(lines); });
    });
    timer->start(150);
}

void convertCurrentPage() {
    if (g_state->value(QStringLiteral("busy")).toBool()) return;
    QQuickItem *dvItem = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    if (!dvItem) { setStatus(QStringLiteral("no open document"), false); return; }
    QPointer<QQuickItem> dv(dvItem);
    QObject *doc = dv->property("document").value<QObject *>();
    const QString docId = doc ? doc->property("id").toString() : QString();
    const QString pageId = dv->property("currentPageId").toString();
    const int page = dv->property("currentPage").toInt();
    const QString path = QStringLiteral("%1/%2/%3.rm").arg(QLatin1String(kPages), docId, pageId);
    if (docId.isEmpty() || pageId.isEmpty() || !QFile::exists(path)) {
        setStatus(QStringLiteral("page file not found (%1)").arg(path), false);
        return;
    }
    setStatus(QStringLiteral("Converting…"), true);
    dv->setProperty("_rmppHwrSrc", pageId);
    // Keep a copy of exactly the ink that is recognized: later edits or deleting the page don't change the sample.
    QDir().mkpath(QLatin1String(kLibDir) + QStringLiteral("/ink"));
    const QString ink = QStringLiteral("ink/%1-%2.rm").arg(pageId).arg(QDateTime::currentMSecsSinceEpoch());
    dv->setProperty("_rmppHwrInk", QFile::copy(path, QLatin1String(kLibDir) + QLatin1Char('/') + ink) ? ink : QString());
    std::thread([dv, page, path] {
        QString err;
        QStringList lines;
        if (loadRecognizer(&err)) {
            QByteArray out(64 * 1024, '\0');
            const int n = g_hwr.page(g_hwr.rec, path.toUtf8().constData(), out.data(), out.size());
            if (n < 0) err = QStringLiteral("recognition failed");
            else lines = QString::fromUtf8(out.constData()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        QMetaObject::invokeMethod(qApp, [dv, page, lines, err] {
            if (!err.isEmpty()) { setStatus(err, false); return; }
            if (lines.isEmpty()) { setStatus(QStringLiteral("no handwriting found on this page"), false); return; }
            qWarning("[rmpp-hwr] %lld lines recognized", (long long)lines.size());
            insertOnNextPage(dv, page, lines);
        }, Qt::QueuedConnection);
    }).detach();
}

// ---- toolbar button next to "Convert to text" ----
const char *kButtonQml = R"QML(
import QtQuick
import xofm.libs.toolbar
ToolbarTool {
    objectName: "rmppHwrButton"
    type: ToolbarTool.Type.FoldoutButton
    shouldShow: toolbar.notePage && hwr.converter === "rmpp"
    label: hwr.busy ? "Converting…" : "Convert page"
    iconSource: "qrc:/ark/icons/hwc"
    onClicked: hwr.command = "page#" + Date.now()
}
)QML";


// Hides the built-in tool while the setting is on; Binding restores xochitl's own binding when it is turned off.
const char *kHideQml = R"QML(
import QtQuick
Binding { property: "visible"; value: false; when: hwr.converter === "rmpp" }
)QML";

const char *kSidebarQml = R"QML(
import QtQuick
import QtQuick.Layouts
import ark.tokens as ArkTokens
import ark.controls as ArkControls
ArkControls.SidebarItem {
    objectName: "rmppHwrSidebarItem"
    readonly property int hwrPage: 1001
    type: ArkTokens.Sidebar.primary
    text: settingsRoot.showLabels ? "Handwriting" : ""
    iconSource: "qrc:/ark/icons/hwc"
    highlighted: settingsRoot._selectedPage === hwrPage
    focusPolicy: Qt.StrongFocus
    Layout.fillWidth: true
    onTriggered: settingsRoot._selectedPage = hwrPage
}
)QML";

const char *kPageQml = R"QML(
import QtQuick
import QtQuick.Layouts
import ark.tokens as ArkTokens
import ark.controls as ArkControls
Rectangle {
    id: page
    readonly property int hwrPage: 1001
    property string openTime: ""
    function send(c) { hwr.command = c + "#" + Date.now(); }
    x: payloadLoaderRef.x
    y: payloadLoaderRef.y
    width: payloadLoaderRef.width
    height: payloadLoaderRef.height
    z: 10
    color: "white"
    visible: settingsRoot._selectedPage === hwrPage
    onVisibleChanged: if (visible) send("refresh")

    ArkControls.PlaceholderBar {
        id: placeholder
        anchors { top: parent.top; left: parent.left; right: parent.right }
    }
    Flickable {
        anchors {
            top: placeholder.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: ArkControls.Values.margin.horizontal.large
            rightMargin: ArkControls.Values.margin.horizontal.large
        }
        contentHeight: column.implicitHeight + ArkControls.Values.margin.vertical.large
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: column
            width: parent.width
            spacing: ArkControls.Values.spacing.large

            ArkControls.Title {
                type: ArkControls.Title.Large
                text: "Handwriting settings"
                Layout.fillWidth: true
            }

            // ---- converter ----
            ArkControls.Panel {
                label: "Handwriting converter"
                description: hwr.converter === "rmpp"
                    ? "rmpp-hwr: runs on this tablet with the model and language you choose below. It replaces reMarkable's Convert to text in the toolbar and the lasso menu."
                    : "reMarkable: the built-in Convert to text. rmpp-hwr changes nothing and loads no model."
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: ArkControls.Values.spacing.medium
                Layout.leftMargin: ArkControls.Values.margin.horizontal.large
                ArkControls.Button {
                    text: "rmpp-hwr"
                    type: hwr.converter === "rmpp" ? ArkTokens.Button.primary : ArkTokens.Button.secondary
                    onClicked: page.send("converter:rmpp")
                }
                ArkControls.Button {
                    text: "reMarkable"
                    type: hwr.converter === "rmpp" ? ArkTokens.Button.secondary : ArkTokens.Button.primary
                    onClicked: page.send("converter:remarkable")
                }
            }

            // ---- language and model (rmpp-hwr only) ----
            ArkControls.Title {
                visible: hwr.converter === "rmpp"
                type: ArkControls.Title.Medium
                text: "Language"
                Layout.fillWidth: true
            }
            Repeater {
                model: { try { return hwr.converter === "rmpp" ? JSON.parse(hwr.languageList || "[]") : []; } catch (e) { return []; } }
                delegate: ColumnLayout {
                    id: lang
                    required property var modelData
                    spacing: ArkControls.Values.spacing.small
                    Layout.fillWidth: true
                    ArkControls.Panel {
                        label: lang.modelData.name
                        description: lang.modelData.models.length + (lang.modelData.models.length === 1 ? " model" : " models")
                                     + ". Pick the one to use; personal models are trained on your practice pages."
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: lang.modelData.models
                        delegate: ArkControls.Panel {
                            required property var modelData
                            label: modelData.name
                            description: modelData.info || ""
                            Layout.fillWidth: true
                            Layout.leftMargin: ArkControls.Values.margin.horizontal.large
                            action: ArkControls.Button {
                                text: hwr.modelInUse === modelData.id ? "In use" : "Use"
                                enabled: hwr.modelInUse !== modelData.id
                                type: ArkTokens.Button.secondary
                                onClicked: page.send("model:" + modelData.id)
                            }
                        }
                    }
                }
            }

            // ---- what the converter does (rmpp-hwr only) ----
            ArkControls.Title {
                visible: hwr.converter === "rmpp"
                type: ArkControls.Title.Medium
                text: "Convert"
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                visible: hwr.converter === "rmpp"
                label: "Convert page (toolbar)"
                description: "Recognizes the whole page and types the text on a new page after it."
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                visible: hwr.converter === "rmpp"
                label: "Lasso convert: ink"
                description: "The convert button under a lasso selection writes the recognized text as ink you can move and resize. Double tap converted ink to edit its text, weight and size."
                Layout.fillWidth: true
            }

            // ---- ink (rmpp-hwr only) ----
            ArkControls.Title {
                visible: hwr.converter === "rmpp"
                type: ArkControls.Title.Medium
                text: "Ink"
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                visible: hwr.converter === "rmpp"
                label: "Ink handwriting: " + (hwr.inkStyle === "own" && hwr.hasOwnGlyphs ? "My handwriting" : "Font (Noto Sans KR)")
                description: hwr.hasOwnGlyphs
                    ? (hwr.inkStyle === "own"
                       ? "Your own characters, from your practice pages; missing ones come from the font."
                       : "The font. Switch on to write with your own characters.")
                    : "The font. My handwriting appears when your own glyphs (glyphs_own.json) are installed."
                Layout.fillWidth: true
                action: ArkControls.Toggle {
                    visible: !!hwr.hasOwnGlyphs
                    checked: hwr.inkStyle === "own"
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -20
                        onClicked: page.send("style:" + (hwr.inkStyle === "own" ? "font" : "own"))
                    }
                }
            }

            ArkControls.Title {
                type: ArkControls.Title.Medium
                text: "Collect training data"
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                label: "Save conversions: " + (hwr.collect ? "On" : "Off")
                description: hwr.collect
                    ? "Each Convert page keeps your handwriting and its text. Fix mistakes on the text page and the fixed text is used to train the model. Stays on this tablet."
                    : "Nothing is saved. Convert page still works."
                Layout.fillWidth: true
                action: ArkControls.Toggle {
                    checked: !!hwr.collect
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -20
                        onClicked: page.send("set:collect:" + (hwr.collect ? "0" : "1"))
                    }
                }
            }
            ArkControls.Panel {
                label: "Practice notebooks"
                description: "Notebooks named rmpp-practice have sentences to copy by hand, the fastest way to teach the model your writing. New ones are added from the computer."
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                id: savedPanel
                property bool confirm: false
                label: "Saved conversions: " + (hwr.conversions || 0) + " (" + (hwr.correctedCount || 0) + " corrected)"
                description: "Each one is a handwritten page and the typed page after it. Fix the typed page, then tap Done correcting (in the Convert menu or here). Only corrected ones are used for training; later edits are picked up too. Deleting keeps the notebook pages."
                Layout.fillWidth: true
                action: ArkControls.Button {
                    visible: (hwr.conversions || 0) > 0
                    text: savedPanel.confirm ? "Tap again to delete all" : "Delete all"
                    type: ArkTokens.Button.secondary
                    onClicked: {
                        if (savedPanel.confirm) { page.send("delall"); savedPanel.confirm = false; }
                        else savedPanel.confirm = true;
                    }
                }
            }
            Repeater {
                model: { try { return JSON.parse(hwr.items || "[]").slice(0, 30); } catch (e) { return []; } }
                delegate: ColumnLayout {
                    id: rec
                    required property var modelData
                    readonly property bool open: page.openTime === modelData.time
                    readonly property var detail: { try { return open ? JSON.parse(hwr.detail || "{}") : {}; } catch (e) { return {}; } }
                    spacing: ArkControls.Values.spacing.small
                    Layout.fillWidth: true
                    Layout.leftMargin: ArkControls.Values.margin.horizontal.large

                    ArkControls.Panel {
                        label: modelData.name + (modelData.page ? ", page " + modelData.page + (modelData.dpage ? " → " + modelData.dpage : "") : "")
                        description: (modelData.corrected ? "Corrected, used for training" : "Waiting for your correction")
                                     + "  ·  " + modelData.when
                        Layout.fillWidth: true
                        action: RowLayout {
                            spacing: ArkControls.Values.spacing.medium
                            ArkControls.Button {
                                text: rec.open ? "Close" : "View"
                                type: ArkTokens.Button.secondary
                                onClicked: {
                                    if (rec.open) page.openTime = "";
                                    else { page.openTime = modelData.time; page.send("show:" + modelData.time); }
                                }
                            }
                            ArkControls.Button {
                                text: "Delete"
                                type: ArkTokens.Button.secondary
                                onClicked: page.send("del:" + modelData.time)
                            }
                        }
                    }
                    ArkControls.Panel {
                        visible: rec.open
                        label: "Recognized"
                        description: (rec.detail.recognized || []).join("\n")
                        Layout.fillWidth: true
                    }
                    ArkControls.Panel {
                        visible: rec.open
                        label: modelData.corrected ? "Your text (saved for training)" : "Text on the page now"
                        description: (rec.detail.text || []).join("\n") || "(empty)"
                        Layout.fillWidth: true
                        action: ArkControls.Button {
                            text: modelData.corrected ? "Undo" : "Done correcting"
                            type: ArkTokens.Button.secondary
                            onClicked: page.send("mark:" + (modelData.corrected ? "0:" : "1:") + modelData.time)
                        }
                    }
                }
            }

        }
    }
}
)QML";

QObject *createInline(QQmlEngine *eng, QQmlContext *ctx, const char *qml, const char *name, QQuickItem *parent,
                      const QVariantMap &init = {}) {
    QQmlComponent comp(eng);
    comp.setData(QByteArray(qml), QUrl(QStringLiteral("qrc:/rmpphwr/%1.qml").arg(QLatin1String(name))));
    QObject *o = comp.createWithInitialProperties(init, ctx);
    if (!o) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-hwr] %s: %s", name, qPrintable(e.toString()));
        return nullptr;
    }
    o->setParent(parent);
    if (auto *item = qobject_cast<QQuickItem *>(o)) item->setParentItem(parent);
    return o;
}

#include "ink.h"

// Settings > Handwriting: a sidebar row below Bluetooth (or Wi-Fi) and the page over the payload area.
void tryInjectSettings() {
    QQuickItem *col = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("settingsColumn"); });
    if (!col || col->property("_rmppHwr").toBool()) return;
    QQuickItem *settings = col;
    while (settings && settings->objectName() != QLatin1String("Settings")) settings = settings->parentItem();
    QQuickItem *loader = settings ? findItem(settings, [](QQuickItem *i) { return i->objectName() == QLatin1String("payloadLoader"); }) : nullptr;
    QQmlEngine *eng = settings ? qmlEngine(settings) : nullptr;
    QQmlContext *base = settings ? qmlContext(settings) : nullptr;
    if (!settings || !loader || !eng || !base) return;
    col->setProperty("_rmppHwr", true);
    auto *ctx = new QQmlContext(base, settings);
    ctx->setContextProperty(QStringLiteral("hwr"), g_state);
    ctx->setContextProperty(QStringLiteral("settingsRoot"), settings);
    ctx->setContextProperty(QStringLiteral("payloadLoaderRef"), loader);
    auto *row = qobject_cast<QQuickItem *>(createInline(eng, ctx, kSidebarQml, "Sidebar", col));
    if (row) {
        QQuickItem *after = nullptr;
        for (QQuickItem *k : col->childItems()) {
            const QString icon = k->property("iconSource").toString();
            if (icon.contains(QLatin1String("bluetooth"))) after = k;
            else if (!after && icon.contains(QLatin1String("wifi"))) after = k;
        }
        if (after) row->stackAfter(after);
    }
    QObject *page = createInline(eng, ctx, kPageQml, "Page", settings);
    qWarning("[rmpp-hwr] settings: row=%s page=%s", row ? "ok" : "FAILED", page ? "ok" : "FAILED");
}

// "Done correcting": shown on a typed page made by Convert page; marks that conversion for training.
const char *kDoneQml = R"QML(
import QtQuick
import xofm.libs.toolbar
ToolbarTool {
    objectName: "rmppHwrDoneButton"
    type: ToolbarTool.Type.FoldoutButton
    shouldShow: toolbar.notePage && !!hwr.textPage && hwr.converter === "rmpp"
    label: hwr.textPageDone ? "Correction saved (undo)" : "Done correcting"
    iconSource: "qrc:/ark/icons/hwc"
    onClicked: hwr.command = "mark:" + (hwr.textPageDone ? "0:" : "1:") + hwr.textPage + "#" + Date.now()
}
)QML";

void tryInject() {
    QQuickItem *native =findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("convertToTextButton"); });
    if (!native || !native->parentItem() || native->property("_rmppHwr").toBool()) return;
    native->setProperty("_rmppHwr", true);
    QQmlEngine *eng = qmlEngine(native);
    QQmlContext *base = qmlContext(native);
    if (!eng || !base) return;
    auto *ctx = new QQmlContext(base, native->parentItem());
    ctx->setContextProperty(QStringLiteral("hwr"), g_state);
    QQmlComponent comp(eng);
    comp.setData(QByteArray(kButtonQml), QUrl(QStringLiteral("qrc:/rmpphwr/Button.qml")));
    QVariantMap init{{QStringLiteral("toolbar"), native->property("toolbar")}};
    QObject *o = comp.createWithInitialProperties(init, ctx);
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-hwr] %s", qPrintable(e.toString()));
        delete o;
        return;
    }
    // The tool may sit in single-child wrappers (loaders); insert next to the outermost wrapper,
    // in the first container that lays out several tools.
    QQuickItem *anchor = native, *box = native->parentItem();
    while (box && box->childItems().size() == 1 && box->parentItem()) { anchor = box; box = box->parentItem(); }
    QString chain;
    for (QQuickItem *p = native; p && chain.count(QLatin1Char('<')) < 6; p = p->parentItem())
        chain += QString::fromLatin1(p->metaObject()->className()) + "(" + QString::number(p->childItems().size()) + ") < ";
    qWarning("[rmpp-hwr] tool chain: %s", qPrintable(chain));
    item->setParentItem(box);
    item->setParent(box);
    item->stackAfter(anchor);
    if (auto *done = qobject_cast<QQuickItem *>(createInline(eng, ctx, kDoneQml, "Done", box, init))) done->stackAfter(item);
    createInline(eng, ctx, kHideQml, "HideNative", item, {{QStringLiteral("target"), QVariant::fromValue<QObject *>(anchor)}});
    qWarning("[rmpp-hwr] button added in %s", box->metaObject()->className());
}

// ---- developer tools (only in dev builds: ui/build.sh with DEV=1) ----
// Trigger by writing a command to /home/root/.local/share/rmpp-hwr/cmd over SSH:
//   probe                 dump the meta-objects reachable from the open DocumentView to probe.txt
//   load <path.so> [arg]  dlopen an experiment (ui/exp) and call its rmpp_exp(arg), without restarting xochitl
//   ink | ink:replace     convert the lasso selection
#ifdef RMPP_HWR_DEV
// Trigger: echo probe > /home/root/.local/share/rmpp-hwr/cmd   (output: probe.txt next to it)
void probeObject(QObject *o, int depth, QSet<QObject *> &seen, QSet<QByteArray> &classes, QTextStream &out,
                 const QString &path) {
    if (!o || depth > 7 || seen.contains(o)) return;
    seen.insert(o);
    const QMetaObject *mo = o->metaObject();
    const QByteArray cls = mo->className();
    out << QString(depth * 2, QLatin1Char(' ')) << path << " : " << cls << " '" << o->objectName() << "'\n";
    if (!classes.contains(cls)) {
        classes.insert(cls);
        for (const QMetaObject *m = mo; m && qstrcmp(m->className(), "QQuickItem") && qstrcmp(m->className(), "QObject");
             m = m->superClass()) {
            out << "   [class " << m->className() << "]\n";
            for (int i = m->propertyOffset(); i < m->propertyCount(); ++i) {
                const QMetaProperty p = m->property(i);
                out << "     prop " << p.typeName() << " " << p.name() << " = "
                    << p.read(o).toString().left(80) << "\n";
            }
            for (int i = m->methodOffset(); i < m->methodCount(); ++i) {
                const QMetaMethod mm = m->method(i);
                static const char *kinds[] = {"method", "signal", "slot", "ctor"};
                out << "     " << kinds[mm.methodType()] << " " << mm.typeName() << " " << mm.methodSignature() << "\n";
            }
        }
    }
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!(p.metaType().flags() & QMetaType::PointerToQObject)) continue;
        if (!qstrcmp(p.name(), "parent")) continue;
        probeObject(p.read(o).value<QObject *>(), depth + 1, seen, classes, out, QStringLiteral(".") + p.name());
    }
    for (QObject *c : o->children()) probeObject(c, depth + 1, seen, classes, out, QStringLiteral("/child"));
    if (auto *it = qobject_cast<QQuickItem *>(o))
        for (QQuickItem *c : it->childItems()) probeObject(c, depth + 1, seen, classes, out, QStringLiteral("/item"));
}

void probe() {
    QFile f(QLatin1String(kLibDir) + QStringLiteral("/probe.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    QSet<QObject *> seen;
    QSet<QByteArray> classes;
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    probeObject(dv, 0, seen, classes, out, QStringLiteral("DocumentView"));
    out << "---- roots ----\n";
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) probeObject(qw->contentItem(), 0, seen, classes, out, QStringLiteral("window"));
}

void devCommand(const QString &cmd);

void watchCmdFile() {
    const QString p = QLatin1String(kLibDir) + QStringLiteral("/cmd");
    QFile f(p);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QString cmd = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    QFile::remove(p);
    qWarning("[rmpp-hwr] dev command: %s", qPrintable(cmd.left(60)));
    devCommand(cmd);
}

void devCommand(const QString &cmd) {
    if (cmd == QLatin1String("probe")) probe();
    else if (cmd == QLatin1String("ink")) convertSelectionToInk(false);
    else if (cmd == QLatin1String("ink:replace")) convertSelectionToInk(true);
    else if (cmd.startsWith(QLatin1String("load "))) {   // load <path.so> [arg]: run an experiment without restarting
        const QString so = cmd.section(QLatin1Char(' '), 1, 1);
        void *h = dlopen(so.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
        auto fn = h ? reinterpret_cast<void (*)(const char *)>(dlsym(h, "rmpp_exp")) : nullptr;
        if (!fn) { qWarning("[rmpp-hwr] load failed: %s", h ? "no rmpp_exp" : dlerror()); return; }
        fn(cmd.section(QLatin1Char(' '), 2).toUtf8().constData());
    }
}
#endif // RMPP_HWR_DEV

// Lasso menu (cut / copy / convert / delete): the convert button converts the selection to ink instead of text.
// A MouseArea on top of xochitl's button takes the tap; the icon becomes a pen tip.
const char *kInkMenuQml = R"QML(
import QtQuick
MouseArea {
    anchors.fill: parent
    z: 1000
    onClicked: hwr.command = "ink#" + Date.now()
}
)QML";

// The lasso menu icon: xochitl's own convert icon ("aa") inside a dashed selection box, composed at run time from
// the icon in xochitl's resources (so no reMarkable artwork is shipped here).
QString inkIconUrl() {
    static QString url;
    if (!url.isEmpty()) return url;
    QFile in(QStringLiteral(":/ark/icons/hwc"));
    const QString out = QLatin1String(kLibDir) + QStringLiteral("/ink_convert.svg");
    QString svg;
    if (in.open(QIODevice::ReadOnly)) {
        const QString src = QString::fromUtf8(in.readAll());
        const int a = src.indexOf(QLatin1Char('>'), src.indexOf(QLatin1String("<svg"))) + 1;
        const int b = src.lastIndexOf(QLatin1String("</svg>"));
        if (a > 0 && b > a)
            svg = QStringLiteral("<svg width=\"48\" height=\"48\" viewBox=\"0 0 48 48\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
                                 "<rect x=\"2\" y=\"4\" width=\"44\" height=\"40\" stroke=\"black\" stroke-width=\"2.5\" "
                                 "stroke-dasharray=\"5 3.5\" fill=\"none\"/><g transform=\"translate(8.5 7.5) scale(0.64)\">")
                  + src.mid(a, b - a) + QStringLiteral("</g></svg>");
    }
    QFile f(out);
    if (!svg.isEmpty() && f.open(QIODevice::WriteOnly) && f.write(svg.toUtf8()) > 0) {
        f.close();
        url = out;   // xochitl's Icon takes a plain path (a file:// URL is "not found")
    } else {
        url = QStringLiteral("qrc:/ark/icons/ballpoint_tip");
    }
    return url;
}

void tryInjectInkMenu() {
    if (builtinRecognizer()) return;   // reMarkable's converter: leave its lasso button alone
    QQuickItem *menu = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("selectionHandlerMenu"); });
    if (!menu) return;
    QQuickItem *btn = findItem(menu, [](QQuickItem *i) {
        return i->property("iconSource").toString() == QLatin1String("qrc:/ark/icons/hwc") && qstrcmp(i->metaObject()->className(), "Button") == 0;
    });
    if (!btn || btn->property("_rmppHwrInk").toBool()) return;
    QQmlEngine *eng = qmlEngine(btn);
    QQmlContext *base = qmlContext(btn);
    if (!eng || !base) return;
    btn->setProperty("_rmppHwrInk", true);
    auto *ctx = new QQmlContext(base, btn);
    ctx->setContextProperty(QStringLiteral("hwr"), g_state);
    QObject *area = createInline(eng, ctx, kInkMenuQml, "InkMenu", btn);
    btn->setProperty("iconSource", inkIconUrl());
    qWarning("[rmpp-hwr] lasso menu: convert button -> ink %s", area ? "ok" : "FAILED");
}

// Calls fn on a signal of an object we have no headers for (like QSignalSpy: overrides qt_metacall).
class SignalHook : public QObject {
public:
    std::function<void()> fn;
    int qt_metacall(QMetaObject::Call c, int id, void **a) override {
        id = QObject::qt_metacall(c, id, a);
        if (id < 0 || c != QMetaObject::InvokeMetaMethod) return id;
        if (id == 0 && fn) fn();
        return -1;
    }
};

// The lasso menu is created when the first selection is made: watch the open page's selection count and patch the
// menu right away (and shortly after, once QML has built it), so the first selection already shows the ink button.
void watchSelection() {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    QObject *sc = dv ? dv->property("sceneController").value<QObject *>() : nullptr;
    if (!sc || sc->property("_rmppHwrWatch").toBool()) return;
    const int sig = sc->metaObject()->indexOfSignal("selectionItemCountChanged()");
    if (sig < 0) return;
    sc->setProperty("_rmppHwrWatch", true);
    auto *hook = new SignalHook;
    hook->setParent(sc);
    hook->fn = [] {
        tryInjectInkMenu();
        for (int ms : {0, 50, 150, 400}) QTimer::singleShot(ms, qApp, [] { tryInjectInkMenu(); });
    };
    QMetaObject::connect(sc, sig, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
}

// Double tap on a lasso selection of converted ink opens the edit box: the selection handler's drag area emits
// doubleClicked (pen and finger). Hooked once per area.
void watchDoubleTap() {
    QQuickItem *h = findAnywhere([](QQuickItem *i) { return classStarts(i, "SceneSelectionHandler"); });
    if (!h) return;
    for (QQuickItem *k : h->childItems()) {
        if (!k->inherits("QQuickMouseArea") || k->property("_rmppHwrTap").toBool()) continue;
        const int sig = k->metaObject()->indexOfSignal("doubleClicked(QQuickMouseEvent*)");
        if (sig < 0) continue;
        k->setProperty("_rmppHwrTap", true);
        auto *hook = new SignalHook;
        hook->setParent(k);
        hook->fn = [] { QTimer::singleShot(0, qApp, [] { convertSelectionToInk(false, true); }); };
        QMetaObject::connect(k, sig, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
    }
}

void hwrInit() {
    auto *app = QCoreApplication::instance();
    g_state = new QQmlPropertyMap(app);
    g_state->insert(QStringLiteral("busy"), false);
    g_state->insert(QStringLiteral("status"), QString());
    g_state->insert(QStringLiteral("command"), QString());
    g_state->insert(QStringLiteral("converter"), QStringLiteral("rmpp"));
    g_state->insert(QStringLiteral("hwrModel"), QStringLiteral("deployed"));
    g_state->insert(QStringLiteral("inkStyle"), QStringLiteral("own"));
    g_state->insert(QStringLiteral("hasOwnGlyphs"), false);
    g_state->insert(QStringLiteral("collect"), true);
    g_state->insert(QStringLiteral("inkText"), QString());
    g_state->insert(QStringLiteral("inkWeight"), 1);
    g_state->insert(QStringLiteral("inkSize"), 100);
    QObject::connect(g_state, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key != QLatin1String("command")) return;
        const QString cmd = v.toString().section(QLatin1Char('#'), 0, 0);
        if (cmd == QLatin1String("page")) convertCurrentPage();
        else if (cmd == QLatin1String("ink")) convertSelectionToInk(false);
        else if (cmd == QLatin1String("ink:replace")) convertSelectionToInk(true);
        else if (cmd == QLatin1String("inkok")) finishInkEdit(true);
        else if (cmd == QLatin1String("inkcancel")) finishInkEdit(false);
        else if (cmd == QLatin1String("refresh")) refreshInfo();
        else if (cmd == QLatin1String("delall")) deleteConversions(QString());
        else if (cmd.startsWith(QLatin1String("del:"))) deleteConversions(cmd.mid(4));
        else if (cmd.startsWith(QLatin1String("show:"))) showRecord(cmd.mid(5));
        else if (cmd.startsWith(QLatin1String("mark:"))) {
            markRecord(cmd.mid(7), cmd.mid(5, 1) == QLatin1String("1"));
            updateCurrentPage();
        }
        else if (cmd.startsWith(QLatin1String("converter:"))) {
            saveSetting(QStringLiteral("converter"), cmd.mid(10) == QLatin1String("remarkable") ? QStringLiteral("remarkable") : QStringLiteral("rmpp"));
            refreshInfo();
        }
        else if (cmd.startsWith(QLatin1String("style:"))) {
            saveSetting(QStringLiteral("inkStyle"), cmd.mid(6));   // glyphs reload on the next conversion
        }
        else if (cmd.startsWith(QLatin1String("model:"))) {
            // settings.conf key "model"; in the QML map it is "hwrModel": a map key named "model" broke xochitl's
            // toolbar (tools failed with "Required property toolbar was not initialized")
            g_state->insert(QStringLiteral("hwrModel"), cmd.mid(6));
            QSettings st(QLatin1String(kConf), QSettings::IniFormat);
            st.setValue(QStringLiteral("model"), cmd.mid(6));
            st.sync();
            refreshInfo();   // the recognizer reloads with the new model on its next use
        }
        else if (cmd.startsWith(QLatin1String("set:"))) {
            const QString name = cmd.section(QLatin1Char(':'), 1, 1);
            if (name == QLatin1String("collect"))
                saveSetting(name, cmd.section(QLatin1Char(':'), 2, 2) == QLatin1String("1"));
        }
    });
    auto *t = new QTimer(app);
    QObject::connect(t, &QTimer::timeout, app, [] { loadSettings(); tryInject(); tryInjectSettings(); tryInjectInkMenu(); watchSelection(); watchDoubleTap(); closeStaleInkEdit(); updateCurrentPage();
#ifdef RMPP_HWR_DEV
        watchCmdFile();
#endif
    });
    t->start(1500);
    qWarning("[rmpp-hwr] loaded");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(hwrInit)
