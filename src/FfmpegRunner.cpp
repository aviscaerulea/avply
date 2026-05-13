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

namespace {

// ffprobe の JSON 出力から VideoInfo を組み立てる
// probeAsync 内のラムダから呼ぶ純粋関数。Qt 依存は QJson* に限る
VideoInfo parseProbeJson(const QByteArray& jsonBytes, FfmpegResult& result)
{
    VideoInfo info;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result = {false, "ffprobe JSON パースエラー: " + parseError.errorString()};
        return info;
    }

    const QJsonObject root = doc.object();
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

} // namespace

QProcess* probeAsync(
    const QString& ffprobePath,
    const QString& filePath,
    QObject* parent,
    std::function<void(const VideoInfo& info, const FfmpegResult& result)> callback)
{
    const QStringList args = {
        "-v", "quiet",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        filePath
    };

    auto* proc = new QProcess(parent);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(proc,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        parent,
        [proc, callback](int code, QProcess::ExitStatus status) {
        VideoInfo info;
        FfmpegResult result;
        if (status != QProcess::NormalExit) {
            result = {false, "ffprobe が異常終了しました"};
        }
        else if (code != 0) {
            result = {false, "ffprobe の実行に失敗しました: "
                             + QString::fromUtf8(proc->readAllStandardError())};
        }
        else {
            info = parseProbeJson(proc->readAllStandardOutput(), result);
        }
        callback(info, result);
        proc->deleteLater();
    });
    proc->start(ffprobePath, args);
    return proc;
}

bool checkAv1Nvenc(const QString& ffmpegPath)
{
    // 結果をプロセス内でキャッシュする
    // -encoders 列挙の結果は ffmpeg バイナリ固定の機能なので変換ボタン押下のたびに呼ぶ必要はない。
    // タイムアウトも 5 秒に短縮して AV ソフト等で ffmpeg が応答しない場合の UI 凍結を抑える
    static QString cachedPath;
    static bool cachedResult = false;
    if (cachedPath == ffmpegPath) return cachedResult;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpegPath, {"-hide_banner", "-encoders"});
    const bool finished = proc.waitForFinished(5000);
    cachedResult = finished && proc.readAllStandardOutput().contains("av1_nvenc");
    cachedPath = ffmpegPath;
    return cachedResult;
}

QString ffprobePath(const QString& ffmpegPath)
{
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
