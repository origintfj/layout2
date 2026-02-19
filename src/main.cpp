#include <QApplication>

#include "TclConsoleWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    TclConsoleWindow window;
    window.show();

    return app.exec();
}
