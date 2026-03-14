#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QSize>
#include <QTableWidget>
#include <QVector>
#include <memory>

#include "LayerManager.h"
#include "LayoutGeometry.h"

class QLabel;
class LayoutCanvas;
class LayoutSceneNode;

// LayoutEditorWindow is the visual editor child window.
//
// It owns:
//  - left layer palette table
//  - right drawing canvas widget
//  - status line for active layer/tool info
//
// The window itself does not apply business logic directly; user interactions
// are forwarded as Tcl command strings through commandRequested().
class LayoutEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LayoutEditorWindow(QWidget* parent = nullptr);
    ~LayoutEditorWindow() override;

    QSize canvasViewportSize() const;

public slots:
    void setEditorIdentity(int editorId, bool isActive);

    // Model-to-view refresh hooks.
    void setLayers(const QVector<LayerDefinition>& layers);
    void onLayerChanged(int index, const LayerDefinition& layer);
    void onActiveLayerChanged(const QString& layerName, const QString& layerType);

    // Tool and view state updates received from Tcl execution side.
    void onToolChanged(const QString& toolName);
    void onViewChanged(double zoom, double panX, double panY, double gridSize);

    // Edit preview updates received from Tcl execution side.
    void onEditPreviewChanged(bool enabled, const SceneRenderPrimitive& primitive);
    void onPrimitiveCommitted(const SceneRenderPrimitive& primitive);

signals:
    // Single dispatch point for UI->Tcl command routing.
    void commandRequested(const QString& command, bool requestActivation);
    // High-frequency preview updates bypass Tcl command evaluation.
    void canvasPreviewRequested(qint64 anchorX, qint64 anchorY, qint64 currentX, qint64 currentY);
    void canvasPreviewClearRequested();
    void activationRequested();

private slots:
    // Table interaction handlers.
    void onCellChanged(int row, int column);
    void onCurrentRowChanged(int currentRow, int previousRow);
    void onObjectDeletionRequested(quint64 objectId);
    void onMouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QBrush makePatternBrush(const LayerDefinition& layer) const;
    void updateActiveLayerHighlight();
    void applyLayerToRow(int row, const LayerDefinition& layer);
    QTableWidgetItem* makeReadOnlyItem(const QString& text);
    void refreshStatusLabel();
    void refreshWindowTitle();

    QTableWidget* m_layerTable;
    LayoutCanvas* m_canvas;
    QLabel* m_statusLabel;

    QVector<LayerDefinition> m_layers;
    QString m_activeLayerName;
    QString m_activeLayerType;
    QString m_activeTool{"<none>"};
    qint64 m_mouseWorldX{0};
    qint64 m_mouseWorldY{0};
    bool m_mouseInsideCanvas{false};
    bool m_internalUpdate{false}; // Guard to suppress feedback loops.
    int m_editorId{0};
    bool m_isActiveEditor{false};

    // Root scene container for committed geometry.
    std::unique_ptr<LayoutSceneNode> m_rootCell;
};
