#pragma once

#include <QMainWindow>
#include <tcl.h>

class QLineEdit;
class QPlainTextEdit;

#include "LayerManager.h"
#include "LayoutEditorWindow.h"

class TclConsoleWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TclConsoleWindow(QWidget* parent = nullptr);
    ~TclConsoleWindow() override;

public slots:
    void executeCommand(const QString& command);

private:
    static int LayerCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ToolCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int CanvasCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    static int ViewCommandBridge(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    int handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleToolCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleCanvasCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
    int handleViewCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    bool parseInt64(Tcl_Interp* interp, Tcl_Obj* obj, qint64& value, const char* fieldName);
    bool parseDouble(Tcl_Interp* interp, Tcl_Obj* obj, double& value, const char* fieldName);
    void appendTranscript(const QString& line);

    QPlainTextEdit* m_output;
    QLineEdit* m_input;
    Tcl_Interp* m_interp;
    LayerManager m_layerManager;
    LayoutEditorWindow* m_editorWindow;

    QString m_activeTool{"none"};
    bool m_rectInProgress{false};
    DrawnRectangle m_previewRectangle{};

    double m_zoom{1.0};
    double m_panX{0.0};
    double m_panY{0.0};
};
