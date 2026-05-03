#include "FfmpegRunner.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

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

    // 映像ストリームの情報を取得
    const QJsonArray streams = root["streams"].toArray();
    for (const QJsonValue& v : streams) {
        const QJsonObject s = v.toObject();
        if (s["codec_type"].toString() == "video") {
            info.codec = s["codec_name"].toString();
            info.width = s["width"].toInt();
            info.height = s["height"].toInt();
            // ビットレートは文字列として格納されている
            const QString br = s["bit_rate"].toString();
            if (!br.isEmpty()) info.videoBitrate = br.toDouble();
            break;
        }
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

} // namespace Ffmpeg
