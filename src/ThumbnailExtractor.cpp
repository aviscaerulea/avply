#include "ThumbnailExtractor.h"
#include <QProcess>
#include <QFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <cmath>

ThumbnailExtractor::ThumbnailExtractor(QObject* parent)
    : QObject(parent)
{
}

ThumbnailExtractor::~ThumbnailExtractor()
{
    cancelInflight(true);
}

void ThumbnailExtractor::setSource(const QString& ffmpegPath,
                                   const QString& inputPath,
                                   const QSize& thumbSize)
{
    cancelInflight(false);
    m_cache.clear();
    m_lru.clear();

    m_ffmpegPath = ffmpegPath;
    m_inputPath  = inputPath;
    m_thumbSize  = thumbSize;
}

void ThumbnailExtractor::request(int seconds,
                                 std::function<void(bool ok, const QPixmap& pix)> callback)
{
    // 抽出抑止条件：ffmpeg・入力未設定、または thumbSize が無効（音声ファイル時）
    const bool disabled =
        m_ffmpegPath.isEmpty()
        || m_inputPath.isEmpty()
        || m_thumbSize.width() <= 0
        || m_thumbSize.height() <= 0;
    if (disabled) {
        callback(false, QPixmap());
        return;
    }

    // キャッシュヒットなら同期返却（ホバー追従の即応性確保）
    QPixmap cached;
    if (getCache(seconds, cached)) {
        callback(true, cached);
        return;
    }

    // 走行中の旧プロセスをキャンセル（旧 callback は disconnect で封殺）
    cancelInflight(false);

    // 一時 PNG パスを生成。プロセス内シーケンスで上書き衝突を避ける
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString outPath = QString("%1/avply_thumb_%2_%3.png")
        .arg(tmpDir)
        .arg(QCoreApplication::applicationPid())
        .arg(++m_seq);

    // ffmpeg コマンドライン
    // -ss を -i の前に置く（input seek）ことで高速にスキップする
    // -frames:v 1 で 1 フレームのみ書き出し、-an -sn -dn で他ストリームを除外して軽量化
    // scale フィルタの force_original_aspect_ratio=decrease で縦横比を維持して縮小する
    const QString filterStr = QString(
        "scale=%1:%2:force_original_aspect_ratio=decrease,format=rgb24")
        .arg(m_thumbSize.width()).arg(m_thumbSize.height());
    const QStringList args = {
        "-hide_banner", "-loglevel", "error",
        "-ss", QString::number(seconds),
        "-i", m_inputPath,
        "-frames:v", "1",
        "-an", "-sn", "-dn",
        "-vf", filterStr,
        "-f", "image2",
        "-y", outPath,
    };

    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    m_proc = proc;
    m_procOutPath = outPath;

    QObject::connect(proc,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        [this, proc, outPath, seconds, callback](int code, QProcess::ExitStatus status) {
        // 正常終了かつ PNG 実体ありで成功扱い
        const bool ok = (status == QProcess::NormalExit
                         && code == 0
                         && QFile::exists(outPath));
        QPixmap pix;
        if (ok) {
            pix.load(outPath);
        }
        // 一時 PNG はディスクに残さず、読み込み後に即削除する
        QFile::remove(outPath);

        if (ok && !pix.isNull()) {
            putCache(seconds, pix);
            callback(true, pix);
        }
        else {
            callback(false, QPixmap());
        }

        // 自身が現在の走行プロセスなら参照をクリアする
        // cancelInflight 後の遅延 finished では既に別プロセスが入っている可能性があるため
        if (m_proc == proc) {
            m_proc = nullptr;
            m_procOutPath.clear();
        }
        proc->deleteLater();
    });

    proc->start(m_ffmpegPath, args);
}

bool ThumbnailExtractor::tryGetCached(int seconds, QPixmap& outPix)
{
    return getCache(seconds, outPix);
}

void ThumbnailExtractor::cancelInflight(bool synchronous)
{
    if (!m_proc) return;

    QProcess* proc = m_proc;
    const QString outPath = m_procOutPath;

    // disconnect でコールバック経路を切ってから kill する
    // 旧 callback が finished 経由で発火して新規ファイルへ誤反映するのを防ぐ
    disconnect(proc, nullptr, this, nullptr);
    proc->kill();

    if (synchronous) {
        proc->waitForFinished(3000);
        delete proc;
    }
    else {
        proc->deleteLater();
    }

    m_proc = nullptr;
    m_procOutPath.clear();

    // 中途まで書かれた可能性のある PNG を削除する
    if (!outPath.isEmpty()) {
        QFile::remove(outPath);
    }
}

void ThumbnailExtractor::putCache(int key, const QPixmap& pix)
{
    if (m_cache.contains(key)) {
        // 既存エントリの更新：LRU 順序の先頭に移動する
        m_lru.removeOne(key);
    }
    m_cache.insert(key, pix);
    m_lru.prepend(key);

    // 容量超過時は末尾（最古）を捨てる
    while (m_lru.size() > kCacheCap) {
        const int oldest = m_lru.takeLast();
        m_cache.remove(oldest);
    }
}

bool ThumbnailExtractor::getCache(int key, QPixmap& outPix)
{
    auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd()) return false;

    outPix = it.value();
    // 参照されたキーを LRU 先頭に移動する
    m_lru.removeOne(key);
    m_lru.prepend(key);
    return true;
}
