#include "OpenGLPrimitiveRenderBackend.h"

#include "RenderTypes.h"

#include <QDebug>
#include <QHash>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QVector2D>
#include <QtGlobal>
#include <algorithm>

OpenGLPrimitiveRenderBackend::OpenGLPrimitiveRenderBackend()
    : m_statsEnabled(qEnvironmentVariableIntValue("LAYOUT2_RENDER_STATS") != 0) {}

void OpenGLPrimitiveRenderBackend::beginFrame(QPainter& painter,
                                              const QColor& clearColor,
                                              const QSize& viewportSize) {
    Q_UNUSED(painter);
    Q_UNUSED(clearColor);
    Q_UNUSED(viewportSize);
}

void OpenGLPrimitiveRenderBackend::drawPrimitives(QPainter& painter,
                                                  const QVector<RenderTypes::RenderItem>& items,
                                                  const QSize& viewportSize) {
    if (!initializeGlResources()) {
        // Fallback to painter-only rendering if GL setup fails so the editor
        // remains usable even on systems with partial/broken GL support.
        drawWithPainterFallback(painter, items);
        return;
    }

    const quint64 geometryHash = hashRenderItems(items, viewportSize);
    if (geometryHash != m_cachedGeometryHash || viewportSize != m_cachedViewportSize) {
        rebuildCachedGeometry(items);
        m_cachedGeometryHash = geometryHash;
        m_cachedViewportSize = viewportSize;
        m_geometryDirty = true;
    }

    if (m_cachedTriangleVertexData.isEmpty() && m_cachedLineVertexData.isEmpty() && m_cachedDetailedItems.isEmpty()) {
        return;
    }

    painter.beginNativePainting();
    m_program.bind();
    m_program.setUniformValue("uViewport", QVector2D(viewportSize.width(), viewportSize.height()));

    constexpr int stride = 6 * static_cast<int>(sizeof(float));
    m_program.enableAttributeArray(0);
    m_program.enableAttributeArray(1);

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    // Qt may enable scissor for partial update regions; disable it so backend
    // draws are not clipped to stale damage rectangles.
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program.setUniformValue("uUseStipple", 0.0f);
    m_vertexBuffer.bind();
    if (m_geometryDirty) {
        uploadVertexData(m_cachedTriangleVertexData, m_cachedLineVertexData);
        m_geometryDirty = false;
    }

    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, stride);
    m_program.setAttributeBuffer(1, GL_FLOAT, 2 * static_cast<int>(sizeof(float)), 4, stride);

    const int triangleVertexCount = static_cast<int>(m_cachedTriangleVertexData.size() / 6);
    if (triangleVertexCount > 0) {
        gl->glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount);
    }

    const int lineVertexCount = static_cast<int>(m_cachedLineVertexData.size() / 6);
    if (lineVertexCount > 0) {
        gl->glLineWidth(2.0f);
        gl->glDrawArrays(GL_LINES, triangleVertexCount, lineVertexCount);
    }
    m_vertexBuffer.release();

    drawDetailedItemsWithGl(gl, stride);

    m_program.disableAttributeArray(0);
    m_program.disableAttributeArray(1);
    m_program.release();
    painter.endNativePainting();

    m_frameCounter += 1;
    m_trianglesSubmitted += (m_cachedTriangleVertexData.size() / 18);
    m_linesSubmitted += (m_cachedLineVertexData.size() / 12);
    m_skippedTinyCount += m_cachedTinySkipped;
    m_detailedPainterCount += m_cachedDetailedPainterCount;
    if (!m_statsEnabled) {
        return;
    }

    if (!m_statsTimer.isValid()) {
        m_statsTimer.start();
    } else if (m_frameCounter % 120 == 0) {
        const qint64 elapsedMs = std::max<qint64>(1, m_statsTimer.elapsed());
        qInfo().noquote() << QString("OpenGL backend stats: frames=%1 triangles=%2 lines=%3 avgTriangles/frame=%4 avgLines/frame=%5 avgMs/frame=%6 tinySkipped=%7 detailedPainter=%8")
                                 .arg(m_frameCounter)
                                 .arg(m_trianglesSubmitted)
                                 .arg(m_linesSubmitted)
                                 .arg(m_trianglesSubmitted / std::max<quint64>(1, m_frameCounter))
                                 .arg(m_linesSubmitted / std::max<quint64>(1, m_frameCounter))
                                 .arg(static_cast<double>(elapsedMs) / std::max<quint64>(1, m_frameCounter), 0, 'f', 3)
                                 .arg(m_skippedTinyCount)
                                 .arg(m_detailedPainterCount);
    }
}

