#pragma once
#include <QObject>
#include <QPixmap>
#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <functional>

class QProcess;

// シークバーホバー時のサムネイル抽出 + LRU キャッシュ
// ffmpeg を非同期起動して 1 フレームを BMP 出力させ、QPixmap として返す
// 走行中プロセスは 1 本のみ保持し、新規要求で kill して上書きする
class ThumbnailExtractor : public QObject {
    Q_OBJECT
public:
    // キャッシュキーの量子化粒度（秒）
    // 1 秒粒度なら同一秒内のホバー微振動でほぼキャッシュヒット
    static constexpr int kQuantSec = 1;

    // LRU 上限（160x90 RGBA32 で 1 枚 ≒ 56KB、100 件で約 5.6MB）
    static constexpr int kCacheCap = 100;

    explicit ThumbnailExtractor(QObject* parent = nullptr);
    ~ThumbnailExtractor() override;

    // ffmpeg パスと入力ファイルを設定する
    // ファイル切替時に呼び出すと、内部のキャッシュと走行中プロセスをクリアする
    // thumbSize が空（width/height いずれか 0 以下）なら抽出を抑止する（音声ファイル用）
    void setSource(const QString& ffmpegPath,
                   const QString& inputPath,
                   const QSize& thumbSize);

    // ffmpeg に渡す -hwaccel 値を設定する（"auto" / "d3d11va" / "cuda" など）
    // "none" または空文字で -hwaccel 指定をスキップする。
    // setSource の前後どちらで呼んでもよい
    void setHwaccel(const QString& hwaccel);

    // 量子化済みの秒数キーでサムネイル取得を要求する
    // キャッシュヒット時は同期的に callback(true, pix) を呼ぶ
    // ミス時は ffmpeg を起動し、終了時に callback を呼ぶ
    // 抽出抑止中（thumbSize 空）は callback(false, {}) を即時呼ぶ
    // 走行中の前要求がある場合は完走優先で新規要求を即 callback(false, {}) で破棄する
    // 注意：callback はミス時に非同期発火するため、参照キャプチャは寿命に注意すること
    void request(int seconds,
                 std::function<void(bool ok, const QPixmap& pix)> callback);

    // キャッシュ参照のみを行う同期 API（ffmpeg は起動しない）
    // ヒット時は outPix にコピーして true、ミス時は false を返す
    // ホバー時の即時表示判定に使う（参照キャプチャ async UB を避けるため request とは分離）
    bool tryGetCached(int seconds, QPixmap& outPix);

    // 走行中プロセスを停止する
    // synchronous=true で waitForFinished + delete（デストラクタ向け）、
    // false で deleteLater（ファイル切替時向け）
    void cancelInflight(bool synchronous);

private:
    // LRU キャッシュ操作
    void putCache(int key, const QPixmap& pix);
    bool getCache(int key, QPixmap& outPix);

    QString m_ffmpegPath;
    QString m_inputPath;
    QSize   m_thumbSize;
    QString m_hwaccel = "auto";

    QHash<int, QPixmap> m_cache;
    // LRU 順序：先頭が最新、末尾が古い。容量超過時に末尾を捨てる
    QList<int>          m_lru;

    QProcess* m_proc        = nullptr;
};
