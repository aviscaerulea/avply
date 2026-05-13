#include "Encoder.h"
#include <QStringList>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

namespace {

// 出力映像の幅上限（px）
// QWXGA = 2048 を上限とし、これを超える入力はアスペクト比維持でダウンスケールする
constexpr int kMaxOutputWidth = 2048;

} // namespace

Encoder::Encoder(const QString& ffmpegPath, QObject* parent)
    : QObject(parent)
    , m_ffmpegPath(ffmpegPath)
{}

Encoder::~Encoder()
{
    // MainWindow デストラクタの waitForFinished 後でも tempfile が残るケースに備える。
    // QProcess は親子破棄で kill+wait されるが、QFile::remove は明示的に呼ぶ必要がある
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    if (!m_tempPath.isEmpty() && QFile::exists(m_tempPath)) {
        QFile::remove(m_tempPath);
    }
}

bool Encoder::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void Encoder::encode(const EncodeParams& params)
{
    if (isRunning()) return;

    m_cancelled = false;
    m_params = params;
    m_totalDuration = params.outSec - params.inSec;
    if (m_totalDuration <= 0.0) {
        emit finished(false, params.outputPath, "IN/OUT の範囲が無効です");
        return;
    }

    // %TEMP% に一時出力ファイルパスを生成する
    // 完了後に本来の出力パスへ移動する設計
    // 一時ファイルの拡張子は出力パスと一致させる（ffmpeg のコンテナ判定が拡張子依存のため）
    // 複数 avply プロセス同時実行時の衝突を避けるため UUID を使う
    const QString outExt = QFileInfo(params.outputPath).suffix().toLower();
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempPath = QString("%1/avply_%2.%3")
        .arg(tempDir,
             QUuid::createUuid().toString(QUuid::WithoutBraces),
             outExt);

    QStringList args;
    args << "-y"
         << "-hide_banner";

    // 映像入力かつ再エンコード時のみ NVENC のための CUDA HW デコードを使う
    if (params.mode == EncodeMode::Reencode && params.hasVideo) {
        args << "-hwaccel" << "cuda";
    }

    args << "-ss" << QString::number(params.inSec, 'f', 3)
         << "-i" << params.inputPath
         << "-t" << QString::number(m_totalDuration, 'f', 3);

    if (params.mode == EncodeMode::Reencode) {
        if (params.hasVideo) {
            // 幅上限超の場合はアスペクト比を維持してスケールダウン
            if (params.inputWidth > kMaxOutputWidth) {
                args << "-vf" << QString("scale=%1:-2").arg(kMaxOutputWidth);
            }

            args << "-c:v" << "av1_nvenc"
                 << "-rc" << "vbr"
                 << "-cq" << "28"
                 << "-preset" << "p6"
                 // スライド切替後の品質回復を最大 4 秒以内（30fps 想定）に抑える
                 << "-g" << "120"
                 // フラットな背景・テキストのビット配分を改善する
                 << "-spatial_aq" << "1";
        }
        else {
            // 音声のみ入力：映像ストリームを除外する（一部コンテナの非映像ストリーム混入も防ぐ）
            args << "-vn";
        }

        args << "-c:a" << "libopus"
             << "-b:a" << "96k";
    }
    else {
        // ストリームコピー：再エンコードせずキーフレーム単位で切り出す
        args << "-c" << "copy";
    }

    // +faststart は ISOBMFF 系コンテナ専用。それ以外（opus/ogg/mp3/wav 等）に付けると ffmpeg が警告を出す
    if (outExt == "mp4" || outExt == "m4a" || outExt == "mov") {
        args << "-movflags" << "+faststart";
    }

    args << m_tempPath;

    m_process = new QProcess(this);
    // ffmpeg は進捗行（time=...）を stderr に出力するため、readyReadStandardOutput でまとめて受け取れるよう統合する
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &Encoder::onReadyReadOutput);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Encoder::onProcessFinished);

    // FailedToStart のとき finished は発火しないため errorOccurred で捕捉する。
    // ここで通知しないと UI が「変換中 0%」のまま無限待機となる
    connect(m_process, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        if (!m_process) return;
        disconnect(m_process, nullptr, this, nullptr);
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(false, m_params.outputPath, "ffmpeg の起動に失敗しました");
    });

    m_process->start(m_ffmpegPath, args);
}

void Encoder::cancel()
{
    if (m_process) {
        m_cancelled = true;
        m_process->kill();
    }
}

bool Encoder::waitForFinished(int timeoutMs)
{
    if (!m_process) return true;
    return m_process->waitForFinished(timeoutMs);
}

void Encoder::onReadyReadOutput()
{
    if (!m_process || m_totalDuration <= 0.0) return;

    const QString text = m_process->readAllStandardOutput();

    // ffmpeg 進捗行の time=HH:MM:SS.nn から経過時間を抽出
    static const QRegularExpression re(R"(time=(\d+):(\d{2}):(\d{2}\.\d+))");
    auto it = re.globalMatch(text);
    double latestSec = -1.0;
    while (it.hasNext()) {
        const auto m = it.next();
        const double sec = m.captured(1).toDouble() * 3600.0
                         + m.captured(2).toDouble() * 60.0
                         + m.captured(3).toDouble();
        latestSec = sec;
    }

    if (latestSec >= 0.0) {
        // 100% は onProcessFinished で出力ファイル確定後に emit するため、進捗段階は 99% で頭打ち
        const int pct = static_cast<int>(latestSec / m_totalDuration * 100.0);
        emit progressChanged(qBound(0, pct, 99));
    }
}

void Encoder::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_process->deleteLater();
    m_process = nullptr;

    // 失敗・中止時は一時ファイルを削除する
    auto cleanupTemp = [this]() {
        if (!m_tempPath.isEmpty() && QFile::exists(m_tempPath)) {
            QFile::remove(m_tempPath);
        }
    };

    // ユーザ要求による中止：err を空文字で発火し呼び出し側でダイアログを抑制する
    if (m_cancelled) {
        cleanupTemp();
        emit finished(false, m_params.outputPath, {});
        return;
    }
    if (status == QProcess::CrashExit) {
        cleanupTemp();
        emit finished(false, m_params.outputPath, "変換処理が中断されました");
        return;
    }
    if (exitCode != 0) {
        cleanupTemp();
        emit finished(false, m_params.outputPath,
                      QString("変換に失敗しました（終了コード: %1）").arg(exitCode));
        return;
    }

    // 一時ファイルを本来の出力パスへ移動する
    // %TEMP% が出力先と別ボリュームだと QFile::rename が失敗するため copy + remove にフォールバックする
    if (QFile::exists(m_params.outputPath)) {
        QFile::remove(m_params.outputPath);
    }
    if (!QFile::rename(m_tempPath, m_params.outputPath)) {
        if (!QFile::copy(m_tempPath, m_params.outputPath)) {
            // 部分書き込みで生じた壊れた出力ファイルを除去してから失敗を通知する
            QFile::remove(m_params.outputPath);
            cleanupTemp();
            emit finished(false, m_params.outputPath,
                          "一時ファイルから出力先への移動に失敗しました");
            return;
        }
        QFile::remove(m_tempPath);
    }

    emit progressChanged(100);
    emit finished(true, m_params.outputPath, {});
}