void OpenGLPrimitiveRenderBackend::endFrame(QPainter& painter, const QSize& viewportSize) {
    Q_UNUSED(painter);
    Q_UNUSED(viewportSize);
}

void OpenGLPrimitiveRenderBackend::appendVertex(QVector<float>& out,
                                                const float x,
                                                const float y,
                                                const float r,
                                                const float g,
                                                const float b,
                                                const float a) {
    out.push_back(x);
    out.push_back(y);
    out.push_back(r);
    out.push_back(g);
    out.push_back(b);
    out.push_back(a);
}

void OpenGLPrimitiveRenderBackend::appendOutlineSegments(QVector<float>& out,
                                                         const QPolygonF& polygon,
                                                         const QColor& color) {
    const float r = color.redF();
    const float g = color.greenF();
    const float b = color.blueF();
    const float a = color.alphaF();
    const int vertexCount = polygon.size();
    if (vertexCount < 2) {
        return;
    }

    for (int i = 0; i < vertexCount; ++i) {
        const QPointF p1 = polygon[i];
        const QPointF p2 = polygon[(i + 1) % vertexCount];
        appendVertex(out, p1.x(), p1.y(), r, g, b, a);
        appendVertex(out, p2.x(), p2.y(), r, g, b, a);
    }
}

quint64 OpenGLPrimitiveRenderBackend::hashRenderItems(const QVector<RenderTypes::RenderItem>& items,
                                                      const QSize& viewportSize) {
    quint64 hash = 1469598103934665603ULL;
    hash ^= static_cast<quint64>(viewportSize.width() & 0xffffffff);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(viewportSize.height() & 0xffffffff);
    hash *= 1099511628211ULL;

    for (const RenderTypes::RenderItem& item : items) {
        const QRectF bounds = item.polygon.boundingRect();
        hash ^= static_cast<quint64>(item.fillColor.rgba64().toArgb32());
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>(qHash(item.pattern));
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>((item.detailLevel & 0xff)
                                     | ((item.selected ? 1 : 0) << 8)
                                     | ((item.preview ? 1 : 0) << 9));
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>(static_cast<qint64>(bounds.x() * 16.0));
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>(static_cast<qint64>(bounds.y() * 16.0));
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>(static_cast<qint64>(bounds.width() * 16.0));
        hash *= 1099511628211ULL;
        hash ^= static_cast<quint64>(static_cast<qint64>(bounds.height() * 16.0));
        hash *= 1099511628211ULL;
    }

    return hash;
}

void OpenGLPrimitiveRenderBackend::rebuildCachedGeometry(const QVector<RenderTypes::RenderItem>& items) {
    m_cachedTriangleVertexData.clear();
    m_cachedLineVertexData.clear();
    m_cachedTriangleVertexData.reserve(items.size() * 36);
    m_cachedLineVertexData.reserve(items.size() * 24);
    m_cachedTinySkipped = 0;
    m_cachedDetailedPainterCount = 0;
    m_cachedDetailedItems.clear();

    QHash<QRgb, QVector<const RenderTypes::RenderItem*>> styleBuckets;
    styleBuckets.reserve(std::max(8, items.size() / 32));

    for (const RenderTypes::RenderItem& item : items) {
        if (item.tinyOnScreen && !item.selected) {
            ++m_cachedTinySkipped;
            continue;
        }

        if (item.detailLevel == 0) {
            ++m_cachedDetailedPainterCount;
            m_cachedDetailedItems.push_back(item);
            continue;
        }

        QColor fillColor = item.fillColor;
        fillColor.setAlpha(item.preview ? 96 : 156);
        if (item.selected) {
            fillColor = fillColor.lighter(120);
        }

        styleBuckets[fillColor.rgba()].push_back(&item);
    }

    for (auto it = styleBuckets.cbegin(); it != styleBuckets.cend(); ++it) {
        const QColor fillColor = QColor::fromRgba(it.key());
        const float r = fillColor.redF();
        const float g = fillColor.greenF();
        const float b = fillColor.blueF();
        const float a = fillColor.alphaF();

        for (const RenderTypes::RenderItem* itemPtr : it.value()) {
            if (!itemPtr) {
                continue;
            }

            const RenderTypes::RenderItem& item = *itemPtr;
            const int vertexCount = item.polygon.size();
            if (vertexCount < 3) {
                continue;
            }

            const QPointF origin = item.polygon[0];
            for (int i = 1; i < vertexCount - 1; ++i) {
                const QPointF p1 = item.polygon[i];
                const QPointF p2 = item.polygon[i + 1];
                appendVertex(m_cachedTriangleVertexData, origin.x(), origin.y(), r, g, b, a);
                appendVertex(m_cachedTriangleVertexData, p1.x(), p1.y(), r, g, b, a);
                appendVertex(m_cachedTriangleVertexData, p2.x(), p2.y(), r, g, b, a);
            }

            if (item.selected) {
                appendOutlineSegments(m_cachedLineVertexData, item.polygon, QColor("#ffffff"));
            }
        }
    }
}

