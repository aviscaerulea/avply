#include "OutputNamer.h"
#include <QFile>
#include <QFileInfo>

QString OutputNamer::generate(const QString& inputPath, const QString& outputExt)
{
    const QFileInfo fi(inputPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName() + "_clip";
    const QString suffix = "." + outputExt;

    QString path = base + suffix;
    if (!QFile::exists(path)) return path;

    for (int n = 2; ; ++n) {
        path = base + "_" + QString::number(n) + suffix;
        if (!QFile::exists(path)) return path;
    }
}
