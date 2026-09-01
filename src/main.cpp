#include "mainwindow.h"
#include "retro_style.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Retro DCS Demo"));
    applyRetroStyle(app);
    QMainWindow *window = createMainWindow(app.arguments());
    window->show();
    const int result = app.exec();
    delete window;
    return result;
}
