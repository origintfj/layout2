#include "LayoutSceneModel.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace {
std::atomic<quint64> g_nextObjectId{1};
}

LayoutObjectModel::LayoutObjectModel()
    : m_objectId(g_nextObjectId.fetch_add(1, std::memory_order_relaxed)) {}

quint64 LayoutObjectModel::objectId() const {
    return m_objectId;
}

RectangleObjectModel::RectangleObjectModel(const DrawnRectangle& rectangle)
    : m_rectangle(rectangle) {}

bool RectangleObjectModel::containsPoint(qint64 x, qint64 y) const {
    const qint64 minX = std::min(m_rectangle.x1, m_rectangle.x2);
    const qint64 maxX = std::max(m_rectangle.x1, m_rectangle.x2);
    const qint64 minY = std::min(m_rectangle.y1, m_rectangle.y2);
    const qint64 maxY = std::max(m_rectangle.y1, m_rectangle.y2);
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

const DrawnRectangle* RectangleObjectModel::asRectangle() const {
    return &m_rectangle;
}

void RectangleObjectModel::appendOutlineSegments(QVector<WorldLineSegment>& outSegments) const {
    const qint64 minX = std::min(m_rectangle.x1, m_rectangle.x2);
    const qint64 maxX = std::max(m_rectangle.x1, m_rectangle.x2);
    const qint64 minY = std::min(m_rectangle.y1, m_rectangle.y2);
    const qint64 maxY = std::max(m_rectangle.y1, m_rectangle.y2);

    outSegments.push_back(WorldLineSegment{minX, minY, maxX, minY});
    outSegments.push_back(WorldLineSegment{maxX, minY, maxX, maxY});
    outSegments.push_back(WorldLineSegment{maxX, maxY, minX, maxY});
    outSegments.push_back(WorldLineSegment{minX, maxY, minX, minY});
}

void RectangleObjectModel::appendRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const {
    const qint64 minX = std::min(m_rectangle.x1, m_rectangle.x2);
    const qint64 maxX = std::max(m_rectangle.x1, m_rectangle.x2);
    const qint64 minY = std::min(m_rectangle.y1, m_rectangle.y2);
    const qint64 maxY = std::max(m_rectangle.y1, m_rectangle.y2);

    SceneRenderPrimitive primitive;
    primitive.objectId = objectId();
    primitive.layerNameId = m_rectangle.layerNameId;
    primitive.layerTypeId = m_rectangle.layerTypeId;
    primitive.preview = false;
    primitive.polygonVertices = {
        WorldPoint{minX, minY},
        WorldPoint{maxX, minY},
        WorldPoint{maxX, maxY},
        WorldPoint{minX, maxY}
    };
    outPrimitives.push_back(std::move(primitive));
}

void LayoutSceneNode::addObject(std::shared_ptr<LayoutObjectModel> object) {
    if (!object) {
        return;
    }

    m_objectById.insert(object->objectId(), object);
    indexObject(object);
    m_objects.push_back(std::move(object));
}

void LayoutSceneNode::addChild(std::shared_ptr<LayoutSceneNode> child) {
    m_children.push_back(std::move(child));
}

void LayoutSceneNode::collectRectangles(QVector<const DrawnRectangle*>& outRectangles) const {
    QVector<const LayoutObjectModel*> objects;
    collectObjects(objects);

    for (const LayoutObjectModel* object : objects) {
        if (const DrawnRectangle* rectangle = object->asRectangle()) {
            outRectangles.push_back(rectangle);
        }
    }
}

void LayoutSceneNode::collectRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const {
    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        if (object) {
            object->appendRenderPrimitives(outPrimitives);
        }
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        child->collectRenderPrimitives(outPrimitives);
    }
}

void LayoutSceneNode::collectRenderPrimitivesInRect(const qint64 minX,
                                                    const qint64 minY,
                                                    const qint64 maxX,
                                                    const qint64 maxY,
                                                    QVector<SceneRenderPrimitive>& outPrimitives) const {
    QSet<quint64> candidateObjectIds;
    collectCandidateObjectIdsInRect(minX, minY, maxX, maxY, candidateObjectIds);

    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        if (!object || !candidateObjectIds.contains(object->objectId())) {
            continue;
        }

        const auto boundsIt = m_objectBoundsById.constFind(object->objectId());
        if (boundsIt != m_objectBoundsById.cend()
            && !boundsIntersectRect(boundsIt.value(), minX, minY, maxX, maxY)) {
            continue;
        }

        object->appendRenderPrimitives(outPrimitives);
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        child->collectRenderPrimitivesInRect(minX, minY, maxX, maxY, outPrimitives);
    }
}

void LayoutSceneNode::collectObjects(QVector<const LayoutObjectModel*>& outObjects) const {
    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        outObjects.push_back(object.get());
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        child->collectObjects(outObjects);
    }
}

