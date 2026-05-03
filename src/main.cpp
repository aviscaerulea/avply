#include <QApplication>
#include <QStringList>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vcutter");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("vcutter");

    // コマンドライン第 1 引数があれば初期ファイルとして MainWindow に渡す
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」用）
    const QStringList args = app.arguments();
    const QString initialPath = (args.size() > 1) ? args[1] : QString();

    MainWindow win(initialPath);
    win.show();

    return app.exec();
}
