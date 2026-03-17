#include "TclFormDialog.h"
#include "TclConsoleWindow.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHash>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

#include <tcl.h>

namespace {

enum class EntryValueType {
    Text,
    Integer,
    Float
};

struct FieldBinding {
    enum class Kind {
        Entry,
        Checkbox,
        Radio
    };

    QString key;
    Kind kind{Kind::Entry};

    // Entry binding state
    QLineEdit* lineEdit{nullptr};
    EntryValueType entryValueType{EntryValueType::Text};
    bool hasMin{false};
    bool hasMax{false};
    qint64 minInt{0};
    qint64 maxInt{0};
    double minFloat{0.0};
    double maxFloat{0.0};

    // Checkbox binding state
    QCheckBox* checkBox{nullptr};

    // Radio binding state
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

bool parseOptionalEntryConstraints(Tcl_Interp* interp,
                                   Tcl_Obj* fieldObj,
                                   const QString& key,
                                   FieldBinding& binding,
                                   QString& errorMessage) {
    Tcl_Obj* valueTypeObj = nullptr;
    Tcl_Obj* minObj = nullptr;
    Tcl_Obj* maxObj = nullptr;

    if (Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("value_type", -1), &valueTypeObj) != TCL_OK
        || Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("min", -1), &minObj) != TCL_OK
        || Tcl_DictObjGet(interp, fieldObj, Tcl_NewStringObj("max", -1), &maxObj) != TCL_OK) {
        return false;
    }

    QString valueType = "text";
    if (valueTypeObj) {
        valueType = QString::fromUtf8(Tcl_GetString(valueTypeObj)).trimmed().toLower();
    }

    if (valueType == "text" || valueType.isEmpty()) {
        binding.entryValueType = EntryValueType::Text;
        if (minObj || maxObj) {
            errorMessage = QString("entry field '%1': min/max can only be used when value_type is int or float").arg(key);
            return false;
        }
        return true;
    }

    if (valueType == "int" || valueType == "integer") {
        binding.entryValueType = EntryValueType::Integer;

        if (minObj) {
            Tcl_WideInt rawMin = 0;
            if (Tcl_GetWideIntFromObj(interp, minObj, &rawMin) != TCL_OK) {
                errorMessage = QString("entry field '%1': min must be an integer").arg(key);
                return false;
            }
            binding.hasMin = true;
            binding.minInt = static_cast<qint64>(rawMin);
        }

        if (maxObj) {
            Tcl_WideInt rawMax = 0;
            if (Tcl_GetWideIntFromObj(interp, maxObj, &rawMax) != TCL_OK) {
                errorMessage = QString("entry field '%1': max must be an integer").arg(key);
                return false;
            }
            binding.hasMax = true;
            binding.maxInt = static_cast<qint64>(rawMax);
        }

        if (binding.hasMin && binding.hasMax && binding.minInt > binding.maxInt) {
            errorMessage = QString("entry field '%1': min must be <= max").arg(key);
            return false;
        }

        return true;
    }

    if (valueType == "float" || valueType == "double") {
        binding.entryValueType = EntryValueType::Float;

        if (minObj) {
            double parsedMin = 0.0;
            if (Tcl_GetDoubleFromObj(interp, minObj, &parsedMin) != TCL_OK) {
                errorMessage = QString("entry field '%1': min must be a float").arg(key);
                return false;
            }
            binding.hasMin = true;
            binding.minFloat = parsedMin;
        }

        if (maxObj) {
            double parsedMax = 0.0;
            if (Tcl_GetDoubleFromObj(interp, maxObj, &parsedMax) != TCL_OK) {
                errorMessage = QString("entry field '%1': max must be a float").arg(key);
                return false;
            }
            binding.hasMax = true;
            binding.maxFloat = parsedMax;
        }

        if (binding.hasMin && binding.hasMax && binding.minFloat > binding.maxFloat) {
            errorMessage = QString("entry field '%1': min must be <= max").arg(key);
            return false;
        }

        return true;
    }

    errorMessage = QString("entry field '%1': unsupported value_type '%2' (supported: text, int, float)")
                       .arg(key)
                       .arg(valueType);
    return false;
}

