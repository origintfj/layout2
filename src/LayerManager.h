#pragma once

#include <QObject>
#include <QVector>
#include <QColor>
#include <QString>
#include <QtGlobal>

// LayerDefinition is the in-memory schema for a single process layer.
//
// Fields are intentionally simple POD-like Qt types to make serialization,
// display, and Tcl command bridging straightforward.
struct LayerDefinition {
    QString name;       // Human-readable and command-addressable layer name.
    QString type;       // Logical layer class (e.g. drawing, cut, implant).
    quint32 nameId;     // Numeric identifier paired with layer name in layer file.
    quint32 typeId;     // Numeric identifier paired with layer type in layer file.
    QColor color;       // Display color used by the canvas/palette swatch.
    QString pattern;    // Pattern token loaded from layer file (hex string).
    bool visible;       // Whether shapes on this layer are currently visible.
    bool selectable;    // Whether geometry on this layer can be selected.
};

// LayerManager is the authoritative model for layer state.
//
// All layer-affecting Tcl commands resolve into calls on this object. UI widgets
// should react to emitted signals rather than mutating model state directly.
class LayerManager : public QObject {
    Q_OBJECT
public:
    explicit LayerManager(QObject* parent = nullptr);

    // Returns immutable access to all layers in display order.
    const QVector<LayerDefinition>& layers() const;

    // Returns active layer identity (empty if no layers loaded).
    QString activeLayerName() const;
    QString activeLayerType() const;

    // Copies the active layer definition; false if no active layer exists.
    bool activeLayerDefinition(LayerDefinition& layer) const;

    // Mutates layer visibility/selectability.
    //
    // option supports: -visible, -selectable
    // value is the new boolean value for the selected option.
    bool configureLayer(const QString& layerName,
                        const QString& layerType,
                        const QString& option,
                        bool value,
                        QString& error);

    // Loads a plain-text layers file and replaces the current palette.
    bool loadLayersFromFile(const QString& filePath, QString& error);

    // Sets the active layer used by drawing tools.
    bool setActiveLayer(const QString& layerName, const QString& layerType, QString& error);

    // Fetches a single layer by name/type for command handlers/tool logic.
    bool layerByNameAndType(const QString& layerName,
                            const QString& layerType,
                            LayerDefinition& layer,
                            QString& error) const;

    // Produces a textual snapshot used by Tcl `layer list`.
    QString serializeLayers() const;

signals:
    // Emitted after a full reload from file.
    void layersReset(const QVector<LayerDefinition>& layers);

    // Emitted after a single-layer property mutation.
    void layerChanged(int index, const LayerDefinition& layer);

    // Emitted whenever active layer changes.
    void activeLayerChanged(const QString& layerName, const QString& layerType);

private:
    // Returns matching index (case-insensitive on name/type), or -1 if not present.
    int findLayerIndex(const QString& layerName, const QString& layerType) const;

    QVector<LayerDefinition> m_layers;
    int m_activeLayerIndex{-1};
};
