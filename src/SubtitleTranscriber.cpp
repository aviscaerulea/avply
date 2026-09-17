#include "SubtitleTranscriber.h"
#include "FfmpegRunner.h"
#include "WhisperEngine.h"
#include <QProcess>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

namespace {

// 部分ハッシュで読む先頭・末尾のバイト数
constexpr qint64 kHashChunkBytes = 1024 * 1024;

// 認識条件ハッシュをキャッシュ名へ埋める長さ（16 進の文字数）
// 16 文字 = 64bit で、1 メディアあたり数個しか作らない用途には衝突の心配がない。
// 名前が長くなるとパス長の上限へ近づくため、SHA-256 の全長は使わない
constexpr int kRecognitionKeyChars = 16;

// kill 後にプロセス終了を待つ上限（ms）。波形生成の stopWaveformProcess と同じ実測値
constexpr int kKillWaitMs = 1000;

} // namespace

SubtitleTranscriber::SubtitleTranscriber(QObject* parent)
    : QObject(parent)
{
    // キューを認識スレッドから QueuedConnection で運ぶためメタタイプを登録する
    qRegisterMetaType<SubtitleCue>();

    m_engineThread = new QThread(this);
    m_engineThread->setObjectName("whisper");
    m_engine = new WhisperEngine;
    m_engine->moveToThread(m_engineThread);
    connect(m_engine, &WhisperEngine::cueAdded, this, &SubtitleTranscriber::onEngineCue);
    connect(m_engine, &WhisperEngine::progressChanged, this, &SubtitleTranscriber::onEngineProgress);
    connect(m_engine, &WhisperEngine::finished, this, &SubtitleTranscriber::onEngineFinished);
    connect(m_engine, &WhisperEngine::modelLoadFailed, this, &SubtitleTranscriber::modelLoadFailed);
    m_engineThread->start();
}

SubtitleTranscriber::~SubtitleTranscriber()
{
    stop();
    // 認識スレッドを止めてから解放する。stop の取り消しで whisper_full が戻り、
    // スロットを抜けたイベントループが quit を処理する。停止後はどのスレッドも
    // エンジンを触らないため GUI thread から delete してよい（VideoView の audio thread と同手順）
    m_engineThread->quit();
    m_engineThread->wait();
    delete m_engine;
    m_engine = nullptr;
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

QString SubtitleTranscriber::recognitionKey(const Params& params)
{
    // 区切りに改行を挟み、隣り合う値の境目が溶けて別の組と同じ並びになるのを防ぐ
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QFileInfo(params.modelPath).fileName().toUtf8() + '\n');
    hash.addData(params.language.toUtf8() + '\n');
    hash.addData(params.prompt.toUtf8() + '\n');
    return QString::fromLatin1(hash.result().toHex().left(kRecognitionKeyChars));
}

void SubtitleTranscriber::start(const Params& params, const QString& mediaPath)
{
    stop();
    m_params    = params;
    m_mediaPath = mediaPath;

    const QString hash = mediaHash(mediaPath);
    if (hash.isEmpty()) {
        qWarning() << "SubtitleTranscriber: メディアのハッシュ計算に失敗:" << mediaPath;
        emit finished(false);
        return;
    }
    // SRT は認識条件ごとに別ファイルへ分ける。中間 PCM は条件に依存しないためメディア側だけで引く
    m_cachePath = params.cacheDir + "/" + hash + "-" + recognitionKey(params) + ".srt";
    m_pcmPath   = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + "/avply_sub_" + hash + ".f32";

    // キャッシュ命中：抽出も認識も起動せず全キューを同期通知する
    QFile cache(m_cachePath);
    if (cache.exists() && cache.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&cache);
        in.setEncoding(QStringConverter::Utf8);
        m_track = SubtitleTrack::fromSrt(in.readAll());
        for (const SubtitleCue& cue : m_track.cues()) {
            emit cueAdded(cue);
        }
        emit finished(true);
        return;
    }

    startExtract();
}

void SubtitleTranscriber::stop()
{
    // ジョブ番号を進め、この後に届く旧ジョブの通知をすべて無効化する。
    // 同じ番号を取り消しの基準としてエンジンへ渡し、実行中と待ち行列の両方を止める
    ++m_jobId;
    m_engine->cancelUpTo(m_jobId);
    releaseProcess();
    m_track.clear();
    if (!m_pcmPath.isEmpty()) {
        QFile::remove(m_pcmPath);
        m_pcmPath.clear();
    }
}

void SubtitleTranscriber::releaseModel()
{
    QMetaObject::invokeMethod(m_engine, &WhisperEngine::releaseModel, Qt::QueuedConnection);
}

void SubtitleTranscriber::startExtract()
{
    // whisper は 16kHz モノラルの float32 サンプル列を要求する。ffmpeg に生 PCM を書かせれば
    // ヘッダ解析なしでそのまま float 列として読める
    const QStringList args = {
        "-y", "-i", m_mediaPath,
        "-vn", "-ac", "1", "-ar", "16000", "-f", "f32le", "-c:a", "pcm_f32le",
        m_pcmPath
    };

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && code == 0 && QFile::exists(m_pcmPath));
        m_proc->deleteLater();
        m_proc = nullptr;
        if (!ok) {
            qWarning() << "SubtitleTranscriber: 音声抽出に失敗 exit=" << code << m_mediaPath;
            finish(false);
            return;
        }
        startRecognize();
    });
    Ffmpeg::connectStartFailureGuard(m_proc, this, [this]() {
        m_proc = nullptr;
        qWarning() << "SubtitleTranscriber: ffmpeg の起動に失敗:" << m_params.ffmpegPath;
        finish(false);
    });
    m_proc->start(m_params.ffmpegPath, args);
}

void SubtitleTranscriber::startRecognize()
{
    const quint64 jobId    = ++m_jobId;
    const QString model    = m_params.modelPath;
    const QString pcm      = m_pcmPath;
    const QString language = m_params.language;
    const QString prompt   = m_params.prompt;
    WhisperEngine* engine  = m_engine;
    QMetaObject::invokeMethod(engine, [engine, jobId, model, pcm, language, prompt]() {
        engine->transcribe(jobId, model, pcm, language, prompt);
    }, Qt::QueuedConnection);
}

void SubtitleTranscriber::onEngineCue(quint64 jobId, const SubtitleCue& cue)
{
    if (jobId != m_jobId) return;
    m_track.append(cue);
    emit cueAdded(cue);
}

void SubtitleTranscriber::onEngineProgress(quint64 jobId, int percent)
{
    if (jobId != m_jobId) return;
    emit progressChanged(percent);
}

void SubtitleTranscriber::onEngineFinished(quint64 jobId, bool ok)
{
    if (jobId != m_jobId) return;
    finish(ok);
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
    QFile::remove(m_pcmPath);
    m_pcmPath.clear();
    emit finished(ok);
}