bool validateEntryBinding(const FieldBinding& binding, QString& errorMessage) {
    if (!binding.lineEdit) {
        return true;
    }

    const QString raw = binding.lineEdit->text().trimmed();

    if (binding.entryValueType == EntryValueType::Text) {
        return true;
    }

    if (binding.entryValueType == EntryValueType::Integer) {
        bool ok = false;
        const qint64 value = raw.toLongLong(&ok);
        if (!ok) {
            errorMessage = QString("Field '%1' must be an integer").arg(binding.key);
            return false;
        }

        if (binding.hasMin && value < binding.minInt) {
            errorMessage = QString("Field '%1' must be >= %2").arg(binding.key).arg(binding.minInt);
            return false;
        }

        if (binding.hasMax && value > binding.maxInt) {
            errorMessage = QString("Field '%1' must be <= %2").arg(binding.key).arg(binding.maxInt);
            return false;
        }

        return true;
    }

    if (binding.entryValueType == EntryValueType::Float) {
        bool ok = false;
        const double value = raw.toDouble(&ok);
        if (!ok) {
            errorMessage = QString("Field '%1' must be a float").arg(binding.key);
            return false;
        }

        if (binding.hasMin && value < binding.minFloat) {
            errorMessage = QString("Field '%1' must be >= %2").arg(binding.key).arg(binding.minFloat);
            return false;
        }

        if (binding.hasMax && value > binding.maxFloat) {
            errorMessage = QString("Field '%1' must be <= %2").arg(binding.key).arg(binding.maxFloat);
            return false;
        }

        return true;
    }

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

        QString constraintError;
        if (!parseOptionalEntryConstraints(interp, fieldObj, key, binding, constraintError)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(constraintError.toUtf8().constData(), -1));
            return false;
        }

        if (binding.entryValueType == EntryValueType::Integer) {
            int validatorMin = std::numeric_limits<int>::min();
            int validatorMax = std::numeric_limits<int>::max();
            if (binding.hasMin) {
                validatorMin = static_cast<int>(std::max<qint64>(binding.minInt, std::numeric_limits<int>::min()));
            }
            if (binding.hasMax) {
                validatorMax = static_cast<int>(std::min<qint64>(binding.maxInt, std::numeric_limits<int>::max()));
            }

            auto* validator = new QIntValidator(validatorMin, validatorMax, lineEdit);
            lineEdit->setValidator(validator);
        } else if (binding.entryValueType == EntryValueType::Float) {
            double validatorMin = binding.hasMin ? binding.minFloat : -std::numeric_limits<double>::max();
            double validatorMax = binding.hasMax ? binding.maxFloat : std::numeric_limits<double>::max();

            auto* validator = new QDoubleValidator(validatorMin, validatorMax, 12, lineEdit);
            validator->setNotation(QDoubleValidator::StandardNotation);
            lineEdit->setValidator(validator);
        }

        QString defaultValidationError;
        if (!validateEntryBinding(binding, defaultValidationError)) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(
                                 QString("entry field '%1' default is invalid: %2").arg(key).arg(defaultValidationError).toUtf8().constData(),
                                 -1));
            return false;
        }

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
                                     QString("failed reading radio option %1 for key '%2'")
                                         .arg(i)
                                         .arg(key)
                                         .toUtf8()
                                         .constData(),
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

Tcl_Obj* collectFormValues(Tcl_Interp* interp,
                           const QHash<QString, QString>& defaults,
                           const QVector<FieldBinding>& bindings,
                           QString& errorMessage) {
    // Validate entry controls first so we fail before creating a partially
    // updated payload dictionary.
    for (const FieldBinding& binding : bindings) {
        if (binding.kind != FieldBinding::Kind::Entry) {
            continue;
        }

        if (!validateEntryBinding(binding, errorMessage)) {
            return nullptr;
        }
    }

    // Start from the provided defaults so untouched values are preserved.
    Tcl_Obj* result = Tcl_NewDictObj();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        Tcl_DictObjPut(interp,
                       result,
                       Tcl_NewStringObj(it.key().toUtf8().constData(), -1),
                       Tcl_NewStringObj(it.value().toUtf8().constData(), -1));
    }

    // Overlay current widget values by field key.
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

    return result;
}

