#include "TclConsoleWindow.h"

#include <QCoreApplication>
#include <QDir>
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

    // Build interpreter console UI.
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    m_output->setReadOnly(true);
    m_output->setPlaceholderText("Tcl console output...");
    m_input->setPlaceholderText("Enter Tcl command and press Enter");
    layout->addWidget(m_output);
    layout->addWidget(m_input);
    setCentralWidget(central);

    // Register command families exposed to Tcl.
    Tcl_CreateObjCommand(m_interp, "layer", &TclConsoleWindow::LayerCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "tool", &TclConsoleWindow::ToolCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "canvas", &TclConsoleWindow::CanvasCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "view", &TclConsoleWindow::ViewCommandBridge, this, nullptr);

    // Manual command entry from console input line.
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        const QString command = m_input->text().trimmed();
        if (!command.isEmpty()) {
            executeCommand(command);
        }
        m_input->clear();
    });

    // GUI interactions from child editor route through the same execute path.
    connect(m_editorWindow, &LayoutEditorWindow::commandRequested,
            this, &TclConsoleWindow::executeCommand);

    // Model->view synchronization wiring.
    connect(&m_layerManager, &LayerManager::layersReset,
            m_editorWindow, &LayoutEditorWindow::setLayers);
    connect(&m_layerManager, &LayerManager::layerChanged,
            m_editorWindow, &LayoutEditorWindow::onLayerChanged);
    connect(&m_layerManager, &LayerManager::activeLayerChanged,
            m_editorWindow, &LayoutEditorWindow::onActiveLayerChanged);

    m_editorWindow->show();

    // Startup script bootstraps initial palette/tool config.
    appendTranscript("Interpreter ready. Loading init.tcl...");
    executeCommand("source init.tcl");
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
    return static_cast<TclConsoleWindow*>(clientData)->handleLayerCommand(interp, objc, objv);
}

int TclConsoleWindow::ToolCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleToolCommand(interp, objc, objv);
}

int TclConsoleWindow::CanvasCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleCanvasCommand(interp, objc, objv);
}

int TclConsoleWindow::ViewCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleViewCommand(interp, objc, objv);
}

bool TclConsoleWindow::parseInt64(Tcl_Interp* interp, Tcl_Obj* obj, qint64& value, const char* fieldName) {
    Tcl_WideInt raw = 0;
    if (Tcl_GetWideIntFromObj(interp, obj, &raw) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("invalid %1").arg(fieldName).toUtf8().constData(), -1));
        return false;
    }

    value = static_cast<qint64>(raw);
    return true;
}

bool TclConsoleWindow::parseDouble(Tcl_Interp* interp, Tcl_Obj* obj, double& value, const char* fieldName) {
    if (Tcl_GetDoubleFromObj(interp, obj, &value) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("invalid %1").arg(fieldName).toUtf8().constData(), -1));
        return false;
    }

    return true;
}

int TclConsoleWindow::handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: layer <list|load|configure|active> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));

    if (subCommand == "list") {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(m_layerManager.serializeLayers().toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "load") {
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: layer load <filePath>"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString rawPath = QString::fromUtf8(Tcl_GetString(objv[2]));
        const QString path = QDir::isAbsolutePath(rawPath)
                                 ? rawPath
                                 : QDir(QCoreApplication::applicationDirPath()).filePath(rawPath);

        QString error;
        if (!m_layerManager.loadLayersFromFile(path, error)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("loaded %1 layers from %2").arg(m_layerManager.layers().size()).arg(path).toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "active") {
        if (objc == 2) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(m_layerManager.activeLayer().toUtf8().constData(), -1));
            return TCL_OK;
        }
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: layer active ?name?"), TCL_STATIC);
            return TCL_ERROR;
        }

        QString error;
        const QString layerName = QString::fromUtf8(Tcl_GetString(objv[2]));
        if (!m_layerManager.setActiveLayer(layerName, error)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("active layer: %1").arg(m_layerManager.activeLayer()).toUtf8().constData(), -1));
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
        if (!m_layerManager.configureLayer(layerName, option, numeric == 1, error)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("layer %1 updated: %2=%3").arg(layerName, option).arg(numeric).toUtf8().constData(), -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown layer subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleToolCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc != 3 || QString::fromUtf8(Tcl_GetString(objv[1])) != "set") {
        Tcl_SetResult(interp, const_cast<char*>("usage: tool set <name>"), TCL_STATIC);
        return TCL_ERROR;
    }

    m_activeTool = QString::fromUtf8(Tcl_GetString(objv[2]));
    m_editorWindow->onToolChanged(m_activeTool);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("tool: %1").arg(m_activeTool).toUtf8().constData(), -1));
    return TCL_OK;
}