QVector<quint64> LayoutSceneNode::matchingObjectIdsAt(
    qint64 x,
    qint64 y,
    const std::function<bool(const LayoutObjectModel&)>& predicate) const {
    QVector<quint64> matches;
    QSet<quint64> candidateObjectIds;
    collectCandidateObjectIdsInRect(x, y, x, y, candidateObjectIds);

    for (int i = m_objects.size() - 1; i >= 0; --i) {
        const std::shared_ptr<LayoutObjectModel>& object = m_objects[i];
        if (!object || !candidateObjectIds.contains(object->objectId())) {
            continue;
        }

        const auto boundsIt = m_objectBoundsById.constFind(object->objectId());
        if (boundsIt != m_objectBoundsById.cend() && !boundsContainPoint(boundsIt.value(), x, y)) {
            continue;
        }

        if (!predicate(*object)) {
            continue;
        }

        if (object->containsPoint(x, y)) {
            matches.push_back(object->objectId());
        }
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        const QVector<quint64> childMatches = child->matchingObjectIdsAt(x, y, predicate);
        for (quint64 objectId : childMatches) {
            matches.push_back(objectId);
        }
    }

    return matches;
}

bool LayoutSceneNode::collectOutlineSegmentsByObjectId(
    quint64 objectId,
    QVector<WorldLineSegment>& outSegments) const {
    return collectOutlineSegmentsByObjectIdRecursive(objectId, outSegments);
}

bool LayoutSceneNode::collectOutlineSegmentsByObjectIdRecursive(
    quint64 objectId,
    QVector<WorldLineSegment>& outSegments) const {
    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        if (!object || object->objectId() != objectId) {
            continue;
        }

        object->appendOutlineSegments(outSegments);
        return true;
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (child->collectOutlineSegmentsByObjectIdRecursive(objectId, outSegments)) {
            return true;
        }
    }

    return false;
}

const LayoutObjectModel* LayoutSceneNode::findObjectById(quint64 objectId) const {
    const auto it = m_objectById.constFind(objectId);
    if (it != m_objectById.cend() && it.value()) {
        return it.value().get();
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (const LayoutObjectModel* found = child->findObjectById(objectId)) {
            return found;
        }
    }

    return nullptr;
}

bool LayoutSceneNode::removeObjectById(quint64 objectId) {
    return removeObjectByIdRecursive(objectId);
}

bool LayoutSceneNode::removeObjectByIdRecursive(quint64 objectId) {
    for (int i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i] && m_objects[i]->objectId() == objectId) {
            deindexObject(objectId);
            m_objectById.remove(objectId);
            m_objects.removeAt(i);
            return true;
        }
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (child->removeObjectByIdRecursive(objectId)) {
            return true;
        }
    }

    return false;
}

bool LayoutSceneNode::tryRectangleBounds(const LayoutObjectModel& object, ObjectBounds& outBounds) {
    const DrawnRectangle* rectangle = object.asRectangle();
    if (!rectangle) {
        return false;
    }

    outBounds.minX = std::min(rectangle->x1, rectangle->x2);
    outBounds.minY = std::min(rectangle->y1, rectangle->y2);
    outBounds.maxX = std::max(rectangle->x1, rectangle->x2);
    outBounds.maxY = std::max(rectangle->y1, rectangle->y2);
    return true;
}

bool LayoutSceneNode::boundsContainPoint(const ObjectBounds& bounds, const qint64 x, const qint64 y) {
    return x >= bounds.minX && x <= bounds.maxX && y >= bounds.minY && y <= bounds.maxY;
}

bool LayoutSceneNode::boundsIntersectRect(const ObjectBounds& bounds,
                                          const qint64 minX,
                                          const qint64 minY,
                                          const qint64 maxX,
                                          const qint64 maxY) {
    return !(bounds.maxX < minX || bounds.minX > maxX || bounds.maxY < minY || bounds.minY > maxY);
}

qint64 LayoutSceneNode::tileCoordFor(const qint64 coordinate) {
    if (coordinate >= 0) {
        return coordinate / kSpatialTileSize;
    }
    return -(((-coordinate) + kSpatialTileSize - 1) / kSpatialTileSize);
}

quint64 LayoutSceneNode::tileKey(const qint64 tileX, const qint64 tileY) {
    return (static_cast<quint64>(static_cast<quint32>(tileX)) << 32)
           | static_cast<quint64>(static_cast<quint32>(tileY));
}