bool evaluateApplyCommand(Tcl_Interp* interp,
                          QWidget* parent,
                          Tcl_Obj* applyCommandObj,
                          const QString& objectId,
                          Tcl_Obj* valuesObj,
                          QString& errorMessage) {
    // -applycmd is optional in non-modal mode.
    if (!applyCommandObj) {
        return true;
    }

    // The callback is a Tcl command prefix list (e.g. {object update}).
    int prefixCount = 0;
    Tcl_Obj** prefixObjv = nullptr;
    if (Tcl_ListObjGetElements(interp, applyCommandObj, &prefixCount, &prefixObjv) != TCL_OK || prefixCount <= 0) {
        errorMessage = "-applycmd must be a non-empty Tcl command prefix list";
        return false;
    }

    // Build argv as: {*prefix} ?objectId? valuesDict
    QVector<Tcl_Obj*> evalObjv;
    evalObjv.reserve(prefixCount + (objectId.isEmpty() ? 1 : 2));
    for (int i = 0; i < prefixCount; ++i) {
        evalObjv.push_back(prefixObjv[i]);
    }

    Tcl_Obj* objectIdObj = nullptr;
    if (!objectId.isEmpty()) {
        // objectId is optional and inserted before the values dict.
        objectIdObj = Tcl_NewStringObj(objectId.toUtf8().constData(), -1);
        Tcl_IncrRefCount(objectIdObj);
        evalObjv.push_back(objectIdObj);
    }

    // Duplicate payload so callback-side mutation cannot affect our source obj.
    Tcl_Obj* payloadObj = Tcl_DuplicateObj(valuesObj);
    Tcl_IncrRefCount(payloadObj);
    evalObjv.push_back(payloadObj);

    // Serialize callback argv into one Tcl command string and run it through
    // the same console evaluation pipeline used by normal user-entered commands.
    Tcl_Obj* commandObj = Tcl_NewListObj(0, nullptr);
    for (Tcl_Obj* argObj : evalObjv) {
        Tcl_ListObjAppendElement(interp, commandObj, argObj);
    }
    const QString commandString = QString::fromUtf8(Tcl_GetString(commandObj));

    int evalStatus = TCL_OK;
    if (TclConsoleWindow* console = qobject_cast<TclConsoleWindow*>(parent)) {
        evalStatus = console->evaluateConsoleCommand(commandString);
    } else {
        evalStatus = Tcl_EvalObjv(interp, evalObjv.size(), evalObjv.data(), TCL_EVAL_GLOBAL);
    }

    Tcl_DecrRefCount(payloadObj);
    if (objectIdObj) {
        Tcl_DecrRefCount(objectIdObj);
    }

    if (evalStatus != TCL_OK) {
        errorMessage = QString::fromUtf8(Tcl_GetStringResult(interp));
        return false;
    }

    return true;
}

} // namespace

namespace TclFormDialog {

int handleDialogCommand(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[], QWidget* parent) {
    if (objc < 2) {
        Tcl_SetResult(interp,
                      const_cast<char*>("usage: dialog form ?-title <title>? ?-nonmodal <bool>? ?-objectid <id>? ?-applycmd <commandPrefix>? <defaultsDict> <formSpec>"),
                      TCL_STATIC);
        return TCL_ERROR;
    }

    const QString subCommand = QString::fromUtf8(Tcl_GetString(objv[1]));
    if (subCommand != "form") {
        Tcl_SetResult(interp, const_cast<char*>("unknown dialog subcommand"), TCL_STATIC);
        return TCL_ERROR;
    }

    // Defaults preserve existing behavior; optional switches extend behavior.
    QString title = "Form";
    bool nonModal = false;
    QString objectId;
    Tcl_Obj* applyCommandObj = nullptr;

    // Parse optional flags in order until we reach positional args.
    int argIndex = 2;
    while (argIndex + 1 < objc) {
        const QString option = QString::fromUtf8(Tcl_GetString(objv[argIndex]));
        if (option == "-title") {
            title = QString::fromUtf8(Tcl_GetString(objv[argIndex + 1]));
            argIndex += 2;
            continue;
        }

        // In non-modal mode the Tcl prompt stays interactive.
        if (option == "-nonmodal") {
            int boolValue = 0;
            if (Tcl_GetBooleanFromObj(interp, objv[argIndex + 1], &boolValue) != TCL_OK) {
                return TCL_ERROR;
            }
            nonModal = boolValue != 0;
            argIndex += 2;
            continue;
        }

        // Optional object identity forwarded to -applycmd.
        if (option == "-objectid") {
            objectId = QString::fromUtf8(Tcl_GetString(objv[argIndex + 1]));
            argIndex += 2;
            continue;
        }

        // Optional command prefix run for Apply/OK in non-modal dialogs.
        if (option == "-applycmd") {
            applyCommandObj = objv[argIndex + 1];
            argIndex += 2;
            continue;
        }

        break;
    }

    if (objc != argIndex + 2) {
        Tcl_SetResult(interp,
                      const_cast<char*>("usage: dialog form ?-title <title>? ?-nonmodal <bool>? ?-objectid <id>? ?-applycmd <commandPrefix>? <defaultsDict> <formSpec>"),
                      TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_Obj* defaultsObj = objv[argIndex];
    Tcl_Obj* formSpecObj = objv[argIndex + 1];

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

    // Allocate on heap so the same dialog instance can live past command return
    // in non-modal mode.
    auto* dialog = new QDialog(nullptr, Qt::Window);
    // Only non-modal dialogs should self-delete on close. Modal dialogs are
    // deleted explicitly below after dialog->exec() returns.
    dialog->setAttribute(Qt::WA_DeleteOnClose, nonModal);
    dialog->setWindowTitle(title);
    dialog->setWindowModality(nonModal ? Qt::NonModal : Qt::ApplicationModal);
    auto* rootLayout = new QVBoxLayout(dialog);
    auto* formLayout = new QFormLayout();
    rootLayout->addLayout(formLayout);

    auto* bindings = new QVector<FieldBinding>();
    bindings->reserve(fieldCount);
    for (int i = 0; i < fieldCount; ++i) {
        Tcl_Obj* fieldObj = nullptr;
        if (Tcl_ListObjIndex(interp, formSpecObj, i, &fieldObj) != TCL_OK || !fieldObj) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj(QString("failed reading form field at index %1").arg(i).toUtf8().constData(), -1));
            delete bindings;
            delete dialog;
            return TCL_ERROR;
        }

        if (!addFieldRow(interp, fieldObj, defaults, formLayout, *bindings)) {
            delete bindings;
            delete dialog;
            return TCL_ERROR;
        }
    }

    // Non-modal mode exposes Apply, modal mode remains OK/Cancel.
    auto* buttons = nonModal
                        ? new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, dialog)
                        : new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    rootLayout->addWidget(buttons);

