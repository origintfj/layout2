#pragma once

#include <QObject>
#include <QVector>
#include <QColor>
#include <QString>

// LayerDefinition is the in-memory schema for a single process layer.
//
// Fields are intentionally simple POD-like Qt types to make serialization,
// display, and Tcl command bridging straightforward.
struct LayerDefinition {
    QString name;       // Human-readable and command-addressable layer name.
    QString type;       // Logical layer class (e.g. drawing, cut, implant).
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

    // Returns the current active layer name (empty if no layers loaded).
    QString activeLayer() const;

    // Mutates layer visibility/selectability.
    //
    // option supports: -visible, -selectable
    // value is the new boolean value for the selected option.
    bool configureLayer(const QString& layerName, const QString& option, bool value, QString& error);

    // Loads a plain-text layers file and replaces the current palette.
    bool loadLayersFromFile(const QString& filePath, QString& error);

    // Sets the active layer used by drawing tools.
    bool setActiveLayer(const QString& layerName, QString& error);

    // Fetches a single layer by name for command handlers/tool logic.
    bool layerByName(const QString& layerName, LayerDefinition& layer, QString& error) const;

    // Produces a textual snapshot used by Tcl `layer list`.
    QString serializeLayers() const;

signals:
    // Emitted after a full reload from file.
    void layersReset(const QVector<LayerDefinition>& layers);

    // Emitted after a single-layer property mutation.
    void layerChanged(int index, const LayerDefinition& layer);

    // Emitted whenever active layer changes.
    void activeLayerChanged(const QString& layerName);

private:
    // Returns matching index (case-insensitive), or -1 if not present.
    int findLayerIndex(const QString& layerName) const;

    QVector<LayerDefinition> m_layers;
    QString m_activeLayer;
};
