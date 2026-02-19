#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QTableWidget>
#include <QVector>

#include "LayerManager.h"

struct DrawnRectangle {
    QString layerName;
    QColor color;
    qint64 x1;
    qint64 y1;
    qint64 x2;
    qint64 y2;
};

class QLabel;
class LayoutCanvas;

class LayoutEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LayoutEditorWindow(QWidget* parent = nullptr);

public slots:
    void setLayers(const QVector<LayerDefinition>& layers);
    void onLayerChanged(int index, const LayerDefinition& layer);
    void onActiveLayerChanged(const QString& layerName);
    void onToolChanged(const QString& toolName);
    void onViewChanged(double zoom, double panX, double panY);
    void onRectanglePreviewChanged(bool enabled, const DrawnRectangle& rectangle);
    void onRectangleCommitted(const DrawnRectangle& rectangle);

signals:
    void commandRequested(const QString& command);

private slots:
    void onCellChanged(int row, int column);
    void onCurrentRowChanged(int currentRow, int previousRow);

private:
    void applyLayerToRow(int row, const LayerDefinition& layer);
    QTableWidgetItem* makeReadOnlyItem(const QString& text);

    QTableWidget* m_layerTable;
    LayoutCanvas* m_canvas;
    QLabel* m_statusLabel;
    QVector<LayerDefinition> m_layers;
    bool m_internalUpdate{false};
    QVector<DrawnRectangle> m_rectangles;
};
