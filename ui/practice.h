// Practice notebooks made on the tablet (Settings > Handwriting > Collect training data > New practice notebook).
// A port of collect/pages.py (layout, sidecar, PDF): the same seed, language and texts give the same sidecar as the
// PC tool, checked against tests/golden/practice-<lang>-seed7.json (ui/test/practice_test.cpp, see that file).
// Python's random.Random (Mersenne Twister, seeded from an int) is reproduced call for call, so every choice the PC
// tool makes is made here in the same order. Keep both in step: change pages.py, then this file, then the golden.
// Included inside hwrui.cpp's namespace (and by the test); needs QtCore, and QtGui for the PDF.
#include <cstdint>
#include <vector>

// ---- Python's random.Random ----
struct PyRandom {
    uint32_t mt[624];
    int mti = 625;
    explicit PyRandom(uint64_t seed) {   // random.seed(int): init_by_array over the 32-bit words of abs(seed)
        std::vector<uint32_t> key;
        if (!seed) key.push_back(0);
        while (seed) { key.push_back(uint32_t(seed & 0xffffffffu)); seed >>= 32; }
        initByArray(key);
    }
    void initGenrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < 624; mti++) mt[mti] = 1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + uint32_t(mti);
    }
    void initByArray(const std::vector<uint32_t> &key) {
        initGenrand(19650218u);
        uint32_t i = 1, j = 0;
        const uint32_t n = uint32_t(key.size());
        for (uint32_t k = 624 > n ? 624 : n; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + j;
            i++; j++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
            if (j >= n) j = 0;
        }
        for (uint32_t k = 623; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - i;
            i++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
        }
        mt[0] = 0x80000000u;
        mti = 624;
    }
    uint32_t next() {
        static const uint32_t mag01[2] = {0u, 0x9908b0dfu};
        uint32_t y;
        if (mti >= 624) {
            int kk;
            for (kk = 0; kk < 624 - 397; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
            }
            for (; kk < 623; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1u];
            }
            y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 1u];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= (y >> 18);
        return y;
    }
    int randbelow(int n) {   // _randbelow_with_getrandbits (n < 2**31 here)
        if (n <= 0) return 0;
        int k = 0;
        for (uint32_t v = uint32_t(n); v; v >>= 1) ++k;
        uint32_t r = next() >> (32 - k);
        while (r >= uint32_t(n)) r = next() >> (32 - k);
        return int(r);
    }
    int randint(int a, int b) { return a + randbelow(b - a + 1); }
    template <typename T> const T &choice(const QList<T> &x) { return x[randbelow(int(x.size()))]; }
    template <typename T> void shuffle(QList<T> &x) {
        for (int i = int(x.size()) - 1; i > 0; --i) x.swapItemsAt(i, randbelow(i + 1));
    }
};

