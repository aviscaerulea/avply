import QtQuick
import QtMultimedia

// 動画レンダリング領域。QQuickView + threaded render loop で描画するため
// Win32 modal size/move loop（ウィンドウリサイズ中）でも再生が継続する。
// D&D は QML 側 DropArea で受ける。createWindowContainer の埋め込み HWND が
// 親 QWidget の dragEnter/drop へイベントを伝搬しないため、QWidget 側のオーバーライドでは
// プレビュー可視時にドロップを受け取れない
Item {
    id: root

    // C++ 側が QMediaPlayer.setVideoSink() に渡すシンク
    property alias videoSink: videoOutput.videoSink

    signal clicked()
    signal contextMenuRequested(real x, real y)
    signal wheelScrolled(bool forward, bool shift, bool ctrl)
    signal fileDropped(string url)

    // C++ 側（VideoView::setSubtitleText）が書き換える字幕テキスト。空なら非表示
    property string subtitleText: ""

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
    }

    // 字幕オーバーレイ
    // QQuickView はネイティブ子ウィンドウのため QWidget を上に重ねられない。
    // そのため字幕も QML 側で映像の上に描く。文字サイズは映像領域の高さに追従させ、
    // 幅は領域の 9 割で折り返す。マウス処理を持たないため後続の MouseArea を妨げない
    Rectangle {
        visible: root.subtitleText !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(parent.height * 0.05)
        width: subtitleLabel.width + 16
        height: subtitleLabel.height + 8
        radius: 4
        color: "#A0000000"

        Text {
            id: subtitleLabel
            anchors.centerIn: parent
            text: root.subtitleText
            color: "white"
            font.pixelSize: Math.max(14, Math.round(root.height / 22))
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            width: Math.min(implicitWidth, root.width * 0.9)
        }
    }

    // DropArea はドラッグ＆ドロップ系イベントのみを受け、MouseArea のマウス・ホイール
    // イベントとは処理対象が重ならないため、宣言順（重なり順）による干渉はない
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.urls.length > 0) {
                root.fileDropped(drop.urls[0].toString())
            }
            // 常にコピー扱いで受理する
            // 提案アクションのまま受理すると Shift ドラッグの MoveAction を返してしまい、
            // Explorer が「移動完了」と解釈して元ファイルを削除する
            drop.accept(Qt.CopyAction)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested(mouse.x, mouse.y)
            }
            else if (mouse.button === Qt.LeftButton) {
                root.clicked()
            }
        }
        onWheel: function(wheel) {
            // チルトホイールやタッチパッドの水平スクロール（縦成分ゼロ）を
            // 「後退」として誤発火させない（C++ 側の delta != 0 ガードと対称）
            if (wheel.angleDelta.y === 0) return
            root.wheelScrolled(
                wheel.angleDelta.y > 0,
                (wheel.modifiers & Qt.ShiftModifier) !== 0,
                (wheel.modifiers & Qt.ControlModifier) !== 0)
        }
    }
}
