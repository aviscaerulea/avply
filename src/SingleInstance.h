#pragma once
#include <QString>

class MainWindow;
class QObject;

// 単一インスタンス強制と引数転送（QLocalServer / QLocalSocket 利用）
// 「常にひとつのプレイヤーで再生する」設定が ON のときに使用する
namespace SingleInstance {

// 既存インスタンスへの引数転送を試みる
// 戻り値 true：既存に転送済み、呼び出し側は exit すべき
// 戻り値 false：既存なし、呼び出し側が primary
// arg は空文字でも構わない（その場合は接続のみで前面化要求のシグナルになる）
bool tryForwardAndExit(const QString& arg);

// primary プロセスとして IPC サーバを起動する
// 受信したパスを win->loadFileFromIpc() に転送する
// parent はサーバオブジェクトの寿命管理用（通常は MainWindow を渡す）
void startServer(MainWindow* win, QObject* parent);

} // namespace SingleInstance
