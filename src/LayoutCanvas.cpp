#include "LayoutCanvas.h"

#include "LayoutSceneModel.h"
#include "PrimitiveRenderBackend.h"
#include "SelectionPropertiesDialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {

QPointF mouseEventPoint(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

QPointF wheelEventPoint(const QWheelEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return QPointF(event->position());
#endif
}

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

LayoutCanvas::LayoutCanvas(QWidget* parent)
    : QOpenGLWidget(parent),
      m_backendType(RenderTypes::backendTypeFromEnv()),
      m_renderBackend(createPrimitiveRenderBackend(m_backendType)) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

LayoutCanvas::~LayoutCanvas() = default;

bool LayoutCanvas::event(QEvent* event) {
    // QOpenGLWidget + NoPartialUpdate can briefly show stale pixels when the
    // widget becomes exposed again. Request a redraw on key lifecycle events
    // so reveal/uncover paths do not wait for mouse movement to repaint.
    switch (event->type()) {
    case QEvent::Expose:
    case QEvent::Show:
    case QEvent::Resize:
        // Queue a repaint (instead of forcing immediate repaint) to keep Qt's
        // normal event coalescing and avoid recursive paint behavior.
        if (isVisible()) {
            update();
        }
        break;
    default:
        break;
    }

    return QOpenGLWidget::event(event);
}

void LayoutCanvas::setRootCell(const LayoutSceneNode* rootCell) {
    m_rootCell = rootCell;
    validateHover();
    update();
}

void LayoutCanvas::setEditPreview(const bool enabled, const SceneRenderPrimitive& primitive) {
    m_editPreviewEnabled = enabled;
    m_editPreview = primitive;
    update();
}

void LayoutCanvas::setView(const double zoom, const double panX, const double panY, const double gridSize) {
    m_zoom = zoom;
    m_panX = panX;
    m_panY = panY;
    m_gridSize = gridSize;
    update();
}

void LayoutCanvas::setLayers(const QVector<LayerDefinition>& layers) {
    m_layers = layers;
    rebuildLayerLookup();
    m_fillBrushCache.clear();
    validateSelection();
    validateHover();
    update();
}

void LayoutCanvas::setActiveTool(const QString& toolName) {
    m_activeTool = toolName;
    if (m_activeTool != "select") {
        m_hoveredObjectId = 0;
    }
    update();
}

void LayoutCanvas::triggerPropertiesDialog() {
    showPropertiesDialog();
}

void LayoutCanvas::applySelectionClick(const qint64 worldX, const qint64 worldY) {
    handleSelectionClick(worldX, worldY);
}

void LayoutCanvas::applySelectionDrag(const qint64 anchorX,
                                      const qint64 anchorY,
                                      const qint64 currentX,
                                      const qint64 currentY) {
    handleSelectionDrag(anchorX, anchorY, currentX, currentY);
}

void LayoutCanvas::paintGL() {
    if (m_backendType == RenderTypes::BackendType::OpenGL) {
        if (QOpenGLContext* ctx = context()) {
            if (QOpenGLFunctions* gl = ctx->functions()) {
                gl->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
                gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }
        }
    }

    QPainter painter(this);

    // Draw order is intentionally explicit here: background/grid, committed
    // geometry via the selected backend, then editor-only overlays.
    m_renderBackend->beginFrame(painter, QColor("#000000"), size());
    drawGrid(painter);

    const RenderDetailLevel detailLevel = currentDetailLevel();
    const QVector<SceneRenderPrimitive> primitives = flattenedRenderPrimitives();
    const QVector<RenderTypes::RenderItem> renderItems = buildRenderItems(primitives, detailLevel);
    m_renderBackend->drawPrimitives(painter, renderItems, size());

    if (m_activeTool == "select" && m_hoveredObjectId != 0 && m_rootCell) {
        QVector<WorldLineSegment> previewSegments;
        if (m_rootCell->collectOutlineSegmentsByObjectId(m_hoveredObjectId, previewSegments)) {
            drawHoverOutline(painter, previewSegments);
        }
    }

    drawDialogSelectionOutlines(painter);
    drawLeftDragPreview(painter);
    m_renderBackend->endFrame(painter, size());
}

void LayoutCanvas::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && !m_selectedObjectIds.isEmpty()) {
        const QVector<quint64> selectedIds = m_selectedObjectIds.values().toVector();
        for (const quint64 objectId : selectedIds) {
            emit objectDeletionRequested(objectId);
        }

        m_selectedObjectIds.clear();
        m_selectedObjectId = 0;
        refreshPropertiesDialogIfOpen();
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

    QOpenGLWidget::keyPressEvent(event);
}

void LayoutCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPointF world = screenToWorld(mouseEventPoint(event));
        const qint64 worldX = static_cast<qint64>(world.x());
        const qint64 worldY = static_cast<qint64>(world.y());
        m_leftAnchorX = worldX;
        m_leftAnchorY = worldY;
        m_leftCurrentX = worldX;
        m_leftCurrentY = worldY;
        m_leftDragActive = true;
        m_leftDragMoved = false;
        emit leftDragPreviewChanged(true, m_leftAnchorX, m_leftAnchorY, m_leftCurrentX, m_leftCurrentY);
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_lastPanPoint = mouseEventPoint(event);
        m_middlePanning = true;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void LayoutCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF world = screenToWorld(mouseEventPoint(event));
    const qint64 worldX = static_cast<qint64>(world.x());
    const qint64 worldY = static_cast<qint64>(world.y());
    emit mouseWorldPositionChanged(worldX, worldY, true);
    const bool leftDown = event->buttons() & Qt::LeftButton;

    if (m_activeTool == "select") {
        m_hoveredObjectId = hoveredSelectableObjectIdAt(worldX, worldY);
    }

    if (m_leftDragActive && leftDown) {
        m_leftCurrentX = worldX;
        m_leftCurrentY = worldY;
        m_leftDragMoved = true;
        emit leftDragPreviewChanged(true, m_leftAnchorX, m_leftAnchorY, m_leftCurrentX, m_leftCurrentY);
        update();
    } else if (m_activeTool == "select") {
        update();
    }

    if (m_middlePanning && (event->buttons() & Qt::MiddleButton)) {
        const QPointF delta = mouseEventPoint(event) - m_lastPanPoint;
        m_lastPanPoint = mouseEventPoint(event);
        emit commandRequested(QString("view pan %1 %2").arg(delta.x()).arg(delta.y()), false);
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void LayoutCanvas::leaveEvent(QEvent* event) {
    emit mouseWorldPositionChanged(0, 0, false);
    if (m_hoveredObjectId != 0) {
        m_hoveredObjectId = 0;
        update();
    }
    QOpenGLWidget::leaveEvent(event);
}

void LayoutCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPointF world = screenToWorld(mouseEventPoint(event));
        const qint64 worldX = static_cast<qint64>(world.x());
        const qint64 worldY = static_cast<qint64>(world.y());

        if (m_leftDragActive) {
            m_leftCurrentX = worldX;
            m_leftCurrentY = worldY;
            const bool didDrag = m_leftDragMoved
                                 || m_leftAnchorX != m_leftCurrentX
                                 || m_leftAnchorY != m_leftCurrentY;
            m_leftDragActive = false;
            emit leftDragPreviewChanged(false, m_leftAnchorX, m_leftAnchorY, m_leftCurrentX, m_leftCurrentY);

            // Keep gesture decoding local to the canvas, but route the final
            // action through Tcl so GUI gestures and typed commands share the
            // same policy/selection entry points.
            if (didDrag) {
                emit commandRequested(QString("canvas drag %1 %2 %3 %4")
                                          .arg(m_leftAnchorX)
                                          .arg(m_leftAnchorY)
                                          .arg(m_leftCurrentX)
                                          .arg(m_leftCurrentY),
                                      true);
            } else {
                emit commandRequested(QString("canvas click %1 %2").arg(worldX).arg(worldY), true);
            }
            update();
        }
    }

    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = false;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void LayoutCanvas::wheelEvent(QWheelEvent* event) {
    const QPointF pos = wheelEventPoint(event);
    emit commandRequested(QString("view zoom %1 %2 %3")
                              .arg(event->angleDelta().y())
                              .arg(pos.x())
                              .arg(pos.y()),
                          false);
    // Zoom is applied through the Tcl command path. Queue a repaint
    // immediately as well so the canvas refresh is not dependent on a
    // subsequent mouse-move event.
    update();
    event->accept();
}

