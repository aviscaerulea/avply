#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vcutter");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("vcutter");

    MainWindow win;
    win.show();

    return app.exec();
}
