#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QTableWidget>
#include <QVector>

#include "LayerManager.h"

// DrawnRectangle stores one committed or preview rectangle in world coordinates.
//
// Coordinates are 64-bit signed integers as requested by the tool contract.
struct DrawnRectangle {
    QString layerName;
    QColor color;
    QString pattern;
    qint64 x1;
    qint64 y1;
    qint64 x2;
    qint64 y2;
};

class QLabel;
class LayoutCanvas;

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

public slots:
    // Model-to-view refresh hooks.
    void setLayers(const QVector<LayerDefinition>& layers);
    void onLayerChanged(int index, const LayerDefinition& layer);
    void onActiveLayerChanged(const QString& layerName);

    // Tool and view state updates received from Tcl execution side.
    void onToolChanged(const QString& toolName);
    void onViewChanged(double zoom, double panX, double panY);

    // Rectangle preview/commit updates received from Tcl execution side.
    void onRectanglePreviewChanged(bool enabled, const DrawnRectangle& rectangle);
    void onRectangleCommitted(const DrawnRectangle& rectangle);

signals:
    // Single dispatch point for UI->Tcl command routing.
    void commandRequested(const QString& command);

private slots:
    // Table interaction handlers.
    void onCellChanged(int row, int column);
    void onCurrentRowChanged(int currentRow, int previousRow);

private:
    QBrush makePatternBrush(const LayerDefinition& layer) const;
    void updateActiveLayerHighlight();
    void applyLayerToRow(int row, const LayerDefinition& layer);
    QTableWidgetItem* makeReadOnlyItem(const QString& text);

    QTableWidget* m_layerTable;
    LayoutCanvas* m_canvas;
    QLabel* m_statusLabel;

    QVector<LayerDefinition> m_layers;
    QString m_activeLayerName;
    bool m_internalUpdate{false}; // Guard to suppress feedback loops.

    // Committed rectangles currently shown on canvas.
    QVector<DrawnRectangle> m_rectangles;
};
