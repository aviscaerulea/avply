#include "FfmpegRunner.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QObject>

namespace Ffmpeg {

VideoInfo probe(const QString& ffprobePath, const QString& filePath, FfmpegResult& result)
{
    VideoInfo info;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);

    const QStringList args = {
        "-v", "quiet",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        filePath
    };
    proc.start(ffprobePath, args);
    if (!proc.waitForFinished(15000)) {
        result = {false, "ffprobe がタイムアウトしました"};
        return info;
    }
    if (proc.exitCode() != 0) {
        result = {false, "ffprobe の実行に失敗しました: " + proc.readAllStandardError()};
        return info;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result = {false, "ffprobe JSON パースエラー: " + parseError.errorString()};
        return info;
    }

    const QJsonObject root = doc.object();

    // フォーマット情報から総時間を取得
    const QJsonObject fmt = root["format"].toObject();
    info.duration = fmt["duration"].toString("0").toDouble();

    // 映像・音声ストリームの情報を取得（最初に見つかったものを採用）
    const QJsonArray streams = root["streams"].toArray();
    bool gotVideo = false;
    bool gotAudio = false;
    for (const QJsonValue& v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s["codec_type"].toString();
        if (!gotVideo && type == "video") {
            info.codec = s["codec_name"].toString();
            info.width = s["width"].toInt();
            info.height = s["height"].toInt();
            // ビットレートは文字列として格納されている
            const QString br = s["bit_rate"].toString();
            if (!br.isEmpty()) info.videoBitrate = br.toDouble();
            // フレームレートは "num/den" 形式の文字列で格納されている
            const QString fr = s["avg_frame_rate"].toString();
            const auto parts = fr.split('/');
            if (parts.size() == 2) {
                const double num = parts[0].toDouble();
                const double den = parts[1].toDouble();
                if (den > 0.0) info.frameRate = num / den;
            }
            gotVideo = true;
        }
        else if (!gotAudio && type == "audio") {
            info.audioCodec = s["codec_name"].toString();
            const QString abr = s["bit_rate"].toString();
            if (!abr.isEmpty()) info.audioBitrate = abr.toDouble();
            const QString sr = s["sample_rate"].toString();
            if (!sr.isEmpty()) info.audioSampleRate = sr.toInt();
            info.audioChannels = s["channels"].toInt();
            gotAudio = true;
        }
        if (gotVideo && gotAudio) break;
    }

    info.valid = true;
    result = {true, {}};
    return info;
}

bool checkAv1Nvenc(const QString& ffmpegPath)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpegPath, {"-hide_banner", "-encoders"});
    if (!proc.waitForFinished(10000)) return false;
    return proc.readAllStandardOutput().contains("av1_nvenc");
}

QString ffprobePath(const QString& ffmpegPath)
{
    // ffmpeg.exe と同ディレクトリの ffprobe.exe を返す
    const QFileInfo fi(ffmpegPath);
    return fi.absoluteDir().filePath("ffprobe.exe");
}

QProcess* generateWaveform(
    const QString& ffmpegPath,
    const QString& inputPath,
    const QString& outputPath,
    const QSize& size,
    QObject* parent,
    std::function<void(bool ok, const QString& outputPath)> callback)
{
    // showwavespic フィルタで全長分の波形を PNG 出力する
    // scale=cbrt で小音量サンプルをさらに強調（会議録の小声を浮き上がらせる）、
    // draw=full でサンプルを横幅一杯に引き伸ばす
    const QString filterStr = QString(
        "[0:a]showwavespic=s=%1x%2:colors=#4080C0:scale=cbrt:draw=full")
        .arg(size.width()).arg(size.height());
    const QStringList args = {
        "-y",
        "-i", inputPath,
        "-lavfi", filterStr,
        "-frames:v", "1",
        outputPath
    };

    auto* proc = new QProcess(parent);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(proc,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        parent,
        [proc, outputPath, callback](int code, QProcess::ExitStatus status) {
        // 正常終了かつ出力 PNG 実体を確認できたときのみ ok=true で通知する
        const bool ok = (status == QProcess::NormalExit
                         && code == 0
                         && QFile::exists(outputPath));
        callback(ok, ok ? outputPath : QString());
        proc->deleteLater();
    });
    proc->start(ffmpegPath, args);
    return proc;
}

} // namespace Ffmpeg
