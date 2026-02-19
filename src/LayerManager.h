#pragma once

#include <QObject>
#include <QVector>
#include <QColor>
#include <QString>

struct LayerDefinition {
    QString name;
    QString type;
    QColor color;
    QString pattern;
    bool visible;
    bool selectable;
};

class LayerManager : public QObject {
    Q_OBJECT
public:
    explicit LayerManager(QObject* parent = nullptr);

    const QVector<LayerDefinition>& layers() const;
    QString activeLayer() const;

    bool configureLayer(const QString& layerName, const QString& option, bool value, QString& error);
    bool loadLayersFromFile(const QString& filePath, QString& error);
    bool setActiveLayer(const QString& layerName, QString& error);
    bool layerByName(const QString& layerName, LayerDefinition& layer, QString& error) const;
    QString serializeLayers() const;

signals:
    void layersReset(const QVector<LayerDefinition>& layers);
    void layerChanged(int index, const LayerDefinition& layer);
    void activeLayerChanged(const QString& layerName);

private:
    int findLayerIndex(const QString& layerName) const;

    QVector<LayerDefinition> m_layers;
    QString m_activeLayer;
};