QPointF LayoutCanvas::worldToScreen(const qint64 x, const qint64 y) const {
    return QPointF((static_cast<double>(x) * m_zoom) + m_panX,
                   m_panY - (static_cast<double>(y) * m_zoom));
}

QPointF LayoutCanvas::screenToWorld(const QPointF& p) const {
    return QPointF((p.x() - m_panX) / m_zoom,
                   (m_panY - p.y()) / m_zoom);
}

void LayoutCanvas::applyDialogSelectionToCanvas(const QSet<quint64>& dialogSelectedObjectIds,
                                                const bool keepSelectedInPane) {
    QSet<quint64> nextSelectedIds = m_selectedObjectIds;
    if (keepSelectedInPane) {
        nextSelectedIds.intersect(dialogSelectedObjectIds);
    } else {
        for (const quint64 objectId : dialogSelectedObjectIds) {
            nextSelectedIds.remove(objectId);
        }
    }

    m_selectedObjectIds = nextSelectedIds;
    if (!m_selectedObjectIds.contains(m_selectedObjectId)) {
        m_selectedObjectId = m_selectedObjectIds.isEmpty() ? 0 : *m_selectedObjectIds.cbegin();
    }

    validateSelection();
    m_dialogSelectedObjectIds = dialogSelectedObjectIds;
    refreshPropertiesDialogIfOpen();
    update();
}

void LayoutCanvas::refreshPropertiesDialogIfOpen() {
    SelectionPropertiesDialog::refreshIfOpen(this,
                                             m_rootCell,
                                             m_selectedObjectIds,
                                             [this](const QSet<quint64>& dialogSelectedObjectIds) {
                                                 m_dialogSelectedObjectIds = dialogSelectedObjectIds;
                                                 update();
                                             },
                                             [this](const QSet<quint64>& dialogSelectedObjectIds,
                                                    const bool keepSelectedInPane) {
                                                 applyDialogSelectionToCanvas(dialogSelectedObjectIds, keepSelectedInPane);
                                             });
}

void LayoutCanvas::showPropertiesDialog() {
    SelectionPropertiesDialog::show(this,
                                    m_rootCell,
                                    m_selectedObjectIds,
                                    [this](const QSet<quint64>& dialogSelectedObjectIds) {
                                        m_dialogSelectedObjectIds = dialogSelectedObjectIds;
                                        update();
                                    },
                                    [this](const QSet<quint64>& dialogSelectedObjectIds,
                                           const bool keepSelectedInPane) {
                                        applyDialogSelectionToCanvas(dialogSelectedObjectIds, keepSelectedInPane);
                                    });
}

QVector<RenderTypes::RenderItem> LayoutCanvas::buildRenderItems(const QVector<SceneRenderPrimitive>& primitives,
                                                                const RenderDetailLevel detailLevel) {
    QVector<RenderTypes::RenderItem> items;
    items.reserve(primitives.size());

    for (const SceneRenderPrimitive& primitive : primitives) {
        const LayerDefinition* layer = layerForPrimitive(primitive);
        if (!layer || !layer->visible || primitive.polygonVertices.isEmpty()) {
            continue;
        }

        RenderTypes::RenderItem item;
        item.polygon.reserve(primitive.polygonVertices.size());
        for (const WorldPoint& vertex : primitive.polygonVertices) {
            item.polygon.push_back(worldToScreen(vertex.x, vertex.y));
        }

        item.selected = primitive.objectId != 0 && m_selectedObjectIds.contains(primitive.objectId);
        item.preview = primitive.preview;
        item.detailLevel = detailLevel == RenderDetailLevel::Detailed
                               ? 0
                               : detailLevel == RenderDetailLevel::Simplified ? 1 : 2;
        item.tinyOnScreen = item.polygon.boundingRect().width() < 1.0
                            && item.polygon.boundingRect().height() < 1.0;

        item.fillColor = layer->color;
        if (item.selected) {
            item.fillColor = item.fillColor.lighter(130);
        }
        item.fillColor.setAlpha(item.preview ? 90 : 140);

        item.outlineColor = layer->color;
        item.outlineColor.setAlpha(item.preview ? 180 : 220);
        if (item.selected) {
            item.outlineColor = QColor("#ffffff");
            item.outlineColor.setAlpha(255);
        }

        item.pattern = layer->pattern;
        item.patternBrush = brushForFillColor(item.fillColor, layer->pattern);
        items.push_back(std::move(item));
    }

    return items;
}

