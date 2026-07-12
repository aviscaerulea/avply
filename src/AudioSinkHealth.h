#pragma once
#include <QAudioSink>

// QAudioSink の不健全判定（AudioWorker / SilenceTone 共用）
// StoppedState、または UnderrunError 以外のエラー状態を不健全とみなす。
// UnderrunError は供給遅延（pause 後のドレイン、サイレンストーンの供給間隔等）で
// 日常的に発生し sink 自体は健全なため除外する。sink が null の場合も不健全。
// 判定条件を両クラスへ複製すると片側だけの変更で乖離するため、ここへ集約する
inline bool isSinkUnhealthy(const QAudioSink* sink)
{
    return !sink
        || sink->state() == QAudio::StoppedState
        || (sink->error() != QAudio::NoError
            && sink->error() != QAudio::UnderrunError);
}
