#include "LayoutEditorWindow.h"
#include "LayoutSceneModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <algorithm>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QIcon>
#include <QLabel>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>
#include <QVector2D>
#include <QWheelEvent>
#include <QWidget>
#include <QtGlobal>
#include <QDebug>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {
// Qt5/Qt6 compatibility helper for mouse event coordinates.
QPointF mouseEventPoint(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

// Qt5/Qt6 compatibility helper for wheel event coordinates.
QPointF wheelEventPoint(const QWheelEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return QPointF(event->pos());
#endif
}

QBrush patternBrushFor(QColor baseColor, const QString& pattern) {
    bool ok = false;
    const quint64 patternValue = static_cast<quint64>(pattern.toULongLong(&ok, 0));
    if (!ok) {
        return QBrush(baseColor, Qt::SolidPattern);
    }

    const int patternMag = 2;

    QPixmap pixmap(8 * patternMag, 8 * patternMag);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(baseColor);

    for (int y = 0; y < (8 * patternMag); ++y) {
        for (int x = 0; x < (8 * patternMag); ++x) {
            const int bitIndex = ((y / patternMag) * 8) + (x / patternMag);
            if ((patternValue >> bitIndex) & 0x1ULL) {
                painter.drawPoint(x, y);
            }
        }
    }

    return QBrush(pixmap);
}

quint64 layerCodeKey(quint32 nameId, quint32 typeId) {
    return (static_cast<quint64>(nameId) << 32) | static_cast<quint64>(typeId);
}

enum class CanvasRenderBackendType {
    Raster,
    OpenGL
};

CanvasRenderBackendType backendTypeFromEnv() {
    const QByteArray backendName = qgetenv("LAYOUT2_RENDER_BACKEND").trimmed().toLower();
    return backendName == "opengl" ? CanvasRenderBackendType::OpenGL : CanvasRenderBackendType::Raster;
}

class PrimitiveRenderBackend {
public:
    virtual ~PrimitiveRenderBackend() = default;

    struct RenderItem {
        QPolygonF polygon;
        QColor fillColor;
        QColor outlineColor;
        QBrush patternBrush;
        bool selected{false};
        bool preview{false};
        bool tinyOnScreen{false};
        int detailLevel{0};
    };

    virtual void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) = 0;

    virtual void drawPrimitives(QPainter& painter,
                                const QVector<RenderItem>& items,
                                const QSize& viewportSize) = 0;

    virtual void endFrame(QPainter& painter, const QSize& viewportSize) = 0;
};

class RasterPrimitiveRenderBackend final : public PrimitiveRenderBackend {
public:
    void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) override {
        Q_UNUSED(viewportSize);
        painter.fillRect(painter.viewport(), clearColor);
    }

    void drawPrimitives(QPainter& painter,
                        const QVector<RenderItem>& items,
                        const QSize& viewportSize) override {
        Q_UNUSED(viewportSize);
        for (const RenderItem& item : items) {
            if (item.detailLevel == 2 && item.tinyOnScreen && !item.selected) {
                continue;
            }

            if (item.detailLevel == 0) {
                painter.setPen(QPen(item.outlineColor, 1, item.preview ? Qt::DashLine : Qt::SolidLine));
                painter.setBrush(item.patternBrush);
            } else if (item.detailLevel == 1) {
                painter.setPen(item.selected
                                   ? QPen(item.outlineColor, 1, Qt::SolidLine)
                                   : QPen(item.outlineColor, 0, Qt::NoPen));
                painter.setBrush(QBrush(item.fillColor, Qt::SolidPattern));
            } else {
                painter.setPen(item.selected
                                   ? QPen(item.outlineColor, 1, Qt::SolidLine)
                                   : QPen(item.outlineColor, 0, Qt::NoPen));
                QColor coarseFill = item.fillColor;
                coarseFill.setAlpha(std::min(255, item.fillColor.alpha() + 50));
                painter.setBrush(QBrush(coarseFill, Qt::SolidPattern));
            }

            painter.drawPolygon(item.polygon);
        }
    }

    void endFrame(QPainter& painter, const QSize& viewportSize) override {
        Q_UNUSED(painter);
        Q_UNUSED(viewportSize);
    }
};

// OpenGL backend:
// - detailed patterned mode remains painter-based for visual parity,
// - simplified/coarse modes submit triangles (and selection outlines) via GL.
class OpenGLPrimitiveRenderBackend final : public PrimitiveRenderBackend {
public:
    OpenGLPrimitiveRenderBackend()
        : m_statsEnabled(qEnvironmentVariableIntValue("LAYOUT2_RENDER_STATS") != 0) {}

    void beginFrame(QPainter& painter, const QColor& clearColor, const QSize& viewportSize) override {
        Q_UNUSED(painter);
        Q_UNUSED(clearColor);
        Q_UNUSED(viewportSize);
    }