void OpenGLPrimitiveRenderBackend::appendPolygonTriangles(QVector<float>& out,
                                                          const QPolygonF& polygon,
                                                          const QColor& color) {
    const int vertexCount = polygon.size();
    if (vertexCount < 3) {
        return;
    }

    const float r = color.redF();
    const float g = color.greenF();
    const float b = color.blueF();
    const float a = color.alphaF();
    const QPointF origin = polygon[0];
    for (int i = 1; i < vertexCount - 1; ++i) {
        const QPointF p1 = polygon[i];
        const QPointF p2 = polygon[i + 1];
        appendVertex(out, origin.x(), origin.y(), r, g, b, a);
        appendVertex(out, p1.x(), p1.y(), r, g, b, a);
        appendVertex(out, p2.x(), p2.y(), r, g, b, a);
    }
}

void OpenGLPrimitiveRenderBackend::drawDetailedItemsWithGl(QOpenGLFunctions* gl, const int stride) {
    if (!gl || m_cachedDetailedItems.isEmpty()) {
        return;
    }

    if (!m_detailVertexBuffer.isCreated() && !m_detailVertexBuffer.create()) {
        return;
    }

    QVector<float> triangleVertices;
    QVector<float> lineVertices;
    for (const RenderTypes::RenderItem& item : m_cachedDetailedItems) {
        triangleVertices.clear();
        lineVertices.clear();
        appendPolygonTriangles(triangleVertices, item.polygon, item.fillColor);
        if (item.selected) {
            appendOutlineSegments(lineVertices, item.polygon, item.outlineColor);
        }

        const qsizetype totalFloats = triangleVertices.size() + lineVertices.size();
        if (totalFloats <= 0) {
            continue;
        }

        const qsizetype totalBytes = totalFloats * static_cast<qsizetype>(sizeof(float));
        m_detailVertexBuffer.bind();
        m_detailVertexBuffer.allocate(static_cast<int>(totalBytes));
        qsizetype byteOffset = 0;
        if (!triangleVertices.isEmpty()) {
            const qsizetype bytes = triangleVertices.size() * static_cast<qsizetype>(sizeof(float));
            m_detailVertexBuffer.write(byteOffset, triangleVertices.constData(), static_cast<int>(bytes));
            byteOffset += bytes;
        }
        if (!lineVertices.isEmpty()) {
            const qsizetype bytes = lineVertices.size() * static_cast<qsizetype>(sizeof(float));
            m_detailVertexBuffer.write(byteOffset, lineVertices.constData(), static_cast<int>(bytes));
        }

        m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, stride);
        m_program.setAttributeBuffer(1, GL_FLOAT, 2 * static_cast<int>(sizeof(float)), 4, stride);

        const std::array<float, 8> rows = RenderTypes::patternRowsFor(item.pattern);
        m_program.setUniformValue("uUseStipple", 1.0f);
        m_program.setUniformValueArray("uPatternRows", rows.data(), 8, 1);

        const int triangleVertexCount = static_cast<int>(triangleVertices.size() / 6);
        if (triangleVertexCount > 0) {
            gl->glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount);
        }

        const int lineVertexCount = static_cast<int>(lineVertices.size() / 6);
        if (lineVertexCount > 0) {
            m_program.setUniformValue("uUseStipple", 0.0f);
            gl->glLineWidth(1.0f);
            gl->glDrawArrays(GL_LINES, triangleVertexCount, lineVertexCount);
        }

        m_detailVertexBuffer.release();
    }

    m_program.setUniformValue("uUseStipple", 0.0f);
}