// ---- layout (pages.py) ----
namespace practice {

const double PX_W = 1620, PX_H = 2160, DPI = 229;
const double MM = DPI / 25.4;
const double MARGIN = 70, TOP = 150;
const char *const FORMAT = "rmpp-hwr/practice@2";

inline double cellMM(const QString &s) { return s == QLatin1String("S") ? 7 : s == QLatin1String("M") ? 10 : 14; }
inline double lineMM(const QString &s) { return s == QLatin1String("S") ? 4.5 : s == QLatin1String("M") ? 6 : 8; }

inline QStringList lookalike() {
    return {QStringLiteral("0 O o"), QStringLiteral("1 l I |"), QStringLiteral("5 S s"), QStringLiteral("2 Z z"),
            QStringLiteral("6 b"), QStringLiteral("9 g q"), QStringLiteral("u v"), QStringLiteral("rn m"),
            QStringLiteral("cl d"), QStringLiteral("- _"), QStringLiteral(". , ;"), QStringLiteral("( ) [ ]"),
            QStringLiteral("% & @")};
}

struct Lang {
    QString code, name;
    bool grid = false;          // cells "가-힣" (Hangul syllables) for ko; no grid pages otherwise
    QString moneyPre, moneyPost;
    QStringList lookalike;
    QString fast;
    QHash<QString, QString> headers;
};

// The languages pages.py knows (LANGS). A language needs an entry here and in pages.py, and texts on the tablet.
inline bool langInfo(const QString &code, Lang *L) {
    L->code = code;
    if (code == QLatin1String("ko")) {
        L->name = QStringLiteral("Korean");
        L->grid = true;
        L->moneyPost = QStringLiteral("원");
        L->lookalike = {QStringLiteral("0 O o ㅇ"), QStringLiteral("1 l I ㅣ |"), QStringLiteral("7 ㄱ"), QStringLiteral("ㅌ E"),
                        QStringLiteral("ㅂ B"), QStringLiteral("ㄹ 2 Z"), QStringLiteral("ㅁ □ 口"), QStringLiteral("- ㅡ _")};
        L->fast = QStringLiteral("빠르게");
        L->headers = {{QStringLiteral("grid"), QStringLiteral("흰 칸에 한 글자씩 쓰세요. 회색 칸은 띄어쓰기라 비워 둡니다. 평소 글씨체로.")},
                      {QStringLiteral("digits"), QStringLiteral("칸마다 한 글자(숫자, 기호)씩 쓰세요. 비슷한 글자는 서로 다르게 보이도록.")},
                      {QStringLiteral("english"), QStringLiteral("영어는 네 줄에 맞춰 쓰세요: 소문자 몸통은 가운데 칸, g y p 꼬리는 아래 칸.")},
                      {QStringLiteral("free"), QStringLiteral("줄 위에 자연스럽게 쓰세요. '빠르게' 표시가 있는 줄은 평소 메모하듯 빨리.")}};
        return true;
    }
    if (code == QLatin1String("en")) {
        L->name = QStringLiteral("English");
        L->moneyPre = QStringLiteral("$");
        L->fast = QStringLiteral("fast");
        L->headers = {{QStringLiteral("grid"), QStringLiteral("One character per white cell. Grey cells are spaces: leave them empty. Your usual handwriting.")},
                      {QStringLiteral("digits"), QStringLiteral("One character (digit, symbol) per cell. Make look-alikes clearly different.")},
                      {QStringLiteral("english"), QStringLiteral("Write on the four lines: small letters in the middle band, tails of g y p in the bottom band.")},
                      {QStringLiteral("free"), QStringLiteral("Write naturally on the line. Lines marked 'fast': write quickly, like everyday notes.")}};
        return true;
    }
    return false;
}

inline QStringList languages(const QString &lang) {
    return lang == QLatin1String("en") ? QStringList{lang, QStringLiteral("digits")}
                                        : QStringList{lang, QStringLiteral("en"), QStringLiteral("digits")};
}

inline int ulen(const QString &s) { return int(s.toUcs4().size()); }   // len() in Python: code points

inline bool isPySpace(char32_t c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

inline QString pyStrip(const QString &s) {
    const QList<uint> u = s.toUcs4();
    int a = 0, b = int(u.size());
    while (a < b && isPySpace(u[a])) ++a;
    while (b > a && isPySpace(u[b - 1])) --b;
    QString out;
    for (int i = a; i < b; ++i) out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&u[i]), 1);
    return out;
}

// Number with thousands separators, as Python's "{:,}".
inline QString grouped(int v) {
    QString d = QString::number(v), out;
    for (int i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += QLatin1Char(',');
        out += d[i];
    }
    return out;
}

inline QString pad2(int v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); }

struct Item {
    QString kind, layout, size, text, speed;
    double promptY = 0;
    QList<double> box;                          // page px
    QList<QPair<QString, QList<double>>> cells;  // row: char, box
    QList<double> guides;
};
struct Page { QString kind; QList<Item> items; };

struct Layout {
    PyRandom rng;
    Lang L;
    QString textsDir;          // <dir>/<lang>/<topic>.txt
    QStringList topics;        // topics of the notebook's language (empty: all); English lines use every en topic
    QStringList enPool;
    Layout(uint64_t seed, const Lang &l, const QString &dir, const QStringList &t) : rng(seed), L(l), textsDir(dir), topics(t) {}

    QStringList lines(const QString &lang) {
        QDir d(textsDir + QLatin1Char('/') + lang);
        QStringList files = d.entryList({QStringLiteral("*.txt")}, QDir::Files);
        std::sort(files.begin(), files.end());   // sorted(glob): by name
        QStringList out;
        for (const QString &f : files) {
            if (lang == L.code && !topics.isEmpty() && !topics.contains(QFileInfo(f).completeBaseName())) continue;
            QFile in(d.filePath(f));
            if (!in.open(QIODevice::ReadOnly)) continue;
            for (QString l : QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'))) {
                if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
                out << l;
            }
        }
        return out;
    }

