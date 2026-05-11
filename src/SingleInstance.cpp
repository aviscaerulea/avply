#include "SingleInstance.h"
#include "MainWindow.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QFileInfo>

namespace {
// パイプ名（ユーザスコープで一意）
// 複数ユーザ同居環境で他ユーザのプロセスへ接続が届くのを防ぐため、ユーザディレクトリ末尾名を付与する。
// 仮に同名パイプを掴まれてもプロトコルマジックで弾く二重防護
QString pipeName()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString user = QFileInfo(home).fileName();
    if (user.isEmpty()) return QStringLiteral("avply-ipc");
    return QStringLiteral("avply-ipc-") + user;
}

// プロトコルマジック
// 同名パイプを別アプリが listen している場合に「相手が avply か」を判別するための識別子。
// 送信側は magic + UTF-8 path + '\n' を送り、ack を受けて初めて「既存 avply あり」と判断する。
// 受信側は magic で始まらない、または改行が無いペイロードを破棄する
constexpr const char* kHelloMagic = "AVPLY1:";
constexpr const char* kAckMagic   = "AVPLY1-OK";

// ペイロード上限
// 受信バッファ無制限による OOM を避ける。Windows のフルパス上限（32KiB 程度）を十分に上回る値
constexpr int kMaxPayloadBytes = 65536;

// 各種タイムアウト
constexpr int kConnectTimeoutMs    = 500;
constexpr int kWriteTimeoutMs      = 1000;
constexpr int kAckTimeoutMs        = 1000;
constexpr int kServerRecvTimeoutMs = 2000;
} // namespace

bool SingleInstance::tryForwardAndExit(const QString& arg)
{
    QLocalSocket socket;
    socket.connectToServer(pipeName());
    if (!socket.waitForConnected(kConnectTimeoutMs)) return false;

    // 改行終端のペイロードを送信する
    // マジックバイト + UTF-8 エンコードした引数 + '\n'。引数なし起動でも改行終端は必須
    QByteArray payload;
    payload.append(kHelloMagic);
    payload.append(arg.toUtf8());
    payload.append('\n');
    socket.write(payload);
    if (!socket.waitForBytesWritten(kWriteTimeoutMs)) return false;

    // 既存 avply からの ack を待つ
    // ack を受け取れない場合、相手は別アプリの可能性があるため転送失敗扱いとする
    // 名前付きパイプでは kAckMagic 相当の短い ack は 1 回の read で届くため、ループ不要
    if (!socket.waitForReadyRead(kAckTimeoutMs)) return false;
    if (!socket.readAll().startsWith(kAckMagic)) return false;

    socket.disconnectFromServer();
    return true;
}

void SingleInstance::startServer(MainWindow* win, QObject* parent)
{
    auto* server = new QLocalServer(parent);

    // 前回プロセス異常終了時にパイプ名が残ったままだと listen が失敗するため、
    // 事前に removeServer で削除する（Qt 公式推奨パターン）
    QLocalServer::removeServer(pipeName());
    if (!server->listen(pipeName())) return;

    QObject::connect(server, &QLocalServer::newConnection, win, [server, win]() {
        QLocalSocket* socket = server->nextPendingConnection();
        if (!socket) return;

        // 切断と同時にソケットを破棄する
        // disconnected と errorOccurred のいずれの経路でも確実に deleteLater を通すことで
        // ハンドル・メモリのリークを防ぐ
        QObject::connect(socket, &QLocalSocket::disconnected,
                         socket, &QLocalSocket::deleteLater);
        QObject::connect(socket, &QLocalSocket::errorOccurred,
                         socket, &QLocalSocket::deleteLater);

        // 受信バッファとペイロード確定フラグをソケット寿命に紐付ける
        // QSharedPointer のキャプチャを使い、socket destroyed と同時に解放させる
        auto buf       = QSharedPointer<QByteArray>::create();
        auto delivered = QSharedPointer<bool>::create(false);

        // 不正クライアント（マジックも改行も送ってこない接続）に対するタイムアウト
        // 未確定のまま放置されてもサーバ側で打ち切る
        auto* recvTimer = new QTimer(socket);
        recvTimer->setSingleShot(true);
        recvTimer->setInterval(kServerRecvTimeoutMs);

        auto deliver = [win, buf, delivered, recvTimer](QLocalSocket* s) {
            if (*delivered) return;
            *delivered = true;
            recvTimer->stop();

            const QByteArray magic(kHelloMagic);
            const int nl = buf->indexOf('\n');
            if (nl < 0 || !buf->startsWith(magic)) {
                // 不正な相手（同名パイプを listen している別アプリ等）
                // ack を返さず即時切断する
                s->abort();
                return;
            }

            const QString path = QString::fromUtf8(
                buf->mid(magic.size(), nl - magic.size()));

            // ack を返してから loadFileFromIpc を呼ぶ
            // 相手側は ack 受信で「avply に転送成功」と判定して exit する
            s->write(kAckMagic);
            s->flush();
            s->waitForBytesWritten(kWriteTimeoutMs);
            s->disconnectFromServer();

            win->loadFileFromIpc(path);
        };

        // 受信：改行検出または上限超過で確定
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, buf, deliver]() {
            buf->append(socket->readAll());
            if (buf->size() > kMaxPayloadBytes) {
                socket->abort();
                return;
            }
            if (buf->contains('\n')) deliver(socket);
        });

        // 受信タイムアウト：マジック・改行が来ないまま放置されたら切断
        QObject::connect(recvTimer, &QTimer::timeout, socket, [socket]() {
            socket->abort();
        });
        recvTimer->start();

        // 起動直後に既にデータが到着しているケース
        // newConnection 受信時点で readyRead が発火済みなら今後 readyRead は来ない
        if (socket->bytesAvailable() > 0) {
            buf->append(socket->readAll());
            if (buf->size() > kMaxPayloadBytes) {
                socket->abort();
                return;
            }
            if (buf->contains('\n')) deliver(socket);
        }
    });
}
