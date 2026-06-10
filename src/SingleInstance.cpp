// std::min / std::max と windows.h の min / max マクロが衝突しないよう
// 全 include より前に定義する
#define NOMINMAX
#include <windows.h>

#include "SingleInstance.h"
#include "MainWindow.h"
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QFileInfo>
#include <QDebug>

namespace {
// ユーザ識別子（ユーザディレクトリ末尾名）
// 複数ユーザ同居環境で他ユーザのプロセスと干渉しないよう、パイプ名・mutex 名に付与する
QString userName()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QFileInfo(home).fileName();
}

// パイプ名（ユーザスコープで一意）
// 仮に同名パイプを掴まれてもプロトコルマジックで弾く二重防護
QString pipeName()
{
    const QString user = userName();
    if (user.isEmpty()) return QStringLiteral("avply-ipc");
    return QStringLiteral("avply-ipc-") + user;
}

// mutex 名（ユーザスコープで一意、セッションローカル名前空間）
// primary 決定をパイプ接続成否でなくカーネルオブジェクトのアトミック性で行うための名前
QString mutexName()
{
    const QString user = userName();
    if (user.isEmpty()) return QStringLiteral("Local\\avply-single");
    return QStringLiteral("Local\\avply-single-") + user;
}

// プロトコルマジック
// 同名パイプを別アプリが listen している場合に「相手が avply か」を判別するための識別子。
// 送信側は magic + UTF-8 path + '\n' を送り、ack を受けて初めて「既存 avply あり」と判断する。
// 受信側は magic で始まらない、または改行が無いペイロードを破棄する
constexpr const char* kHelloMagic = "AVPLY1:";
constexpr const char* kAckMagic   = "AVPLY1-OK";

// ペイロード上限
// 受信バッファ無制限による OOM を避ける。実用上のフルパス長を十分に上回る値
constexpr int kMaxPayloadBytes = 65536;

// 各種タイムアウト
constexpr int kConnectTimeoutMs    = 500;
constexpr int kWriteTimeoutMs      = 1000;
constexpr int kAckTimeoutMs        = 1000;
constexpr int kServerRecvTimeoutMs = 2000;

// 転送リトライ
// primary は Qt 初期化 + MainWindow 構築後に listen を開始するため、
// その所要時間（通常 1 秒未満）を十分にカバーする上限とする
constexpr int kForwardRetryIntervalMs = 150;
constexpr int kForwardRetryTotalMs    = 5000;
} // namespace

bool SingleInstance::tryBecomePrimary()
{
    // CreateMutexW のアトミック性で primary を一意に決める
    // 同時複数起動でも新規作成に成功するのは 1 プロセスのみ。
    // ハンドルは意図的に保持し続ける（プロセス終了・クラッシュ時に OS が解放するため残留しない）。
    // 転送失敗時のフォールバック primary もこの参照を保持しており、
    // 以降の起動は ERROR_ALREADY_EXISTS でこのプロセスへ転送される
    const HANDLE mutex = CreateMutexW(nullptr, FALSE,
        reinterpret_cast<const wchar_t*>(mutexName().utf16()));
    // エラーコードは直後に退避する（以降の処理による上書きを防ぐ防御）
    const DWORD err = GetLastError();
    if (!mutex) {
        // 判定不能時は従来動作（パイプ接続成否のみ）へフォールバックする
        qWarning("SingleInstance: CreateMutexW failed (error=%lu)", err);
        return true;
    }
    return err != ERROR_ALREADY_EXISTS;
}

bool SingleInstance::forwardWithRetry(const QString& arg)
{
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        if (tryForwardAndExit(arg)) return true;
        if (timer.elapsed() >= kForwardRetryTotalMs) return false;
        Sleep(kForwardRetryIntervalMs);
    }
}

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

    // MainWindow への弱参照を構築する
    // 受信ソケットの readyRead や ack 送信待ちは MainWindow の寿命より長引く可能性があり、
    // 生ポインタを lambda にキャプチャすると MainWindow 破棄後の deliver 発火で
    // dangling 参照になる。QPointer は破棄後に自動で null 化される
    QPointer<MainWindow> winSafe(win);

    QObject::connect(server, &QLocalServer::newConnection, win, [server, winSafe]() {
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

        auto deliver = [winSafe, buf, delivered, recvTimer](QLocalSocket* s) {
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
            // 相手側は ack 受信で「avply に転送成功」と判定して exit する。
            // 通常経路は flush + disconnectFromServer による非同期ドレイン
            // （QLocalSocket::disconnectFromServer は書き込み中データ送出後に切断する仕様）。
            // flush が pending データを送り切れない場合のみ最大 50ms の同期待ちでドレインを試みる
            const QByteArray ack(kAckMagic);
            if (s->write(ack) != ack.size()) {
                // write 失敗時は ack 送れないため即時 abort
                // 後追跡のためログを残す（相手側 readAll は空となり「ack 失敗 → forward 失敗」になる）
                qWarning("SingleInstance: ack write failed (state=%d, error=%d)",
                         static_cast<int>(s->state()), static_cast<int>(s->error()));
                s->abort();
                return;
            }
            // flush 戻り値確認
            // Windows パイプの非同期書き込みが pending のまま disconnect すると相手側 readAll が空を返すため、
            // false なら短時間の同期待ちでドレインを試みる
            if (!s->flush()) {
                if (!s->waitForBytesWritten(50)) {
                    qWarning("SingleInstance: ack flush incomplete (state=%d, error=%d, pending=%lld)",
                             static_cast<int>(s->state()), static_cast<int>(s->error()),
                             s->bytesToWrite());
                }
            }
            s->disconnectFromServer();

            // MainWindow への呼び出しは GUI thread へキューイングする
            // functor 型 invokeMethod を使うことで MainWindow::loadFileFromIpc の
            // シグネチャ変更をコンパイル時に検知できる（string-based の "loadFileFromIpc" 解決は
            // メタオブジェクトに該当 slot がない場合 silent fail するため避ける）。
            // QueuedConnection 経由とすることで receiver 破棄後にディスパッチされる
            // invokeMethod イベントを Qt 側で安全に破棄させる
            if (winSafe) {
                QMetaObject::invokeMethod(winSafe.data(), [winSafe, path]() {
                    if (winSafe) winSafe->loadFileFromIpc(path);
                }, Qt::QueuedConnection);
            }
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