void LayoutSceneNode::indexObject(const std::shared_ptr<LayoutObjectModel>& object) {
    if (!object) {
        return;
    }

    ObjectBounds bounds;
    if (!tryRectangleBounds(*object, bounds)) {
        return;
    }

    const quint64 objectId = object->objectId();
    m_objectBoundsById.insert(objectId, bounds);

    const qint64 minTileX = tileCoordFor(bounds.minX);
    const qint64 maxTileX = tileCoordFor(bounds.maxX);
    const qint64 minTileY = tileCoordFor(bounds.minY);
    const qint64 maxTileY = tileCoordFor(bounds.maxY);

    QVector<quint64> tileKeys;
    tileKeys.reserve(static_cast<int>((maxTileX - minTileX + 1) * (maxTileY - minTileY + 1)));
    for (qint64 tileX = minTileX; tileX <= maxTileX; ++tileX) {
        for (qint64 tileY = minTileY; tileY <= maxTileY; ++tileY) {
            const quint64 key = tileKey(tileX, tileY);
            m_tileObjectIds[key].push_back(objectId);
            tileKeys.push_back(key);
        }
    }

    m_objectTileKeys.insert(objectId, std::move(tileKeys));
}

void LayoutSceneNode::deindexObject(const quint64 objectId) {
    const auto tileKeysIt = m_objectTileKeys.constFind(objectId);
    if (tileKeysIt != m_objectTileKeys.cend()) {
        for (quint64 key : tileKeysIt.value()) {
            auto idsIt = m_tileObjectIds.find(key);
            if (idsIt == m_tileObjectIds.end()) {
                continue;
            }

            QVector<quint64>& ids = idsIt.value();
            ids.removeAll(objectId);
            if (ids.isEmpty()) {
                m_tileObjectIds.erase(idsIt);
            }
        }

        m_objectTileKeys.erase(tileKeysIt);
    }

    m_objectBoundsById.remove(objectId);
}

void LayoutSceneNode::collectCandidateObjectIdsInRect(const qint64 minX,
                                                      const qint64 minY,
                                                      const qint64 maxX,
                                                      const qint64 maxY,
                                                      QSet<quint64>& outCandidateIds) const {
    const qint64 minTileX = tileCoordFor(minX);
    const qint64 maxTileX = tileCoordFor(maxX);
    const qint64 minTileY = tileCoordFor(minY);
    const qint64 maxTileY = tileCoordFor(maxY);

    for (qint64 tileX = minTileX; tileX <= maxTileX; ++tileX) {
        for (qint64 tileY = minTileY; tileY <= maxTileY; ++tileY) {
            const auto idsIt = m_tileObjectIds.constFind(tileKey(tileX, tileY));
            if (idsIt == m_tileObjectIds.cend()) {
                continue;
            }

            for (quint64 objectId : idsIt.value()) {
                outCandidateIds.insert(objectId);
            }
        }
    }
}

bool LayoutEditPreviewModel::tryBuildPreviewPrimitive(const QString& activeTool,
                                                  const quint32 layerNameId,
                                                  const quint32 layerTypeId,
                                                  const qint64 anchorX,
                                                  const qint64 anchorY,
                                                  const qint64 currentX,
                                                  const qint64 currentY,
                                                  SceneRenderPrimitive& outPrimitive) {
    if (activeTool != "rect") {
        return false;
    }

    outPrimitive.objectId = 0;
    outPrimitive.layerNameId = layerNameId;
    outPrimitive.layerTypeId = layerTypeId;
    outPrimitive.preview = true;
    outPrimitive.polygonVertices = {
        WorldPoint{anchorX, anchorY},
        WorldPoint{currentX, anchorY},
        WorldPoint{currentX, currentY},
        WorldPoint{anchorX, currentY}
    };
    return true;
}

bool LayoutEditPreviewModel::tryBuildCommittedPrimitive(const QString& activeTool,
                                                        const quint32 layerNameId,
                                                        const quint32 layerTypeId,
                                                        const qint64 anchorX,
                                                        const qint64 anchorY,
                                                        const qint64 currentX,
                                                        const qint64 currentY,
                                                        SceneRenderPrimitive& outPrimitive) {
    if (activeTool != "rect") {
        return false;
    }

    outPrimitive.objectId = 0;
    outPrimitive.layerNameId = layerNameId;
    outPrimitive.layerTypeId = layerTypeId;
    outPrimitive.preview = false;
    outPrimitive.polygonVertices = {
        WorldPoint{anchorX, anchorY},
        WorldPoint{currentX, anchorY},
        WorldPoint{currentX, currentY},
        WorldPoint{anchorX, currentY}
    };
    return true;
}

bool LayoutEditPreviewModel::tryBuildCommittedObject(const QString& activeTool,
                                                     const SceneRenderPrimitive& primitive,
                                                     std::shared_ptr<LayoutObjectModel>& outObject) {
    if (activeTool != "rect" || primitive.polygonVertices.size() != 4) {
        return false;
    }

    const WorldPoint& anchor = primitive.polygonVertices[0];
    const WorldPoint& current = primitive.polygonVertices[2];
    const DrawnRectangle rectangle{primitive.layerNameId,
                                   primitive.layerTypeId,
                                   anchor.x,
                                   anchor.y,
                                   current.x,
                                   current.y};
    outObject = std::make_shared<RectangleObjectModel>(rectangle);
    return true;
}
