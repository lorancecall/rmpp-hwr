// Experiment 13: copy xochitl's own icon resources (":/ark/icons/<name>*") to /tmp/icons for reference.
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>

extern "C" __attribute__((visibility("default"))) void rmpp_exp(const char *arg) {
    QDir().mkpath(QStringLiteral("/tmp/icons"));
    int n = 0;
    QDirIterator it(QStringLiteral(":/ark/icons"), QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QString name = p.section(QLatin1Char('/'), -1);
        if (!name.contains(QString::fromUtf8(arg)) && qstrlen(arg)) continue;
        QFile::remove(QStringLiteral("/tmp/icons/") + name);
        if (QFile::copy(p, QStringLiteral("/tmp/icons/") + name)) ++n;
    }
    qWarning("[rmpp-exp] copied %d icons", n);
}
