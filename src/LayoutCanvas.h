#pragma once

#include "LayerManager.h"
#include "LayoutGeometry.h"
#include "RenderTypes.h"

#include <QHash>
#include <QOpenGLWidget>
#include <QPointF>
#include <QSet>
#include <QVector>
#include <memory>

class LayoutSceneNode;
class PrimitiveRenderBackend;
class QPainter;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

// LayoutCanvas is the editor's viewport/input surface. It owns transient view
// state (pan/zoom), turns visible scene data into backend-agnostic render
// items, and keeps interaction logic close to the hit-testing/rendering code.
class LayoutCanvas : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);
    ~LayoutCanvas() override;

    void setRootCell(const LayoutSceneNode* rootCell);
    void setEditPreview(bool enabled, const SceneRenderPrimitive& primitive);
    void setView(double zoom, double panX, double panY, double gridSize);
    void setLayers(const QVector<LayerDefinition>& layers);
    void setActiveTool(const QString& toolName);
    void triggerPropertiesDialog();
    void applySelectionClick(qint64 worldX, qint64 worldY);
    void applySelectionDrag(qint64 anchorX, qint64 anchorY, qint64 currentX, qint64 currentY);

signals:
    void commandRequested(const QString& command, bool requestActivation);
    void objectDeletionRequested(quint64 objectId);
    void mouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas);
    void leftDragPreviewChanged(bool enabled, qint64 anchorX, qint64 anchorY, qint64 currentX, qint64 currentY);

protected:
    // Centralized event hook used to request redraws for expose/show/resize
    // style lifecycle events so uncovered regions repaint promptly.
    bool event(QEvent* event) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class RenderDetailLevel {
        Detailed,
        Simplified,
        Coarse
    };

    QPointF worldToScreen(qint64 x, qint64 y) const;
    QPointF screenToWorld(const QPointF& p) const;
    void applyDialogSelectionToCanvas(const QSet<quint64>& dialogSelectedObjectIds,
                                      bool keepSelectedInPane);
    void refreshPropertiesDialogIfOpen();
    void showPropertiesDialog();
    QVector<RenderTypes::RenderItem> buildRenderItems(const QVector<SceneRenderPrimitive>& primitives,
                                                      RenderDetailLevel detailLevel);
    RenderDetailLevel currentDetailLevel() const;
    QBrush brushForFillColor(const QColor& fillColor, const QString& pattern);
    void drawLeftDragPreview(QPainter& painter);
    void drawHoverOutline(QPainter& painter, const QVector<WorldLineSegment>& segments);
    void drawDialogSelectionOutlines(QPainter& painter);
    const LayerDefinition* layerForPrimitive(const SceneRenderPrimitive& primitive) const;
    const LayerDefinition* layerForRectangle(const DrawnRectangle& rectangle) const;
    void rebuildLayerLookup();
    bool isSelectableRectangle(const DrawnRectangle& rectangle) const;
    bool isSelectableObjectId(quint64 objectId) const;
    QVector<SceneRenderPrimitive> flattenedRenderPrimitives() const;
    void visibleWorldBounds(qint64& minX, qint64& minY, qint64& maxX, qint64& maxY) const;
    QVector<quint64> selectableObjectCandidatesAt(qint64 x, qint64 y) const;
    quint64 hoveredSelectableObjectIdAt(qint64 x, qint64 y) const;
    void handleSelectionDrag(qint64 anchorX, qint64 anchorY, qint64 currentX, qint64 currentY);
    void handleSelectionClick(qint64 x, qint64 y);
    void validateSelection();
    void validateHover();
    void drawGrid(QPainter& painter);

    const LayoutSceneNode* m_rootCell{nullptr};
    QVector<LayerDefinition> m_layers;
    QHash<quint64, int> m_layerIndexByCode;
    QHash<QString, QBrush> m_fillBrushCache;
    RenderTypes::BackendType m_backendType{RenderTypes::BackendType::Raster};
    std::unique_ptr<PrimitiveRenderBackend> m_renderBackend;
    SceneRenderPrimitive m_editPreview;
    QString m_activeTool{"none"};

    quint64 m_selectedObjectId{0};
    QSet<quint64> m_selectedObjectIds;
    QSet<quint64> m_dialogSelectedObjectIds;
    quint64 m_hoveredObjectId{0};
    QVector<quint64> m_lastSelectionCandidateIds;
    QPointF m_lastSelectionPoint;
    bool m_hasSelectionPoint{false};

    bool m_editPreviewEnabled{false};
    bool m_middlePanning{false};
    bool m_leftDragActive{false};
    bool m_leftDragMoved{false};
    qint64 m_leftAnchorX{0};
    qint64 m_leftAnchorY{0};
    qint64 m_leftCurrentX{0};
    qint64 m_leftCurrentY{0};

    QPointF m_lastPanPoint;
    double m_zoom{1.0};
    double m_panX{0.0};
    double m_panY{0.0};
    double m_gridSize{40.0};
};
