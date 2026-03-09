#include "TclConsoleWindow.h"

#include "LayoutSceneModel.h"

#include <QCoreApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

TclConsoleWindow::TclConsoleWindow(QWidget* parent)
    : QMainWindow(parent),
      m_output(new QPlainTextEdit(this)),
      m_input(new QLineEdit(this)),
      m_interp(Tcl_CreateInterp()) {
    setWindowTitle("Tcl Interpreter");
    resize(900, 450);

    // Build interpreter console UI.
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    m_output->setReadOnly(true);
    m_output->setPlaceholderText("Tcl console output...");
    m_input->setPlaceholderText("Enter Tcl command and press Enter");

    const QFont consoleFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output->setFont(consoleFont);
    m_input->setFont(consoleFont);
    m_input->installEventFilter(this);
    layout->addWidget(m_output);
    layout->addWidget(m_input);
    setCentralWidget(central);

    // Register command families exposed to Tcl.
    Tcl_CreateObjCommand(m_interp, "layer", &TclConsoleWindow::LayerCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "tool", &TclConsoleWindow::ToolCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "canvas", &TclConsoleWindow::CanvasCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "view", &TclConsoleWindow::ViewCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "bindkey", &TclConsoleWindow::BindKeyCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "transcript", &TclConsoleWindow::TranscriptCommandBridge, this, nullptr);
    Tcl_CreateObjCommand(m_interp, "app", &TclConsoleWindow::AppCommandBridge, this, nullptr);

    auto* fileMenu = menuBar()->addMenu("File");
    auto* exitAction = fileMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, [this]() {
        executeCommand("app exit");
    });

    auto* toolsMenu = menuBar()->addMenu("Tools");
    auto* layoutEditorAction = toolsMenu->addAction("Layout Editor");
    connect(layoutEditorAction, &QAction::triggered, this, [this]() {
        executeCommand("app layout_editor");
    });

    // Manual command entry from console input line.
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        const QString command = m_input->text().trimmed();
        if (!command.isEmpty()) {
            pushHistoryCommand(command);
            executeCommand(command);
        }
        m_input->clear();
    });

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

void TclConsoleWindow::closeEvent(QCloseEvent* event) {
    // Ensure auxiliary editor windows are explicitly torn down when the
    // interpreter window is closed.
    QVector<LayoutEditorWindow*> windows;
    windows.reserve(m_sessionController.sessions().size());
    for (auto it = m_sessionController.sessions().cbegin(); it != m_sessionController.sessions().cend(); ++it) {
        if (it.value().window) {
            windows.push_back(it.value().window);
        }
    }

    for (LayoutEditorWindow* window : windows) {
        window->close();
    }

    QMainWindow::closeEvent(event);
}

void TclConsoleWindow::appendTranscript(const QString& line) {
    m_output->appendPlainText(line);
}

