#pragma once
#include <QString>
#include <vector>

// 字幕の 1 区間（キュー）
// 時刻はメディア先頭からのミリ秒。text は改行を含まない 1 行の文字列
struct SubtitleCue {
    qint64  startMs = 0;
    qint64  endMs   = 0;
    QString text;
};

// 字幕キューの集合と、その SRT 入出力・whisper-cli 出力行の解釈
// 外部プロセスに依存しない純粋なデータ型で、逐次追加と再生位置からのテキスト検索を担う。
// キューは開始時刻の昇順で保持する。whisper-cli は先頭から順に区間を吐くため append は
// 通常末尾追加になるが、順序が崩れた入力（SRT の手編集等）にも備えて挿入位置を探す
class SubtitleTrack {
public:
    // キューを開始時刻順の位置へ挿入する
    void append(const SubtitleCue& cue);

    void clear() { m_cues.clear(); }
    bool isEmpty() const { return m_cues.empty(); }
    int size() const { return static_cast<int>(m_cues.size()); }

    // 開始時刻順のキュー列
    const std::vector<SubtitleCue>& cues() const { return m_cues; }

    // 再生位置 ms に対応する字幕テキストを返す
    // startMs <= ms < endMs を満たすキューのうち開始時刻が最も遅いものを採用する。
    // 該当が無ければ空文字を返す（字幕未到達の区間・無音区間）
    QString textAt(qint64 ms) const;

    // SRT 形式（index 行、`HH:MM:SS,mmm --> HH:MM:SS,mmm` 行、本文、空行）へ直列化する
    // 改行コードは LF。末尾は空行で終える
    QString toSrt() const;

    // SRT 文字列からキューを復元する
    // 時刻行を持たないブロックは読み飛ばす。本文が複数行なら半角スペースで連結する。
    // CRLF / LF いずれの改行も受理する
    static SubtitleTrack fromSrt(const QString& srt);

    // whisper-cli の標準出力 1 行をキューへ変換する
    // 書式は `[HH:MM:SS.mmm --> HH:MM:SS.mmm]  本文` で、それ以外の行（進捗・空行）は
    // false を返して無視する。本文の前後空白は除去し、空本文の行も false とする
    static bool parseWhisperLine(const QString& line, SubtitleCue& out);

private:
    std::vector<SubtitleCue> m_cues;
};
