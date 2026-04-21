#pragma once

#include "PrimitiveRenderBackend.h"

#include <QElapsedTimer>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>

class QOpenGLFunctions;

// OpenGLPrimitiveRenderBackend is the optimized renderer used by default. It
// caches frame geometry, batches simplified primitives, and renders detailed
// stipple patterns in the fragment shader for parity with the raster path.
class OpenGLPrimitiveRenderBackend final : public PrimitiveRenderBackend {
public:
    OpenGLPrimitiveRenderBackend();

    void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) override;
    void drawPrimitives(QPainter& painter,
                        const QVector<RenderTypes::RenderItem>& items,
                        const QSize& viewportSize) override;
    void endFrame(QPainter& painter, const QSize& viewportSize) override;

private:
    static void appendVertex(QVector<float>& out,
                             float x,
                             float y,
                             float r,
                             float g,
                             float b,
                             float a);
    static void appendOutlineSegments(QVector<float>& out,
                                      const QPolygonF& polygon,
                                      const QColor& color);
    static quint64 hashRenderItems(const QVector<RenderTypes::RenderItem>& items, const QSize& viewportSize);

    void rebuildCachedGeometry(const QVector<RenderTypes::RenderItem>& items);
    void appendPolygonTriangles(QVector<float>& out, const QPolygonF& polygon, const QColor& color);
    void drawDetailedItemsWithGl(QOpenGLFunctions* gl, int stride);
    void uploadVertexData(const QVector<float>& triangleVertexData,
                          const QVector<float>& lineVertexData);
    void drawWithPainterFallback(QPainter& painter, const QVector<RenderTypes::RenderItem>& items);
    bool initializeGlResources();

    bool m_initialized{false};
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_detailVertexBuffer;
    qsizetype m_vertexCapacityBytes{0};
    bool m_geometryDirty{true};
    quint64 m_cachedGeometryHash{0};
    QSize m_cachedViewportSize;
    QVector<float> m_cachedTriangleVertexData;
    QVector<float> m_cachedLineVertexData;
    QVector<RenderTypes::RenderItem> m_cachedDetailedItems;
    quint64 m_cachedTinySkipped{0};
    quint64 m_cachedDetailedPainterCount{0};
    bool m_statsEnabled{false};
    QElapsedTimer m_statsTimer;
    quint64 m_frameCounter{0};
    quint64 m_trianglesSubmitted{0};
    quint64 m_linesSubmitted{0};
    quint64 m_skippedTinyCount{0};
    quint64 m_detailedPainterCount{0};
};