bool TclConsoleWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            navigateHistory(-1);
            return true;
        }

        if (keyEvent->key() == Qt::Key_Down) {
            navigateHistory(1);
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void TclConsoleWindow::pushHistoryCommand(const QString& command) {
    m_commandHistory.push_back(command);
    m_historyIndex = m_commandHistory.size();
    m_inProgressInput.clear();
}

void TclConsoleWindow::navigateHistory(const int direction) {
    if (m_commandHistory.isEmpty()) {
        return;
    }

    if (m_historyIndex < 0 || m_historyIndex > m_commandHistory.size()) {
        m_historyIndex = m_commandHistory.size();
    }

    if (m_historyIndex == m_commandHistory.size()) {
        m_inProgressInput = m_input->text();
    }

    const int nextIndex = m_historyIndex + direction;
    if (nextIndex < 0) {
        return;
    }

    if (nextIndex > m_commandHistory.size()) {
        return;
    }

    m_historyIndex = nextIndex;
    if (m_historyIndex == m_commandHistory.size()) {
        m_input->setText(m_inProgressInput);
    } else {
        m_input->setText(m_commandHistory.at(m_historyIndex));
    }

    m_input->setCursorPosition(m_input->text().size());
}

int TclConsoleWindow::evaluateCommand(const QString& command,
                                      const bool echoCommand,
                                      const bool echoResult,
                                      const bool echoErrorLine,
                                      const bool applyTranscriptFilters) {
    const bool suppressEcho = applyTranscriptFilters && shouldSuppressTranscriptCommand(command);
    if (echoCommand && !suppressEcho) {
        appendTranscript(QString("> %1").arg(command));
    }

    const QByteArray utf8 = command.toUtf8();
    const int rc = Tcl_Eval(m_interp, utf8.constData());
    const QString result = QString::fromUtf8(Tcl_GetStringResult(m_interp));

    if (echoResult && !result.isEmpty() && (!suppressEcho || rc != TCL_OK)) {
        appendTranscript(result);
    }

    if (echoErrorLine && rc != TCL_OK) {
        appendTranscript(QString("ERROR (%1)").arg(rc));
    }

    return rc;
}

int TclConsoleWindow::evaluateCommandForEditor(const QString& command,
                                               const bool echoCommand,
                                               const bool echoResult,
                                               const bool echoErrorLine,
                                               const int editorId,
                                               const bool requestActivation) {
    const int previousCommandEditorId = m_sessionController.commandEditorId();
    m_sessionController.setCommandEditorId(editorId);
    if (requestActivation && editorId > 0) {
        const QString activateCommand = QString("app editor active %1").arg(editorId);
        (void)evaluateCommand(activateCommand, true, false, false);
    }

    const int rc = evaluateCommand(command, echoCommand, echoResult, echoErrorLine);
    m_sessionController.setCommandEditorId(previousCommandEditorId);
    return rc;
}

void TclConsoleWindow::executeCommand(const QString& command) {
    (void)evaluateCommand(command, true, true, true, false);
}

void TclConsoleWindow::executeEditorCommand(const int editorId, const QString& command, const bool requestActivation) {
    (void)evaluateCommandForEditor(command, true, true, true, editorId, requestActivation);
}

EditorSession* TclConsoleWindow::sessionById(const int editorId) {
    return m_sessionController.sessionById(editorId);
}

const EditorSession* TclConsoleWindow::sessionById(const int editorId) const {
    return m_sessionController.sessionById(editorId);
}

EditorSession* TclConsoleWindow::activeSession() {
    return m_sessionController.activeSession();
}

const EditorSession* TclConsoleWindow::activeSession() const {
    return m_sessionController.activeSession();
}

EditorSession* TclConsoleWindow::effectiveSession() {
    return m_sessionController.effectiveSession();
}

void TclConsoleWindow::initializeSessionLayers(EditorSession& session) {
    m_sessionController.initializeSessionLayers(session, m_layerManager.layers());
}

void TclConsoleWindow::applySessionToWindow(EditorSession& session) {
    m_sessionController.applySessionToWindow(session);
}

int TclConsoleWindow::createEditorSession(const bool activate) {
    auto* window = new LayoutEditorWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose, true);

    const int editorId = m_sessionController.createSession(window);
    EditorSession* session = sessionById(editorId);
    if (!session) {
        return 0;
    }

    initializeSessionLayers(*session);
    session->activeTool = m_defaultTool;

    connect(window, &LayoutEditorWindow::commandRequested,
            this, [this, editorId](const QString& command, const bool requestActivation) {
                executeEditorCommand(editorId, command, requestActivation);
            });

    connect(window, &LayoutEditorWindow::activationRequested,
            this, [this, editorId]() {
                setActiveEditor(editorId);
            });

    connect(window, &QObject::destroyed, this, [this, editorId]() {
        m_sessionController.removeSession(editorId);
        refreshEditorWindowTitles();
    });

    applySessionToWindow(*session);
    session->window->show();
    session->window->raise();
    session->window->activateWindow();

    if (activate) {
        setActiveEditor(editorId);
    } else {
        refreshEditorWindowTitles();
    }

    return editorId;
}

void TclConsoleWindow::setActiveEditor(const int editorId) {
    m_sessionController.setActiveEditor(editorId);
    refreshEditorWindowTitles();
}

void TclConsoleWindow::refreshEditorWindowTitles() {
    const int activeEditorId = m_sessionController.activeEditorId();
    for (auto it = m_sessionController.sessions().begin(); it != m_sessionController.sessions().end(); ++it) {
        if (!it.value().window) {
            continue;
        }

        it.value().window->setEditorIdentity(it.key(), it.key() == activeEditorId);
    }
}

