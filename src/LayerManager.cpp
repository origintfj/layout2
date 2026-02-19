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

        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 4) {
            error = QString("Invalid line %1 in %2 (expected: name type #RRGGBB 0xPATTERN)")
                        .arg(lineNo)
                        .arg(filePath);
            return false;
        }

        const QString name = parts[0];
        const QString type = parts[1];
        const QString colorText = parts[2];
        const QString patternText = parts[3];

        const QColor color(colorText);
        if (!color.isValid()) {
            error = QString("Invalid color '%1' at line %2").arg(colorText).arg(lineNo);
            return false;
        }

        bool patternOk = false;
        patternText.toUInt(&patternOk, 0);
        if (!patternOk) {
            error = QString("Invalid pattern '%1' at line %2 (expected hex like 0x00FF)")
                        .arg(patternText)
                        .arg(lineNo);
            return false;
        }

        loaded.push_back({name, type, color, patternText, true, true});
    }

    if (loaded.isEmpty()) {
        error = QString("No layers loaded from '%1'").arg(filePath);
        return false;
    }

    m_layers = loaded;
    emit layersReset(m_layers);
    return true;
}

QString LayerManager::serializeLayers() const {
    QString out;
    for (const LayerDefinition& layer : m_layers) {
        out += QString("%1 {%2} %3 %4 %5 %6\n")
                   .arg(layer.name, layer.type, layer.color.name(), layer.pattern)
                   .arg(layer.visible ? "visible" : "hidden")
                   .arg(layer.selectable ? "selectable" : "locked");
    }
    return out.trimmed();
}
