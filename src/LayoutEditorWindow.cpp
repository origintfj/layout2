#include "LayoutEditorWindow.h"
#include "LayoutSceneModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <algorithm>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QtGlobal>
#include <cmath>
#include <memory>

namespace {
// Qt5/Qt6 compatibility helper for mouse event coordinates.
QPointF mouseEventPoint(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

// Qt5/Qt6 compatibility helper for wheel event coordinates.
QPointF wheelEventPoint(const QWheelEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return QPointF(event->pos());
#endif
}

QBrush patternBrushFor(QColor baseColor, const QString& pattern) {
    bool ok = false;
    const quint64 patternValue = static_cast<quint64>(pattern.toULongLong(&ok, 0));
    if (!ok) {
        return QBrush(baseColor, Qt::SolidPattern);
    }

    QPixmap pixmap(8, 8);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(baseColor);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int bitIndex = (y * 8) + x;
            if ((patternValue >> bitIndex) & 0x1ULL) {
                painter.drawPoint(x, y);
            }
        }
    }

    return QBrush(pixmap);
}

quint64 layerCodeKey(quint32 nameId, quint32 typeId) {
    return (static_cast<quint64>(nameId) << 32) | static_cast<quint64>(typeId);
}
} // namespace

bool isModifierOnlyKey(int key) {
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

    Qt::KeyboardModifiers modifiers = event->modifiers();

    const QKeySequence sequence(modifiers | key);
    return sequence.toString(QKeySequence::PortableText);
}

// LayoutCanvas is the drawable area on the right side of the editor.
//
// It is intentionally thin: all interactions are converted into Tcl command
// strings and emitted upward via commandRequested().
class LayoutCanvas : public QWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    void setRootCell(const LayoutSceneNode* rootCell) {
        m_rootCell = rootCell;
        validateHover();
        update();
    }

    void setPreview(bool enabled, const DrawnRectangle& rectangle) {
        m_previewEnabled = enabled;
        m_preview = rectangle;
        update();
    }

    void setView(double zoom, double panX, double panY, double gridSize) {
        m_zoom = zoom;
        m_panX = panX;
        m_panY = panY;
        m_gridSize = gridSize;
        update();
    }

    void setLayers(const QVector<LayerDefinition>& layers) {
        m_layers = layers;
        rebuildLayerLookup();
        validateSelection();
        validateHover();
        update();
    }

    void setActiveTool(const QString& toolName) {
        m_activeTool = toolName;
        if (m_activeTool != "select") {
            m_hoveredIndex = -1;
        }
        update();
    }

