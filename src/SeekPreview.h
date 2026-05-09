#pragma once
#include <QWidget>
#include <QPixmap>

class QLabel;

// MPC-HC 風シークバーホバー時のサムネイル + 時刻ポップアップ
// フレームレスのトップレベルウィジェット
// マウスイベントは透過させ、シーク操作を妨げない
class SeekPreview : public QWidget {
    Q_OBJECT
public:
    explicit SeekPreview(QWidget* parent = nullptr);

    // サムネイル + 時刻文字列をまとめて反映する
    // thumb が null の場合はサムネ領域を非表示にして時刻のみのコンパクト表示にする
    void setContent(const QPixmap& thumb, const QString& timeText);

    // 既存サムネイルを保ったまま時刻のみ更新する
    // ホバー位置追従中に ffmpeg 起動を待たずに時刻表示だけを高頻度更新するために使う
    void setTimeOnly(const QString& timeText);

    // シークバー上のグローバルカーソル位置を中心に、シークバー直上へ吸着して配置する
    // 画面端ではみ出す場合は左右にクランプ、上端ではみ出す場合はシークバー直下に回す
    void showAt(const QPoint& cursorGlobal,
                const QRect& sliderGlobal,
                const QRect& screenAvail);

private:
    QLabel* m_thumbLabel;
    QLabel* m_timeLabel;
};