    void drawPrimitives(QPainter& painter,
                        const QVector<RenderItem>& items,
                        const QSize& viewportSize) override {
        if (!initializeGlResources()) {
            // Fallback to painter-only rendering if GL setup fails.
            drawDetailedWithPainter(painter, items);
            return;
        }

        QVector<float> triangleVertexData;
        QVector<float> lineVertexData;
        triangleVertexData.reserve(items.size() * 36);
        lineVertexData.reserve(items.size() * 24);

        QHash<QRgb, QVector<const RenderItem*>> styleBuckets;
        styleBuckets.reserve(std::max(8, items.size() / 32));

        int skippedTinyCount = 0;
        int painterDetailedCount = 0;

        for (const RenderItem& item : items) {
            if (item.tinyOnScreen && !item.selected) {
                ++skippedTinyCount;
                continue;
            }

            if (item.detailLevel == 0) {
                ++painterDetailedCount;
                painter.setPen(QPen(item.outlineColor, 1, item.preview ? Qt::DashLine : Qt::SolidLine));
                painter.setBrush(item.patternBrush);
                painter.drawPolygon(item.polygon);
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
            QColor fillColor = QColor::fromRgba(it.key());

            const float r = fillColor.redF();
            const float g = fillColor.greenF();
            const float b = fillColor.blueF();
            const float a = fillColor.alphaF();

            for (const RenderItem* itemPtr : it.value()) {
                if (!itemPtr) {
                    continue;
                }
                const RenderItem& item = *itemPtr;

                const int vertexCount = item.polygon.size();
                if (vertexCount < 3) {
                    continue;
                }

                const QPointF origin = item.polygon[0];
                for (int i = 1; i < vertexCount - 1; ++i) {
                    const QPointF p1 = item.polygon[i];
                    const QPointF p2 = item.polygon[i + 1];
                    appendVertex(triangleVertexData, origin.x(), origin.y(), r, g, b, a);
                    appendVertex(triangleVertexData, p1.x(), p1.y(), r, g, b, a);
                    appendVertex(triangleVertexData, p2.x(), p2.y(), r, g, b, a);
                }

                if (item.selected) {
                    appendOutlineSegments(lineVertexData, item.polygon, QColor("#ffffff"));
                }
            }
        }

        if (triangleVertexData.isEmpty() && lineVertexData.isEmpty()) {
            return;
        }

        painter.beginNativePainting();
        m_program.bind();
        m_program.setUniformValue("uViewport", QVector2D(viewportSize.width(), viewportSize.height()));

        m_vertexBuffer.bind();
        uploadVertexData(triangleVertexData, lineVertexData);

        constexpr int stride = 6 * static_cast<int>(sizeof(float));
        m_program.enableAttributeArray(0);
        m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, stride);
        m_program.enableAttributeArray(1);
        m_program.setAttributeBuffer(1, GL_FLOAT, 2 * static_cast<int>(sizeof(float)), 4, stride);

        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        gl->glDisable(GL_DEPTH_TEST);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const int triangleVertexCount = static_cast<int>(triangleVertexData.size() / 6);
        if (triangleVertexCount > 0) {
            gl->glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount);
        }

        const int lineVertexCount = static_cast<int>(lineVertexData.size() / 6);
        if (lineVertexCount > 0) {
            gl->glLineWidth(2.0f);
            gl->glDrawArrays(GL_LINES, triangleVertexCount, lineVertexCount);
        }

        m_program.disableAttributeArray(0);
        m_program.disableAttributeArray(1);
        m_vertexBuffer.release();
        m_program.release();
        painter.endNativePainting();

        m_frameCounter += 1;
        m_trianglesSubmitted += (triangleVertexData.size() / 18);
        m_linesSubmitted += (lineVertexData.size() / 12);
        m_skippedTinyCount += static_cast<quint64>(std::max(0, skippedTinyCount));
        m_painterDetailedCount += static_cast<quint64>(std::max(0, painterDetailedCount));
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
                                     .arg(m_painterDetailedCount);
        }
    }

    void endFrame(QPainter& painter, const QSize& viewportSize) override {
        Q_UNUSED(painter);
        Q_UNUSED(viewportSize);
    }

