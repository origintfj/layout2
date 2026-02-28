#include "LayoutEditorWindow.h"

#include <QAbstractItemView>
#include <algorithm>
#include <QFrame>
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
#include <QtGlobal>
#include <cmath>

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
    const quint16 patternValue = static_cast<quint16>(pattern.toUInt(&ok, 0) & 0xFFFFu);
    if (!ok) {
        return QBrush(baseColor, Qt::SolidPattern);
    }

    QPixmap pixmap(8, 8);
    pixmap.fill(baseColor);
    QPainter painter(&pixmap);
    painter.setPen(QColor(0, 0, 0, 120));

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int bitIndex = ((y % 4) * 4) + (x % 4);
            if ((patternValue >> bitIndex) & 0x1u) {
                painter.drawPoint(x, y);
            }
        }
    }

    return QBrush(pixmap);
}

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
} // namespace

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

    void setRectangles(const QVector<DrawnRectangle>& rectangles) {
        m_rectangles = rectangles;
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

signals:
    void commandRequested(const QString& command);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        // Background and world-anchored grid for orientation.
        painter.fillRect(rect(), QColor("#101820"));
        painter.setPen(QPen(QColor("#2a2a2a"), 1));
        drawGrid(painter);

        // Draw committed geometry first.
        for (const DrawnRectangle& r : m_rectangles) {
            drawRectangle(painter, r, false);
        }

        // Draw rubber-band preview on top.
        if (m_previewEnabled) {
            drawRectangle(painter, m_preview, true);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        const QString keySpec = keySpecFromEvent(event);
        if (!keySpec.isEmpty()) {
            emit commandRequested(QString("bindkey dispatch {%1}").arg(keySpec));
            event->accept();
            return;
        }

        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            emit commandRequested(QString("canvas press %1 %2 1")
                                      .arg(static_cast<qint64>(world.x()))
                                      .arg(static_cast<qint64>(world.y())));
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
        const bool leftDown = event->buttons() & Qt::LeftButton;
        emit commandRequested(QString("canvas move %1 %2 %3")
                                  .arg(static_cast<qint64>(world.x()))
                                  .arg(static_cast<qint64>(world.y()))
                                  .arg(leftDown ? 1 : 0));

        // Middle-button drag emits view pan commands.
        if (m_middlePanning && (event->buttons() & Qt::MiddleButton)) {
            const QPointF delta = mouseEventPoint(event) - m_lastPanPoint;
            m_lastPanPoint = mouseEventPoint(event);
            emit commandRequested(QString("view pan %1 %2")
                                      .arg(delta.x())
                                      .arg(delta.y()));
        }

        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            emit commandRequested(QString("canvas release %1 %2 1")
                                      .arg(static_cast<qint64>(world.x()))
                                      .arg(static_cast<qint64>(world.y())));
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
                                  .arg(pos.y()));
        event->accept();
    }

