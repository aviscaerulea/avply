import QtQuick
import QtMultimedia

// 動画レンダリング領域。QQuickView + threaded render loop で描画するため
// Win32 modal size/move loop（ウィンドウリサイズ中）でも再生が継続する。
// D&D は createWindowContainer 経由で QQuickView を埋め込んだ QWidget 側
// （VideoView::dragEnterEvent / dropEvent）で受けるため、QML 側に DropArea は置かない
Item {
    id: root

    // C++ 側が QMediaPlayer.setVideoSink() に渡すシンク
    property alias videoSink: videoOutput.videoSink

    signal clicked()
    signal contextMenuRequested(real x, real y)
    signal wheelScrolled(bool forward)

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
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
            root.wheelScrolled(wheel.angleDelta.y > 0)
        }
    }
}