private:
    static void appendVertex(QVector<float>& out,
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

    static void appendOutlineSegments(QVector<float>& out,
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

    void uploadVertexData(const QVector<float>& triangleVertexData,
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

    void drawDetailedWithPainter(QPainter& painter, const QVector<RenderItem>& items) {
        for (const RenderItem& item : items) {
            painter.setPen(QPen(item.outlineColor, 1, item.preview ? Qt::DashLine : Qt::SolidLine));
            painter.setBrush(item.detailLevel == 0 ? item.patternBrush : QBrush(item.fillColor, Qt::SolidPattern));
            painter.drawPolygon(item.polygon);
        }
    }

    bool initializeGlResources() {
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
            void main() {
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

        m_initialized = true;
        return true;
    }

    bool m_initialized{false};
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vertexBuffer;
    qsizetype m_vertexCapacityBytes{0};
    bool m_statsEnabled{false};
    QElapsedTimer m_statsTimer;
    quint64 m_frameCounter{0};
    quint64 m_trianglesSubmitted{0};
    quint64 m_linesSubmitted{0};
    quint64 m_skippedTinyCount{0};
    quint64 m_painterDetailedCount{0};
};
} // namespace

bool isModifierOnlyKey(int key) {
    return key == Qt::Key_Shift
           || key == Qt::Key_Control
           || key == Qt::Key_Alt
           || key == Qt::Key_Meta
           || key == Qt::Key_AltGr;
}

QString keySpecFromEvent(const QKeyEvent* event) {
    const int key = event->key();
    if (key == Qt::Key_unknown || isModifierOnlyKey(key)) {
        return QString();
    }

    Qt::KeyboardModifiers modifiers = event->modifiers();

    const QKeySequence sequence(modifiers | key);
    return sequence.toString(QKeySequence::PortableText);
}

// LayoutCanvas is the drawable area on the right side of the editor.
//
// It is intentionally thin: all interactions are converted into Tcl command
// strings and emitted upward via commandRequested().
class LayoutCanvas : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr)
        : QOpenGLWidget(parent),
          m_backendType(backendTypeFromEnv()) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

        if (m_backendType == CanvasRenderBackendType::OpenGL) {
            m_renderBackend = std::make_unique<OpenGLPrimitiveRenderBackend>();
        } else {
            m_renderBackend = std::make_unique<RasterPrimitiveRenderBackend>();
        }
    }

    void setRootCell(const LayoutSceneNode* rootCell) {
        m_rootCell = rootCell;
        validateHover();
        update();
    }

    void setEditPreview(bool enabled, const SceneRenderPrimitive& primitive) {
        m_editPreviewEnabled = enabled;
        m_editPreview = primitive;
        update();
    }

    void setView(double zoom, double panX, double panY, double gridSize) {
        m_zoom = zoom;
        m_panX = panX;
        m_panY = panY;
        m_gridSize = gridSize;
        update();
    }

    void setLayers(const QVector<LayerDefinition>& layers) {
        m_layers = layers;
        rebuildLayerLookup();
        m_fillBrushCache.clear();
        validateSelection();
        validateHover();
        update();
    }

    void setActiveTool(const QString& toolName) {
        m_activeTool = toolName;
        if (m_activeTool != "select") {
            m_hoveredObjectId = 0;
        }
        update();
    }

signals:
    void commandRequested(const QString& command, bool requestActivation);
    void objectDeletionRequested(quint64 objectId);
    void mouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas);

protected:
    enum class RenderDetailLevel {
        Detailed,
        Simplified,
        Coarse
    };

    void paintGL() override {
        if (m_backendType == CanvasRenderBackendType::OpenGL) {
            if (QOpenGLContext* ctx = context()) {
                if (QOpenGLFunctions* gl = ctx->functions()) {
                    gl->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
                    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
            }
        }

        QPainter painter(this);

        // Background and world-anchored grid for orientation.
        m_renderBackend->beginFrame(painter, QColor("#000000"), size());
        drawGrid(painter);

        // Draw committed geometry first from model-provided primitives.
        const RenderDetailLevel detailLevel = currentDetailLevel();
        const QVector<SceneRenderPrimitive> primitives = flattenedRenderPrimitives();
        const QVector<PrimitiveRenderBackend::RenderItem> renderItems =
            buildRenderItems(primitives, detailLevel);
        m_renderBackend->drawPrimitives(painter, renderItems, size());

        if (m_activeTool == "select" && m_hoveredObjectId != 0 && m_rootCell) {
            QVector<WorldLineSegment> previewSegments;
            if (m_rootCell->collectOutlineSegmentsByObjectId(m_hoveredObjectId, previewSegments)) {
                drawHoverOutline(painter, previewSegments);
            }
        }

        m_renderBackend->endFrame(painter, size());

    }

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            && m_selectedObjectId != 0) {
            emit objectDeletionRequested(m_selectedObjectId);
            m_selectedObjectId = 0;
            update();
            event->accept();
            return;
        }

        const QString keySpec = keySpecFromEvent(event);
        if (!keySpec.isEmpty()) {
            emit commandRequested(QString("bindkey dispatch {%1}").arg(keySpec), true);
            event->accept();
            return;
        }

        QOpenGLWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            const qint64 worldX = static_cast<qint64>(world.x());
            const qint64 worldY = static_cast<qint64>(world.y());

            if (m_activeTool == "select") {
                handleSelectionClick(worldX, worldY);
                event->accept();
                return;
            }

            emit commandRequested(QString("canvas press %1 %2 1")
                                      .arg(worldX)
                                      .arg(worldY),
                                true);
        }

        if (event->button() == Qt::MiddleButton) {
            m_lastPanPoint = mouseEventPoint(event);
            m_middlePanning = true;
        }

        QOpenGLWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        // Move events carry current cursor position + left-button state.
        const QPointF world = screenToWorld(mouseEventPoint(event));
        const qint64 worldX = static_cast<qint64>(world.x());
        const qint64 worldY = static_cast<qint64>(world.y());
        emit mouseWorldPositionChanged(worldX, worldY, true);
        const bool leftDown = event->buttons() & Qt::LeftButton;

        if (m_activeTool == "select") {
            m_hoveredObjectId = hoveredSelectableObjectIdAt(worldX, worldY);
            update();
        }

        emit commandRequested(QString("canvas move %1 %2 %3")
                                  .arg(worldX)
                                  .arg(worldY)
                                  .arg(leftDown ? 1 : 0),
                            false);

        // Middle-button drag emits view pan commands.
        if (m_middlePanning && (event->buttons() & Qt::MiddleButton)) {
            const QPointF delta = mouseEventPoint(event) - m_lastPanPoint;
            m_lastPanPoint = mouseEventPoint(event);
            emit commandRequested(QString("view pan %1 %2")
                                      .arg(delta.x())
                                      .arg(delta.y()),
                                false);
        }

        QOpenGLWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        emit mouseWorldPositionChanged(0, 0, false);
        if (m_hoveredObjectId != 0) {
            m_hoveredObjectId = 0;
            update();
        }
        QOpenGLWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPointF world = screenToWorld(mouseEventPoint(event));
            emit commandRequested(QString("canvas release %1 %2 1")
                                      .arg(static_cast<qint64>(world.x()))
                                      .arg(static_cast<qint64>(world.y())),
                                false);
        }

        if (event->button() == Qt::MiddleButton) {
            m_middlePanning = false;
        }

        QOpenGLWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        const QPointF pos = wheelEventPoint(event);
        emit commandRequested(QString("view zoom %1 %2 %3")
                                  .arg(event->angleDelta().y())
                                  .arg(pos.x())
                                  .arg(pos.y()),
                            false);
        event->accept();
    }

