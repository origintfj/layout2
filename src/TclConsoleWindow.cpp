#include "TclConsoleWindow.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

TclConsoleWindow::TclConsoleWindow(QWidget* parent)
    : QMainWindow(parent),
      m_output(new QPlainTextEdit(this)),
      m_input(new QLineEdit(this)),
      m_interp(Tcl_CreateInterp()),
      m_editorWindow(new LayoutEditorWindow(this)) {
    setWindowTitle("Tcl Interpreter");
    resize(900, 450);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    m_output->setReadOnly(true);
    m_output->setPlaceholderText("Tcl console output...");
    m_input->setPlaceholderText("Enter Tcl command and press Enter");

    layout->addWidget(m_output);
    layout->addWidget(m_input);
    setCentralWidget(central);

    Tcl_CreateObjCommand(m_interp, "layer", &TclConsoleWindow::LayerCommandBridge, this, nullptr);

    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        const QString command = m_input->text().trimmed();
        if (!command.isEmpty()) {
            executeCommand(command);
        }
        m_input->clear();
    });

    connect(m_editorWindow, &LayoutEditorWindow::commandRequested,
            this, &TclConsoleWindow::executeCommand);

    connect(&m_layerManager, &LayerManager::layerChanged,
            m_editorWindow, &LayoutEditorWindow::onLayerChanged);

    m_editorWindow->setLayers(m_layerManager.layers());
    m_editorWindow->show();

    appendTranscript("Interpreter ready. Try: layer list");
}

TclConsoleWindow::~TclConsoleWindow() {
    if (m_interp) {
        Tcl_DeleteInterp(m_interp);
        m_interp = nullptr;
    }
}

void TclConsoleWindow::appendTranscript(const QString& line) {
    m_output->appendPlainText(line);
}

void TclConsoleWindow::executeCommand(const QString& command) {
    appendTranscript(QString("> %1").arg(command));

    const QByteArray utf8 = command.toUtf8();
    const int rc = Tcl_Eval(m_interp, utf8.constData());
    const QString result = QString::fromUtf8(Tcl_GetStringResult(m_interp));

    if (!result.isEmpty()) {
        appendTranscript(result);
    }

    if (rc != TCL_OK) {
        appendTranscript(QString("ERROR (%1)").arg(rc));
    }
}

int TclConsoleWindow::LayerCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* self = static_cast<TclConsoleWindow*>(clientData);
    return self->handleLayerCommand(interp, objc, objv);
}

int TclConsoleWindow::handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: layer <list|configure> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));

    if (subCommand == "list") {
        const QString listing = m_layerManager.serializeLayers();
        Tcl_SetObjResult(interp, Tcl_NewStringObj(listing.toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "configure") {
        if (objc != 5) {
            Tcl_SetResult(interp,
                          const_cast<char*>("usage: layer configure <name> <-visible|-selectable> <0|1>"),
                          TCL_STATIC);
            return TCL_ERROR;
        }

        const QString layerName = QString::fromUtf8(Tcl_GetString(objv[2]));
        const QString option = QString::fromUtf8(Tcl_GetString(objv[3]));
        const QString valueRaw = QString::fromUtf8(Tcl_GetString(objv[4]));

        bool ok = false;
        const int numeric = valueRaw.toInt(&ok);
        if (!ok || (numeric != 0 && numeric != 1)) {
            Tcl_SetResult(interp, const_cast<char*>("value must be 0 or 1"), TCL_STATIC);
            return TCL_ERROR;
        }

        QString error;
        const bool changed = m_layerManager.configureLayer(layerName, option, numeric == 1, error);
        if (!changed) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        const QString message = QString("layer %1 updated: %2=%3").arg(layerName, option).arg(numeric);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(message.toUtf8().constData(), -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown subcommand (expected list or configure)"), TCL_STATIC);
    return TCL_ERROR;
}
