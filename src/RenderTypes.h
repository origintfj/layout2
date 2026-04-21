#pragma once

#include <QBrush>
#include <QColor>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <array>

// RenderTypes groups small rendering-focused value types and helpers so the
// canvas and rendering backends can share one vocabulary without including one
// another's implementation details.
namespace RenderTypes {

enum class BackendType {
    Raster,
    OpenGL
};

// RenderItem is the immutable, backend-agnostic payload prepared by the canvas
// for one frame. Backends receive only these screen-space items, not scene
// models, which keeps drawing code isolated from editor/business logic.
struct RenderItem {
    QPolygonF polygon;
    QColor fillColor;
    QColor outlineColor;
    QBrush patternBrush;
    QString pattern;
    bool selected{false};
    bool preview{false};
    bool tinyOnScreen{false};
    int detailLevel{0};
};

BackendType backendTypeFromEnv();
QBrush patternBrushFor(QColor baseColor, const QString& pattern);
std::array<float, 8> patternRowsFor(const QString& pattern);
quint64 layerCodeKey(quint32 nameId, quint32 typeId);

} // namespace RenderTypes
