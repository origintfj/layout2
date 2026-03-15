#include "SelectionPropertiesDialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
struct SelectedObjectDescriptor {
    quint64 objectId{0};
    QString typeName;
    LayoutObjectModel::Bounds bounds;
    bool hasBounds{false};
    const DrawnRectangle* rectangle{nullptr};
};

QVector<SelectedObjectDescriptor> selectedObjectDescriptors(const LayoutSceneNode* rootCell,
                                                            const QSet<quint64>& selectedObjectIds) {
    QVector<SelectedObjectDescriptor> descriptors;
    if (!rootCell) {
        return descriptors;
    }

    const QVector<quint64> selectedIds = selectedObjectIds.values().toVector();
    descriptors.reserve(selectedIds.size());
    for (quint64 objectId : selectedIds) {
        const LayoutObjectModel* object = rootCell->findObjectById(objectId);
        if (!object) {
            continue;
        }

        SelectedObjectDescriptor descriptor;
        descriptor.objectId = objectId;
        descriptor.rectangle = object->asRectangle();
        descriptor.typeName = descriptor.rectangle ? QStringLiteral("Rectangle") : QStringLiteral("Object");
        descriptor.hasBounds = object->tryGetBounds(descriptor.bounds);
        descriptors.push_back(descriptor);
    }

    std::sort(descriptors.begin(), descriptors.end(),
              [](const SelectedObjectDescriptor& lhs, const SelectedObjectDescriptor& rhs) {
                  if (lhs.typeName != rhs.typeName) {
                      return lhs.typeName < rhs.typeName;
                  }
                  return lhs.objectId < rhs.objectId;
              });
    return descriptors;
}

QWidget* makeEmptyPropertiesPane(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    auto* label = new QLabel("No selected objects.", panel);
    label->setStyleSheet("color:#bbb;");
    layout->addWidget(label);
    layout->addStretch(1);
    return panel;
}

QWidget* makeGroupPropertiesPane(const QString& typeName,
                                 const QVector<SelectedObjectDescriptor>& descriptors,
                                 QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* form = new QFormLayout(panel);
    form->addRow("Selection type:", new QLabel(typeName, panel));
    form->addRow("Selected count:", new QLabel(QString::number(descriptors.size()), panel));

    qint64 minX = 0;
    qint64 minY = 0;
    qint64 maxX = 0;
    qint64 maxY = 0;
    bool initialized = false;
    for (const SelectedObjectDescriptor& descriptor : descriptors) {
        if (!descriptor.hasBounds) {
            continue;
        }

        if (!initialized) {
            minX = descriptor.bounds.minX;
            minY = descriptor.bounds.minY;
            maxX = descriptor.bounds.maxX;
            maxY = descriptor.bounds.maxY;
            initialized = true;
        } else {
            minX = std::min(minX, descriptor.bounds.minX);
            minY = std::min(minY, descriptor.bounds.minY);
            maxX = std::max(maxX, descriptor.bounds.maxX);
            maxY = std::max(maxY, descriptor.bounds.maxY);
        }
    }

    if (initialized) {
        form->addRow("Group bounds:",
                     new QLabel(QString("(%1, %2) - (%3, %4)")
                                    .arg(minX)
                                    .arg(minY)
                                    .arg(maxX)
                                    .arg(maxY),
                                panel));
    }

    return panel;
}

QWidget* makeRectanglePropertiesPane(const SelectedObjectDescriptor& descriptor, QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* form = new QFormLayout(panel);
    form->addRow("Type:", new QLabel(descriptor.typeName, panel));
    form->addRow("Object ID:", new QLabel(QString::number(descriptor.objectId), panel));

    if (descriptor.rectangle) {
        form->addRow("Layer name id:", new QLabel(QString::number(descriptor.rectangle->layerNameId), panel));
        form->addRow("Layer type id:", new QLabel(QString::number(descriptor.rectangle->layerTypeId), panel));
        form->addRow("x1:", new QLabel(QString::number(descriptor.rectangle->x1), panel));
        form->addRow("y1:", new QLabel(QString::number(descriptor.rectangle->y1), panel));
        form->addRow("x2:", new QLabel(QString::number(descriptor.rectangle->x2), panel));
        form->addRow("y2:", new QLabel(QString::number(descriptor.rectangle->y2), panel));
    }

    if (descriptor.hasBounds) {
        form->addRow("Bounds:",
                     new QLabel(QString("(%1, %2) - (%3, %4)")
                                    .arg(descriptor.bounds.minX)
                                    .arg(descriptor.bounds.minY)
                                    .arg(descriptor.bounds.maxX)
                                    .arg(descriptor.bounds.maxY),
                                panel));
    }

    return panel;
}

