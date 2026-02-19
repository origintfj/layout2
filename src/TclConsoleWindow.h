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
    int handleLayerCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    void appendTranscript(const QString& line);

    QPlainTextEdit* m_output;
    QLineEdit* m_input;
    Tcl_Interp* m_interp;
    LayerManager m_layerManager;
    LayoutEditorWindow* m_editorWindow;
};
