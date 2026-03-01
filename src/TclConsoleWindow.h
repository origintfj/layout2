#pragma once

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <tcl.h>

class QEvent;
class QObject;
class QLineEdit;
class QPlainTextEdit;

#include "LayerManager.h"
#include "LayoutEditorWindow.h"

// TclConsoleWindow hosts the primary Tcl interpreter UI.
//
// Responsibilities:
//  - expose Tcl commands (layer/tool/canvas/view)
//  - accept text commands from console input
//  - relay GUI-generated command strings into Tcl evaluation
//  - apply command results back onto model/view
class TclConsoleWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TclConsoleWindow(QWidget* parent = nullptr);
    ~TclConsoleWindow() override;

public slots:
    // Evaluates a Tcl command and appends result/error text to transcript.
    void executeCommand(const QString& command);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Static C bridges required by Tcl_CreateObjCommand.
    static int LayerCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ToolCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int CanvasCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ViewCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int BindKeyCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int TranscriptCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int AppCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    // Per-command-family handlers.
    int handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleToolCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleCanvasCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleViewCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleBindKeyCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleTranscriptCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleAppCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    // Common argument parsing helpers.
    bool parseInt64(Tcl_Interp* interp, Tcl_Obj* obj, qint64& value, const char* fieldName);
    bool parseDouble(Tcl_Interp* interp, Tcl_Obj* obj, double& value, const char* fieldName);

    // Console transcript helper.
    void appendTranscript(const QString& line);
    bool shouldSuppressTranscriptCommand(const QString& command) const;
    int evaluateCommand(const QString& command, bool echoCommand, bool echoResult, bool echoErrorLine);
    void pushHistoryCommand(const QString& command);
    void navigateHistory(int direction);

    QPlainTextEdit* m_output;
    QLineEdit* m_input;
    Tcl_Interp* m_interp;

    // Bash-like interactive command history state for the input field.
    QStringList m_commandHistory;
    int m_historyIndex{-1};
    QString m_inProgressInput;

    // Authoritative layer model.
    LayerManager m_layerManager;

    // Child window that emits GUI interactions as Tcl commands.
    LayoutEditorWindow* m_editorWindow;

    // Current drawing/tool session state.
    QString m_activeTool{"none"};
    bool m_rectInProgress{false};
    DrawnRectangle m_previewRectangle{};

    // Current view transform state.
    double m_zoom{1.0};
    double m_panX{0.0};
    double m_panY{0.0};
    double m_gridSize{40.0};

    // Key binding table used by bindkey set/dispatch commands.
    QHash<QString, QString> m_keyBindings;

    // Glob patterns for commands that should not be echoed to the transcript.
    QStringList m_transcriptFilters;
};