    QStringList chunks(int n, int width) {
        QStringList src;
        for (const QString &line : lines(L.code)) {
            QString s;
            for (char32_t c : line.toUcs4()) {
                const bool keep = c == ' ' || (c >= 0xAC00 && c <= 0xD7A3);
                s += keep ? QString::fromUcs4(&c, 1) : QStringLiteral(" ");
            }
            s = s.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char(' '));
            int hangul = 0;
            for (char32_t c : s.toUcs4()) hangul += c >= 0xAC00 && c <= 0xD7A3;
            if (hangul >= 3) src << s;
        }
        const QStringList words = src.join(QLatin1Char(' ')).split(QLatin1Char(' '));
        QStringList out;
        while (out.size() < n) {
            int i = rng.randbelow(int(words.size()));
            QString piece;
            while (i < words.size() && ulen(piece) + ulen(words[i]) + (piece.isEmpty() ? 0 : 1) <= width) {
                piece += (piece.isEmpty() ? QString() : QStringLiteral(" ")) + words[i];
                i++;
            }
            if (ulen(piece) >= std::min(width, 4) * 0.6) out << piece;
        }
        return out;
    }

    QString digitGen(int g, int width) {
        switch (g) {
        case 0: {
            const int a = rng.randint(1, 12), b = rng.randint(1, 28), c = rng.randint(1, 12);
            const int m = rng.choice(QList<int>{0, 15, 30, 45});
            return QStringLiteral("%1/%2 %3:%4").arg(a).arg(b).arg(c).arg(pad2(m));
        }
        case 1: return L.moneyPre + grouped(rng.randint(1000, 99999)) + L.moneyPost;
        case 2: {
            const int a = rng.randint(0, 100), b = rng.randint(1, 9), c = rng.randint(0, 9);
            return QStringLiteral("%1% %2.%3kg").arg(a).arg(b).arg(c);
        }
        case 3: {
            const int a = rng.randint(1, 12), b = rng.randint(1, 28);
            return QStringLiteral("2026-%1-%2").arg(pad2(a), pad2(b));
        }
        case 4: {
            const int n = rng.randint(width / 2, width);
            QString s;
            for (int i = 0; i < n; ++i) s += QString::number(rng.randint(0, 9));
            return s;
        }
        case 5: return rng.choice(lookalike() + L.lookalike);
        case 6: {
            const int x = rng.randint(1, 99), y = rng.randint(1, 99);
            return QStringLiteral("%1+%2=%3").arg(x).arg(y).arg(x + y);
        }
        default: {
            const int x = rng.randint(2, 19), y = rng.randint(2, 19);
            return QStringLiteral("%1x%2=%3").arg(x).arg(y).arg(x * y);
        }
        }
    }

    QStringList digitItems(int n, int width) {
        QStringList out;
        while (out.size() < n) {
            const QString s = digitGen(rng.randbelow(8), width);
            if (ulen(s) <= width) out << s;
        }
        return out;
    }

    QString english() {
        if (enPool.isEmpty()) {
            QStringList ls;
            for (const QString &l : lines(QStringLiteral("en"))) {
                const QString s = pyStrip(l);
                if (!s.isEmpty()) ls << s;
            }
            rng.shuffle(ls);
            enPool += ls;
        }
        return enPool.takeLast();
    }

    QString freeLine() {
        QStringList src;
        QStringList langs{L.code};
        if (L.code != QLatin1String("en")) langs << QStringLiteral("en");
        for (const QString &g : langs)
            for (const QString &l : lines(g)) {
                const int n = ulen(pyStrip(l));
                if (n < 6 || n > 34) continue;
                QString s = pyStrip(l);
                while (s.startsWith(QLatin1Char('-'))) s.remove(0, 1);
                src << pyStrip(s);
            }
        return rng.choice(src);
    }

    QList<Page> layout(const QStringList &kinds) {
        QList<Page> pages;
        for (const QString &kind : kinds) {
            Page pg{kind, {}};
            double y = TOP;
            QStringList sizes{QStringLiteral("M"), QStringLiteral("L"), QStringLiteral("S")};
            rng.shuffle(sizes);
            for (int k = 0;; ++k) {
                const QString size = sizes[k % 3];
                Item it;
                if (kind == QLatin1String("grid") || kind == QLatin1String("digits")) {
                    const double cell = cellMM(size) * MM;
                    const int ncell = int(std::floor((PX_W - 2 * MARGIN) / cell));
                    const double h = 46 + cell;
                    if (y + h > PX_H - 80) break;
                    it.text = kind == QLatin1String("grid") ? chunks(1, ncell)[0] : digitItems(1, ncell)[0];
                    const double x0 = MARGIN;
                    const QList<uint> chars = it.text.toUcs4();
                    for (int i = 0; i < chars.size(); ++i) {
                        const double cx0 = x0 + i * cell;
                        const char32_t ch = chars[i];
                        it.cells.append({QString::fromUcs4(&ch, 1), {cx0, y + 46, cx0 + cell, y + 46 + cell}});
                    }
                    it.kind = QStringLiteral("row");
                    it.layout = kind;
                    it.size = size;
                    it.promptY = y + 30;
                    it.box = {x0, y + 46, x0 + chars.size() * cell, y + 46 + cell};
                    y += h + 26;
                } else if (kind == QLatin1String("english")) {
                    const double s = lineMM(size) * MM;
                    const double h = 46 + 3 * s;
                    if (y + h > PX_H - 80) break;
                    it.text = english();
                    const double top = y + 46;
                    it.kind = QStringLiteral("line");
                    it.layout = kind;
                    it.size = size;
                    it.promptY = y + 30;
                    it.box = {MARGIN, top, PX_W - MARGIN, top + 3 * s};
                    it.guides = {top, top + s, top + 2 * s, top + 3 * s};
                    y += h + 30;
                } else {   // free
                    const double h = 46 + 130;
                    if (y + h > PX_H - 80) break;
                    it.text = freeLine();
                    it.kind = QStringLiteral("line");
                    it.layout = QStringLiteral("free");
                    it.size = QStringLiteral("M");
                    it.promptY = y + 30;
                    it.speed = k % 2 == 1 ? QStringLiteral("fast") : QStringLiteral("normal");
                    it.box = {MARGIN, y + 46, PX_W - MARGIN, y + h};
                    it.guides = {y + 46 + 100};
                    y += h + 24;
                }
                pg.items << it;
            }
            pages << pg;
        }
        return pages;
    }
};

