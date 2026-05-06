#include "SingleInstance.h"
#include "MainWindow.h"
#include <QLocalServer>
#include <QLocalSocket>

namespace {
// パイプ名（ユーザセッション内で一意）
constexpr const char* kPipeName = "avply-ipc";

// 接続タイムアウト
constexpr int kConnectTimeoutMs = 500;
constexpr int kWriteTimeoutMs   = 1000;
} // namespace

bool SingleInstance::tryForwardAndExit(const QString& arg)
{
    QLocalSocket socket;
    socket.connectToServer(kPipeName);
    if (!socket.waitForConnected(kConnectTimeoutMs)) return false;

    // UTF-8 エンコードしたパスを送信する
    // 引数なし起動（payload 空）でも primary を前面化するため接続成功＝転送成功とみなす
    // 0 バイト書き込みでは waitForBytesWritten が false を返すため、空のときは書き込み確認を省く
    const QByteArray payload = arg.toUtf8();
    if (!payload.isEmpty()) {
        socket.write(payload);
        socket.flush();
        if (!socket.waitForBytesWritten(kWriteTimeoutMs)) return false;
    }
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::startServer(MainWindow* win, QObject* parent)
{
    auto* server = new QLocalServer(parent);

    // 前回プロセス異常終了時にパイプ名が残ったままだと listen が失敗するため、
    // 事前に removeServer で削除する（Qt 公式推奨パターン）
    QLocalServer::removeServer(kPipeName);
    if (!server->listen(kPipeName)) return;

    QObject::connect(server, &QLocalServer::newConnection, win, [server, win]() {
        QLocalSocket* socket = server->nextPendingConnection();
        if (!socket) return;

        // データ蓄積バッファ
        // newConnection 受信時点で既に bytesAvailable がある可能性があるため、
        // 接続フックの前に一度 readAll して取りこぼしを防ぐ
        auto* buf = new QByteArray;
        buf->append(socket->readAll());

        QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, buf]() {
            buf->append(socket->readAll());
        });

        // 接続断時に残データを取り込んで確定する
        // 送信側が短時間で write→disconnect する場合、disconnected と同時に
        // 未読バイトが残るケースがあるため readAll を再度呼ぶ
        QObject::connect(socket, &QLocalSocket::disconnected, win, [socket, win, buf]() {
            buf->append(socket->readAll());
            const QString path = QString::fromUtf8(*buf);
            delete buf;
            socket->deleteLater();
            win->loadFileFromIpc(path);
        });
    });
}
