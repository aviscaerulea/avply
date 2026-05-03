#pragma once
#include <QString>

// 動画ファイルの基本情報
struct VideoInfo {
    double duration = 0.0; // 総再生時間（秒）
    int width = 0;
    int height = 0;
    QString codec;         // 映像コーデック名（例: av1, h264）
    double videoBitrate = 0.0; // 映像ビットレート（bps）。取得できない場合は 0
    bool valid = false;
};

// ffprobe/ffmpeg 実行結果
struct FfmpegResult {
    bool ok = false;
    QString err;
};

// ffprobe/ffmpeg 実行ユーティリティ
namespace Ffmpeg {
    // ffprobe で動画情報を取得する
    VideoInfo probe(const QString& ffprobePath, const QString& filePath, FfmpegResult& result);

    // av1_nvenc エンコーダが利用可能か確認する
    bool checkAv1Nvenc(const QString& ffmpegPath);

    // ffmpeg パスから ffprobe パスを生成する（同一ディレクトリを想定）
    QString ffprobePath(const QString& ffmpegPath);
} // namespace Ffmpeg
