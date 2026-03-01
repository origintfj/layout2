#include "LayerManager.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

LayerManager::LayerManager(QObject* parent)
    : QObject(parent) {
}

const QVector<LayerDefinition>& LayerManager::layers() const {
    return m_layers;
}

QString LayerManager::activeLayerName() const {
    if (m_activeLayerIndex < 0 || m_activeLayerIndex >= m_layers.size()) {
        return QString();
    }
    return m_layers[m_activeLayerIndex].name;
}

QString LayerManager::activeLayerType() const {
    if (m_activeLayerIndex < 0 || m_activeLayerIndex >= m_layers.size()) {
        return QString();
    }
    return m_layers[m_activeLayerIndex].type;
}

bool LayerManager::activeLayerDefinition(LayerDefinition& layer) const {
    if (m_activeLayerIndex < 0 || m_activeLayerIndex >= m_layers.size()) {
        return false;
    }

    layer = m_layers[m_activeLayerIndex];
    return true;
}

int LayerManager::findLayerIndex(const QString& layerName, const QString& layerType) const {
    // Case-insensitive lookup keeps Tcl UX forgiving.
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].name.compare(layerName, Qt::CaseInsensitive) == 0
            && m_layers[i].type.compare(layerType, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

bool LayerManager::configureLayer(const QString& layerName,
                                  const QString& layerType,
                                  const QString& option,
                                  bool value,
                                  QString& error) {
    const int index = findLayerIndex(layerName, layerType);
    if (index < 0) {
        error = QString("Unknown layer '%1' of type '%2'").arg(layerName, layerType);
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
    QHash<QString, int> firstLineByIdentity;
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
        // <name> <type> <name_id>/<type_id> <#RRGGBB> <0xPATTERN>
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 5) {
            error = QString("Invalid line %1 in %2 (expected: name type name_id/type_id #RRGGBB 0xPATTERN)")
                        .arg(lineNo)
                        .arg(filePath);
            return false;
        }

        const QString identityKey = parts[0].toLower() + QChar(0x1F) + parts[1].toLower();
        if (firstLineByIdentity.contains(identityKey)) {
            error = QString("Duplicate layer identity '%1' type '%2' at line %3 (first declared at line %4)")
                        .arg(parts[0], parts[1])
                        .arg(lineNo)
                        .arg(firstLineByIdentity.value(identityKey));
            return false;
        }

        const QStringList idParts = parts[2].split('/');
        if (idParts.size() != 2) {
            error = QString("Invalid ID token '%1' at line %2 (expected name_id/type_id)")
                        .arg(parts[2])
                        .arg(lineNo);
            return false;
        }

        bool nameIdOk = false;
        bool typeIdOk = false;
        const quint32 nameId = idParts[0].toUInt(&nameIdOk, 0);
        const quint32 typeId = idParts[1].toUInt(&typeIdOk, 0);
        if (!nameIdOk || !typeIdOk) {
            error = QString("Invalid numeric IDs '%1' at line %2 (expected name_id/type_id)")
                        .arg(parts[2])
                        .arg(lineNo);
            return false;
        }

        const QColor color(parts[3]);
        if (!color.isValid()) {
            error = QString("Invalid color '%1' at line %2").arg(parts[3]).arg(lineNo);
            return false;
        }

        // Pattern token is stored as string but validated numerically.
        bool patternOk = false;
        parts[4].toULongLong(&patternOk, 0);
        if (!patternOk) {
            error = QString("Invalid pattern '%1' at line %2 (expected hex like 0x00FF)")
                        .arg(parts[4])
                        .arg(lineNo);
            return false;
        }

        // New layers are visible/selectable by default.
        loaded.push_back({parts[0], parts[1], nameId, typeId, color, parts[4], true, true});
        firstLineByIdentity.insert(identityKey, lineNo);
    }

    if (loaded.isEmpty()) {
        error = QString("No layers loaded from '%1'").arg(filePath);
        return false;
    }

    // Replace model atomically and choose first layer as active.
    m_layers = loaded;
    m_activeLayerIndex = 0;

    emit layersReset(m_layers);
    emit activeLayerChanged(activeLayerName(), activeLayerType());
    return true;
}

bool LayerManager::setActiveLayer(const QString& layerName, const QString& layerType, QString& error) {
    const int index = findLayerIndex(layerName, layerType);
    if (index < 0) {
        error = QString("Unknown layer '%1' of type '%2'").arg(layerName, layerType);
        return false;
    }

    if (index == m_activeLayerIndex) {
        return true;
    }

    m_activeLayerIndex = index;
    emit activeLayerChanged(activeLayerName(), activeLayerType());
    return true;
}

bool LayerManager::layerByNameAndType(const QString& layerName,
                                      const QString& layerType,
                                      LayerDefinition& layer,
                                      QString& error) const {
    const int index = findLayerIndex(layerName, layerType);
    if (index < 0) {
        error = QString("Unknown layer '%1' of type '%2'").arg(layerName, layerType);
        return false;
    }

    layer = m_layers[index];
    return true;
}

QString LayerManager::serializeLayers() const {
    QString out;

    // Encode one human-readable row per layer.
    for (int i = 0; i < m_layers.size(); ++i) {
        const LayerDefinition& layer = m_layers[i];
        const QString activeMark = i == m_activeLayerIndex ? "active" : "inactive";
        out += QString("%1 {%2} %3/%4 %5 %6 %7 %8\n")
                   .arg(layer.name, layer.type)
                   .arg(layer.nameId)
                   .arg(layer.typeId)
                   .arg(layer.color.name(), layer.pattern)
                   .arg(layer.visible ? "visible" : "hidden")
                   .arg(layer.selectable ? "selectable" : "locked")
                   .arg(activeMark);
    }

    return out.trimmed();
}
