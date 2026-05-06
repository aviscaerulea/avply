// std::min / std::max と windows.h の min / max マクロが衝突しないよう
// 全 include より前に定義する
#define NOMINMAX
#include <windows.h>

#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QStringList>
#include <QTimer>
#include "MainWindow.h"
#include "Settings.h"
#include "SingleInstance.h"

int main(int argc, char* argv[])
{
    // 再生速度変更時に pitchCompensation を有効化するため
    // FFmpeg バックエンドを強制する（Media Foundation はピッチ保存非対応）
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    // 単一インスタンス強制が ON のとき、自身が 2 個目以降なら引数を既存へ転送して即時終了する
    // QApplication 構築前に判定することで、不要な GUI 初期化を避ける
    const bool singleInstanceEnabled = Settings::instance().singleInstance();
    QString preliminaryArg;
    if (argc > 1) preliminaryArg = QString::fromLocal8Bit(argv[1]);

    if (singleInstanceEnabled) {
        if (SingleInstance::tryForwardAndExit(preliminaryArg)) return 0;
    }

    // プロセス優先度設定（レジストリ値が ON のときのみ ABOVE_NORMAL）
    if (Settings::instance().aboveNormalPriority()) {
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }

    QApplication app(argc, argv);

    // ウィンドウアイコンを設定する
    // QRC 経由のアイコンは実行時のタイトルバー・タスクバー・Alt+Tab 表示用。
    // EXE シェル表示（エクスプローラ等）は src/app.rc で別途埋め込み済み。
    // setWindowIcon は QApplication 構築直後に呼び、起動初期のダイアログにも反映させる。
    QIcon appIcon(":/icons/app.ico");
    if (appIcon.isNull()) {
        qWarning("ウィンドウアイコンのロードに失敗しました：:/icons/app.ico");
    }
    app.setWindowIcon(appIcon);

    app.setApplicationName("avply");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("avply");

    // コマンドライン第 1 引数があれば初期ファイルとして MainWindow に渡す
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」用）
    const QStringList args = app.arguments();
    const QString initialPath = (args.size() > 1) ? args[1] : QString();

    // 起動時の白フラッシュ抑制
    // Windows のネイティブウィンドウ作成直後に発生する WM_ERASEBKGND による白塗りは
    // Qt 側の背景属性では抑止しきれないため、最初の paint が終わるまでウィンドウを
    // 透明化して視覚的に隠す。次のイベントループで不透明に戻すと、その時点では既に
    // VideoView の暗色背景および UI が描画済みのためフラッシュは見えない
    MainWindow win(initialPath);
    win.setWindowOpacity(0.0);
    win.show();
    QTimer::singleShot(0, &win, [&win]() {
        win.setWindowOpacity(1.0);
    });

    // 単一インスタンスが ON のときは primary として IPC サーバを起動し、
    // 後続の起動から送られてくるファイルパスを受信する
    if (singleInstanceEnabled) {
        SingleInstance::startServer(&win, &win);
    }

    return app.exec();
}
