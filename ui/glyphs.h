// Stroke glyphs for text objects (the "ink font"; included from hwrui.cpp before ink.h). Per character, in order:
//   1. rmpp-lang's glyph file of the text's language: /home/root/.local/share/rmpp-lang/languages/<code>/glyphs.rmgl
//   2. the other installed rmpp-lang glyph files, in rmpp-lang's language order (lang.conf "languages=")
//   3. rmpp-hwr's own glyphs.json (Korean 2,350 syllables + ASCII; the language files carry no ASCII)
// Nothing is read at xochitl start: a glyph file is memory-mapped the first time a character is looked up in it, and
// glyphs.json is parsed the first time a character is not in any glyph file. Everything is dropped again after two
// minutes without a lookup (releaseIdleGlyphs, from the 1.5 s timer).
//
// RMGL v1 (rmpp-lang docs/DESIGN.md "Glyph files"), little-endian:
//   0 "RMGL", 4 u16 version (1), 6 u16 flags, 8 u32 count, 12 f32 baseline (em, y down), 16 u32 meta length M,
//   20 meta (UTF-8 JSON), then at 20+M rounded up to 4: count x {u32 codepoint, u32 offset from file start},
//   sorted by codepoint. Glyph record: u16 advance, u8 strokes, per stroke u8 points + points x {i16 x, i16 y}.
//   Advance and coordinates are em x 4096 (y down, 0 = top of the em box), as glyphs.json's in em.

struct Glyph {
    double adv = 0.5;
    QList<QList<QPointF>> strokes;
};

const char *kLangGlyphDir = "/home/root/.local/share/rmpp-lang/languages";
const char *kLangConf = "/home/root/.config/rmpp-lang/lang.conf";
const double kGlyphBaseline = 0.82;   // glyphs.json's baseline (Noto Sans KR); other sources are shifted to it

struct RmglFile {
    QString lang, path;
    std::unique_ptr<QFile> file;
    const uchar *base = nullptr;   // mapped file, nullptr until first use
    qint64 size = 0;
    quint32 count = 0, index = 0;
    double shift = 0;              // added to y: baseline difference to kGlyphBaseline
    bool bad = false;              // not a usable RMGL v1 file (logged once)
};

std::map<QString, RmglFile> g_rmglFiles;   // installed glyph files by language (mapped or not)
QStringList g_rmglOrder;                   // lookup order (languages)
QHash<char32_t, Glyph> g_glyphCache;   // looked-up glyphs (from any source), for the current order
QSet<char32_t> g_glyphMiss;            // looked up, found nowhere
QHash<char32_t, Glyph> g_jsonGlyphs;   // glyphs.json, parsed on first need
QString g_jsonGlyphsFile;              // the file g_jsonGlyphs came from ("" = not parsed yet)
QElapsedTimer g_glyphUse;              // since the last lookup

inline quint16 rd16(const uchar *p) { return quint16(p[0] | p[1] << 8); }
inline quint32 rd32(const uchar *p) { return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24; }

QString glyphsJsonFile() { return libFile(QStringLiteral("glyphs.json")); }

// rmpp-lang's languages in its order (Settings > Languages), else none.
QStringList rmppLangOrder() {
    QFile f{QLatin1String(kLangConf)};
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String("languages="))) return l.mid(10).split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    return {};
}

void unmapGlyphFiles() {
    for (auto &kv : g_rmglFiles) {
        RmglFile &f = kv.second;
        if (f.base) f.file->unmap(const_cast<uchar *>(f.base));
        f.base = nullptr;
        f.file.reset();
    }
}

