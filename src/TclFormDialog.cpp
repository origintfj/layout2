#include "TclFormDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QLineEdit>
#include <QObject>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWidget>

#include <tcl.h>

namespace {

struct FieldBinding {
    enum class Kind {
        Entry,
        Checkbox,
        Radio
    };

    QString key;
    Kind kind{Kind::Entry};
    QLineEdit* lineEdit{nullptr};
    QCheckBox* checkBox{nullptr};
    QButtonGroup* radioGroup{nullptr};
};

bool parseBooleanToken(const QString& value, bool& result) {
    const QString lowered = value.trimmed().toLower();
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        result = true;
        return true;
    }

    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        result = false;
        return true;
    }

    return false;
}

bool readStringDict(Tcl_Interp* interp, Tcl_Obj* dictObj, QHash<QString, QString>& out) {
    Tcl_DictSearch search;
    Tcl_Obj* keyObj = nullptr;
    Tcl_Obj* valueObj = nullptr;
    int done = 0;
    if (Tcl_DictObjFirst(interp, dictObj, &search, &keyObj, &valueObj, &done) != TCL_OK) {
        return false;
    }

    while (!done) {
        out.insert(QString::fromUtf8(Tcl_GetString(keyObj)),
                   QString::fromUtf8(Tcl_GetString(valueObj)));
        Tcl_DictObjNext(&search, &keyObj, &valueObj, &done);
    }

    Tcl_DictObjDone(&search);
    return true;
}

bool addFieldRow(Tcl_Interp* interp,
                 Tcl_Obj* fieldObj,
                 const QHash<QString, QString>& defaults,
                 QFormLayout* formLayout,
                 QVector<FieldBinding>& bindings) {
    Tcl_Obj* typeObj = nullptr;
    Tcl_Obj* keyObj = nullptr;
    Tcl_Obj* labelObj = nullptr;

    if (Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("type", -1), &typeObj) != TCL_OK
        || Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("key", -1), &keyObj) != TCL_OK
        || Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("label", -1), &labelObj) != TCL_OK) {
        return false;
    }

    if (!typeObj || !keyObj) {
        Tcl_SetResult(interp,
                      const_cast<char*>("each form field must provide dict keys: type and key"),
                      TCL_STATIC);
        return false;
    }

    const QString type = QString::fromUtf8(Tcl_GetString(typeObj)).toLower();
    const QString key = QString::fromUtf8(Tcl_GetString(keyObj));
    const QString label = labelObj ? QString::fromUtf8(Tcl_GetString(labelObj)) : key;
    const QString defaultValue = defaults.value(key);

    if (type == "entry") {
        auto* lineEdit = new QLineEdit();
        lineEdit->setText(defaultValue);
        formLayout->addRow(label + ':', lineEdit);

        FieldBinding binding;
        binding.key = key;
        binding.kind = FieldBinding::Kind::Entry;
        binding.lineEdit = lineEdit;
        bindings.push_back(binding);
        return true;
    }

    if (type == "checkbox") {
        auto* checkBox = new QCheckBox();
        bool checked = false;
        if (!defaultValue.isEmpty() && !parseBooleanToken(defaultValue, checked)) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(
                                 QString("default for checkbox key '%1' must be a boolean token").arg(key).toUtf8().constData(),
                                 -1));
            return false;
        }

        checkBox->setChecked(checked);
        formLayout->addRow(label + ':', checkBox);

        FieldBinding binding;
        binding.key = key;
        binding.kind = FieldBinding::Kind::Checkbox;
        binding.checkBox = checkBox;
        bindings.push_back(binding);
        return true;
    }

    if (type == "radio") {
        Tcl_Obj* optionsObj = nullptr;
        if (Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("options", -1), &optionsObj) != TCL_OK) {
            return false;
        }

        if (!optionsObj) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(
                                 QString("radio field '%1' must provide an options list").arg(key).toUtf8().constData(),
                                 -1));
            return false;
        }

        int optionCount = 0;
        if (Tcl_ListObjLength(interp, optionsObj, &optionCount) != TCL_OK || optionCount <= 0) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(
                                 QString("radio field '%1' options must be a non-empty Tcl list").arg(key).toUtf8().constData(),
                                 -1));
            return false;
        }

        auto* optionsWidget = new QWidget();
        auto* optionsLayout = new QVBoxLayout(optionsWidget);
        optionsLayout->setContentsMargins(0, 0, 0, 0);
        optionsLayout->setSpacing(4);

        auto* radioGroup = new QButtonGroup(optionsWidget);
        bool matchedDefault = false;
        for (int i = 0; i < optionCount; ++i) {
            Tcl_Obj* optionObj = nullptr;
            if (Tcl_ListObjIndex(interp, optionsObj, i, &optionObj) != TCL_OK || !optionObj) {
                Tcl_SetObjResult(interp,
                                 Tcl_NewStringObj(
                                     QString("failed reading radio option %1 for key '%2'").arg(i).arg(key).toUtf8().constData(),
                                     -1));
                return false;
            }

            const QString optionValue = QString::fromUtf8(Tcl_GetString(optionObj));
            auto* radioButton = new QRadioButton(optionValue, optionsWidget);
            radioGroup->addButton(radioButton);
            optionsLayout->addWidget(radioButton);

            if (defaultValue == optionValue) {
                radioButton->setChecked(true);
                matchedDefault = true;
            }
        }

        if (!matchedDefault && !radioGroup->buttons().isEmpty()) {
            radioGroup->buttons().first()->setChecked(true);
        }

        formLayout->addRow(label + ':', optionsWidget);

        FieldBinding binding;
        binding.key = key;
        binding.kind = FieldBinding::Kind::Radio;
        binding.radioGroup = radioGroup;
        bindings.push_back(binding);
        return true;
    }

    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(
                         QString("unsupported form field type '%1' (supported: entry, checkbox, radio)").arg(type).toUtf8().constData(),
                         -1));
    return false;
}

} // namespace

