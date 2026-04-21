#pragma once

#include "RenderTypes.h"

#include <QPainter>
#include <QSize>
#include <QVector>
#include <memory>

// PrimitiveRenderBackend defines the narrow contract between the canvas and the
// concrete rendering implementations. The canvas decides *what* to draw and the
// backend decides *how* to draw it.
class PrimitiveRenderBackend {
public:
    virtual ~PrimitiveRenderBackend() = default;

    virtual void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) = 0;
    virtual void drawPrimitives(QPainter& painter,
                                const QVector<RenderTypes::RenderItem>& items,
                                const QSize& viewportSize) = 0;
    virtual void endFrame(QPainter& painter, const QSize& viewportSize) = 0;
};

std::unique_ptr<PrimitiveRenderBackend> createPrimitiveRenderBackend(RenderTypes::BackendType backendType);