// Languages with a glyph file installed.
QStringList glyphFileLanguages() {
    QStringList codes = QDir(QLatin1String(kLangGlyphDir)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    codes.erase(std::remove_if(codes.begin(), codes.end(), [](const QString &c) {
        return !QFileInfo::exists(QLatin1String(kLangGlyphDir) + QLatin1Char('/') + c + QStringLiteral("/glyphs.rmgl")); }), codes.end());
    return codes;
}

// The installed languages in lookup order: `first`, then rmpp-lang's order, then the rest by code.
QStringList glyphOrder(const QString &first) {
    const QStringList codes = glyphFileLanguages();
    QStringList order;
    if (codes.contains(first)) order << first;
    for (const QString &c : rmppLangOrder()) if (codes.contains(c) && !order.contains(c)) order << c;
    for (const QString &c : codes) if (!order.contains(c)) order << c;
    return order;
}

RmglFile &glyphFile(const QString &lang) {
    RmglFile &f = g_rmglFiles[lang];
    if (f.path.isEmpty()) {
        f.lang = lang;
        f.path = QLatin1String(kLangGlyphDir) + QLatin1Char('/') + lang + QStringLiteral("/glyphs.rmgl");
    }
    return f;
}

// Look glyphs up in this order from now on; the looked-up glyphs are dropped when the order changes (a character
// may come from another file now). Mapped files stay mapped.
void orderGlyphFiles(const QStringList &order) {
    if (order == g_rmglOrder) return;
    g_rmglOrder = order;
    g_glyphCache.clear();
    g_glyphMiss.clear();
}

bool mapGlyphFile(RmglFile &f) {
    if (f.base) return true;
    if (f.bad) return false;
    QElapsedTimer t;
    t.start();
    f.file = std::make_unique<QFile>(f.path);
    const uchar *b = f.file->open(QIODevice::ReadOnly) ? f.file->map(0, f.file->size()) : nullptr;
    const qint64 n = f.file->size();
    QString why;
    if (!b) why = QStringLiteral("cannot map");
    else if (n < 20 || memcmp(b, "RMGL", 4) != 0) why = QStringLiteral("not RMGL");
    else if (rd16(b + 4) != 1) why = QStringLiteral("version %1").arg(rd16(b + 4));
    if (why.isEmpty()) {
        const quint32 count = rd32(b + 8), meta = rd32(b + 16);
        const quint64 index = (20ull + meta + 3) & ~3ull;
        if (index + 8ull * count > quint64(n)) why = QStringLiteral("truncated");
        else {
            float baseline;
            memcpy(&baseline, b + 12, 4);
            f.base = b; f.size = n; f.count = count; f.index = quint32(index);
            f.shift = std::isfinite(baseline) && baseline > 0 && baseline < 2 ? kGlyphBaseline - baseline : 0;
        }
    }
    if (!f.base) {
        f.bad = true;
        if (b) f.file->unmap(const_cast<uchar *>(b));
        f.file.reset();
        qWarning("[rmpp-hwr] glyphs %s: %s", qPrintable(f.path), qPrintable(why));
        return false;
    }
    qWarning("[rmpp-hwr] glyphs %s mapped: %u glyphs, %lld KB, %lld ms", qPrintable(f.lang), f.count, f.size / 1024,
             (long long)t.elapsed());
    return true;
}

// One glyph from a mapped file (binary search in the index); false if it is not there or the record is broken.
bool readGlyph(const RmglFile &f, char32_t cp, Glyph *g) {
    quint32 lo = 0, hi = f.count;
    const uchar *idx = f.base + f.index;
    while (lo < hi) {
        const quint32 mid = lo + (hi - lo) / 2;
        const quint32 c = rd32(idx + 8ull * mid);
        if (c < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= f.count || rd32(idx + 8ull * lo) != cp) return false;
    qint64 p = rd32(idx + 8ull * lo + 4);
    if (p + 3 > f.size) return false;
    const uchar *b = f.base;
    g->adv = rd16(b + p) / 4096.0;
    const int strokes = b[p + 2];
    p += 3;
    g->strokes.clear();
    for (int s = 0; s < strokes; ++s) {
        if (p + 1 > f.size) return false;
        const int pts = b[p++];
        if (p + 4ll * pts > f.size) return false;
        QList<QPointF> st;
        st.reserve(pts);
        for (int i = 0; i < pts; ++i, p += 4)
            st.append(QPointF(qint16(rd16(b + p)) / 4096.0, qint16(rd16(b + p + 2)) / 4096.0 + f.shift));
        g->strokes.append(st);
    }
    return true;
}

// glyphs.json (ink/glyphs.py build): {"glyphs": {char: {"adv", "strokes": [[x, y, ...]]}}}, em units, y down.
void loadJsonGlyphs() {
    const QString file = glyphsJsonFile();
    if (g_jsonGlyphsFile == file) return;
    QElapsedTimer t;
    t.start();
    g_jsonGlyphs.clear();
    g_jsonGlyphsFile = file;
    QFile f{file};
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject gs = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("glyphs")).toObject();
    for (auto it = gs.begin(); it != gs.end(); ++it) {
        const QList<uint> cps = it.key().toUcs4();
        if (cps.size() != 1) continue;
        const QJsonObject o = it.value().toObject();
        Glyph g;
        g.adv = o.value(QStringLiteral("adv")).toDouble(0.5);
        for (const QJsonValue &s : o.value(QStringLiteral("strokes")).toArray()) {
            const QJsonArray a = s.toArray();
            QList<QPointF> p;
            for (int i = 0; i + 1 < a.size(); i += 2) p.append(QPointF(a[i].toDouble(), a[i + 1].toDouble()));
            g.strokes.append(p);
        }
        g_jsonGlyphs.insert(char32_t(cps[0]), g);
    }
    qWarning("[rmpp-hwr] glyphs.json parsed: %lld glyphs, %lld ms", (long long)g_jsonGlyphs.size(), (long long)t.elapsed());
}

// The glyph of a character, nullptr if no source has it. Call useGlyphLanguage() for the text first.
const Glyph *glyphOf(char32_t cp) {
    g_glyphUse.start();
    auto it = g_glyphCache.constFind(cp);
    if (it != g_glyphCache.constEnd()) return &*it;
    if (g_glyphMiss.contains(cp)) return nullptr;
    Glyph g;
    for (const QString &l : g_rmglOrder) {
        RmglFile &f = glyphFile(l);
        if (mapGlyphFile(f) && readGlyph(f, cp, &g)) return &*g_glyphCache.insert(cp, g);
    }
    loadJsonGlyphs();
    auto j = g_jsonGlyphs.constFind(cp);
    if (j != g_jsonGlyphs.constEnd()) return &*g_glyphCache.insert(cp, *j);
    g_glyphMiss.insert(cp);
    return nullptr;
}

// The glyph file has the character (index only, no glyph decoded).
bool glyphFileHas(const QString &lang, char32_t cp) {
    RmglFile &f = glyphFile(lang);
    if (!mapGlyphFile(f)) return false;
    quint32 lo = 0, hi = f.count;
    const uchar *idx = f.base + f.index;
    while (lo < hi) {
        const quint32 mid = lo + (hi - lo) / 2;
        if (rd32(idx + 8ull * mid) < cp) lo = mid + 1;
        else hi = mid;
    }
    return lo < f.count && rd32(idx + 8ull * lo) == cp;
}

// The lookup order for `text`. First the text's language: Japanese for kana, Korean for Hangul; Russian (or
// Bulgarian with a Bulgarian model) for Cyrillic. Han without kana or Hangul is drawn differently in zh, ja and ko:
// the first language, in the order model's language -> rmpp-lang's order, whose file has every Han character of the
// text (simplified Chinese is only in zh, so Chinese text gets zh), else the model's language.
QStringList glyphOrderFor(const QString &text) {
    const QString dir = currentModelDir();
    const QString model = dir.isEmpty() ? QString() : modelLanguage(dir, modelName(dir, QString()));
    bool cyr = false;
    QList<char32_t> han;
    for (const uint c : text.toUcs4()) {
        if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x31f0 && c <= 0x31ff)) return glyphOrder(QStringLiteral("ja"));
        if ((c >= 0xac00 && c <= 0xd7a3) || (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3130 && c <= 0x318f)) return glyphOrder(QStringLiteral("ko"));
        if (c >= 0x0400 && c <= 0x04ff) cyr = true;
        if ((c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x3ffff)) han << char32_t(c);
    }
    if (cyr) return glyphOrder(model == QLatin1String("bg") ? model : QStringLiteral("ru"));
    const QStringList order = glyphOrder(model);
    if (!han.isEmpty())
        for (const QString &l : order)
            if (std::all_of(han.begin(), han.end(), [&](char32_t c) { return glyphFileHas(l, c); })) return glyphOrder(l);
    return order;
}

void useGlyphLanguage(const QString &text) { orderGlyphFiles(glyphOrderFor(text)); }

// Some ink font is installed (a glyph file or glyphs.json). Reads no glyph.
bool glyphsAvailable() {
    if (QFileInfo::exists(glyphsJsonFile())) return true;
    for (const QString &c : QDir(QLatin1String(kLangGlyphDir)).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFileInfo::exists(QLatin1String(kLangGlyphDir) + QLatin1Char('/') + c + QStringLiteral("/glyphs.rmgl"))) return true;
    return false;
}

// From the 1.5 s timer: unmap the glyph files and drop glyphs.json two minutes after the last lookup.
void releaseIdleGlyphs() {
    if (!g_glyphUse.isValid() || g_glyphUse.elapsed() < 120000) return;
    g_glyphUse.invalidate();
    unmapGlyphFiles();
    g_rmglFiles.clear();
    g_rmglOrder.clear();
    g_glyphCache.clear();
    g_glyphMiss.clear();
    g_jsonGlyphs.clear();
    g_jsonGlyphsFile.clear();
    qWarning("[rmpp-hwr] glyphs released (idle)");
}