inline QStringList planKinds(int npages, const Lang &L) {
    static const char *withGrid[] = {"grid", "english", "grid", "digits", "free", "grid", "english", "free"};
    static const char *noGrid[] = {"english", "digits", "free", "english", "free", "digits", "english", "free"};
    QStringList out;
    for (int i = 0; i < npages; ++i) out << QLatin1String((L.grid ? withGrid : noGrid)[i % 8]);
    return out;
}

// round(x, 1) as Python does (correctly rounded from the exact binary value, ties to even)
inline double round1(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f", x);
    return std::strtod(buf, nullptr);
}

inline QJsonArray rmBox(const QList<double> &b) {
    return {round1(b[0] - PX_W / 2), round1(b[1]), round1(b[2] - PX_W / 2), round1(b[3])};
}

inline QJsonObject sidecar(const QList<Page> &pages, uint64_t seed, const QString &lang) {
    QJsonArray items, kinds;
    int n = 0;
    for (int p = 0; p < pages.size(); ++p) {
        kinds.append(pages[p].kind);
        for (const Item &it : pages[p].items) {
            QJsonObject d{{QStringLiteral("id"), n++}, {QStringLiteral("page"), p}, {QStringLiteral("kind"), it.kind},
                          {QStringLiteral("layout"), it.layout}, {QStringLiteral("size"), it.size},
                          {QStringLiteral("text"), it.text}, {QStringLiteral("box"), rmBox(it.box)}};
            if (it.kind == QLatin1String("row")) {
                QJsonArray cells;
                for (const auto &c : it.cells)
                    cells.append(QJsonObject{{QStringLiteral("char"), c.first}, {QStringLiteral("box"), rmBox(c.second)}});
                d.insert(QStringLiteral("cells"), cells);
            } else {
                QJsonArray g;
                for (double v : it.guides) g.append(round1(v));
                d.insert(QStringLiteral("guides"), g);
            }
            if (!it.speed.isEmpty()) d.insert(QStringLiteral("speed"), it.speed);
            items.append(d);
        }
    }
    return {{QStringLiteral("format"), QLatin1String(FORMAT)},
            {QStringLiteral("page_px"), QJsonArray{PX_W, PX_H}},
            {QStringLiteral("dpi"), DPI},
            {QStringLiteral("pages"), int(pages.size())},
            {QStringLiteral("seed"), qint64(seed)},
            {QStringLiteral("languages"), QJsonArray::fromStringList(languages(lang))},
            {QStringLiteral("page_kinds"), kinds},
            {QStringLiteral("items"), items}};
}

