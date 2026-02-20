#pragma once

#include <QMainWindow>
#include <tcl.h>

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
    // Static C bridges required by Tcl_CreateObjCommand.
    static int LayerCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ToolCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int CanvasCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ViewCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    // Per-command-family handlers.
    int handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleToolCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleCanvasCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleViewCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    // Common argument parsing helpers.
    bool parseInt64(Tcl_Interp* interp, Tcl_Obj* obj, qint64& value, const char* fieldName);
    bool parseDouble(Tcl_Interp* interp, Tcl_Obj* obj, double& value, const char* fieldName);

    // Console transcript helper.
    void appendTranscript(const QString& line);

    QPlainTextEdit* m_output;
    QLineEdit* m_input;
    Tcl_Interp* m_interp;

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
};
