#pragma once

#include <QString>

class QWidget;
struct Tcl_Interp;
struct Tcl_Obj;

namespace TclFormDialog {

int handleDialogCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[], QWidget* parent);

} // namespace TclFormDialog