signals:
    void commandRequested(const QString& command, bool requestActivation);
    void rectangleDeletionRequested(int rectangleIndex);
    void mouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        // Background and world-anchored grid for orientation.
        painter.fillRect(rect(), QColor("#000000"));
        drawGrid(painter);

        // Draw committed geometry first.
        const QVector<const DrawnRectangle*> rectangles = flattenedRectangles();
        for (int i = 0; i < rectangles.size(); ++i) {
            const DrawnRectangle& r = *rectangles[i];
            const LayerDefinition* layer = layerForRectangle(r);
            if (!layer || !layer->visible) {
                continue;
            }
            drawRectangle(painter, r, false, i == m_selectedIndex);
        }

        if (m_activeTool == "select" && m_hoveredIndex >= 0 && m_hoveredIndex < rectangles.size()) {
            drawHoverOutline(painter, *rectangles[m_hoveredIndex]);
        }

        // Draw rubber-band preview on top.
        if (m_previewEnabled) {
            drawRectangle(painter, m_preview, true, false);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            && m_selectedIndex >= 0
            && m_selectedIndex < flattenedRectangles().size()) {
            emit rectangleDeletionRequested(m_selectedIndex);
            m_selectedIndex = -1;
            update();
            event->accept();
            return;
        }

        const QString keySpec = keySpecFromEvent(event);
        if (!keySpec.isEmpty()) {
            emit commandRequested(QString("bindkey dispatch {%1}").arg(keySpec), true);
            event->accept();
            return;
        }

        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            const qint64 worldX = static_cast<qint64>(world.x());
            const qint64 worldY = static_cast<qint64>(world.y());

            if (m_activeTool == "select") {
                handleSelectionClick(worldX, worldY);
                event->accept();
                return;
            }

            emit commandRequested(QString("canvas press %1 %2 1")
                                      .arg(worldX)
                                      .arg(worldY),
                                true);
        }

        if (event->button() == Qt::MiddleButton) {
            m_lastPanPoint = mouseEventPoint(event);
            m_middlePanning = true;
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        // Move events carry current cursor position + left-button state.
        const QPointF world = screenToWorld(mouseEventPoint(event));
        const qint64 worldX = static_cast<qint64>(world.x());
        const qint64 worldY = static_cast<qint64>(world.y());
        emit mouseWorldPositionChanged(worldX, worldY, true);
        const bool leftDown = event->buttons() & Qt::LeftButton;

        if (m_activeTool == "select") {
            m_hoveredIndex = hoveredSelectableIndexAt(worldX, worldY);
            update();
        }

        emit commandRequested(QString("canvas move %1 %2 %3")
                                  .arg(worldX)
                                  .arg(worldY)
                                  .arg(leftDown ? 1 : 0),
                            false);

        // Middle-button drag emits view pan commands.
        if (m_middlePanning && (event->buttons() & Qt::MiddleButton)) {
            const QPointF delta = mouseEventPoint(event) - m_lastPanPoint;
            m_lastPanPoint = mouseEventPoint(event);
            emit commandRequested(QString("view pan %1 %2")
                                      .arg(delta.x())
                                      .arg(delta.y()),
                                false);
        }

        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        emit mouseWorldPositionChanged(0, 0, false);
        if (m_hoveredIndex != -1) {
            m_hoveredIndex = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            emit commandRequested(QString("canvas release %1 %2 1")
                                      .arg(static_cast<qint64>(world.x()))
                                      .arg(static_cast<qint64>(world.y())),
                                false);
        }

        if (event->button() == Qt::MiddleButton) {
            m_middlePanning = false;
        }

        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        const QPointF pos = wheelEventPoint(event);
        emit commandRequested(QString("view zoom %1 %2 %3")
                                  .arg(event->angleDelta().y())
                                  .arg(pos.x())
                                  .arg(pos.y()),
                            false);
        event->accept();
    }