private:
    // Converts world integer coordinates into screen-space doubles.
    QPointF worldToScreen(qint64 x, qint64 y) const {
        return QPointF((static_cast<double>(x) * m_zoom) + m_panX,
                       m_panY - (static_cast<double>(y) * m_zoom));
    }

    // Converts screen coordinates into world-space doubles.
    QPointF screenToWorld(const QPointF& p) const {
        return QPointF((p.x() - m_panX) / m_zoom,
                       (m_panY - p.y()) / m_zoom);
    }

    QVector<PrimitiveRenderBackend::RenderItem> buildRenderItems(
        const QVector<SceneRenderPrimitive>& primitives,
        const RenderDetailLevel detailLevel) {
        QVector<PrimitiveRenderBackend::RenderItem> items;
        items.reserve(primitives.size());

        for (const SceneRenderPrimitive& primitive : primitives) {
            const LayerDefinition* layer = layerForPrimitive(primitive);
            if (!layer || !layer->visible || primitive.polygonVertices.isEmpty()) {
                continue;
            }

            PrimitiveRenderBackend::RenderItem item;
            item.polygon.reserve(primitive.polygonVertices.size());
            for (const WorldPoint& vertex : primitive.polygonVertices) {
                item.polygon.push_back(worldToScreen(vertex.x, vertex.y));
            }

            item.selected = primitive.objectId == m_selectedObjectId;
            item.preview = primitive.preview;
            item.detailLevel = detailLevel == RenderDetailLevel::Detailed
                                   ? 0
                                   : detailLevel == RenderDetailLevel::Simplified ? 1 : 2;
            item.tinyOnScreen = item.polygon.boundingRect().width() < 1.0
                                && item.polygon.boundingRect().height() < 1.0;

            item.fillColor = layer->color;
            if (item.selected) {
                item.fillColor = item.fillColor.lighter(130);
            }
            item.fillColor.setAlpha(item.preview ? 90 : 140);

            item.outlineColor = layer->color;
            item.outlineColor.setAlpha(item.preview ? 180 : 220);
            if (item.selected) {
                item.outlineColor = QColor("#ffffff");
                item.outlineColor.setAlpha(255);
            }

            item.patternBrush = brushForFillColor(item.fillColor, layer->pattern);
            items.push_back(std::move(item));
        }

        return items;
    }

    RenderDetailLevel currentDetailLevel() const {
        if (m_zoom < 0.30) {
            return RenderDetailLevel::Coarse;
        }
        if (m_zoom < 1.0) {
            return RenderDetailLevel::Simplified;
        }
        return RenderDetailLevel::Detailed;
    }

    QBrush brushForFillColor(const QColor& fillColor, const QString& pattern) {
        const QString cacheKey = QString("%1|%2").arg(fillColor.name(QColor::HexArgb), pattern);
        const auto it = m_fillBrushCache.constFind(cacheKey);
        if (it != m_fillBrushCache.cend()) {
            return it.value();
        }

        const QBrush brush = patternBrushFor(fillColor, pattern);
        m_fillBrushCache.insert(cacheKey, brush);
        return brush;
    }

    void drawHoverOutline(QPainter& painter, const QVector<WorldLineSegment>& segments) {
        painter.setPen(QPen(QColor("#ffd400"), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);

        for (const WorldLineSegment& segment : segments) {
            const QPointF p1 = worldToScreen(segment.x1, segment.y1);
            const QPointF p2 = worldToScreen(segment.x2, segment.y2);
            painter.drawLine(p1, p2);
        }
    }

    const LayerDefinition* layerForPrimitive(const SceneRenderPrimitive& primitive) const {
        const auto it = m_layerIndexByCode.constFind(layerCodeKey(primitive.layerNameId, primitive.layerTypeId));
        if (it == m_layerIndexByCode.cend()) {
            return nullptr;
        }

        const int index = it.value();
        if (index < 0 || index >= m_layers.size()) {
            return nullptr;
        }

        return &m_layers[index];
    }

    const LayerDefinition* layerForRectangle(const DrawnRectangle& rectangle) const {
        const auto it = m_layerIndexByCode.constFind(layerCodeKey(rectangle.layerNameId, rectangle.layerTypeId));
        if (it == m_layerIndexByCode.cend()) {
            return nullptr;
        }

        const int index = it.value();
        if (index < 0 || index >= m_layers.size()) {
            return nullptr;
        }

        return &m_layers[index];
    }

    void rebuildLayerLookup() {
        m_layerIndexByCode.clear();

        // Lookup index is independent of palette order and optimized for ID-based queries.
        for (int i = 0; i < m_layers.size(); ++i) {
            m_layerIndexByCode.insert(layerCodeKey(m_layers[i].nameId, m_layers[i].typeId), i);
        }
    }

    bool isSelectableRectangle(const DrawnRectangle& rectangle) const {
        const LayerDefinition* layer = layerForRectangle(rectangle);
        return layer && layer->visible && layer->selectable;
    }

    bool isSelectableObjectId(quint64 objectId) const {
        if (!m_rootCell || objectId == 0) {
            return false;
        }

        const LayoutObjectModel* object = m_rootCell->findObjectById(objectId);
        if (!object) {
            return false;
        }

        const DrawnRectangle* rectangle = object->asRectangle();
        return rectangle && isSelectableRectangle(*rectangle);
    }

    QVector<SceneRenderPrimitive> flattenedRenderPrimitives() const {
        QVector<SceneRenderPrimitive> primitives;
        if (m_rootCell) {
            qint64 minX = 0;
            qint64 minY = 0;
            qint64 maxX = 0;
            qint64 maxY = 0;
            visibleWorldBounds(minX, minY, maxX, maxY);
            m_rootCell->collectRenderPrimitivesInRect(minX, minY, maxX, maxY, primitives);
        }

        if (m_editPreviewEnabled) {
            primitives.push_back(m_editPreview);
        }

        return primitives;
    }

    void visibleWorldBounds(qint64& minX, qint64& minY, qint64& maxX, qint64& maxY) const {
        const QPointF topLeftWorld = screenToWorld(QPointF(0.0, 0.0));
        const QPointF bottomRightWorld = screenToWorld(QPointF(width(), height()));

        minX = static_cast<qint64>(std::floor(std::min(topLeftWorld.x(), bottomRightWorld.x())));
        maxX = static_cast<qint64>(std::ceil(std::max(topLeftWorld.x(), bottomRightWorld.x())));
        minY = static_cast<qint64>(std::floor(std::min(topLeftWorld.y(), bottomRightWorld.y())));
        maxY = static_cast<qint64>(std::ceil(std::max(topLeftWorld.y(), bottomRightWorld.y())));
    }

    QVector<quint64> selectableObjectCandidatesAt(qint64 x, qint64 y) const {
        QVector<quint64> candidates;
        if (!m_rootCell) {
            return candidates;
        }

        const QVector<quint64> objectMatches = m_rootCell->matchingObjectIdsAt(
            x,
            y,
            [this](const LayoutObjectModel& object) {
                const DrawnRectangle* rectangle = object.asRectangle();
                return rectangle && isSelectableRectangle(*rectangle);
            });
        if (objectMatches.isEmpty()) {
            return candidates;
        }

        for (quint64 objectId : objectMatches) {
            candidates.push_back(objectId);
        }

        return candidates;
    }

    quint64 hoveredSelectableObjectIdAt(qint64 x, qint64 y) const {
        const QVector<quint64> candidates = selectableObjectCandidatesAt(x, y);
        return candidates.isEmpty() ? 0 : candidates.front();
    }

    void handleSelectionClick(qint64 x, qint64 y) {

        const QVector<quint64> candidates = selectableObjectCandidatesAt(x, y);
        if (candidates.isEmpty()) {
            m_selectedObjectId = 0;
            m_hoveredObjectId = 0;
            m_lastSelectionCandidateIds.clear();
            m_lastSelectionPoint = QPointF();
            update();
            return;
        }

        const QPointF selectionPoint(static_cast<double>(x), static_cast<double>(y));
        const bool samePoint = m_hasSelectionPoint && m_lastSelectionPoint == selectionPoint;
        const bool sameCandidates = samePoint && (candidates == m_lastSelectionCandidateIds);

        if (!sameCandidates) {
            m_selectedObjectId = candidates.front();
            m_hoveredObjectId = candidates.size() > 1 ? candidates[1] : candidates.front();
        } else {
            int currentCandidate = candidates.indexOf(m_selectedObjectId);
            if (currentCandidate < 0) {
                currentCandidate = 0;
            }

            const int nextSelectedCandidate = (currentCandidate + 1) % candidates.size();
            m_selectedObjectId = candidates[nextSelectedCandidate];
            m_hoveredObjectId = candidates[(nextSelectedCandidate + 1) % candidates.size()];
        }

        m_lastSelectionCandidateIds = candidates;
        m_lastSelectionPoint = selectionPoint;
        m_hasSelectionPoint = true;
        update();
    }

    void validateSelection() {
        if (!isSelectableObjectId(m_selectedObjectId)) {
            m_selectedObjectId = 0;
        }
    }

    void validateHover() {
        if (!isSelectableObjectId(m_hoveredObjectId)) {
            m_hoveredObjectId = 0;
        }
    }

    void drawGrid(QPainter& painter) {
        if (m_gridSize <= 0.0 || m_zoom <= 0.0) {
            return;
        }

        // Prevent very dense grid rendering when zoomed out by adaptively
        // increasing the displayed step while keeping it world-anchored.
        const double minimumPixelSpacing = 10.0;
        const double basePixelSpacing = m_gridSize * m_zoom;
        const double multiplier = std::max(1.0, std::ceil(minimumPixelSpacing / basePixelSpacing));
        const double visibleStep = m_gridSize * multiplier;

        const QPointF topLeftWorld = screenToWorld(QPointF(0.0, 0.0));
        const QPointF bottomRightWorld = screenToWorld(QPointF(width(), height()));

        const double worldMinX = std::min(topLeftWorld.x(), bottomRightWorld.x());
        const double worldMaxX = std::max(topLeftWorld.x(), bottomRightWorld.x());
        const double worldMinY = std::min(topLeftWorld.y(), bottomRightWorld.y());
        const double worldMaxY = std::max(topLeftWorld.y(), bottomRightWorld.y());

        const double firstGridX = std::floor(worldMinX / visibleStep) * visibleStep;
        const double firstGridY = std::floor(worldMinY / visibleStep) * visibleStep;

        painter.setPen(QPen(QColor("#5a5a5a"), 1));
        for (double worldX = firstGridX; worldX <= worldMaxX; worldX += visibleStep) {
            const double screenX = (worldX * m_zoom) + m_panX;
            for (double worldY = firstGridY; worldY <= worldMaxY; worldY += visibleStep) {
                const double screenY = m_panY - (worldY * m_zoom);
                painter.drawPoint(QPointF(screenX, screenY));
            }
        }

        painter.setPen(QPen(QColor("#5a5a5a"), 1));
        const double originScreenX = m_panX;
        const double originScreenY = m_panY;

        if (originScreenX >= 0.0 && originScreenX <= width()) {
            painter.drawLine(QPointF(originScreenX, 0.0), QPointF(originScreenX, height()));
        }

        if (originScreenY >= 0.0 && originScreenY <= height()) {
            painter.drawLine(QPointF(0.0, originScreenY), QPointF(width(), originScreenY));
        }
    }

    const LayoutSceneNode* m_rootCell{nullptr};
    QVector<LayerDefinition> m_layers;
    QHash<quint64, int> m_layerIndexByCode;
    QHash<QString, QBrush> m_fillBrushCache;
    CanvasRenderBackendType m_backendType{CanvasRenderBackendType::Raster};
    std::unique_ptr<PrimitiveRenderBackend> m_renderBackend;
    SceneRenderPrimitive m_editPreview;
    QString m_activeTool{"none"};

    quint64 m_selectedObjectId{0};
    quint64 m_hoveredObjectId{0};
    QVector<quint64> m_lastSelectionCandidateIds;
    QPointF m_lastSelectionPoint;
    bool m_hasSelectionPoint{false};

    bool m_editPreviewEnabled{false};
    bool m_middlePanning{false};

    QPointF m_lastPanPoint;
    double m_zoom{1.0};
    double m_panX{0.0};
    double m_panY{0.0};
    double m_gridSize{40.0};
};

