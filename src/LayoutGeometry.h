#pragma once

#include <QtGlobal>

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
