#include "SubtitleTranscriber.h"
#include "FfmpegRunner.h"
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

namespace {

// 部分ハッシュで読む先頭・末尾のバイト数
constexpr qint64 kHashChunkBytes = 1024 * 1024;

// kill 後にプロセス終了を待つ上限（ms）。波形生成の stopWaveformProcess と同じ実測値
constexpr int kKillWaitMs = 1000;

} // namespace

SubtitleTranscriber::SubtitleTranscriber(QObject* parent)
    : QObject(parent)
{
}

SubtitleTranscriber::~SubtitleTranscriber()
{
    stop();
}

QString SubtitleTranscriber::mediaHash(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();

    const qint64 size = f.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(size) + '\n');
    hash.addData(f.read(kHashChunkBytes));
    if (size > kHashChunkBytes) {
        if (!f.seek(size - kHashChunkBytes)) return QString();
        hash.addData(f.read(kHashChunkBytes));
    }
    return QString::fromLatin1(hash.result().toHex());
}

void SubtitleTranscriber::start(const Params& params, const QString& mediaPath)
{
    stop();
    m_params    = params;
    m_mediaPath = mediaPath;

    const QString hash = mediaHash(mediaPath);
    if (hash.isEmpty()) {
        qWarning() << "SubtitleTranscriber: メディアのハッシュ計算に失敗:" << mediaPath;
        return;
    }
    m_cachePath = params.cacheDir + "/" + hash + ".srt";
    m_wavPath   = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + "/avply_sub_" + hash + ".wav";

    // キャッシュ命中：プロセスを起動せず全キューを同期通知する
    QFile cache(m_cachePath);
    if (cache.exists() && cache.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&cache);
        in.setEncoding(QStringConverter::Utf8);
        m_track = SubtitleTrack::fromSrt(in.readAll());
        for (const SubtitleCue& cue : m_track.cues()) {
            emit cueAdded(cue);
        }
        return;
    }

    startExtract();
}

void SubtitleTranscriber::stop()
{
    releaseProcess();
    m_stdoutBuf.clear();
    m_track.clear();
    if (!m_wavPath.isEmpty()) {
        QFile::remove(m_wavPath);
        m_wavPath.clear();
    }
}

void SubtitleTranscriber::startExtract()
{
    // whisper は 16kHz モノラルを要求する。ffmpeg 側で変換しておくと whisper-cli の
    // 内蔵デコーダ（wav/mp3/flac/ogg のみ）に依存せず、任意のコンテナを扱える
    const QStringList args = {
        "-y", "-i", m_mediaPath,
        "-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le",
        m_wavPath
    };

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && code == 0 && QFile::exists(m_wavPath));
        m_proc->deleteLater();
        m_proc = nullptr;
        if (!ok) {
            qWarning() << "SubtitleTranscriber: 音声抽出に失敗 exit=" << code << m_mediaPath;
            finish(false);
            return;
        }
        startWhisper();
    });
    Ffmpeg::connectStartFailureGuard(m_proc, this, [this]() {
        m_proc = nullptr;
        qWarning() << "SubtitleTranscriber: ffmpeg の起動に失敗:" << m_params.ffmpegPath;
        finish(false);
    });
    m_proc->start(m_params.ffmpegPath, args);
}

void SubtitleTranscriber::startWhisper()
{
    // -np はモデル情報・タイミング等のログ出力だけを抑止し、区間行の出力は残る。
    // 区間行は標準出力へ 1 区間ごとに flush されるため、readyRead で逐次読める
    const QStringList args = {
        "-m", m_params.modelPath,
        "-l", m_params.language,
        "-np",
        "-f", m_wavPath
    };

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuf += m_proc->readAllStandardOutput();
        consumeStdout();
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        m_stdoutBuf += m_proc->readAllStandardOutput();
        consumeStdout();
        const bool ok = (status == QProcess::NormalExit && code == 0);
        if (!ok) {
            qWarning() << "SubtitleTranscriber: whisper-cli が異常終了 exit=" << code
                       << QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        }
        m_proc->deleteLater();
        m_proc = nullptr;
        finish(ok);
    });
    Ffmpeg::connectStartFailureGuard(m_proc, this, [this]() {
        m_proc = nullptr;
        qWarning() << "SubtitleTranscriber: whisper-cli の起動に失敗:" << m_params.whisperPath;
        finish(false);
    });
    m_proc->start(m_params.whisperPath, args);
}

void SubtitleTranscriber::consumeStdout()
{
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stdoutBuf.left(nl));
        m_stdoutBuf.remove(0, nl + 1);
        SubtitleCue cue;
        if (!SubtitleTrack::parseWhisperLine(line, cue)) continue;
        m_track.append(cue);
        emit cueAdded(cue);
    }
}

void SubtitleTranscriber::releaseProcess()
{
    if (!m_proc) return;
    disconnect(m_proc, nullptr, nullptr, nullptr);
    m_proc->kill();
    m_proc->waitForFinished(kKillWaitMs);
    m_proc->setParent(nullptr);
    m_proc->deleteLater();
    m_proc = nullptr;
}

void SubtitleTranscriber::finish(bool ok)
{
    if (ok && !m_track.isEmpty()) {
        QDir().mkpath(m_params.cacheDir);
        QFile out(m_cachePath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            out.write(m_track.toSrt().toUtf8());
        }
        else {
            qWarning() << "SubtitleTranscriber: キャッシュの書き込みに失敗:" << m_cachePath;
        }
    }
    QFile::remove(m_wavPath);
    m_wavPath.clear();
}
