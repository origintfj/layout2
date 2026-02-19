#include "LayoutEditorWindow.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFrame>

LayoutEditorWindow::LayoutEditorWindow(QWidget* parent)
    : QMainWindow(parent),
      m_layerTable(new QTableWidget(this)),
      m_canvasLabel(new QLabel(this)) {
    setWindowTitle("Layout Editor");
    resize(1000, 650);

    auto* splitter = new QSplitter(this);

    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(6, 6, 6, 6);

    m_layerTable->setColumnCount(5);
    m_layerTable->setHorizontalHeaderLabels({"Style", "Layer", "Type", "Visible", "Selectable"});
    m_layerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_layerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_layerTable->verticalHeader()->setVisible(false);
    m_layerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    leftLayout->addWidget(m_layerTable);

    auto* rightPane = new QFrame(splitter);
    auto* rightLayout = new QHBoxLayout(rightPane);
    m_canvasLabel->setAlignment(Qt::AlignCenter);
    m_canvasLabel->setText("Main layout editing pane\n(Canvas placeholder)");
    m_canvasLabel->setStyleSheet("background:#101820; color:#eeeeee; border:1px solid #555;");
    rightLayout->addWidget(m_canvasLabel);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    setCentralWidget(splitter);

    connect(m_layerTable, &QTableWidget::cellChanged, this, &LayoutEditorWindow::onCellChanged);
}

QTableWidgetItem* LayoutEditorWindow::makeReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

void LayoutEditorWindow::setLayers(const QVector<LayerDefinition>& layers) {
    m_internalUpdate = true;
    m_layers = layers;
    m_layerTable->setRowCount(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        applyLayerToRow(row, layers[row]);
    }
    m_internalUpdate = false;
}

void LayoutEditorWindow::applyLayerToRow(int row, const LayerDefinition& layer) {
    auto* swatch = new QTableWidgetItem(QString("%1 %2").arg(layer.color.name(), layer.pattern));
    swatch->setBackground(layer.color);
    swatch->setFlags(Qt::ItemIsEnabled);
    m_layerTable->setItem(row, 0, swatch);
    m_layerTable->setItem(row, 1, makeReadOnlyItem(layer.name));
    m_layerTable->setItem(row, 2, makeReadOnlyItem(layer.type));

    auto* visibleItem = new QTableWidgetItem();
    visibleItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    visibleItem->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    m_layerTable->setItem(row, 3, visibleItem);

    auto* selectableItem = new QTableWidgetItem();
    selectableItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    selectableItem->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
    m_layerTable->setItem(row, 4, selectableItem);
}

void LayoutEditorWindow::onLayerChanged(int index, const LayerDefinition& layer) {
    if (index < 0 || index >= m_layers.size()) {
        return;
    }

    m_layers[index] = layer;
    m_internalUpdate = true;
    m_layerTable->item(index, 3)->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    m_layerTable->item(index, 4)->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
    m_internalUpdate = false;
}

void LayoutEditorWindow::onCellChanged(int row, int column) {
    if (m_internalUpdate || row < 0 || row >= m_layers.size()) {
        return;
    }

    if (column != 3 && column != 4) {
        return;
    }

    auto* item = m_layerTable->item(row, column);
    if (!item) {
        return;
    }

    const bool requestedValue = item->checkState() == Qt::Checked;
    const LayerDefinition& current = m_layers[row];

    // Revert visual state immediately; interpreter command execution will apply official state.
    m_internalUpdate = true;
    if (column == 3) {
        item->setCheckState(current.visible ? Qt::Checked : Qt::Unchecked);
    } else {
        item->setCheckState(current.selectable ? Qt::Checked : Qt::Unchecked);
    }
    m_internalUpdate = false;

    const QString option = column == 3 ? "-visible" : "-selectable";
    emit commandRequested(QString("layer configure %1 %2 %3")
                              .arg(current.name, option)
                              .arg(requestedValue ? 1 : 0));
}
