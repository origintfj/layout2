#include "LayerManager.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

LayerManager::LayerManager(QObject* parent)
    : QObject(parent) {
}

const QVector<LayerDefinition>& LayerManager::layers() const {
    return m_layers;
}

QString LayerManager::activeLayer() const {
    return m_activeLayer;
}

int LayerManager::findLayerIndex(const QString& layerName) const {
    // Case-insensitive lookup keeps Tcl UX forgiving.
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

    // Only two toggles are currently supported.
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

bool LayerManager::loadLayersFromFile(const QString& filePath, QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("Cannot open layers file '%1'").arg(filePath);
        return false;
    }

    QVector<LayerDefinition> loaded;
    QTextStream stream(&file);
    int lineNo = 0;

    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        ++lineNo;

        // Skip comments/blank lines for readability.
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        // Expected format:
        // <name> <type> <#RRGGBB> <0xPATTERN>
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 4) {
            error = QString("Invalid line %1 in %2 (expected: name type #RRGGBB 0xPATTERN)")
                        .arg(lineNo)
                        .arg(filePath);
            return false;
        }

        const QColor color(parts[2]);
        if (!color.isValid()) {
            error = QString("Invalid color '%1' at line %2").arg(parts[2]).arg(lineNo);
            return false;
        }

        // Pattern token is stored as string but validated numerically.
        bool patternOk = false;
        parts[3].toULongLong(&patternOk, 0);
        if (!patternOk) {
            error = QString("Invalid pattern '%1' at line %2 (expected hex like 0x00FF)")
                        .arg(parts[3])
                        .arg(lineNo);
            return false;
        }

        // New layers are visible/selectable by default.
        loaded.push_back({parts[0], parts[1], color, parts[3], true, true});
    }

    if (loaded.isEmpty()) {
        error = QString("No layers loaded from '%1'").arg(filePath);
        return false;
    }

    // Replace model atomically and choose first layer as active.
    m_layers = loaded;
    m_activeLayer = m_layers[0].name;

    emit layersReset(m_layers);
    emit activeLayerChanged(m_activeLayer);
    return true;
}

bool LayerManager::setActiveLayer(const QString& layerName, QString& error) {
    const int index = findLayerIndex(layerName);
    if (index < 0) {
        error = QString("Unknown layer '%1'").arg(layerName);
        return false;
    }

    const QString resolved = m_layers[index].name;
    if (resolved == m_activeLayer) {
        return true;
    }

    m_activeLayer = resolved;
    emit activeLayerChanged(m_activeLayer);
    return true;
}

bool LayerManager::layerByName(const QString& layerName, LayerDefinition& layer, QString& error) const {
    const int index = findLayerIndex(layerName);
    if (index < 0) {
        error = QString("Unknown layer '%1'").arg(layerName);
        return false;
    }

    layer = m_layers[index];
    return true;
}

QString LayerManager::serializeLayers() const {
    QString out;

    // Encode one human-readable row per layer.
    for (const LayerDefinition& layer : m_layers) {
        const QString activeMark = layer.name == m_activeLayer ? "active" : "inactive";
        out += QString("%1 {%2} %3 %4 %5 %6 %7\n")
                   .arg(layer.name, layer.type, layer.color.name(), layer.pattern)
                   .arg(layer.visible ? "visible" : "hidden")
                   .arg(layer.selectable ? "selectable" : "locked")
                   .arg(activeMark);
    }

    return out.trimmed();
}