QWidget* makeGenericPropertiesPane(const SelectedObjectDescriptor& descriptor, QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* form = new QFormLayout(panel);
    form->addRow("Type:", new QLabel(descriptor.typeName, panel));
    form->addRow("Object ID:", new QLabel(QString::number(descriptor.objectId), panel));
    if (descriptor.hasBounds) {
        form->addRow("Bounds:",
                     new QLabel(QString("(%1, %2) - (%3, %4)")
                                    .arg(descriptor.bounds.minX)
                                    .arg(descriptor.bounds.minY)
                                    .arg(descriptor.bounds.maxX)
                                    .arg(descriptor.bounds.maxY),
                                panel));
    }
    return panel;
}
} // namespace

void SelectionPropertiesDialog::show(QWidget* parent,
                                     const LayoutSceneNode* rootCell,
                                     const QSet<quint64>& selectedObjectIds) {
    // Keep this as a top-level window tied to the invoking editor session
    // without making it modal, so users can move it independently.
    QDialog dialog(parent);
    dialog.setWindowFlag(Qt::Window, true);
    dialog.setWindowModality(Qt::NonModal);
    dialog.setWindowTitle("Selection Properties");
    dialog.resize(880, 540);

    auto* rootLayout = new QVBoxLayout(&dialog);
    auto* splitter = new QSplitter(Qt::Horizontal, &dialog);
    auto* tree = new QTreeWidget(splitter);
    tree->setHeaderLabel("Selected objects");
    tree->setRootIsDecorated(true);
    tree->setAlternatingRowColors(true);

    auto* propertiesStack = new QStackedWidget(splitter);
    splitter->addWidget(tree);
    splitter->addWidget(propertiesStack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({280, 600});

    const int emptyPaneIndex = propertiesStack->addWidget(makeEmptyPropertiesPane(propertiesStack));
    propertiesStack->setCurrentIndex(emptyPaneIndex);

    const QVector<SelectedObjectDescriptor> descriptors = selectedObjectDescriptors(rootCell, selectedObjectIds);
    QHash<QString, QVector<SelectedObjectDescriptor>> byType;
    for (const SelectedObjectDescriptor& descriptor : descriptors) {
        byType[descriptor.typeName].push_back(descriptor);
    }

    QHash<QTreeWidgetItem*, int> paneByItem;
    QStringList groupTypes = byType.keys();
    std::sort(groupTypes.begin(), groupTypes.end());
    for (const QString& typeName : groupTypes) {
        const QVector<SelectedObjectDescriptor>& typeDescriptors = byType[typeName];
        auto* groupItem = new QTreeWidgetItem(tree, QStringList() << QString("%1 (%2)").arg(typeName).arg(typeDescriptors.size()));
        groupItem->setExpanded(true);
        paneByItem[groupItem] = propertiesStack->addWidget(makeGroupPropertiesPane(typeName, typeDescriptors, propertiesStack));

        for (const SelectedObjectDescriptor& descriptor : typeDescriptors) {
            auto* objectItem = new QTreeWidgetItem(groupItem,
                                                   QStringList() << QString("%1 #%2")
                                                                        .arg(typeName)
                                                                        .arg(descriptor.objectId));
            if (descriptor.rectangle) {
                paneByItem[objectItem] = propertiesStack->addWidget(
                    makeRectanglePropertiesPane(descriptor, propertiesStack));
            } else {
                paneByItem[objectItem] = propertiesStack->addWidget(
                    makeGenericPropertiesPane(descriptor, propertiesStack));
            }
        }
    }

    QObject::connect(tree, &QTreeWidget::currentItemChanged,
                     &dialog,
                     [propertiesStack, paneByItem](QTreeWidgetItem* current, QTreeWidgetItem*) {
                         const auto it = paneByItem.constFind(current);
                         if (it != paneByItem.cend()) {
                             propertiesStack->setCurrentIndex(it.value());
                         }
                     });

    if (tree->topLevelItemCount() > 0) {
        tree->setCurrentItem(tree->topLevelItem(0));
    }

    rootLayout->addWidget(splitter);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    dialog.exec();
}
