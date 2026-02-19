#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>

#include "LayerManager.h"

class LayoutEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LayoutEditorWindow(QWidget* parent = nullptr);

    void setLayers(const QVector<LayerDefinition>& layers);

signals:
    void commandRequested(const QString& command);

public slots:
    void onLayerChanged(int index, const LayerDefinition& layer);

private slots:
    void onCellChanged(int row, int column);

private:
    void applyLayerToRow(int row, const LayerDefinition& layer);
    QTableWidgetItem* makeReadOnlyItem(const QString& text);

    QTableWidget* m_layerTable;
    QLabel* m_canvasLabel;
    QVector<LayerDefinition> m_layers;
    bool m_internalUpdate{false};
};