#ifdef QPDFWRITER_H
// The PDF as pages.py draw_pdf draws it (reportlab points there, page pixels at 229 dpi here).
inline bool drawPdf(const QList<Page> &pages, const QString &out, const Lang &L, const QString &title, QString *err) {
    QPdfWriter w(out);
    w.setTitle(title);
    w.setCreator(QStringLiteral("rmpp-hwr"));
    w.setPageSize(QPageSize(QSizeF(PX_W * 72 / DPI, PX_H * 72 / DPI), QPageSize::Point, QString(), QPageSize::ExactMatch));
    w.setPageMargins(QMarginsF(0, 0, 0, 0));
    w.setResolution(int(DPI));
    QPainter p;
    if (!p.begin(&w)) { *err = QStringLiteral("cannot write %1").arg(out); return false; }
    const double pt = DPI / 72;   // reportlab line widths and dashes are in points
    auto gray = [](double g) { return QColor::fromRgbF(float(g), float(g), float(g)); };
    auto pen = [&](double g, double width, double dashOn = 0, double dashOff = 0) {
        QPen pn(gray(g));
        pn.setWidthF(width * pt);
        pn.setCapStyle(Qt::FlatCap);
        if (dashOn > 0) pn.setDashPattern({dashOn / width, dashOff / width});   // in units of the pen width
        return pn;
    };
    QFont font(QStringLiteral("Noto Sans KR"));
    for (int pno = 0; pno < pages.size(); ++pno) {
        if (pno) w.newPage();
        font.setPixelSize(28);
        p.setFont(font);
        p.setPen(gray(0.45));
        p.drawText(QPointF(MARGIN, 70), QStringLiteral("%1/%2  %3").arg(pno + 1).arg(pages.size()).arg(L.headers.value(pages[pno].kind)));
        for (const Item &it : pages[pno].items) {
            font.setPixelSize(34);
            p.setFont(font);
            p.setPen(gray(0));
            p.drawText(QPointF(MARGIN, it.promptY), it.text + (it.speed == QLatin1String("fast") ? QStringLiteral("   (%1)").arg(L.fast) : QString()));
            if (it.kind == QLatin1String("row")) {
                for (const auto &c : it.cells) {
                    const QRectF r(QPointF(c.second[0], c.second[1]), QPointF(c.second[2], c.second[3]));
                    if (c.first == QLatin1String(" ")) {   // a space: shaded, no guide, clearly left empty
                        p.setPen(pen(0.9, 0.5));
                        p.setBrush(gray(0.9));
                        p.drawRect(r);
                        p.setBrush(Qt::NoBrush);
                        continue;
                    }
                    p.setPen(pen(0.72, 1.2));
                    p.drawRect(r);
                    p.setPen(pen(0.86, 0.8, 3, 4));
                    p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));
                    p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
                }
            } else {
                for (int gi = 0; gi < it.guides.size(); ++gi) {
                    const bool dashed = it.layout == QLatin1String("english") && (gi == 0 || gi == 1 || gi == 3);
                    const double g = gi == 2 || it.layout == QLatin1String("free") ? 0.6 : 0.82;
                    p.setPen(dashed ? pen(g, 1.0, 4, 4) : pen(g, 1.0));
                    p.drawLine(QPointF(it.box[0], it.guides[gi]), QPointF(it.box[2], it.guides[gi]));
                }
            }
        }
    }
    p.end();
    return true;
}
#endif

}  // namespace practice
