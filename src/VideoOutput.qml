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
    signal wheelScrolled(bool forward, bool shift)
    signal fileDropped(string url)

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
    }

    // DropArea は MouseArea より前に宣言する。
    // QML は後宣言ほど前面に配置するため、MouseArea を後にしてクリック・ホイールが
    // DropArea に吸収されないようにする
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.urls.length > 0) {
                root.fileDropped(drop.urls[0].toString())
            }
            drop.accept()
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
            root.wheelScrolled(wheel.angleDelta.y > 0, (wheel.modifiers & Qt.ShiftModifier) !== 0)
        }
    }
}
