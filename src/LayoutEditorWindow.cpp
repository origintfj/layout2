#include "LayoutEditorWindow.h"

#include "LayoutCanvas.h"
#include "LayoutSceneModel.h"
#include "RenderTypes.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <memory>

namespace {

bool isModifierOnlyKey(const int key) {
    return key == Qt::Key_Shift
           || key == Qt::Key_Control
           || key == Qt::Key_Alt
           || key == Qt::Key_Meta
           || key == Qt::Key_AltGr;
}

QString keySpecFromEvent(const QKeyEvent* event) {
    const int key = event->key();
    if (key == Qt::Key_unknown || isModifierOnlyKey(key)) {
        return QString();
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const QKeySequence sequence(modifiers | key);
    return sequence.toString(QKeySequence::PortableText);
}

} // namespace

LayoutEditorWindow::LayoutEditorWindow(QWidget* parent)
    : QMainWindow(parent),
      m_layerTable(new QTableWidget()),
      m_canvas(new LayoutCanvas()),
      m_statusLabel(new QLabel()),
      m_rootCell(std::make_unique<LayoutSceneNode>()) {
    refreshWindowTitle();
    resize(1100, 700);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto* splitter = new QSplitter(central);

    // Left pane: layer palette table for layer visibility/selectability state.
    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(6, 6, 6, 6);

    m_layerTable->setColumnCount(5);
    m_layerTable->setHorizontalHeaderLabels({"", "Layer", "Type", "V", "S"});
    m_layerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_layerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_layerTable->verticalHeader()->setVisible(false);
    m_layerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_layerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerTable->setStyleSheet(
        "QTableWidget::item:selected {"
        "background: transparent;"
        "color: palette(text);"
        "}");
    leftLayout->addWidget(m_layerTable);

    // Right pane: the dedicated canvas module owns rendering and interaction.
    auto* rightPane = new QFrame(splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_canvas);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({280, 820});

    m_statusLabel->setText("Active layer: <none> | Tool: <none>");
    m_statusLabel->setStyleSheet("color:#ddd; background:#222; padding:2px 6px;");
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    centralLayout->addWidget(splitter);
    centralLayout->addWidget(m_statusLabel);
    setCentralWidget(central);

    connect(m_layerTable, &QTableWidget::cellChanged, this, &LayoutEditorWindow::onCellChanged);
    connect(m_layerTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int previousRow, int) { onCurrentRowChanged(currentRow, previousRow); });
    connect(m_canvas, &LayoutCanvas::commandRequested, this, &LayoutEditorWindow::commandRequested);
    connect(m_canvas, &LayoutCanvas::objectDeletionRequested,
            this, &LayoutEditorWindow::onObjectDeletionRequested);
    connect(m_canvas, &LayoutCanvas::mouseWorldPositionChanged,
            this, &LayoutEditorWindow::onMouseWorldPositionChanged);
    connect(m_canvas, &LayoutCanvas::leftDragPreviewChanged,
            this, &LayoutEditorWindow::onLeftDragPreviewChanged);

    qApp->installEventFilter(this);

    m_canvas->setRootCell(m_rootCell.get());
    refreshStatusLabel();
}

LayoutEditorWindow::~LayoutEditorWindow() {
    qApp->removeEventFilter(this);
}

QSize LayoutEditorWindow::canvasViewportSize() const {
    return m_canvas->size();
}

void LayoutEditorWindow::setEditorIdentity(const int editorId, const bool isActive) {
    m_editorId = editorId;
    m_isActiveEditor = isActive;
    refreshWindowTitle();
}