LayoutEditorWindow::LayoutEditorWindow(QWidget* parent)
    : QMainWindow(parent),
      m_layerTable(new QTableWidget()),
      m_canvas(new LayoutCanvas()),
      m_statusLabel(new QLabel()),
      m_rootCell(std::make_unique<LayoutSceneNode>()) {
    refreshWindowTitle();
    resize(1100, 700);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto* splitter = new QSplitter(central);

    // Left pane: layer palette table.
    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(6, 6, 6, 6);

    m_layerTable->setColumnCount(5);
    m_layerTable->setHorizontalHeaderLabels({"", "Layer", "Type", "V", "S"});
    m_layerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_layerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_layerTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_layerTable->verticalHeader()->setVisible(false);
    m_layerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_layerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerTable->setStyleSheet(
        "QTableWidget::item:selected {"
        "background: transparent;"
        "color: palette(text);"
        "}");
    leftLayout->addWidget(m_layerTable);

    // Right pane: interactive canvas.
    auto* rightPane = new QFrame(splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_canvas);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({280, 820});

    m_statusLabel->setText("Active layer: <none> | Tool: <none>");
    m_statusLabel->setStyleSheet("color:#ddd; background:#222; padding:2px 6px;");
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    centralLayout->addWidget(splitter);
    centralLayout->addWidget(m_statusLabel);
    setCentralWidget(central);

    // UI events are converted into Tcl command strings.
    connect(m_layerTable, &QTableWidget::cellChanged, this, &LayoutEditorWindow::onCellChanged);
    connect(m_layerTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int previousRow, int) { onCurrentRowChanged(currentRow, previousRow); });
    connect(m_canvas, &LayoutCanvas::commandRequested, this, &LayoutEditorWindow::commandRequested);
    connect(m_canvas, &LayoutCanvas::objectDeletionRequested,
            this, &LayoutEditorWindow::onObjectDeletionRequested);
    connect(m_canvas, &LayoutCanvas::mouseWorldPositionChanged,
            this, &LayoutEditorWindow::onMouseWorldPositionChanged);

    qApp->installEventFilter(this);

    m_canvas->setRootCell(m_rootCell.get());
    refreshStatusLabel();
}

