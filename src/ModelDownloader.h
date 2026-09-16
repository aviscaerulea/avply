#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// モデルファイルのダウンロード（中断と再開に対応）
// GUI thread 上で動く。受信は destPath + ".part" へ逐次書き、完了時に destPath へリネームする。
// .part が残っていれば Range ヘッダで続きから取得する。サーバが Range を無視して 200 を返した
// 場合は先頭から書き直す。中断（cancel）では .part を残し、次回の start が続きから再開する
class ModelDownloader : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    // ダウンロードを開始する（実行中なら何もしない）
    // destPath の親ディレクトリが無ければ作る
    void start(const QUrl& url, const QString& destPath);

    // 実行中のダウンロードを中断する（.part は残す）
    // finished は発火しない。実行中でなければ何もしない
    void cancel();

    bool isRunning() const { return m_reply != nullptr; }

signals:
    // 受信量が増えたとき発火する。total はサーバが長さを返さない場合 -1
    void progress(qint64 received, qint64 total);

    // 完了したとき発火する。ok=false のとき error に理由が入る
    void finished(bool ok, const QString& error);

private:
    // reply と .part ファイルを解放する（ファイルは閉じるだけで削除しない）
    void cleanup();

    // 失敗として終了する。.part は次回の再開のため残す
    void fail(const QString& error);

    QNetworkAccessManager* m_nam  = nullptr;
    QNetworkReply*         m_reply = nullptr;
    QFile*                 m_part  = nullptr;
    QString                m_destPath;
    // 再開時の既存バイト数。進捗の分子と分母へ加算する
    qint64                 m_resumeFrom = 0;
};