LayoutCanvas::RenderDetailLevel LayoutCanvas::currentDetailLevel() const {
    if (m_zoom < 0.30) {
        return RenderDetailLevel::Coarse;
    }
    if (m_zoom < 1.0) {
        return RenderDetailLevel::Simplified;
    }
    return RenderDetailLevel::Detailed;
}

QBrush LayoutCanvas::brushForFillColor(const QColor& fillColor, const QString& pattern) {
    const QString cacheKey = QString("%1|%2").arg(fillColor.name(QColor::HexArgb), pattern);
    const auto it = m_fillBrushCache.constFind(cacheKey);
    if (it != m_fillBrushCache.cend()) {
        return it.value();
    }

    const QBrush brush = RenderTypes::patternBrushFor(fillColor, pattern);
    m_fillBrushCache.insert(cacheKey, brush);
    return brush;
}

void LayoutCanvas::drawLeftDragPreview(QPainter& painter) {
    if (!m_leftDragActive) {
        return;
    }

    const qint64 minX = std::min(m_leftAnchorX, m_leftCurrentX);
    const qint64 minY = std::min(m_leftAnchorY, m_leftCurrentY);
    const qint64 maxX = std::max(m_leftAnchorX, m_leftCurrentX);
    const qint64 maxY = std::max(m_leftAnchorY, m_leftCurrentY);

    const QPointF p1 = worldToScreen(minX, minY);
    const QPointF p2 = worldToScreen(maxX, maxY);
    const QRectF rect(QPointF(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y())),
                      QPointF(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y())));

    painter.setPen(QPen(QColor("#ffffff"), 1, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
}

void LayoutCanvas::drawHoverOutline(QPainter& painter, const QVector<WorldLineSegment>& segments) {
    painter.setPen(QPen(QColor("#ffd400"), 2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);

    for (const WorldLineSegment& segment : segments) {
        const QPointF p1 = worldToScreen(segment.x1, segment.y1);
        const QPointF p2 = worldToScreen(segment.x2, segment.y2);
        painter.drawLine(p1, p2);
    }
}

void LayoutCanvas::drawDialogSelectionOutlines(QPainter& painter) {
    if (!m_rootCell || m_dialogSelectedObjectIds.isEmpty()) {
        return;
    }

    painter.setPen(QPen(QColor("#ff00ff"), 2, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);

    for (const quint64 objectId : m_dialogSelectedObjectIds) {
        QVector<WorldLineSegment> outlineSegments;
        if (!m_rootCell->collectOutlineSegmentsByObjectId(objectId, outlineSegments)) {
            continue;
        }

        for (const WorldLineSegment& segment : outlineSegments) {
            const QPointF p1 = worldToScreen(segment.x1, segment.y1);
            const QPointF p2 = worldToScreen(segment.x2, segment.y2);
            painter.drawLine(p1, p2);
        }
    }
}

const LayerDefinition* LayoutCanvas::layerForPrimitive(const SceneRenderPrimitive& primitive) const {
    const auto it = m_layerIndexByCode.constFind(RenderTypes::layerCodeKey(primitive.layerNameId, primitive.layerTypeId));
    if (it == m_layerIndexByCode.cend()) {
        return nullptr;
    }

    const int index = it.value();
    if (index < 0 || index >= m_layers.size()) {
        return nullptr;
    }

    return &m_layers[index];
}

const LayerDefinition* LayoutCanvas::layerForRectangle(const DrawnRectangle& rectangle) const {
    const auto it = m_layerIndexByCode.constFind(RenderTypes::layerCodeKey(rectangle.layerNameId, rectangle.layerTypeId));
    if (it == m_layerIndexByCode.cend()) {
        return nullptr;
    }

    const int index = it.value();
    if (index < 0 || index >= m_layers.size()) {
        return nullptr;
    }

    return &m_layers[index];
}

void LayoutCanvas::rebuildLayerLookup() {
    m_layerIndexByCode.clear();

    // The lookup is keyed by stable layer IDs rather than table row order, so
    // rendering/hit-testing stays correct even if the palette presentation changes.
    for (int i = 0; i < m_layers.size(); ++i) {
        m_layerIndexByCode.insert(RenderTypes::layerCodeKey(m_layers[i].nameId, m_layers[i].typeId), i);
    }
}

bool LayoutCanvas::isSelectableRectangle(const DrawnRectangle& rectangle) const {
    const LayerDefinition* layer = layerForRectangle(rectangle);
    return layer && layer->visible && layer->selectable;
}

bool LayoutCanvas::isSelectableObjectId(const quint64 objectId) const {
    if (!m_rootCell || objectId == 0) {
        return false;
    }

    const LayoutObjectModel* object = m_rootCell->findObjectById(objectId);
    if (!object) {
        return false;
    }

    const DrawnRectangle* rectangle = object->asRectangle();
    return rectangle && isSelectableRectangle(*rectangle);
}

QVector<SceneRenderPrimitive> LayoutCanvas::flattenedRenderPrimitives() const {
    QVector<SceneRenderPrimitive> primitives;
    if (m_rootCell) {
        qint64 minX = 0;
        qint64 minY = 0;
        qint64 maxX = 0;
        qint64 maxY = 0;
        visibleWorldBounds(minX, minY, maxX, maxY);
        m_rootCell->collectRenderPrimitivesInRect(minX, minY, maxX, maxY, primitives);
    }

    if (m_editPreviewEnabled) {
        primitives.push_back(m_editPreview);
    }

    return primitives;
}

void LayoutCanvas::visibleWorldBounds(qint64& minX, qint64& minY, qint64& maxX, qint64& maxY) const {
    const QPointF topLeftWorld = screenToWorld(QPointF(0.0, 0.0));
    const QPointF bottomRightWorld = screenToWorld(QPointF(width(), height()));

    minX = static_cast<qint64>(std::floor(std::min(topLeftWorld.x(), bottomRightWorld.x())));
    maxX = static_cast<qint64>(std::ceil(std::max(topLeftWorld.x(), bottomRightWorld.x())));
    minY = static_cast<qint64>(std::floor(std::min(topLeftWorld.y(), bottomRightWorld.y())));
    maxY = static_cast<qint64>(std::ceil(std::max(topLeftWorld.y(), bottomRightWorld.y())));
}

QVector<quint64> LayoutCanvas::selectableObjectCandidatesAt(const qint64 x, const qint64 y) const {
    QVector<quint64> candidates;
    if (!m_rootCell) {
        return candidates;
    }

    const QVector<quint64> objectMatches = m_rootCell->matchingObjectIdsAt(
        x,
        y,
        [this](const LayoutObjectModel& object) {
            const DrawnRectangle* rectangle = object.asRectangle();
            return rectangle && isSelectableRectangle(*rectangle);
        });
    if (objectMatches.isEmpty()) {
        return candidates;
    }

    for (const quint64 objectId : objectMatches) {
        candidates.push_back(objectId);
    }

    return candidates;
}

quint64 LayoutCanvas::hoveredSelectableObjectIdAt(const qint64 x, const qint64 y) const {
    const QVector<quint64> candidates = selectableObjectCandidatesAt(x, y);
    return candidates.isEmpty() ? 0 : candidates.front();
}

void LayoutCanvas::handleSelectionDrag(const qint64 anchorX,
                                       const qint64 anchorY,
                                       const qint64 currentX,
                                       const qint64 currentY) {
    if (!m_rootCell) {
        m_selectedObjectId = 0;
        m_selectedObjectIds.clear();
        m_hoveredObjectId = 0;
        m_lastSelectionCandidateIds.clear();
        m_lastSelectionPoint = QPointF();
        m_hasSelectionPoint = false;
        refreshPropertiesDialogIfOpen();
        update();
        return;
    }

    const qint64 minX = std::min(anchorX, currentX);
    const qint64 maxX = std::max(anchorX, currentX);
    const qint64 minY = std::min(anchorY, currentY);
    const qint64 maxY = std::max(anchorY, currentY);
    const QVector<quint64> selectedIds = m_rootCell->matchingObjectIdsFullyInsideRect(
        minX,
        minY,
        maxX,
        maxY,
        [this](const LayoutObjectModel& object) {
            const DrawnRectangle* rectangle = object.asRectangle();
            return rectangle && isSelectableRectangle(*rectangle);
        });

    m_selectedObjectIds = QSet<quint64>(selectedIds.cbegin(), selectedIds.cend());
    m_selectedObjectId = selectedIds.isEmpty() ? 0 : selectedIds.front();
    m_hoveredObjectId = m_selectedObjectId;
    m_lastSelectionCandidateIds.clear();
    m_lastSelectionPoint = QPointF();
    m_hasSelectionPoint = false;
    refreshPropertiesDialogIfOpen();
    update();
}

void LayoutCanvas::handleSelectionClick(const qint64 x, const qint64 y) {
    const QVector<quint64> candidates = selectableObjectCandidatesAt(x, y);
    if (candidates.isEmpty()) {
        m_selectedObjectId = 0;
        m_selectedObjectIds.clear();
        m_hoveredObjectId = 0;
        m_lastSelectionCandidateIds.clear();
        m_lastSelectionPoint = QPointF();
        m_hasSelectionPoint = false;
        refreshPropertiesDialogIfOpen();
        update();
        return;
    }

    const QPointF selectionPoint(static_cast<double>(x), static_cast<double>(y));
    const bool samePoint = m_hasSelectionPoint && m_lastSelectionPoint == selectionPoint;
    const bool sameCandidates = samePoint && (candidates == m_lastSelectionCandidateIds);

    if (!sameCandidates) {
        m_selectedObjectId = candidates.front();
        m_hoveredObjectId = candidates.size() > 1 ? candidates[1] : candidates.front();
    } else {
        int currentCandidate = candidates.indexOf(m_selectedObjectId);
        if (currentCandidate < 0) {
            currentCandidate = 0;
        }

        const int nextSelectedCandidate = (currentCandidate + 1) % candidates.size();
        m_selectedObjectId = candidates[nextSelectedCandidate];
        m_hoveredObjectId = candidates[(nextSelectedCandidate + 1) % candidates.size()];
    }

    m_selectedObjectIds.clear();
    m_selectedObjectIds.insert(m_selectedObjectId);

    m_lastSelectionCandidateIds = candidates;
    m_lastSelectionPoint = selectionPoint;
    m_hasSelectionPoint = true;
    refreshPropertiesDialogIfOpen();
    update();
}

void LayoutCanvas::validateSelection() {
    const QSet<quint64> previousSelectedIds = m_selectedObjectIds;
    const quint64 previousSelectedObjectId = m_selectedObjectId;

    QSet<quint64> validSelectedIds;
    for (const quint64 objectId : m_selectedObjectIds) {
        if (isSelectableObjectId(objectId)) {
            validSelectedIds.insert(objectId);
        }
    }

    m_selectedObjectIds = validSelectedIds;
    if (!m_selectedObjectIds.contains(m_selectedObjectId)) {
        m_selectedObjectId = m_selectedObjectIds.isEmpty() ? 0 : *m_selectedObjectIds.cbegin();
    }

    if (m_selectedObjectIds != previousSelectedIds || m_selectedObjectId != previousSelectedObjectId) {
        refreshPropertiesDialogIfOpen();
    }
}

void LayoutCanvas::validateHover() {
    if (!isSelectableObjectId(m_hoveredObjectId)) {
        m_hoveredObjectId = 0;
    }
}

void LayoutCanvas::drawGrid(QPainter& painter) {
    if (m_gridSize <= 0.0 || m_zoom <= 0.0) {
        return;
    }

    // When zoomed far out, drawing every base-grid point is noisy and slow.
    // Increase the visible step adaptively while keeping the grid anchored in
    // world space so pan/zoom still feels stable and predictable.
    constexpr double minimumPixelSpacing = 10.0;
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
