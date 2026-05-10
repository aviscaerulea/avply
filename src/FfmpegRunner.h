#pragma once
#include <QString>
#include <QSize>
#include <functional>

class QObject;
class QProcess;

// メディアファイル（動画・音声）の基本情報
struct VideoInfo {
    double duration = 0.0; // 総再生時間（秒）
    int width = 0;
    int height = 0;
    double frameRate = 0.0;  // 映像フレームレート（fps）。取得できない場合は 0
    QString codec;           // 映像コーデック名（例: av1, h264）
    double videoBitrate = 0.0; // 映像ビットレート（bps）。取得できない場合は 0
    QString audioCodec;      // 音声コーデック名（例: aac, opus）。取得できない場合は空文字
    double audioBitrate = 0.0; // 音声ビットレート（bps）。取得できない場合は 0
    int audioSampleRate = 0;   // 音声サンプリングレート（Hz）。取得できない場合は 0
    int audioChannels = 0;     // 音声チャンネル数。取得できない場合は 0
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
    // 同期版は UI スレッドを waitForFinished で 15 秒間ブロックしうるため非同期化した。
    // QProcess は parent の子オブジェクトとして生成され、完了時に callback を呼んだ後 deleteLater される。
    // 戻り値の QProcess* は呼び出し側が保持し、新ファイル読込時の kill キャンセル用に使う
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
    // 戻り値の QProcess* は途中キャンセル用（kill 呼び出し）として保持できる
    QProcess* generateWaveform(
        const QString& ffmpegPath,
        const QString& inputPath,
        const QString& outputPath,
        const QSize& size,
        QObject* parent,
        std::function<void(bool ok, const QString& outputPath)> callback);
} // namespace Ffmpeg