private:
    // Converts world integer coordinates into screen-space doubles.
    QPointF worldToScreen(qint64 x, qint64 y) const {
        return QPointF((static_cast<double>(x) * m_zoom) + m_panX,
                       m_panY - (static_cast<double>(y) * m_zoom));
    }

    // Converts screen coordinates into world-space doubles.
    QPointF screenToWorld(const QPointF& p) const {
        return QPointF((p.x() - m_panX) / m_zoom,
                       (m_panY - p.y()) / m_zoom);
    }

    // Shared draw helper for committed and preview rectangles.
    void drawRectangle(QPainter& painter, const DrawnRectangle& r, bool preview, bool selected) {
        const LayerDefinition* layer = layerForRectangle(r);
        if (!layer) {
            return;
        }
        QPointF p1 = worldToScreen(r.x1, r.y1);
        QPointF p2 = worldToScreen(r.x2, r.y2);
        QRectF rect = QRectF(p1, p2).normalized();

        QColor fillColor = layer->color;
        if (selected) {
            fillColor = fillColor.lighter(130);
        }
        fillColor.setAlpha(preview ? 90 : 140);

        QColor outlineColor = layer->color;
        outlineColor.setAlpha(preview ? 180 : 220);
        if (selected) {
            outlineColor = QColor("#ffffff");
            outlineColor.setAlpha(255);
        }

        painter.setPen(QPen(outlineColor, selected ? 1 : 1, preview ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(patternBrushFor(fillColor, layer->pattern));
        painter.drawRect(rect);
    }

    void drawHoverOutline(QPainter& painter, const DrawnRectangle& rectangle) {
        QPointF p1 = worldToScreen(rectangle.x1, rectangle.y1);
        QPointF p2 = worldToScreen(rectangle.x2, rectangle.y2);
        QRectF rect = QRectF(p1, p2).normalized();

        painter.setPen(QPen(QColor("#ffd400"), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    }

    const LayerDefinition* layerForRectangle(const DrawnRectangle& rectangle) const {
        const auto it = m_layerIndexByCode.constFind(layerCodeKey(rectangle.layerNameId, rectangle.layerTypeId));
        if (it == m_layerIndexByCode.cend()) {
            return nullptr;
        }

        const int index = it.value();
        if (index < 0 || index >= m_layers.size()) {
            return nullptr;
        }

        return &m_layers[index];
    }

    void rebuildLayerLookup() {
        m_layerIndexByCode.clear();

        // Lookup index is independent of palette order and optimized for ID-based queries.
        for (int i = 0; i < m_layers.size(); ++i) {
            m_layerIndexByCode.insert(layerCodeKey(m_layers[i].nameId, m_layers[i].typeId), i);
        }
    }

    bool isSelectableRectangle(const DrawnRectangle& rectangle) const {
        const LayerDefinition* layer = layerForRectangle(rectangle);
        return layer && layer->visible && layer->selectable;
    }

    QVector<const DrawnRectangle*> flattenedRectangles() const {
        QVector<const DrawnRectangle*> rectangles;
        if (!m_rootCell) {
            return rectangles;
        }

        m_rootCell->collectRectangles(rectangles);
        return rectangles;
    }

    QVector<int> rectangleIndexByObjectIndex() const {
        QVector<int> indexByObject;
        if (!m_rootCell) {
            return indexByObject;
        }

        QVector<const LayoutObjectModel*> objects;
        m_rootCell->collectObjects(objects);
        indexByObject.fill(-1, objects.size());

        int rectangleIndex = 0;
        for (int i = 0; i < objects.size(); ++i) {
            if (objects[i] && objects[i]->asRectangle()) {
                indexByObject[i] = rectangleIndex;
                ++rectangleIndex;
            }
        }

        return indexByObject;
    }

    QVector<int> selectableRectangleCandidatesAt(qint64 x, qint64 y) const {
        QVector<int> candidates;
        if (!m_rootCell) {
            return candidates;
        }

        const QVector<int> objectMatches = m_rootCell->matchingObjectIndicesAt(
            x,
            y,
            [this](const LayoutObjectModel& object) {
                const DrawnRectangle* rectangle = object.asRectangle();
                return rectangle && isSelectableRectangle(*rectangle);
            });
        if (objectMatches.isEmpty()) {
            return candidates;
        }

        const QVector<int> rectangleByObject = rectangleIndexByObjectIndex();
        for (int objectIndex : objectMatches) {
            if (objectIndex < 0 || objectIndex >= rectangleByObject.size()) {
                continue;
            }

            const int rectangleIndex = rectangleByObject[objectIndex];
            if (rectangleIndex >= 0) {
                candidates.push_back(rectangleIndex);
            }
        }

        return candidates;
    }

    int hoveredSelectableIndexAt(qint64 x, qint64 y) const {
        const QVector<int> candidates = selectableRectangleCandidatesAt(x, y);
        return candidates.isEmpty() ? -1 : candidates.front();
    }

    void handleSelectionClick(qint64 x, qint64 y) {
        const QVector<int> candidates = selectableRectangleCandidatesAt(x, y);
        if (candidates.isEmpty()) {
            m_selectedIndex = -1;
            m_lastSelectionCandidates.clear();
            m_lastSelectionPoint = QPointF();
            update();
            return;
        }

        const QPointF selectionPoint(static_cast<double>(x), static_cast<double>(y));
        const bool samePoint = m_hasSelectionPoint && m_lastSelectionPoint == selectionPoint;
        const bool sameCandidates = samePoint && (candidates == m_lastSelectionCandidates);

        if (!sameCandidates) {
            m_selectedIndex = candidates.front();
        } else {
            int currentCandidate = candidates.indexOf(m_selectedIndex);
            if (currentCandidate < 0) {
                currentCandidate = 0;
            }
            m_selectedIndex = candidates[(currentCandidate + 1) % candidates.size()];
        }

        m_lastSelectionCandidates = candidates;
        m_lastSelectionPoint = selectionPoint;
        m_hasSelectionPoint = true;
        update();
    }

    void validateSelection() {
        const QVector<const DrawnRectangle*> rectangles = flattenedRectangles();
        if (m_selectedIndex < 0 || m_selectedIndex >= rectangles.size()) {
            m_selectedIndex = -1;
            return;
        }

        const DrawnRectangle& rectangle = *rectangles[m_selectedIndex];
        if (!isSelectableRectangle(rectangle)) {
            m_selectedIndex = -1;
        }
    }

    void validateHover() {
        const QVector<const DrawnRectangle*> rectangles = flattenedRectangles();
        if (m_hoveredIndex < 0 || m_hoveredIndex >= rectangles.size()) {
            m_hoveredIndex = -1;
            return;
        }

        const DrawnRectangle& rectangle = *rectangles[m_hoveredIndex];
        if (!isSelectableRectangle(rectangle)) {
            m_hoveredIndex = -1;
        }
    }

    void drawGrid(QPainter& painter) {
        if (m_gridSize <= 0.0 || m_zoom <= 0.0) {
            return;
        }

        // Prevent very dense grid rendering when zoomed out by adaptively
        // increasing the displayed step while keeping it world-anchored.
        const double minimumPixelSpacing = 10.0;
        const double basePixelSpacing = m_gridSize * m_zoom;
        const double multiplier = std::max(1.0, std::ceil(minimumPixelSpacing / basePixelSpacing));
        const double visibleStep = m_gridSize * multiplier;

        const QPointF topLeftWorld = screenToWorld(QPointF(0.0, 0.0));
        const QPointF bottomRightWorld = screenToWorld(QPointF(width(), height()));

        const double worldMinX = std::min(topLeftWorld.x(), bottomRightWorld.x());
        const double worldMaxX = std::max(topLeftWorld.x(), bottomRightWorld.x());
        const double worldMinY = std::min(topLeftWorld.y(), bottomRightWorld.y());
        const double worldMaxY = std::max(topLeftWorld.y(), bottomRightWorld.y());

        const double firstGridX = std::floor(worldMinX / visibleStep) * visibleStep;
        const double firstGridY = std::floor(worldMinY / visibleStep) * visibleStep;

        painter.setPen(QPen(QColor("#5a5a5a"), 1));
        for (double worldX = firstGridX; worldX <= worldMaxX; worldX += visibleStep) {
            const double screenX = (worldX * m_zoom) + m_panX;
            for (double worldY = firstGridY; worldY <= worldMaxY; worldY += visibleStep) {
                const double screenY = m_panY - (worldY * m_zoom);
                painter.drawPoint(QPointF(screenX, screenY));
            }
        }

        painter.setPen(QPen(QColor("#5a5a5a"), 1));
        const double originScreenX = m_panX;
        const double originScreenY = m_panY;

        if (originScreenX >= 0.0 && originScreenX <= width()) {
            painter.drawLine(QPointF(originScreenX, 0.0), QPointF(originScreenX, height()));
        }

        if (originScreenY >= 0.0 && originScreenY <= height()) {
            painter.drawLine(QPointF(0.0, originScreenY), QPointF(width(), originScreenY));
        }
    }

    const LayoutSceneNode* m_rootCell{nullptr};
    QVector<LayerDefinition> m_layers;
    QHash<quint64, int> m_layerIndexByCode;
    DrawnRectangle m_preview;
    QString m_activeTool{"none"};

    int m_selectedIndex{-1};
    int m_hoveredIndex{-1};
    QVector<int> m_lastSelectionCandidates;
    QPointF m_lastSelectionPoint;
    bool m_hasSelectionPoint{false};

    bool m_previewEnabled{false};
    bool m_middlePanning{false};

    QPointF m_lastPanPoint;
    double m_zoom{1.0};
    double m_panX{0.0};
    double m_panY{0.0};
    double m_gridSize{40.0};
};

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

    // Left pane: layer palette table.
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

    // Right pane: interactive canvas.
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

    // UI events are converted into Tcl command strings.
    connect(m_layerTable, &QTableWidget::cellChanged, this, &LayoutEditorWindow::onCellChanged);
    connect(m_layerTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int previousRow, int) { onCurrentRowChanged(currentRow, previousRow); });
    connect(m_canvas, &LayoutCanvas::commandRequested, this, &LayoutEditorWindow::commandRequested);
    connect(m_canvas, &LayoutCanvas::rectangleDeletionRequested,
            this, &LayoutEditorWindow::onRectangleDeletionRequested);
    connect(m_canvas, &LayoutCanvas::mouseWorldPositionChanged,
            this, &LayoutEditorWindow::onMouseWorldPositionChanged);

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
    if (event->type() == QEvent::MouseButtonPress) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (widget->window() == this) {
                emit activationRequested();
            }
        }
    }

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
    return patternBrushFor(layer.color, layer.pattern);
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

