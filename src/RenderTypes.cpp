#include "RenderTypes.h"

#include <QPainter>
#include <QPixmap>
#include <QtGlobal>

namespace RenderTypes {

BackendType backendTypeFromEnv() {
    const QByteArray backendName = qgetenv("LAYOUT2_RENDER_BACKEND").trimmed().toLower();
    if (backendName == "raster") {
        return BackendType::Raster;
    }

    // Default to OpenGL unless explicitly overridden so the fast path remains
    // the standard behavior while preserving an escape hatch for debugging.
    return BackendType::OpenGL;
}

QBrush patternBrushFor(QColor baseColor, const QString& pattern) {
    bool ok = false;
    const quint64 patternValue = static_cast<quint64>(pattern.toULongLong(&ok, 0));
    if (!ok) {
        return QBrush(baseColor, Qt::SolidPattern);
    }

    // Keep stipple texel-to-pixel mapping at 1:1 so each pattern bit maps to
    // one screen pixel in the raster fallback path.
    constexpr int patternMag = 1;

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

std::array<float, 8> patternRowsFor(const QString& pattern) {
    std::array<float, 8> rows{};
    bool ok = false;
    const quint64 patternValue = static_cast<quint64>(pattern.toULongLong(&ok, 0));
    if (!ok) {
        rows.fill(255.0f);
        return rows;
    }

    for (int y = 0; y < 8; ++y) {
        const quint64 rowBits = (patternValue >> (y * 8)) & 0xFFULL;
        rows[static_cast<size_t>(y)] = static_cast<float>(rowBits);
    }

    return rows;
}

quint64 layerCodeKey(quint32 nameId, quint32 typeId) {
    return (static_cast<quint64>(nameId) << 32) | static_cast<quint64>(typeId);
}

} // namespace RenderTypes
