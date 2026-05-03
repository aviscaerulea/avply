#include "OutputNamer.h"
#include <QFile>
#include <QFileInfo>

QString OutputNamer::generate(const QString& inputPath)
{
    const QFileInfo fi(inputPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName() + "_cut";

    QString path = base + ".mp4";
    if (!QFile::exists(path)) return path;

    for (int n = 2; ; ++n) {
        path = base + "_" + QString::number(n) + ".mp4";
        if (!QFile::exists(path)) return path;
    }
}
