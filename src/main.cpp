#include <QApplication>
#include <QByteArray>
#include <QStringList>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    // 再生速度変更時に pitchCompensation を有効化するため
    // FFmpeg バックエンドを強制する（Media Foundation はピッチ保存非対応）
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    QApplication app(argc, argv);
    app.setApplicationName("avply");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("avply");

    // コマンドライン第 1 引数があれば初期ファイルとして MainWindow に渡す
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」用）
    const QStringList args = app.arguments();
    const QString initialPath = (args.size() > 1) ? args[1] : QString();

    MainWindow win(initialPath);
    win.show();

    return app.exec();
}
