#include "LayerManager.h"

LayerManager::LayerManager(QObject* parent)
    : QObject(parent) {
    m_layers = {
        {"Metal1", "routing", QColor("#1f77b4"), "solid", true, true},
        {"Metal2", "routing", QColor("#ff7f0e"), "solid", true, true},
        {"Via1", "cut", QColor("#2ca02c"), "cross", true, false},
        {"Poly", "device", QColor("#d62728"), "dots", true, true},
        {"Nwell", "implant", QColor("#9467bd"), "diag", false, false}
    };
}

const QVector<LayerDefinition>& LayerManager::layers() const {
    return m_layers;
}

int LayerManager::findLayerIndex(const QString& layerName) const {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].name.compare(layerName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

bool LayerManager::configureLayer(const QString& layerName, const QString& option, bool value, QString& error) {
    const int index = findLayerIndex(layerName);
    if (index < 0) {
        error = QString("Unknown layer '%1'").arg(layerName);
        return false;
    }

    LayerDefinition layer = m_layers[index];

    if (option == "-visible") {
        layer.visible = value;
    } else if (option == "-selectable") {
        layer.selectable = value;
    } else {
        error = QString("Unknown option '%1' (expected -visible or -selectable)").arg(option);
        return false;
    }

    m_layers[index] = layer;
    emit layerChanged(index, layer);
    return true;
}

QString LayerManager::serializeLayers() const {
    QString out;
    for (const LayerDefinition& layer : m_layers) {
        out += QString("%1 {%2} %3 %4\n")
                   .arg(layer.name, layer.type)
                   .arg(layer.visible ? "visible" : "hidden")
                   .arg(layer.selectable ? "selectable" : "locked");
    }
    return out.trimmed();
}