bool LayoutEditorWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_layerTable && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const QString keySpec = keySpecFromEvent(keyEvent);
        if (!keySpec.isEmpty()) {
            emit commandRequested(QString("bindkey dispatch {%1}").arg(keySpec), true);
            keyEvent->accept();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

QTableWidgetItem* LayoutEditorWindow::makeReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

void LayoutEditorWindow::setLayers(const QVector<LayerDefinition>& layers) {
    m_internalUpdate = true;
    m_layers = layers;
    m_activeLayerName = layers.isEmpty() ? QString() : layers[0].name;
    m_activeLayerType = layers.isEmpty() ? QString() : layers[0].type;
    m_layerTable->setRowCount(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        applyLayerToRow(row, layers[row]);
    }

    m_internalUpdate = false;
    updateActiveLayerHighlight();
    m_canvas->setLayers(m_layers);
}

QBrush LayoutEditorWindow::makePatternBrush(const LayerDefinition& layer) const {
    return RenderTypes::patternBrushFor(layer.color, layer.pattern);
}

void LayoutEditorWindow::updateActiveLayerHighlight() {
    const QColor highlight(53, 86, 118, 130);

    const bool wasInternalUpdate = m_internalUpdate;
    m_internalUpdate = true;

    for (int row = 0; row < m_layers.size(); ++row) {
        const bool isActive = m_layers[row].name.compare(m_activeLayerName, Qt::CaseInsensitive) == 0
                              && m_layers[row].type.compare(m_activeLayerType, Qt::CaseInsensitive) == 0;
        for (int column = 1; column < m_layerTable->columnCount(); ++column) {
            if (auto* item = m_layerTable->item(row, column)) {
                item->setBackground(isActive ? QBrush(highlight) : QBrush(Qt::NoBrush));
            }
        }
    }

    m_internalUpdate = wasInternalUpdate;
}

void LayoutEditorWindow::applyLayerToRow(const int row, const LayerDefinition& layer) {
    auto* swatch = new QTableWidgetItem();
    swatch->setText(QString());
    swatch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    swatch->setSizeHint(QSize(24, 24));

    QPixmap swatchPixmap(16, 16);
    swatchPixmap.fill(QColor("#000000"));
    QPainter swatchPainter(&swatchPixmap);
    swatchPainter.fillRect(swatchPixmap.rect(), makePatternBrush(layer));
    swatchPainter.setPen(QColor("#1a1a1a"));
    swatchPainter.drawRect(swatchPixmap.rect().adjusted(0, 0, -1, -1));
    swatch->setIcon(QIcon(swatchPixmap));

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

    updateActiveLayerHighlight();
}

void LayoutEditorWindow::onLayerChanged(const int index, const LayerDefinition& layer) {
    if (index < 0 || index >= m_layers.size()) {
        return;
    }

    m_layers[index] = layer;

    m_internalUpdate = true;
    if (m_layerTable->item(index, 3)) {
        m_layerTable->item(index, 3)->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    }
    if (m_layerTable->item(index, 4)) {
        m_layerTable->item(index, 4)->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
    }
    m_internalUpdate = false;
    m_canvas->setLayers(m_layers);
}

void LayoutEditorWindow::onActiveLayerChanged(const QString& layerName, const QString& layerType) {
    m_activeLayerName = layerName;
    m_activeLayerType = layerType;
    refreshStatusLabel();

    m_internalUpdate = true;
    for (int row = 0; row < m_layers.size(); ++row) {
        if (m_layers[row].name.compare(layerName, Qt::CaseInsensitive) == 0
            && m_layers[row].type.compare(layerType, Qt::CaseInsensitive) == 0) {
            m_layerTable->setCurrentCell(row, 1);
            break;
        }
    }
    m_internalUpdate = false;

    updateActiveLayerHighlight();
}

void LayoutEditorWindow::onToolChanged(const QString& toolName) {
    m_activeTool = toolName;
    refreshStatusLabel();
    m_canvas->setActiveTool(toolName);
}

void LayoutEditorWindow::onMouseWorldPositionChanged(const qint64 worldX,
                                                     const qint64 worldY,
                                                     const bool insideCanvas) {
    m_mouseInsideCanvas = insideCanvas;
    if (insideCanvas) {
        m_mouseWorldX = worldX;
        m_mouseWorldY = worldY;
    }
    refreshStatusLabel();
}

void LayoutEditorWindow::onCanvasClickSelect(const qint64 worldX, const qint64 worldY) {
    m_canvas->applySelectionClick(worldX, worldY);
}

void LayoutEditorWindow::onCanvasDragSelect(const qint64 anchorX,
                                            const qint64 anchorY,
                                            const qint64 releaseX,
                                            const qint64 releaseY) {
    m_canvas->applySelectionDrag(anchorX, anchorY, releaseX, releaseY);
}

void LayoutEditorWindow::onLeftDragPreviewChanged(bool enabled,
                                                  qint64 anchorX,
                                                  qint64 anchorY,
                                                  qint64 currentX,
                                                  qint64 currentY) {
    if (!enabled) {
        m_canvas->setEditPreview(false, SceneRenderPrimitive{});
        return;
    }

    const auto it = std::find_if(m_layers.cbegin(), m_layers.cend(),
                                 [this](const LayerDefinition& layer) {
                                     return layer.name.compare(m_activeLayerName, Qt::CaseInsensitive) == 0
                                            && layer.type.compare(m_activeLayerType, Qt::CaseInsensitive) == 0;
                                 });
    if (it == m_layers.cend()) {
        m_canvas->setEditPreview(false, SceneRenderPrimitive{});
        return;
    }

    SceneRenderPrimitive primitive;
    if (LayoutEditPreviewModel::tryBuildPreviewPrimitive(m_activeTool,
                                                         it->nameId,
                                                         it->typeId,
                                                         anchorX,
                                                         anchorY,
                                                         currentX,
                                                         currentY,
                                                         primitive)) {
        m_canvas->setEditPreview(true, primitive);
    } else {
        m_canvas->setEditPreview(false, SceneRenderPrimitive{});
    }
}

void LayoutEditorWindow::refreshStatusLabel() {
    const QString layerPart = m_activeLayerName.isEmpty() || m_activeLayerType.isEmpty()
                                  ? "<none>"
                                  : QString("%1 (%2)").arg(m_activeLayerName, m_activeLayerType);
    const QString cursorPart = m_mouseInsideCanvas
                                   ? QString("X: %1 Y: %2").arg(m_mouseWorldX).arg(m_mouseWorldY)
                                   : "X: -- Y: --";
    m_statusLabel->setText(QString("Active layer: %1 | Tool: %2 | Cursor: %3")
                               .arg(layerPart, m_activeTool, cursorPart));
}

void LayoutEditorWindow::refreshWindowTitle() {
    const QString editorPart = m_editorId > 0 ? QString::number(m_editorId) : QString("?");
    const QString activeSuffix = m_isActiveEditor ? QString(" [active]") : QString();
    setWindowTitle(QString("Layout Editor %1%2").arg(editorPart, activeSuffix));
}

void LayoutEditorWindow::onViewChanged(const double zoom,
                                       const double panX,
                                       const double panY,
                                       const double gridSize) {
    m_canvas->setView(zoom, panX, panY, gridSize);
}

void LayoutEditorWindow::onEditPreviewChanged(const bool enabled, const SceneRenderPrimitive& primitive) {
    m_canvas->setEditPreview(enabled, primitive);
}

void LayoutEditorWindow::onPrimitiveCommitted(const SceneRenderPrimitive& primitive) {
    std::shared_ptr<LayoutObjectModel> object;
    if (!LayoutEditPreviewModel::tryBuildCommittedObject(m_activeTool, primitive, object) || !object) {
        return;
    }

    m_rootCell->addObject(object);
    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onSelectionPropertiesRequested() {
    m_canvas->triggerPropertiesDialog();
}

void LayoutEditorWindow::onObjectDeletionRequested(const quint64 objectId) {
    if (objectId == 0) {
        return;
    }

    if (!m_rootCell->removeObjectById(objectId)) {
        return;
    }

    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onCellChanged(const int row, const int column) {
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
    const bool currentValue = column == 3 ? current.visible : current.selectable;

    if (requestedValue == currentValue) {
        return;
    }

    m_internalUpdate = true;
    item->setCheckState(currentValue ? Qt::Checked : Qt::Unchecked);
    m_internalUpdate = false;

    const QString option = column == 3 ? "-visible" : "-selectable";
    emit commandRequested(QString("layer configure {%1} {%2} %3 %4")
                              .arg(current.name, current.type, option)
                              .arg(requestedValue ? 1 : 0),
                          true);
}

void LayoutEditorWindow::onCurrentRowChanged(const int currentRow, int) {
    if (m_internalUpdate || currentRow < 0 || currentRow >= m_layers.size()) {
        return;
    }

    emit commandRequested(QString("layer active {%1} {%2}")
                              .arg(m_layers[currentRow].name, m_layers[currentRow].type),
                          true);
}
