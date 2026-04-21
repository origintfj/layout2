#include "RasterPrimitiveRenderBackend.h"

#include <QPen>
#include <QtGlobal>
#include <algorithm>

void RasterPrimitiveRenderBackend::beginFrame(QPainter& painter,
                                              const QColor& clearColor,
                                              const QSize& viewportSize) {
    Q_UNUSED(viewportSize);
    painter.fillRect(painter.viewport(), clearColor);
}

void RasterPrimitiveRenderBackend::drawPrimitives(QPainter& painter,
                                                  const QVector<RenderTypes::RenderItem>& items,
                                                  const QSize& viewportSize) {
    Q_UNUSED(viewportSize);

    for (const RenderTypes::RenderItem& item : items) {
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

void RasterPrimitiveRenderBackend::endFrame(QPainter& painter, const QSize& viewportSize) {
    Q_UNUSED(painter);
    Q_UNUSED(viewportSize);
}
