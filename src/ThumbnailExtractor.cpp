#include "ThumbnailExtractor.h"
#include <QProcess>
#include <QImage>
#include <QByteArray>
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

void ThumbnailExtractor::setHwaccel(const QString& hwaccel)
{
    m_hwaccel = hwaccel;
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

    // ffmpeg コマンドライン
    // -hwaccel は -i の前（input options）に置く。GPU デコードでシーク + 1 フレーム取り出しを高速化
    // -ss を -i の前に置く（input seek）ことで高速にスキップする
    // -frames:v 1 で 1 フレームのみ書き出し、-an -sn -dn で他ストリームを除外して軽量化
    // scale フィルタの force_original_aspect_ratio=decrease で縦横比を維持して縮小する
    // 出力は image2pipe + png で stdout へ送り、一時ファイル I/O を排除する
    const QString filterStr = QString(
        "scale=%1:%2:force_original_aspect_ratio=decrease,format=rgb24")
        .arg(m_thumbSize.width()).arg(m_thumbSize.height());

    QStringList args = { "-hide_banner", "-loglevel", "error" };
    if (!m_hwaccel.isEmpty() && m_hwaccel.compare("none", Qt::CaseInsensitive) != 0) {
        args << "-hwaccel" << m_hwaccel;
    }
    args
        << "-ss" << QString::number(seconds)
        << "-i" << m_inputPath
        << "-frames:v" << "1"
        << "-an" << "-sn" << "-dn"
        << "-vf" << filterStr
        << "-f" << "image2pipe"
        << "-vcodec" << "png"
        << "pipe:1";

    auto* proc = new QProcess(this);
    // stdout には PNG バイナリのみを流したいため stderr を分離する
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    m_proc = proc;

    QObject::connect(proc,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        [this, proc, seconds, callback](int code, QProcess::ExitStatus status) {
        const bool exitOk = (status == QProcess::NormalExit && code == 0);
        QPixmap pix;
        bool ok = false;
        if (exitOk) {
            const QByteArray pngBytes = proc->readAllStandardOutput();
            QImage img;
            if (img.loadFromData(pngBytes, "PNG")) {
                pix = QPixmap::fromImage(std::move(img));
                ok = !pix.isNull();
            }
        }

        if (ok) {
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
        }
        proc->deleteLater();
    });

    // 起動失敗を捕捉する
    // FailedToStart のとき finished は発火しないため、
    // ここで callback を確実に呼ばないと呼び出し元が永久待機する。
    // 起動成功後の Crashed 等は finished も発火するためここでは無視する
    QObject::connect(proc, &QProcess::errorOccurred, this,
        [this, proc, callback](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;

        // finished 経路と二重発火しないよう、ここで disconnect する
        disconnect(proc, nullptr, this, nullptr);
        callback(false, QPixmap());
        if (m_proc == proc) {
            m_proc = nullptr;
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

    // disconnect でコールバック経路を切ってから kill する
    // 旧 callback が finished 経由で発火して新規結果へ誤反映するのを防ぐ
    disconnect(proc, nullptr, this, nullptr);
    proc->kill();

    // kill 後にプロセス終端を短時間待つ
    // 同期モード（デストラクタ）では確実に終端を待ってから delete する
    proc->waitForFinished(synchronous ? 3000 : 1000);

    if (synchronous) {
        delete proc;
    }
    else {
        proc->deleteLater();
    }

    m_proc = nullptr;
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
