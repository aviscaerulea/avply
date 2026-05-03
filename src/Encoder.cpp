#include "Encoder.h"
#include <QStringList>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>

Encoder::Encoder(const QString& ffmpegPath, QObject* parent)
    : QObject(parent)
    , m_ffmpegPath(ffmpegPath)
{}

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
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempPath = QString("%1/avply_%2.mp4")
        .arg(tempDir)
        .arg(QDateTime::currentMSecsSinceEpoch());

    QStringList args;
    args << "-y"
         << "-hide_banner";

    if (params.mode == EncodeMode::Reencode) {
        args << "-hwaccel" << "cuda";
    }

    args << "-ss" << QString::number(params.inSec, 'f', 3)
         << "-i" << params.inputPath
         << "-t" << QString::number(m_totalDuration, 'f', 3);

    if (params.mode == EncodeMode::Reencode) {
        // QWXGA（幅 2048px）超の場合はアスペクト比を維持してスケールダウン
        if (params.inputWidth > 2048) {
            args << "-vf" << "scale=2048:-2";
        }

        args << "-c:v" << "av1_nvenc"
             << "-rc" << "vbr"
             << "-cq" << "28"
             << "-preset" << "p6"
             // スライド切替後の品質回復を最大 4 秒以内（30fps 想定）に抑える
             << "-g" << "120"
             // フラットな背景・テキストのビット配分を改善する
             << "-spatial_aq" << "1";

        args << "-c:a" << "libopus"
             << "-b:a" << "96k";
    }
    else {
        // ストリームコピー：再エンコードせずキーフレーム単位で切り出す
        args << "-c" << "copy";
    }

    args << "-movflags" << "+faststart"
         << m_tempPath;

    m_process = new QProcess(this);
    // stderr と stdout をマージして進捗行を受け取る
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &Encoder::onReadyReadOutput);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Encoder::onProcessFinished);

    m_process->start(m_ffmpegPath, args);
}

void Encoder::cancel()
{
    if (m_process) {
        m_cancelled = true;
        m_process->kill();
    }
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
    if (QFile::exists(m_params.outputPath)) {
        QFile::remove(m_params.outputPath);
    }
    if (!QFile::rename(m_tempPath, m_params.outputPath)) {
        cleanupTemp();
        emit finished(false, m_params.outputPath,
                      "一時ファイルから出力先への移動に失敗しました");
        return;
    }

    emit progressChanged(100);
    emit finished(true, m_params.outputPath, {});
}
