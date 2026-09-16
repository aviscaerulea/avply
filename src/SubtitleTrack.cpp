#include "SubtitleTrack.h"
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace {

// `HH:MM:SS<sep>mmm` をミリ秒へ変換する
// sep は SRT 書式の `,`。時の桁数は 2 桁以上を許容する（99 時間超の長尺）
qint64 parseTimestamp(const QString& h, const QString& m, const QString& s, const QString& ms)
{
    return ((h.toLongLong() * 60 + m.toLongLong()) * 60 + s.toLongLong()) * 1000 + ms.toLongLong();
}

// ミリ秒を `HH:MM:SS,mmm`（SRT 書式）へ変換する
QString formatSrtTimestamp(qint64 ms)
{
    const qint64 totalSec = ms / 1000;
    return QString::asprintf("%02lld:%02lld:%02lld,%03lld",
                             totalSec / 3600, (totalSec / 60) % 60, totalSec % 60, ms % 1000);
}

// SRT の時刻行
// 例：`00:00:00,000 --> 00:00:05,120`
const QRegularExpression kSrtTimeRe(
    R"(^(\d{2,}):(\d{2}):(\d{2}),(\d{3}) --> (\d{2,}):(\d{2}):(\d{2}),(\d{3})\s*$)");

} // namespace

void SubtitleTrack::append(const SubtitleCue& cue)
{
    const auto pos = std::upper_bound(m_cues.begin(), m_cues.end(), cue,
        [](const SubtitleCue& a, const SubtitleCue& b) { return a.startMs < b.startMs; });
    m_cues.insert(pos, cue);
}

QString SubtitleTrack::textAt(qint64 ms) const
{
    // 開始時刻が ms 以下の最後のキューから遡り、ms を含む最初のキューを返す
    // whisper の区間はまれに数十 ms 重なるため、直近開始のキューを優先する
    auto it = std::upper_bound(m_cues.begin(), m_cues.end(), ms,
        [](qint64 v, const SubtitleCue& c) { return v < c.startMs; });
    while (it != m_cues.begin()) {
        --it;
        if (ms < it->endMs) return it->text;
        // 開始順で保持しているため、より前のキューは終了時刻も概ね前にある。
        // 重なりの範囲は短いので 1 つ前まで見れば十分だが、安全側に遡り続ける
    }
    return QString();
}

QString SubtitleTrack::toSrt() const
{
    QString out;
    int index = 1;
    for (const SubtitleCue& c : m_cues) {
        out += QString::number(index++) + '\n';
        out += formatSrtTimestamp(c.startMs) + " --> " + formatSrtTimestamp(c.endMs) + '\n';
        out += c.text + "\n\n";
    }
    return out;
}

SubtitleTrack SubtitleTrack::fromSrt(const QString& srt)
{
    SubtitleTrack track;
    QString normalized = srt;
    normalized.replace("\r\n", "\n");

    // 空行区切りのブロックごとに、時刻行とそれに続く本文行を取り出す
    const QStringList blocks = normalized.split("\n\n", Qt::SkipEmptyParts);
    for (const QString& block : blocks) {
        const QStringList lines = block.split('\n');
        int timeLine = -1;
        QRegularExpressionMatch m;
        for (int i = 0; i < lines.size(); ++i) {
            m = kSrtTimeRe.match(lines[i]);
            if (m.hasMatch()) { timeLine = i; break; }
        }
        if (timeLine < 0) continue;

        SubtitleCue cue;
        cue.startMs = parseTimestamp(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
        cue.endMs   = parseTimestamp(m.captured(5), m.captured(6), m.captured(7), m.captured(8));
        QStringList body;
        for (int i = timeLine + 1; i < lines.size(); ++i) {
            const QString t = lines[i].trimmed();
            if (!t.isEmpty()) body << t;
        }
        cue.text = body.join(' ');
        if (cue.text.isEmpty()) continue;
        track.append(cue);
    }
    return track;
}
