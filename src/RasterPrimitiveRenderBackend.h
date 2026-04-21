#pragma once

#include "PrimitiveRenderBackend.h"

// RasterPrimitiveRenderBackend preserves the older QPainter-based code path.
// It is useful both as a compatibility fallback and as a rendering reference
// when comparing behavior against the OpenGL backend.
class RasterPrimitiveRenderBackend final : public PrimitiveRenderBackend {
public:
    void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) override;
    void drawPrimitives(QPainter& painter,
                        const QVector<RenderTypes::RenderItem>& items,
                        const QSize& viewportSize) override;
    void endFrame(QPainter& painter, const QSize& viewportSize) override;
};
