// Prints the sidecar the tablet's "New practice notebook" makes (ui/practice.h) for a texts folder, language, number
// of pages and seed, so it can be checked against the PC tool's golden files:
//   ui/test/build.sh  ->  build/practice_test (aarch64; runs on the tablet)
//   scp -r tests/golden/texts build/practice_test tablet:/tmp/pt/ && ssh tablet /tmp/pt/practice_test /tmp/pt/texts ko 8 7 > ko.json
//   python tests/test_practice_golden.py --tablet ko.json
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include "../practice.h"

int main(int argc, char **argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: practice_test TEXTS LANG PAGES SEED [topic,topic]\n"); return 2; }
    QCoreApplication app(argc, argv);
    practice::Lang L;
    const QString lang = QString::fromUtf8(argv[2]);
    if (!practice::langInfo(lang, &L)) { std::fprintf(stderr, "unknown language\n"); return 2; }
    const QStringList topics = argc > 5 ? QString::fromUtf8(argv[5]).split(QLatin1Char(','), Qt::SkipEmptyParts) : QStringList();
    const uint64_t seed = std::strtoull(argv[4], nullptr, 10);
    practice::Layout lay(seed, L, QString::fromUtf8(argv[1]), topics);
    const auto pages = lay.layout(practice::planKinds(std::atoi(argv[3]), L));
    std::fputs(QJsonDocument(practice::sidecar(pages, seed, lang)).toJson(QJsonDocument::Indented).constData(), stdout);
    return 0;
}