namespace TclFormDialog {

int handleDialogCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[], QWidget* parent) {
    Q_UNUSED(parent);

    if (objc < 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: dialog form ?-title <title>? <defaultsDict> <formSpec>"), TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));
    if (subCommand != "form") {
        Tcl_SetResult(interp, const_cast<char*>("unknown dialog subcommand"), TCL_STATIC);
        return TCL_ERROR;
    }

    QString title = "Form";
    int defaultsIndex = 2;
    if (objc >= 4 && QString::fromUtf8(Tcl_GetString(objv[2])) == "-title") {
        title = QString::fromUtf8(Tcl_GetString(objv[3]));
        defaultsIndex = 4;
    }

    if (objc != defaultsIndex + 2) {
        Tcl_SetResult(interp, const_cast<char*>("usage: dialog form ?-title <title>? <defaultsDict> <formSpec>"), TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_Obj* defaultsObj = objv[defaultsIndex];
    Tcl_Obj* formSpecObj = objv[defaultsIndex + 1];

    QHash<QString, QString> defaults;
    if (!readStringDict(interp, defaultsObj, defaults)) {
        Tcl_SetResult(interp, const_cast<char*>("defaultsDict must be a valid Tcl dict"), TCL_STATIC);
        return TCL_ERROR;
    }

    int fieldCount = 0;
    if (Tcl_ListObjLength(interp, formSpecObj, &fieldCount) != TCL_OK) {
        Tcl_SetResult(interp, const_cast<char*>("formSpec must be a Tcl list of field dicts"), TCL_STATIC);
        return TCL_ERROR;
    }

    QDialog dialog(nullptr, Qt::Window);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setWindowTitle(title);
    auto* rootLayout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();
    rootLayout->addLayout(formLayout);

    QVector<FieldBinding> bindings;
    bindings.reserve(fieldCount);
    for (int i = 0; i < fieldCount; ++i) {
        Tcl_Obj* fieldObj = nullptr;
        if (Tcl_ListObjIndex(interp, formSpecObj, i, &fieldObj) != TCL_OK || !fieldObj) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(QString("failed reading form field at index %1").arg(i).toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        if (!addFieldRow(interp, fieldObj, defaults, formLayout, bindings)) {
            return TCL_ERROR;
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    rootLayout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        Tcl_SetResult(interp, const_cast<char*>("dialog cancelled"), TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_Obj* result = Tcl_NewDictObj();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        Tcl_DictObjPut(interp,
                       result,
                       Tcl_NewStringObj(it.key().toUtf8().constData(), -1),
                       Tcl_NewStringObj(it.value().toUtf8().constData(), -1));
    }

    for (const FieldBinding& binding : bindings) {
        QString value;
        if (binding.kind == FieldBinding::Kind::Entry && binding.lineEdit) {
            value = binding.lineEdit->text();
        } else if (binding.kind == FieldBinding::Kind::Checkbox && binding.checkBox) {
            value = binding.checkBox->isChecked() ? "1" : "0";
        } else if (binding.kind == FieldBinding::Kind::Radio && binding.radioGroup && binding.radioGroup->checkedButton()) {
            value = binding.radioGroup->checkedButton()->text();
        }

        Tcl_DictObjPut(interp,
                       result,
                       Tcl_NewStringObj(binding.key.toUtf8().constData(), -1),
                       Tcl_NewStringObj(value.toUtf8().constData(), -1));
    }

    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

} // namespace TclFormDialog
