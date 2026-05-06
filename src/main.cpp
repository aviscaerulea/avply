#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QIcon>
#include <QStringList>
#include <QTimer>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    // 再生速度変更時に pitchCompensation を有効化するため
    // FFmpeg バックエンドを強制する（Media Foundation はピッチ保存非対応）
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

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

    return app.exec();
}
