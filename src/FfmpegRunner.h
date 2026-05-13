#pragma once
#include <QString>
#include <QSize>
#include <functional>

class QObject;
class QProcess;

// メディアファイル（動画・音声）の基本情報
// 取得できない数値フィールドは 0、文字列フィールドは空文字となる
struct VideoInfo {
    double duration = 0.0;     // 総再生時間（秒）
    int width = 0;
    int height = 0;
    double frameRate = 0.0;    // 映像フレームレート（fps）
    QString codec;             // 映像コーデック名（例: av1, h264）
    double videoBitrate = 0.0; // 映像ビットレート（bps）
    QString audioCodec;        // 音声コーデック名（例: aac, opus）
    double audioBitrate = 0.0; // 音声ビットレート（bps）
    int audioSampleRate = 0;   // 音声サンプリングレート（Hz）
    int audioChannels = 0;     // 音声チャンネル数
    bool valid = false;

    // 音声ストリームの有無
    // 「音声のみか（映像なしか）」とは別軸の判定。波形生成可否などに使う
    bool hasAudio() const { return !audioCodec.isEmpty(); }
};

// ffprobe/ffmpeg 実行結果
struct FfmpegResult {
    bool ok = false;
    QString err;
};

// ffprobe/ffmpeg 実行ユーティリティ
namespace Ffmpeg {
    // ffprobe で動画情報を非同期取得する
    // QProcess は parent の子として生成され、完了時に callback を呼んだ後 deleteLater される。
    // 戻り値は呼び出し側で保持し、新ファイル読込時の kill キャンセル用ハンドルとして使う
    QProcess* probeAsync(
        const QString& ffprobePath,
        const QString& filePath,
        QObject* parent,
        std::function<void(const VideoInfo& info, const FfmpegResult& result)> callback);

    // av1_nvenc エンコーダが利用可能か確認する
    bool checkAv1Nvenc(const QString& ffmpegPath);

    // ffmpeg パスから ffprobe パスを生成する（同一ディレクトリを想定）
    QString ffprobePath(const QString& ffmpegPath);

    // 音声波形 PNG を非同期生成する
    // showwavespic フィルタで全長分の波形を 1 枚の PNG に出力する。完了時に callback を呼び出す。
    // 失敗時（無音動画・ffmpeg 不在など）は callback(false, "") で通知する。
    // QProcess は parent の子オブジェクトとして生成され、finished 後に deleteLater される。
    // 戻り値の QProcess* は途中キャンセル用（kill 呼び出し）として保持できる。
    // -hwaccel は意図的に渡さない。showwavespic は CPU パスで十分高速なため、
    // ThumbnailExtractor と非対称になるが GPU 初期化オーバーヘッドを避ける
    QProcess* generateWaveform(
        const QString& ffmpegPath,
        const QString& inputPath,
        const QString& outputPath,
        const QSize& size,
        QObject* parent,
        std::function<void(bool ok, const QString& outputPath)> callback);
} // namespace Ffmpeg
