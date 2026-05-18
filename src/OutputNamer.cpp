#include "OutputNamer.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>

QString OutputNamer::generate(const QString& inputPath, const QString& outputExt)
{
    const QFileInfo fi(inputPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName() + "_clip";
    const QString suffix = "." + outputExt;

    QString path = base + suffix;
    if (!QFile::exists(path)) return path;

    // 連番リネームの探索上限
    // 攻撃的に _clip_N が大量生成された環境で QFile::exists の I/O が GUI thread を
    // 長時間ブロックし、int オーバフロー（_clip_-2147483648 等の UB）に到達するのを防ぐ
    constexpr int kMaxAttempts = 10000;
    for (int n = 2; n < kMaxAttempts; ++n) {
        path = base + "_" + QString::number(n) + suffix;
        if (!QFile::exists(path)) return path;
    }

    // 上限到達時は UUID 末尾でユニーク化する（衝突確率は実質ゼロ）
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return base + "_" + uuid + suffix;
}
