#include <QApplication>

#include "TclConsoleWindow.h"

// Application entry point.
//
// This binary launches a Qt event loop and creates the Tcl interpreter window,
// which in turn opens the child layout editor window.
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // The interpreter window owns the primary command/control surface.
    TclConsoleWindow window;
    window.show();

    // Run until the user closes all top-level windows.
    return app.exec();
}