bool TclConsoleWindow::shouldSuppressTranscriptCommand(const QString& command) const {
    for (const QString& pattern : m_transcriptFilters) {
        const QRegularExpression regex(
            QRegularExpression::wildcardToRegularExpression(pattern),
            QRegularExpression::CaseInsensitiveOption);
        if (regex.match(command).hasMatch()) {
            return true;
        }
    }

    return false;
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

int TclConsoleWindow::BindKeyCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleBindKeyCommand(interp, objc, objv);
}

int TclConsoleWindow::TranscriptCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleTranscriptCommand(interp, objc, objv);
}

int TclConsoleWindow::AppCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_cast<TclConsoleWindow*>(clientData)->handleAppCommand(interp, objc, objv);
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

int TclConsoleWindow::handleTranscriptCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 3 || QString::fromUtf8(Tcl_GetString(objv[1])) != "filter") {
        Tcl_SetResult(interp, const_cast<char*>("usage: transcript filter <add|remove|list|clear> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[2]));

    if (subCommand == "add") {
        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: transcript filter add <globPattern>"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString pattern = QString::fromUtf8(Tcl_GetString(objv[3]));
        if (pattern.isEmpty()) {
            Tcl_SetResult(interp, const_cast<char*>("pattern must not be empty"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (!m_transcriptFilters.contains(pattern, Qt::CaseInsensitive)) {
            m_transcriptFilters.push_back(pattern);
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("added transcript filter: %1").arg(pattern).toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "remove") {
        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: transcript filter remove <globPattern>"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString pattern = QString::fromUtf8(Tcl_GetString(objv[3]));
        const int removed = m_transcriptFilters.removeAll(pattern);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(removed));
        return TCL_OK;
    }

    if (subCommand == "list") {
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: transcript filter list"), TCL_STATIC);
            return TCL_ERROR;
        }

        Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
        for (const QString& pattern : m_transcriptFilters) {
            Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(pattern.toUtf8().constData(), -1));
        }
        Tcl_SetObjResult(interp, list);
        return TCL_OK;
    }

    if (subCommand == "clear") {
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: transcript filter clear"), TCL_STATIC);
            return TCL_ERROR;
        }

        m_transcriptFilters.clear();
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown transcript filter subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleAppCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: app <exit|layout_editor|editor ...>"), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));

    if (subCommand == "exit") {
        close();
        Tcl_SetObjResult(interp, Tcl_NewStringObj("closing", -1));
        return TCL_OK;
    }

    if (subCommand == "layout_editor") {
        if (objc != 2) {
            Tcl_SetResult(interp, const_cast<char*>("usage: app layout_editor"), TCL_STATIC);
            return TCL_ERROR;
        }

        const int editorId = createEditorSession(true);
        EditorSession* session = sessionById(editorId);
        if (session && session->window) {
            // Center the world origin in the canvas whenever an editor is opened.
            const QSize viewport = session->window->canvasViewportSize();
            if (!viewport.isEmpty()) {
                session->panX = viewport.width() / 2.0;
                session->panY = viewport.height() / 2.0;
                session->window->onViewChanged(session->zoom, session->panX, session->panY, session->gridSize);
            }
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("layout editor %1 shown").arg(editorId).toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "editor") {
        if (objc < 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: app editor active ?editorId?"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString editorSubCommand = QString::fromUtf8(Tcl_GetString(objv[2]));
        if (editorSubCommand != "active") {
            Tcl_SetResult(interp, const_cast<char*>("unknown app editor subcommand"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (objc == 3) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(m_sessionController.activeEditorId()));
            return TCL_OK;
        }

        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: app editor active ?editorId?"), TCL_STATIC);
            return TCL_ERROR;
        }

        int editorId = 0;
        if (Tcl_GetIntFromObj(interp, objv[3], &editorId) != TCL_OK) {
            Tcl_SetResult(interp, const_cast<char*>("invalid editorId"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (!sessionById(editorId)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("unknown editorId: %1").arg(editorId).toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        setActiveEditor(editorId);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(editorId));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown app subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleBindKeyCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: bindkey <set|dispatch|list|clear> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));

    if (subCommand == "set") {
        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: bindkey set <keySpec> <tclCommand>"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString keySpec = QString::fromUtf8(Tcl_GetString(objv[2]));
        const QString command = QString::fromUtf8(Tcl_GetString(objv[3]));
        if (keySpec.isEmpty()) {
            Tcl_SetResult(interp, const_cast<char*>("keySpec must not be empty"), TCL_STATIC);
            return TCL_ERROR;
        }

        m_keyBindings.insert(keySpec, command);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("bound %1").arg(keySpec).toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "dispatch") {
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: bindkey dispatch <keySpec>"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString keySpec = QString::fromUtf8(Tcl_GetString(objv[2]));
        if (!m_keyBindings.contains(keySpec)) {
            Tcl_ResetResult(interp);
            return TCL_OK;
        }

        const QString command = m_keyBindings.value(keySpec);
        if (m_sessionController.commandEditorId() > 0) {
            return evaluateCommandForEditor(command, true, false, false, m_sessionController.commandEditorId(), false);
        }
        return evaluateCommand(command, true, false, false);
    }

    if (subCommand == "list") {
        Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
        for (auto it = m_keyBindings.constBegin(); it != m_keyBindings.constEnd(); ++it) {
            Tcl_Obj* pair = Tcl_NewListObj(0, nullptr);
            Tcl_ListObjAppendElement(interp, pair, Tcl_NewStringObj(it.key().toUtf8().constData(), -1));
            Tcl_ListObjAppendElement(interp, pair, Tcl_NewStringObj(it.value().toUtf8().constData(), -1));
            Tcl_ListObjAppendElement(interp, list, pair);
        }
        Tcl_SetObjResult(interp, list);
        return TCL_OK;
    }

    if (subCommand == "clear") {
        if (objc == 2) {
            m_keyBindings.clear();
            return TCL_OK;
        }
        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: bindkey clear ?keySpec?"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString keySpec = QString::fromUtf8(Tcl_GetString(objv[2]));
        m_keyBindings.remove(keySpec);
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown bindkey subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: layer <list|load|configure|active> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));
    EditorSession* session = effectiveSession();

    if (subCommand == "list") {
        if (!session) {
            Tcl_SetResult(interp, const_cast<char*>("no active editor"), TCL_STATIC);
            return TCL_ERROR;
        }

        QString out;
        for (const LayerDefinition& layer : session->layers) {
            const bool isActive = layer.name.compare(session->activeLayerName, Qt::CaseInsensitive) == 0
                                  && layer.type.compare(session->activeLayerType, Qt::CaseInsensitive) == 0;
            out += QString("%1 {%2} %3/%4 %5 %6 %7 %8\n")
                       .arg(layer.name, layer.type)
                       .arg(layer.nameId)
                       .arg(layer.typeId)
                       .arg(layer.color.name(), layer.pattern)
                       .arg(layer.visible ? "visible" : "hidden")
                       .arg(layer.selectable ? "selectable" : "locked")
                       .arg(isActive ? "active" : "inactive");
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj(out.trimmed().toUtf8().constData(), -1));
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

        for (auto it = m_sessionController.sessions().begin(); it != m_sessionController.sessions().end(); ++it) {
            initializeSessionLayers(it.value());
            applySessionToWindow(it.value());
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("loaded %1 layers from %2").arg(m_layerManager.layers().size()).arg(path).toUtf8().constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "active") {
        if (!session) {
            Tcl_SetResult(interp, const_cast<char*>("no active editor"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (objc == 2) {
            Tcl_Obj* pair = Tcl_NewListObj(0, nullptr);
            Tcl_ListObjAppendElement(interp, pair, Tcl_NewStringObj(session->activeLayerName.toUtf8().constData(), -1));
            Tcl_ListObjAppendElement(interp, pair, Tcl_NewStringObj(session->activeLayerType.toUtf8().constData(), -1));
            Tcl_SetObjResult(interp, pair);
            return TCL_OK;
        }
        if (objc != 4) {
            Tcl_SetResult(interp, const_cast<char*>("usage: layer active ?name type?"), TCL_STATIC);
            return TCL_ERROR;
        }

        const QString layerName = QString::fromUtf8(Tcl_GetString(objv[2]));
        const QString layerType = QString::fromUtf8(Tcl_GetString(objv[3]));
        const int foundIndex = std::find_if(session->layers.cbegin(), session->layers.cend(),
                                            [&layerName, &layerType](const LayerDefinition& layer) {
                                                return layer.name.compare(layerName, Qt::CaseInsensitive) == 0
                                                       && layer.type.compare(layerType, Qt::CaseInsensitive) == 0;
                                            })
                               - session->layers.cbegin();
        if (foundIndex < 0 || foundIndex >= session->layers.size()) {
            const QString error = QString("Unknown layer '%1' of type '%2'").arg(layerName, layerType);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        session->activeLayerName = session->layers[foundIndex].name;
        session->activeLayerType = session->layers[foundIndex].type;
        session->window->onActiveLayerChanged(session->activeLayerName, session->activeLayerType);

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("active layer: %1 (%2)").arg(session->activeLayerName, session->activeLayerType)
                .toUtf8()
                .constData(), -1));
        return TCL_OK;
    }

    if (subCommand == "configure") {
        if (objc != 6) {
            Tcl_SetResult(interp,
                          const_cast<char*>("usage: layer configure <name> <type> <-visible|-selectable> <0|1>"),
                          TCL_STATIC);
            return TCL_ERROR;
        }

        const QString layerName = QString::fromUtf8(Tcl_GetString(objv[2]));
        const QString layerType = QString::fromUtf8(Tcl_GetString(objv[3]));
        const QString option = QString::fromUtf8(Tcl_GetString(objv[4]));
        const QString valueRaw = QString::fromUtf8(Tcl_GetString(objv[5]));

        bool ok = false;
        const int numeric = valueRaw.toInt(&ok);
        if (!ok || (numeric != 0 && numeric != 1)) {
            Tcl_SetResult(interp, const_cast<char*>("value must be 0 or 1"), TCL_STATIC);
            return TCL_ERROR;
        }

        if (!session) {
            Tcl_SetResult(interp, const_cast<char*>("no active editor"), TCL_STATIC);
            return TCL_ERROR;
        }

        int index = -1;
        for (int i = 0; i < session->layers.size(); ++i) {
            if (session->layers[i].name.compare(layerName, Qt::CaseInsensitive) == 0
                && session->layers[i].type.compare(layerType, Qt::CaseInsensitive) == 0) {
                index = i;
                break;
            }
        }

        if (index < 0) {
            const QString error = QString("Unknown layer '%1' of type '%2'").arg(layerName, layerType);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        LayerDefinition& layer = session->layers[index];
        if (option == "-visible") {
            layer.visible = numeric == 1;
        } else if (option == "-selectable") {
            layer.selectable = numeric == 1;
        } else {
            const QString error = QString("Unknown option '%1' (expected -visible or -selectable)").arg(option);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        session->window->onLayerChanged(index, layer);

        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            QString("layer %1 (%2) updated: %3=%4").arg(layerName, layerType, option).arg(numeric).toUtf8().constData(), -1));
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

    m_defaultTool = QString::fromUtf8(Tcl_GetString(objv[2]));

    EditorSession* session = effectiveSession();
    if (session) {
        const bool changedTool = session->activeTool.compare(m_defaultTool, Qt::CaseInsensitive) != 0;
        if (changedTool && session->editInProgress) {
            session->window->onEditPreviewChanged(false, session->editPreview);
            session->editInProgress = false;
        }

        session->activeTool = m_defaultTool;
        session->window->onToolChanged(session->activeTool);
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(QString("tool: %1").arg(m_defaultTool).toUtf8().constData(), -1));
    return TCL_OK;
}

int TclConsoleWindow::handleCanvasCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 5) {
        Tcl_SetResult(interp, const_cast<char*>("usage: canvas <press|move|release> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString sub = QString::fromUtf8(Tcl_GetString(objv[1]));
    EditorSession* session = effectiveSession();
    if (!session) {
        Tcl_SetResult(interp, const_cast<char*>("no active editor"), TCL_STATIC);
        return TCL_ERROR;
    }

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
        if (button == 1 && session->activeTool == "rect" && !session->activeLayerName.isEmpty()) {
            auto it = std::find_if(session->layers.cbegin(), session->layers.cend(),
                                   [session](const LayerDefinition& layer) {
                                       return layer.name.compare(session->activeLayerName, Qt::CaseInsensitive) == 0
                                              && layer.type.compare(session->activeLayerType, Qt::CaseInsensitive) == 0;
                                   });
            if (it == session->layers.cend()) {
                const QString error = "No active layer";
                Tcl_SetObjResult(interp, Tcl_NewStringObj(error.toUtf8().constData(), -1));
                return TCL_ERROR;
            }

            session->editInProgress = true;
            session->editAnchorX = x;
            session->editAnchorY = y;
            session->editLayerNameId = it->nameId;
            session->editLayerTypeId = it->typeId;

            if (LayoutEditPreviewModel::tryBuildPreviewPrimitive(session->activeTool,
                                                                 session->editLayerNameId,
                                                                 session->editLayerTypeId,
                                                                 session->editAnchorX,
                                                                 session->editAnchorY,
                                                                 x,
                                                                 y,
                                                                 session->editPreview)) {
                session->window->onEditPreviewChanged(true, session->editPreview);
            }
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

        if (session->editInProgress && leftDown == 1) {
            if (LayoutEditPreviewModel::tryBuildPreviewPrimitive(session->activeTool,
                                                                 session->editLayerNameId,
                                                                 session->editLayerTypeId,
                                                                 session->editAnchorX,
                                                                 session->editAnchorY,
                                                                 x,
                                                                 y,
                                                                 session->editPreview)) {
                session->window->onEditPreviewChanged(true, session->editPreview);
            }
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

        if (button == 1 && session->editInProgress) {
            SceneRenderPrimitive primitive{};
            if (LayoutEditPreviewModel::tryBuildCommittedPrimitive(session->activeTool,
                                                                   session->editLayerNameId,
                                                                   session->editLayerTypeId,
                                                                   session->editAnchorX,
                                                                   session->editAnchorY,
                                                                   x,
                                                                   y,
                                                                   primitive)) {
                session->window->onPrimitiveCommitted(primitive);
            }

            session->window->onEditPreviewChanged(false, session->editPreview);
            session->editInProgress = false;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown canvas subcommand"), TCL_STATIC);
    return TCL_ERROR;
}

int TclConsoleWindow::handleViewCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: view <zoom|pan|grid> ..."), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString sub = QString::fromUtf8(Tcl_GetString(objv[1]));
    EditorSession* session = effectiveSession();
    if (!session) {
        Tcl_SetResult(interp, const_cast<char*>("no active editor"), TCL_STATIC);
        return TCL_ERROR;
    }

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

        session->panX += dx;
        session->panY += dy;
        session->window->onViewChanged(session->zoom, session->panX, session->panY, session->gridSize);
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

        // Incremental zoom with an upper bound to avoid huge transforms.
        const double factor = wheelDelta > 0 ? 1.15 : 1.0 / 1.15;
        const double oldZoom = session->zoom;
        session->zoom *= factor;
        if (session->zoom < std::numeric_limits<double>::min()) {
            session->zoom = std::numeric_limits<double>::min();
        }
        if (session->zoom > 200.0) {
            session->zoom = 200.0;
        }

        // Anchor-preserving zoom: keep anchor point fixed on screen.
        const double worldX = (anchorX - session->panX) / oldZoom;
        const double worldY = (anchorY - session->panY) / oldZoom;
        session->panX = anchorX - (worldX * session->zoom);
        session->panY = anchorY - (worldY * session->zoom);

        session->window->onViewChanged(session->zoom, session->panX, session->panY, session->gridSize);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    if (sub == "grid") {
        if (objc == 2) {
            Tcl_SetObjResult(interp, Tcl_NewDoubleObj(session->gridSize));
            return TCL_OK;
        }

        if (objc != 3) {
            Tcl_SetResult(interp, const_cast<char*>("usage: view grid ?<size>?"), TCL_STATIC);
            return TCL_ERROR;
        }

        double requestedGridSize = 0.0;
        if (!parseDouble(interp, objv[2], requestedGridSize, "size")) {
            return TCL_ERROR;
        }

        if (requestedGridSize <= 0.0) {
            Tcl_SetResult(interp, const_cast<char*>("grid size must be > 0"), TCL_STATIC);
            return TCL_ERROR;
        }

        session->gridSize = requestedGridSize;
        session->window->onViewChanged(session->zoom, session->panX, session->panY, session->gridSize);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    Tcl_SetResult(interp, const_cast<char*>("unknown view subcommand"), TCL_STATIC);
    return TCL_ERROR;
}
