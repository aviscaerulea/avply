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

    // 走行中の ffmpeg があれば新規要求は破棄する（完走優先 — 連続ホバーでも各ジョブを最後まで実行）
    // 呼び出し側は finished 後に最新位置を改めて request すること
    if (m_proc) {
        callback(false, QPixmap());
        return;
    }

    // ffmpeg コマンドライン
    // -hwaccel は -i の前（input options）に置く。GPU デコードでシーク + 1 フレーム取り出しを高速化
    // -ss を -i の前に置く（input seek）ことで高速にスキップする
    // -frames:v 1 で 1 フレームのみ書き出し、-an -sn -dn で他ストリームを除外して軽量化
    // scale フィルタは fast_bilinear で軽量化（lanczos より明確に高速、サムネイル用途で画質劣化は実用上無視可）
    // force_original_aspect_ratio=decrease で縦横比を維持して縮小する
    // 出力は image2pipe + bmp で stdout へ送り、一時ファイル I/O と PNG 圧縮 CPU コストを排除する
    const QString filterStr = QString(
        "scale=%1:%2:force_original_aspect_ratio=decrease:flags=fast_bilinear,format=rgb24")
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
        << "-vcodec" << "bmp"
        << "pipe:1";

    auto* proc = new QProcess(this);
    // stdout には BMP バイナリのみを流したいため stderr を分離する
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
            const QByteArray bmpBytes = proc->readAllStandardOutput();
            QImage img;
            if (img.loadFromData(bmpBytes, "BMP")) {
                pix = QPixmap::fromImage(std::move(img));
                ok = !pix.isNull();
            }
        }

        // callback 呼出前に m_proc をクリアする。
        // callback 内から request() を呼んで連鎖実行できるようにする（完走優先設計のキモ）
        if (m_proc == proc) {
            m_proc = nullptr;
        }
        proc->deleteLater();

        if (ok) {
            putCache(seconds, pix);
            callback(true, pix);
        }
        else {
            callback(false, QPixmap());
        }
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
        if (m_proc == proc) {
            m_proc = nullptr;
        }
        proc->deleteLater();
        callback(false, QPixmap());
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
    m_proc = nullptr;

    // disconnect でコールバック経路を切ってから kill する
    // 旧 callback が finished 経由で発火して新規結果へ誤反映するのを防ぐ
    disconnect(proc, nullptr, this, nullptr);

    if (synchronous) {
        // 同期モード（デストラクタ）では確実に終端を待ってから delete する
        proc->kill();
        proc->waitForFinished(3000);
        delete proc;
        return;
    }

    // 非同期モード：setSource からのファイル切替時のみ呼ばれる（連続ホバーでは呼ばれない）。
    // 旧プロセスの kill が Windows 上で遅延すると新規 ffmpeg と並走して GPU/I/O 競合を招くため、
    // 50ms だけ終端を待って新規プロセスとの並走を緩和する。
    // 終了時に自動で解放されるよう finished から自身の deleteLater を直結する。
    // disconnect で this 受信は切ってあるので旧 callback は呼ばれない。
    QObject::connect(proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     proc, &QProcess::deleteLater);
    proc->kill();
    proc->waitForFinished(50);
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
