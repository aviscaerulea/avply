#include "OutputNamer.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

bool OutputNamer::isModName(const QString& inputPath)
{
    static const QRegularExpression modSuffix("_mod\\d*$");
    return QFileInfo(inputPath).completeBaseName().contains(modSuffix);
}

QString OutputNamer::generate(const QString& inputPath, const QString& outputExt)
{
    const QFileInfo fi(inputPath);
    const QString suffix = "." + outputExt;

    // 既に _mod 形式の入力は同名へ上書き出力する
    // 編集済みファイルの再編集は成果物の置き換えとみなし、シーケンス番号で増殖させない。
    // 拡張子はモードに従う（トリムは入力と同一、変換はコンテナが変わると別になる）
    if (isModName(inputPath)) {
        return fi.absolutePath() + "/" + fi.completeBaseName() + suffix;
    }

    const QString base = fi.absolutePath() + "/" + fi.completeBaseName() + "_mod";

    QString path = base + suffix;
    if (!QFile::exists(path)) return path;

    // _mod シーケンス探索の上限（_mod2 〜 _mod100 の 99 件）
    // QFile::exists は GUI thread で同期 I/O を発行するため上限を 100 に抑える。
    // 最悪ブロック時間はローカルディスクで約 100ms 程度。NAS（SMB）では数十 ms/件のレイテンシで
    // 数秒に達することがある。これを超える衝突時はシーケンス探索を諦め、UUID フォールバックへ切り替える
    constexpr int kMaxAttempts = 100;
    for (int n = 2; n <= kMaxAttempts; ++n) {
        path = base + QString::number(n) + suffix;
        if (!QFile::exists(path)) return path;
    }

    // 上限到達時は UUID 末尾でユニーク化する（衝突確率は実質ゼロ）
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return base + "_" + uuid + suffix;
}