void LayoutEditorWindow::applyLayerToRow(int row, const LayerDefinition& layer) {
    // Color/pattern swatch column.
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

    // Name and type columns.
    m_layerTable->setItem(row, 1, makeReadOnlyItem(layer.name));
    m_layerTable->setItem(row, 2, makeReadOnlyItem(layer.type));

    // Visibility and selectability checkboxes.
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

void LayoutEditorWindow::onLayerChanged(int index, const LayerDefinition& layer) {
    if (index < 0 || index >= m_layers.size()) {
        return;
    }

    m_layers[index] = layer;

    // Apply state from model while muting feedback events.
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

    // Highlight corresponding row (without retriggering command emission).
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

void LayoutEditorWindow::onMouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas) {
    m_mouseInsideCanvas = insideCanvas;
    if (insideCanvas) {
        m_mouseWorldX = worldX;
        m_mouseWorldY = worldY;
    }
    refreshStatusLabel();
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

void LayoutEditorWindow::onViewChanged(double zoom, double panX, double panY, double gridSize) {
    m_canvas->setView(zoom, panX, panY, gridSize);
}

void LayoutEditorWindow::onRectanglePreviewChanged(bool enabled, const DrawnRectangle& rectangle) {
    m_canvas->setPreview(enabled, rectangle);
}

void LayoutEditorWindow::onRectangleCommitted(const DrawnRectangle& rectangle) {
    m_rootCell->addObject(std::make_shared<RectangleObjectModel>(rectangle));
    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onRectangleDeletionRequested(int rectangleIndex) {
    if (rectangleIndex < 0) {
        return;
    }

    if (!m_rootCell->removeRectangleAt(rectangleIndex)) {
        return;
    }

    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onCellChanged(int row, int column) {
    if (m_internalUpdate || row < 0 || row >= m_layers.size()) {
        return;
    }

    // Only visibility/selectability columns map to layer configure commands.
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

    // Non-checkstate updates (for example row highlight styling) can also trigger
    // cellChanged; ignore those when the value did not actually change.
    if (requestedValue == currentValue) {
        return;
    }

    // Revert immediate local visual change; Tcl command execution drives truth.
    m_internalUpdate = true;
    item->setCheckState(currentValue ? Qt::Checked : Qt::Unchecked);
    m_internalUpdate = false;

    const QString option = column == 3 ? "-visible" : "-selectable";
    emit commandRequested(QString("layer configure {%1} {%2} %3 %4")
                              .arg(current.name, current.type, option)
                              .arg(requestedValue ? 1 : 0),
                        true);
}

void LayoutEditorWindow::onCurrentRowChanged(int currentRow, int) {
    if (m_internalUpdate || currentRow < 0 || currentRow >= m_layers.size()) {
        return;
    }

    // Row selection sets active layer via Tcl command.
    emit commandRequested(QString("layer active {%1} {%2}")
                              .arg(m_layers[currentRow].name, m_layers[currentRow].type),
                        true);
}

#include "LayoutEditorWindow.moc"