LayoutEditorWindow::~LayoutEditorWindow() {
    qApp->removeEventFilter(this);
}

QSize LayoutEditorWindow::canvasViewportSize() const {
    return m_canvas->size();
}

void LayoutEditorWindow::setEditorIdentity(const int editorId, const bool isActive) {
    m_editorId = editorId;
    m_isActiveEditor = isActive;
    refreshWindowTitle();
}

bool LayoutEditorWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (widget->window() == this) {
                emit activationRequested();
            }
        }
    }

    if (watched == m_layerTable && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const QString keySpec = keySpecFromEvent(keyEvent);
        if (!keySpec.isEmpty()) {
            emit commandRequested(QString("bindkey dispatch {%1}").arg(keySpec), true);
            keyEvent->accept();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

QTableWidgetItem* LayoutEditorWindow::makeReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

void LayoutEditorWindow::setLayers(const QVector<LayerDefinition>& layers) {
    m_internalUpdate = true;
    m_layers = layers;
    m_activeLayerName = layers.isEmpty() ? QString() : layers[0].name;
    m_activeLayerType = layers.isEmpty() ? QString() : layers[0].type;
    m_layerTable->setRowCount(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        applyLayerToRow(row, layers[row]);
    }

    m_internalUpdate = false;
    updateActiveLayerHighlight();
    m_canvas->setLayers(m_layers);
}

QBrush LayoutEditorWindow::makePatternBrush(const LayerDefinition& layer) const {
    return patternBrushFor(layer.color, layer.pattern);
}

void LayoutEditorWindow::updateActiveLayerHighlight() {
    const QColor highlight(53, 86, 118, 130);

    const bool wasInternalUpdate = m_internalUpdate;
    m_internalUpdate = true;

    for (int row = 0; row < m_layers.size(); ++row) {
        const bool isActive = m_layers[row].name.compare(m_activeLayerName, Qt::CaseInsensitive) == 0
                              && m_layers[row].type.compare(m_activeLayerType, Qt::CaseInsensitive) == 0;
        for (int column = 1; column < m_layerTable->columnCount(); ++column) {
            if (auto* item = m_layerTable->item(row, column)) {
                item->setBackground(isActive ? QBrush(highlight) : QBrush(Qt::NoBrush));
            }
        }
    }

    m_internalUpdate = wasInternalUpdate;
}

void LayoutEditorWindow::applyLayerToRow(int row, const LayerDefinition& layer) {
    // Color/pattern swatch column.
    auto* swatch = new QTableWidgetItem();
    swatch->setText(QString());
    swatch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    swatch->setSizeHint(QSize(24, 24));

    QPixmap swatchPixmap(16, 16);
    swatchPixmap.fill(QColor("#000000"));
    QPainter swatchPainter(&swatchPixmap);
    swatchPainter.fillRect(swatchPixmap.rect(), makePatternBrush(layer));
    swatchPainter.setPen(QColor("#1a1a1a"));
    swatchPainter.drawRect(swatchPixmap.rect().adjusted(0, 0, -1, -1));
    swatch->setIcon(QIcon(swatchPixmap));

    m_layerTable->setItem(row, 0, swatch);

    // Name and type columns.
    m_layerTable->setItem(row, 1, makeReadOnlyItem(layer.name));
    m_layerTable->setItem(row, 2, makeReadOnlyItem(layer.type));

    // Visibility and selectability checkboxes.
    auto* visibleItem = new QTableWidgetItem();
    visibleItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    visibleItem->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    m_layerTable->setItem(row, 3, visibleItem);

    auto* selectableItem = new QTableWidgetItem();
    selectableItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    selectableItem->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
    m_layerTable->setItem(row, 4, selectableItem);

    updateActiveLayerHighlight();
}

void LayoutEditorWindow::onLayerChanged(int index, const LayerDefinition& layer) {
    if (index < 0 || index >= m_layers.size()) {
        return;
    }

    m_layers[index] = layer;

    // Apply state from model while muting feedback events.
    m_internalUpdate = true;
    if (m_layerTable->item(index, 3)) {
        m_layerTable->item(index, 3)->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    }
    if (m_layerTable->item(index, 4)) {
        m_layerTable->item(index, 4)->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
    }
    m_internalUpdate = false;
    m_canvas->setLayers(m_layers);
}

void LayoutEditorWindow::onActiveLayerChanged(const QString& layerName, const QString& layerType) {
    m_activeLayerName = layerName;
    m_activeLayerType = layerType;
    refreshStatusLabel();

    // Highlight corresponding row (without retriggering command emission).
    m_internalUpdate = true;
    for (int row = 0; row < m_layers.size(); ++row) {
        if (m_layers[row].name.compare(layerName, Qt::CaseInsensitive) == 0
            && m_layers[row].type.compare(layerType, Qt::CaseInsensitive) == 0) {
            m_layerTable->setCurrentCell(row, 1);
            break;
        }
    }
    m_internalUpdate = false;

    updateActiveLayerHighlight();
}

void LayoutEditorWindow::onToolChanged(const QString& toolName) {
    m_activeTool = toolName;
    refreshStatusLabel();
    m_canvas->setActiveTool(toolName);
}

void LayoutEditorWindow::onMouseWorldPositionChanged(qint64 worldX, qint64 worldY, bool insideCanvas) {
    m_mouseInsideCanvas = insideCanvas;
    if (insideCanvas) {
        m_mouseWorldX = worldX;
        m_mouseWorldY = worldY;
    }
    refreshStatusLabel();
}

void LayoutEditorWindow::refreshStatusLabel() {
    const QString layerPart = m_activeLayerName.isEmpty() || m_activeLayerType.isEmpty()
                                  ? "<none>"
                                  : QString("%1 (%2)").arg(m_activeLayerName, m_activeLayerType);
    const QString cursorPart = m_mouseInsideCanvas
                                   ? QString("X: %1 Y: %2").arg(m_mouseWorldX).arg(m_mouseWorldY)
                                   : "X: -- Y: --";
    m_statusLabel->setText(QString("Active layer: %1 | Tool: %2 | Cursor: %3")
                               .arg(layerPart, m_activeTool, cursorPart));
}

void LayoutEditorWindow::refreshWindowTitle() {
    const QString editorPart = m_editorId > 0 ? QString::number(m_editorId) : QString("?");
    const QString activeSuffix = m_isActiveEditor ? QString(" [active]") : QString();
    setWindowTitle(QString("Layout Editor %1%2").arg(editorPart, activeSuffix));
}

void LayoutEditorWindow::onViewChanged(double zoom, double panX, double panY, double gridSize) {
    m_canvas->setView(zoom, panX, panY, gridSize);
}

void LayoutEditorWindow::onEditPreviewChanged(bool enabled, const SceneRenderPrimitive& primitive) {
    m_canvas->setEditPreview(enabled, primitive);
}

void LayoutEditorWindow::onPrimitiveCommitted(const SceneRenderPrimitive& primitive) {
    std::shared_ptr<LayoutObjectModel> object;
    if (!LayoutEditPreviewModel::tryBuildCommittedObject(m_activeTool, primitive, object) || !object) {
        return;
    }

    m_rootCell->addObject(object);
    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onObjectDeletionRequested(quint64 objectId) {
    if (objectId == 0) {
        return;
    }

    if (!m_rootCell->removeObjectById(objectId)) {
        return;
    }

    m_canvas->setRootCell(m_rootCell.get());
}

void LayoutEditorWindow::onCellChanged(int row, int column) {
    if (m_internalUpdate || row < 0 || row >= m_layers.size()) {
        return;
    }

    // Only visibility/selectability columns map to layer configure commands.
    if (column != 3 && column != 4) {
        return;
    }

    auto* item = m_layerTable->item(row, column);
    if (!item) {
        return;
    }

    const bool requestedValue = item->checkState() == Qt::Checked;
    const LayerDefinition& current = m_layers[row];
    const bool currentValue = column == 3 ? current.visible : current.selectable;

    // Non-checkstate updates (for example row highlight styling) can also trigger
    // cellChanged; ignore those when the value did not actually change.
    if (requestedValue == currentValue) {
        return;
    }

    // Revert immediate local visual change; Tcl command execution drives truth.
    m_internalUpdate = true;
    item->setCheckState(currentValue ? Qt::Checked : Qt::Unchecked);
    m_internalUpdate = false;

    const QString option = column == 3 ? "-visible" : "-selectable";
    emit commandRequested(QString("layer configure {%1} {%2} %3 %4")
                              .arg(current.name, current.type, option)
                              .arg(requestedValue ? 1 : 0),
                        true);
}

void LayoutEditorWindow::onCurrentRowChanged(int currentRow, int) {
    if (m_internalUpdate || currentRow < 0 || currentRow >= m_layers.size()) {
        return;
    }

    // Row selection sets active layer via Tcl command.
    emit commandRequested(QString("layer active {%1} {%2}")
                              .arg(m_layers[currentRow].name, m_layers[currentRow].type),
                        true);
}

#include "LayoutEditorWindow.moc"