    if (!nonModal) {
        // Keep legacy modal behavior: validate then accept.
        QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, bindings]() {
            for (const FieldBinding& binding : *bindings) {
                if (binding.kind != FieldBinding::Kind::Entry) {
                    continue;
                }

                QString validationError;
                if (!validateEntryBinding(binding, validationError)) {
                    QMessageBox::warning(dialog, "Invalid field value", validationError);
                    return;
                }
            }

            dialog->accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

        // Modal execution blocks this command until close.
        if (dialog->exec() != QDialog::Accepted) {
            delete bindings;
            delete dialog;
            Tcl_SetResult(interp, const_cast<char*>("dialog cancelled"), TCL_STATIC);
            return TCL_ERROR;
        }

        QString errorMessage;
        Tcl_Obj* result = collectFormValues(interp, defaults, *bindings, errorMessage);
        delete bindings;
        delete dialog;
        if (!result) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(errorMessage.toUtf8().constData(), -1));
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    }

    // Keep callback alive for asynchronous button handling.
    if (applyCommandObj) {
        Tcl_IncrRefCount(applyCommandObj);
    }

    QObject::connect(buttons, &QDialogButtonBox::clicked, dialog,
                     [interp, parent, dialog, buttons, bindings, defaults, objectId, applyCommandObj](QAbstractButton* button) {
                         // Determine semantic action from the clicked button.
                         const QDialogButtonBox::ButtonRole role = buttons->buttonRole(button);

                         // Cancel simply closes.
                         if (role == QDialogButtonBox::RejectRole) {
                             dialog->close();
                             return;
                         }

                         // Ignore roles we do not use (e.g. help/reset if added later).
                         if (role != QDialogButtonBox::ApplyRole && role != QDialogButtonBox::AcceptRole) {
                             return;
                         }

                         // Rebuild and validate payload at click time.
                         QString validationError;
                         Tcl_Obj* valuesObj = collectFormValues(interp, defaults, *bindings, validationError);
                         if (!valuesObj) {
                             QMessageBox::warning(dialog, "Invalid field value", validationError);
                             return;
                         }

                         // Dispatch optional Tcl callback with object id + dict payload.
                         QString commandError;
                         const bool callbackOk = evaluateApplyCommand(interp,
                                                                     parent,
                                                                     applyCommandObj,
                                                                     objectId,
                                                                     valuesObj,
                                                                     commandError);

                         if (!callbackOk) {
                             QMessageBox::warning(dialog,
                                                  "Tickle command failed",
                                                  commandError.isEmpty() ? QStringLiteral("Failed to execute -applycmd") : commandError);
                             return;
                         }

                         // Apply keeps the window open; OK closes after callback.
                         if (role == QDialogButtonBox::AcceptRole) {
                             dialog->close();
                         }
                     });

    // Reclaim heap allocations when the top-level dialog is destroyed.
    QObject::connect(dialog, &QObject::destroyed, [bindings, applyCommandObj]() {
        delete bindings;
        if (applyCommandObj) {
            Tcl_DecrRefCount(applyCommandObj);
        }
    });

    // Non-modal returns immediately after showing the window.
    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    Tcl_SetObjResult(interp, Tcl_NewStringObj("dialog shown", -1));
    return TCL_OK;
}

} // namespace TclFormDialog