void OpenGLPrimitiveRenderBackend::uploadVertexData(const QVector<float>& triangleVertexData,
                                                    const QVector<float>& lineVertexData) {
    const qsizetype totalFloats = triangleVertexData.size() + lineVertexData.size();
    const qsizetype totalBytes = totalFloats * static_cast<qsizetype>(sizeof(float));
    if (totalBytes <= 0) {
        return;
    }

    if (totalBytes > m_vertexCapacityBytes) {
        m_vertexBuffer.allocate(static_cast<int>(totalBytes));
        m_vertexCapacityBytes = totalBytes;
    }

    qsizetype byteOffset = 0;
    if (!triangleVertexData.isEmpty()) {
        const qsizetype triangleBytes = triangleVertexData.size() * static_cast<qsizetype>(sizeof(float));
        m_vertexBuffer.write(byteOffset, triangleVertexData.constData(), static_cast<int>(triangleBytes));
        byteOffset += triangleBytes;
    }

    if (!lineVertexData.isEmpty()) {
        const qsizetype lineBytes = lineVertexData.size() * static_cast<qsizetype>(sizeof(float));
        m_vertexBuffer.write(byteOffset, lineVertexData.constData(), static_cast<int>(lineBytes));
    }
}

void OpenGLPrimitiveRenderBackend::drawWithPainterFallback(QPainter& painter,
                                                           const QVector<RenderTypes::RenderItem>& items) {
    for (const RenderTypes::RenderItem& item : items) {
        painter.setPen(QPen(item.outlineColor, 1, item.preview ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(item.detailLevel == 0 ? item.patternBrush : QBrush(item.fillColor, Qt::SolidPattern));
        painter.drawPolygon(item.polygon);
    }
}

bool OpenGLPrimitiveRenderBackend::initializeGlResources() {
    if (m_initialized) {
        return true;
    }

    if (!QOpenGLContext::currentContext()) {
        return false;
    }

    const char* vertexShader = R"(
        attribute vec2 aPosition;
        attribute vec4 aColor;
        varying vec4 vColor;
        uniform vec2 uViewport;
        void main() {
            vec2 ndc = vec2((aPosition.x / uViewport.x) * 2.0 - 1.0,
                            1.0 - ((aPosition.y / uViewport.y) * 2.0));
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )";

    const char* fragmentShader = R"(
        varying vec4 vColor;
        uniform float uUseStipple;
        uniform float uPatternRows[8];
        void main() {
            if (uUseStipple > 0.5) {
                // Repeat the stipple every 8 pixels to match the native 8x8 pattern definition.
                float x = mod(floor(gl_FragCoord.x), 8.0);
                float y = mod(floor(gl_FragCoord.y), 8.0);
                // Convert wrapped fragment coordinates directly into bit indices (unit-scale lookup).
                int bitX = int(x);
                int bitY = int(y);
                float row = uPatternRows[bitY];
                float divisor = exp2(float(bitX));
                float bit = mod(floor(row / divisor), 2.0);
                if (bit < 0.5) {
                    discard;
                }
            }
            gl_FragColor = vColor;
        }
    )";

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
        || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)) {
        return false;
    }

    m_program.bindAttributeLocation("aPosition", 0);
    m_program.bindAttributeLocation("aColor", 1);

    if (!m_program.link()) {
        return false;
    }

    if (m_vertexBuffer.isCreated()) {
        m_vertexBuffer.destroy();
    }
    if (!m_vertexBuffer.create()) {
        return false;
    }

    if (m_detailVertexBuffer.isCreated()) {
        m_detailVertexBuffer.destroy();
    }
    if (!m_detailVertexBuffer.create()) {
        return false;
    }

    m_initialized = true;
    return true;
}