int TclConsoleWindow::handleCanvasCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 5) {
        Tcl_SetResult(interp, const_cast<char*>("usage: canvas <press|move|release> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString sub = QString::fromUtf8(Tcl_GetString(objv[1]));

    qint64 x = 0;
    qint64 y = 0;
    if (!parseInt64(interp, objv[2], x, "x") || !parseInt64(interp, objv[3], y, "y")) {
        return TCL_ERROR;
    }

    if (sub == "press") {
        int button = 0;
        if (Tcl_GetIntFromObj(interp, objv[4], &button) != TCL_OK) {
            Tcl_SetResult(interp, const_cast<char*>("invalid button"), TCL_STATIC);
            return TCL_ERROR;
        }

        // Rectangle tool starts preview only for left button and valid active layer.
        if (button == 1 && m_activeTool == "rect" && !m_layerManager.activeLayer().isEmpty()) {
            m_rectInProgress = true;

            LayerDefinition active;
            QString error;
            if (!m_layerManager.layerByName(m_layerManager.activeLayer(), active, error)) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
                return TCL_ERROR;
            }

            m_previewRectangle = {active.name, active.color, x, y, x, y};
            m_editorWindow->onRectanglePreviewChanged(true, m_previewRectangle);
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    if (sub == "move") {
        int leftDown = 0;
        if (Tcl_GetIntFromObj(interp, objv[4], &leftDown) != TCL_OK) {
            Tcl_SetResult(interp, const_cast<char*>("invalid leftDown"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (m_rectInProgress && leftDown == 1) {
            m_previewRectangle.x2 = x;
            m_previewRectangle.y2 = y;
            m_editorWindow->onRectanglePreviewChanged(true, m_previewRectangle);
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    if (sub == "release") {
        int button = 0;
        if (Tcl_GetIntFromObj(interp, objv[4], &button) != TCL_OK) {
            Tcl_SetResult(interp, const_cast<char*>("invalid button"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (button == 1 && m_rectInProgress) {
            m_previewRectangle.x2 = x;
            m_previewRectangle.y2 = y;
            m_editorWindow->onRectangleCommitted(m_previewRectangle);
            m_editorWindow->onRectanglePreviewChanged(false, m_previewRectangle);
            m_rectInProgress = false;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown canvas subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleViewCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: view <zoom|pan> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString sub = QString::fromUtf8(Tcl_GetString(objv[1]));

    if (sub == "pan") {
        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: view pan <dx> <dy>"), TCL_STATIC);
            return TCL_ERROR;
        }

        double dx = 0.0;
        double dy = 0.0;
        if (!parseDouble(interp, objv[2], dx, "dx") || !parseDouble(interp, objv[3], dy, "dy")) {
            return TCL_ERROR;
        }

        m_panX += dx;
        m_panY += dy;
        m_editorWindow->onViewChanged(m_zoom, m_panX, m_panY);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    if (sub == "zoom") {
        if (objc != 5) {
            Tcl_SetResult(interp, const_cast<char*>("usage: view zoom <wheelDelta> <anchorX> <anchorY>"), TCL_STATIC);
            return TCL_ERROR;
        }

        double wheelDelta = 0.0;
        double anchorX = 0.0;
        double anchorY = 0.0;
        if (!parseDouble(interp, objv[2], wheelDelta, "wheelDelta") ||
            !parseDouble(interp, objv[3], anchorX, "anchorX") ||
            !parseDouble(interp, objv[4], anchorY, "anchorY")) {
            return TCL_ERROR;
        }

        // Incremental zoom with bounds to avoid singular/huge transforms.
        const double factor = wheelDelta > 0 ? 1.15 : 1.0 / 1.15;
        const double oldZoom = m_zoom;
        m_zoom *= factor;
        if (m_zoom < 0.05) {
            m_zoom = 0.05;
        }
        if (m_zoom > 200.0) {
            m_zoom = 200.0;
        }

        // Anchor-preserving zoom: keep anchor point fixed on screen.
        const double worldX = (anchorX - m_panX) / oldZoom;
        const double worldY = (anchorY - m_panY) / oldZoom;
        m_panX = anchorX - (worldX * m_zoom);
        m_panY = anchorY - (worldY * m_zoom);

        m_editorWindow->onViewChanged(m_zoom, m_panX, m_panY);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown view subcommand"), TCL_STATIC);
    return TCL_ERROR;
}
