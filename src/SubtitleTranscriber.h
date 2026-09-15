#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include "SubtitleTrack.h"

class QProcess;

// whisper-cli による字幕生成の実行とキャッシュを担う
// 1 メディアにつき ffmpeg（16kHz モノラル WAV 抽出）→ whisper-cli（認識）の 2 段を順に走らせる。
// whisper-cli が標準出力へ区間を書き出すたびに cueAdded で逐次通知する。
// 完了時は全キューを SRT としてキャッシュへ保存する（キューが 1 件も無ければ作らない）。次回以降はプロセスを起動せず、
// キャッシュから即時に全キューを通知する。
//
// キャッシュのキーはメディアの部分ハッシュ（mediaHash）で、パス・更新日時に依存しない。
// 同じ動画をコピーしても移動しても再認識しない。
//
// whisper-cli は narrow 文字の argv で動くため、ANSI コードページ外の文字を含むパスを開けない。
// 入力音声はメディアの元パスを渡さず、%TEMP% 直下にハッシュ名（ASCII）で置いた WAV を渡して
// ファイル名側の制約を避ける。%TEMP% 自体とモデルパス（-m）は利用者の環境に委ねる
// （avply.toml が英数字のみのモデルパスを求める根拠）。
//
// 失敗（ffmpeg / whisper-cli の異常終了）は avply.log へ警告を残し、finished(false) で呼び出し側へ通知する。
// それまでに通知したキューは有効なまま残り、呼び出し側が破棄しない限り表示に使える。
// 再試行はしない。
class SubtitleTranscriber : public QObject {
    Q_OBJECT
public:
    struct Params {
        QString ffmpegPath;   // 音声抽出に使う ffmpeg.exe
        QString whisperPath;  // whisper-cli.exe
        QString modelPath;    // ggml モデル（.bin）
        QString language;     // whisper の -l に渡す言語コード（例：ja）
        QString cacheDir;     // SRT キャッシュの置き場（無ければ作る）
    };

    explicit SubtitleTranscriber(QObject* parent = nullptr);
    ~SubtitleTranscriber() override;

    // mediaPath の字幕生成を開始する
    // 実行中なら stop() で打ち切ってから始める。キャッシュ命中時はプロセスを起動せず、
    // この呼び出しの中で全キューの cueAdded を同期的に emit する
    void start(const Params& params, const QString& mediaPath);

    // 実行中のプロセスを止め、中間 WAV を削除する
    // 以後 cueAdded / finished は発火しない。実行中でなければ何もしない
    void stop();

    // メディアの部分ハッシュ（16 進小文字）を返す
    // ファイルサイズ + 先頭 1MiB + 末尾 1MiB の SHA-256。全体を読まないため数 GB でも一瞬で済む。
    // 同一サイズで中身だけ違う動画は実用上存在しないため、これで同一性判定とする。
    // 読み出しに失敗したら空文字を返す
    static QString mediaHash(const QString& path);

signals:
    // 字幕キューが 1 件確定したとき発火する（whisper-cli の逐次出力、またはキャッシュ復元）
    void cueAdded(const SubtitleCue& cue);

    // 生成が終了したとき発火する。ok=false は ffmpeg / whisper-cli の失敗
    // （ハッシュ計算失敗を含む）を示す。それまでに通知したキューは有効なまま残る。
    // キャッシュ命中時は start() の中で全 cueAdded に続けて同期的に emit する
    void finished(bool ok);

private:
    // ffmpeg で 16kHz モノラル WAV を抽出する。完了後 startWhisper へ進む
    void startExtract();

    // whisper-cli を起動し、標準出力を逐次パースする
    void startWhisper();

    // 標準出力の未処理バッファから完成した行を取り出してキューへ変換する
    void consumeStdout();

    // 実行中プロセスを解放する（disconnect → kill → 短時間 wait → deleteLater）
    // ~QProcess() の waitForFinished(30000) ブロックを避けるため親から切り離す
    void releaseProcess();

    // 生成の終了処理。WAV を削除する
    // 成功かつキューが 1 件以上のときだけキャッシュへ SRT を書く
    // 無音メディアは exit 0 でもキャッシュを作らず、次回も再認識する（空 SRT を残さないため）
    void finish(bool ok);

    Params        m_params;
    QString       m_mediaPath;
    QString       m_wavPath;
    QString       m_cachePath;
    QProcess*     m_proc = nullptr;
    QByteArray    m_stdoutBuf;
    SubtitleTrack m_track;
};
