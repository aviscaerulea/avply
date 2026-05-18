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

    // 連番リネームの探索上限（_clip_2 〜 _clip_100 の 99 件）
    // QFile::exists は GUI thread で同期 I/O を発行するため上限を 100 に抑える。
    // 最悪ブロック時間はローカルディスクで約 100ms 程度。NAS（SMB）では数十 ms/件のレイテンシで
    // 数秒に達することがある。これを超える衝突時は連番探索を諦め、UUID フォールバックへ切り替える
    constexpr int kMaxAttempts = 100;
    for (int n = 2; n <= kMaxAttempts; ++n) {
        path = base + "_" + QString::number(n) + suffix;
        if (!QFile::exists(path)) return path;
    }

    // 上限到達時は UUID 末尾でユニーク化する（衝突確率は実質ゼロ）
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return base + "_" + uuid + suffix;
}