private:
    // Converts world integer coordinates into screen-space doubles.
    QPointF worldToScreen(qint64 x, qint64 y) const {
        return QPointF((static_cast<double>(x) * m_zoom) + m_panX,
                       (static_cast<double>(y) * m_zoom) + m_panY);
    }

    // Converts screen coordinates into world-space doubles.
    QPointF screenToWorld(const QPointF& p) const {
        return QPointF((p.x() - m_panX) / m_zoom,
                       (p.y() - m_panY) / m_zoom);
    }

    // Shared draw helper for committed and preview rectangles.
    void drawRectangle(QPainter& painter, const DrawnRectangle& r, bool preview) {
        QPointF p1 = worldToScreen(r.x1, r.y1);
        QPointF p2 = worldToScreen(r.x2, r.y2);
        QRectF rect = QRectF(p1, p2).normalized();

        QColor fillColor = r.color;
        fillColor.setAlpha(preview ? 90 : 140);

        QColor outlineColor = r.color;
        outlineColor.setAlpha(preview ? 180 : 220);

        painter.setPen(QPen(outlineColor, 1, preview ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(patternBrushFor(fillColor, r.pattern));
        painter.drawRect(rect);
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

        for (double worldX = firstGridX; worldX <= worldMaxX; worldX += visibleStep) {
            const double screenX = (worldX * m_zoom) + m_panX;
            painter.drawLine(QPointF(screenX, 0.0), QPointF(screenX, height()));
        }

        for (double worldY = firstGridY; worldY <= worldMaxY; worldY += visibleStep) {
            const double screenY = (worldY * m_zoom) + m_panY;
            painter.drawLine(QPointF(0.0, screenY), QPointF(width(), screenY));
        }
    }

    QVector<DrawnRectangle> m_rectangles;
    DrawnRectangle m_preview;

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
      m_statusLabel(new QLabel()) {
    setWindowTitle("Layout Editor");
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
    m_layerTable->setHorizontalHeaderLabels({"Style", "Layer", "Type", "Visible", "Selectable"});
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
    m_layerTable->setRowCount(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        applyLayerToRow(row, layers[row]);
    }

    m_internalUpdate = false;
    updateActiveLayerHighlight();
}

QBrush LayoutEditorWindow::makePatternBrush(const LayerDefinition& layer) const {
    return patternBrushFor(layer.color, layer.pattern);
}

void LayoutEditorWindow::updateActiveLayerHighlight() {
    const QColor highlight(53, 86, 118, 130);

    const bool wasInternalUpdate = m_internalUpdate;
    m_internalUpdate = true;

    for (int row = 0; row < m_layers.size(); ++row) {
        const bool isActive = m_layers[row].name.compare(m_activeLayerName, Qt::CaseInsensitive) == 0;
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
    swatchPixmap.fill(Qt::transparent);
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
}

void LayoutEditorWindow::onActiveLayerChanged(const QString& layerName) {
    // Update status label while preserving the existing tool text suffix.
    QString status = m_statusLabel->text();
    const int toolIdx = status.indexOf("| Tool:");
    const QString toolPart = toolIdx >= 0 ? status.mid(toolIdx + 2) : "Tool: <none>";
    m_statusLabel->setText(QString("Active layer: %1 | %2").arg(layerName, toolPart));

    // Highlight corresponding row (without retriggering command emission).
    m_activeLayerName = layerName;
    m_internalUpdate = true;
    for (int row = 0; row < m_layers.size(); ++row) {
        if (m_layers[row].name.compare(layerName, Qt::CaseInsensitive) == 0) {
            m_layerTable->setCurrentCell(row, 1);
            break;
        }
    }
    m_internalUpdate = false;

    updateActiveLayerHighlight();
}

void LayoutEditorWindow::onToolChanged(const QString& toolName) {
    QString status = m_statusLabel->text();
    const int idx = status.indexOf("| Tool:");
    if (idx >= 0) {
        status = status.left(idx).trimmed();
    }
    m_statusLabel->setText(QString("%1 | Tool: %2").arg(status, toolName));
}

void LayoutEditorWindow::onViewChanged(double zoom, double panX, double panY, double gridSize) {
    m_canvas->setView(zoom, panX, panY, gridSize);
}

void LayoutEditorWindow::onRectanglePreviewChanged(bool enabled, const DrawnRectangle& rectangle) {
    m_canvas->setPreview(enabled, rectangle);
}

void LayoutEditorWindow::onRectangleCommitted(const DrawnRectangle& rectangle) {
    m_rectangles.push_back(rectangle);
    m_canvas->setRectangles(m_rectangles);
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
    emit commandRequested(QString("layer configure %1 %2 %3")
                              .arg(current.name, option)
                              .arg(requestedValue ? 1 : 0));
}

void LayoutEditorWindow::onCurrentRowChanged(int currentRow, int) {
    if (m_internalUpdate || currentRow < 0 || currentRow >= m_layers.size()) {
        return;
    }

    // Row selection sets active layer via Tcl command.
    emit commandRequested(QString("layer active %1").arg(m_layers[currentRow].name));
}

#include "LayoutEditorWindow.moc"
