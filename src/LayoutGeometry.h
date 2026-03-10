#pragma once

#include <QtGlobal>
#include <QVector>

// DrawnRectangle stores one committed or preview rectangle in world coordinates.
//
// Coordinates are 64-bit signed integers as requested by the tool contract.
struct DrawnRectangle {
    quint32 layerNameId;
    quint32 layerTypeId;
    qint64 x1;
    qint64 y1;
    qint64 x2;
    qint64 y2;
};

// WorldLineSegment stores one line segment in world coordinates.
struct WorldLineSegment {
    qint64 x1;
    qint64 y1;
    qint64 x2;
    qint64 y2;
};

struct WorldPoint {
    qint64 x;
    qint64 y;
};

struct DrawnPath {
    quint32 layerNameId;
    quint32 layerTypeId;
    qint64 width;
    QVector<WorldPoint> points;
};

// SceneRenderPrimitive describes one drawable scene primitive in world space.
struct SceneRenderPrimitive {
    quint64 objectId;
    quint32 layerNameId;
    quint32 layerTypeId;
    bool preview{false};
    QVector<WorldPoint> polygonVertices;
    qint64 pathWidth{0};
    QVector<WorldPoint> pathPoints;
};
