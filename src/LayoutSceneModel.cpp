#include "LayoutSceneModel.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace {
std::atomic<quint64> g_nextObjectId{1};

bool isAxisAlignedSegment(const WorldPoint& a, const WorldPoint& b) {
    return a.x == b.x || a.y == b.y;
}

QVector<WorldPoint> polygonForPath(const DrawnPath& path) {
    QVector<WorldPoint> vertices;
    if (path.points.size() < 2 || path.width <= 0) {
        return vertices;
    }

    const qint64 half = path.width / 2;
    for (int i = 0; i < path.points.size() - 1; ++i) {
        const WorldPoint a = path.points[i];
        const WorldPoint b = path.points[i + 1];
        if (!isAxisAlignedSegment(a, b)) {
            continue;
        }

        if (a.x == b.x) {
            const qint64 minY = std::min(a.y, b.y);
            const qint64 maxY = std::max(a.y, b.y);
            const qint64 minX = a.x - half;
            const qint64 maxX = a.x + half;
            vertices.push_back(WorldPoint{minX, minY});
            vertices.push_back(WorldPoint{maxX, minY});
            vertices.push_back(WorldPoint{maxX, maxY});
            vertices.push_back(WorldPoint{minX, maxY});
        } else {
            const qint64 minX = std::min(a.x, b.x);
            const qint64 maxX = std::max(a.x, b.x);
            const qint64 minY = a.y - half;
            const qint64 maxY = a.y + half;
            vertices.push_back(WorldPoint{minX, minY});
            vertices.push_back(WorldPoint{maxX, minY});
            vertices.push_back(WorldPoint{maxX, maxY});
            vertices.push_back(WorldPoint{minX, maxY});
        }
    }

    return vertices;
}
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

PathObjectModel::PathObjectModel(const DrawnPath& path)
    : m_path(path) {}

bool PathObjectModel::containsPoint(qint64 x, qint64 y) const {
    if (m_path.points.size() < 2 || m_path.width <= 0) {
        return false;
    }

    const qint64 half = m_path.width / 2;
    for (int i = 0; i < m_path.points.size() - 1; ++i) {
        const WorldPoint a = m_path.points[i];
        const WorldPoint b = m_path.points[i + 1];
        if (!isAxisAlignedSegment(a, b)) {
            continue;
        }

        if (a.x == b.x) {
            const qint64 minX = a.x - half;
            const qint64 maxX = a.x + half;
            const qint64 minY = std::min(a.y, b.y);
            const qint64 maxY = std::max(a.y, b.y);
            if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                return true;
            }
        } else {
            const qint64 minX = std::min(a.x, b.x);
            const qint64 maxX = std::max(a.x, b.x);
            const qint64 minY = a.y - half;
            const qint64 maxY = a.y + half;
            if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                return true;
            }
        }
    }

    return false;
}

const DrawnPath* PathObjectModel::asPath() const {
    return &m_path;
}

void PathObjectModel::appendOutlineSegments(QVector<WorldLineSegment>& outSegments) const {
    if (m_path.points.size() < 2 || m_path.width <= 0) {
        return;
    }

    const qint64 half = m_path.width / 2;
    for (int i = 0; i < m_path.points.size() - 1; ++i) {
        const WorldPoint a = m_path.points[i];
        const WorldPoint b = m_path.points[i + 1];
        if (!isAxisAlignedSegment(a, b)) {
            continue;
        }

        if (a.x == b.x) {
            const qint64 minY = std::min(a.y, b.y);
            const qint64 maxY = std::max(a.y, b.y);
            const qint64 minX = a.x - half;
            const qint64 maxX = a.x + half;
            outSegments.push_back(WorldLineSegment{minX, minY, maxX, minY});
            outSegments.push_back(WorldLineSegment{maxX, minY, maxX, maxY});
            outSegments.push_back(WorldLineSegment{maxX, maxY, minX, maxY});
            outSegments.push_back(WorldLineSegment{minX, maxY, minX, minY});
        } else {
            const qint64 minX = std::min(a.x, b.x);
            const qint64 maxX = std::max(a.x, b.x);
            const qint64 minY = a.y - half;
            const qint64 maxY = a.y + half;
            outSegments.push_back(WorldLineSegment{minX, minY, maxX, minY});
            outSegments.push_back(WorldLineSegment{maxX, minY, maxX, maxY});
            outSegments.push_back(WorldLineSegment{maxX, maxY, minX, maxY});
            outSegments.push_back(WorldLineSegment{minX, maxY, minX, minY});
        }
    }
}

void PathObjectModel::appendRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const {
    SceneRenderPrimitive primitive;
    primitive.objectId = objectId();
    primitive.layerNameId = m_path.layerNameId;
    primitive.layerTypeId = m_path.layerTypeId;
    primitive.preview = false;
    primitive.pathWidth = m_path.width;
    primitive.pathPoints = m_path.points;
    primitive.polygonVertices = polygonForPath(m_path);
    outPrimitives.push_back(std::move(primitive));
}

void LayoutSceneNode::addObject(std::shared_ptr<LayoutObjectModel> object) {
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
    QVector<const LayoutObjectModel*> objects;
    collectObjects(objects);

    for (const LayoutObjectModel* object : objects) {
        if (!object) {
            continue;
        }
        object->appendRenderPrimitives(outPrimitives);
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
    QVector<const LayoutObjectModel*> objects;
    collectObjects(objects);

    for (int i = objects.size() - 1; i >= 0; --i) {
        const LayoutObjectModel* object = objects[i];
        if (!object || !predicate(*object)) {
            continue;
        }

        if (object->containsPoint(x, y)) {
            matches.push_back(object->objectId());
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
    return findObjectByIdRecursive(objectId);
}

const LayoutObjectModel* LayoutSceneNode::findObjectByIdRecursive(quint64 objectId) const {
    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        if (object && object->objectId() == objectId) {
            return object.get();
        }
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (const LayoutObjectModel* found = child->findObjectByIdRecursive(objectId)) {
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

bool LayoutEditPreviewModel::tryBuildPreviewPrimitive(const QString& activeTool,
                                                      const quint32 layerNameId,
                                                      const quint32 layerTypeId,
                                                      const qint64 anchorX,
                                                      const qint64 anchorY,
                                                      const qint64 currentX,
                                                      const qint64 currentY,
                                                      SceneRenderPrimitive& outPrimitive) {
    if (activeTool == "rect") {
        outPrimitive.objectId = 0;
        outPrimitive.layerNameId = layerNameId;
        outPrimitive.layerTypeId = layerTypeId;
        outPrimitive.preview = true;
        outPrimitive.pathWidth = 0;
        outPrimitive.pathPoints.clear();
        outPrimitive.polygonVertices = {
            WorldPoint{anchorX, anchorY},
            WorldPoint{currentX, anchorY},
            WorldPoint{currentX, currentY},
            WorldPoint{anchorX, currentY}
        };
        return true;
    }

    if (activeTool == "path") {
        outPrimitive.objectId = 0;
        outPrimitive.layerNameId = layerNameId;
        outPrimitive.layerTypeId = layerTypeId;
        outPrimitive.preview = true;
        outPrimitive.pathWidth = 40;
        outPrimitive.pathPoints = {
            WorldPoint{anchorX, anchorY},
            WorldPoint{currentX, anchorY},
            WorldPoint{currentX, currentY}
        };
        const DrawnPath path{layerNameId, layerTypeId, outPrimitive.pathWidth, outPrimitive.pathPoints};
        outPrimitive.polygonVertices = polygonForPath(path);
        return true;
    }

    return false;
}

bool LayoutEditPreviewModel::tryBuildCommittedPrimitive(const QString& activeTool,
                                                        const quint32 layerNameId,
                                                        const quint32 layerTypeId,
                                                        const qint64 anchorX,
                                                        const qint64 anchorY,
                                                        const qint64 currentX,
                                                        const qint64 currentY,
                                                        SceneRenderPrimitive& outPrimitive) {
    if (activeTool == "rect") {
        outPrimitive.objectId = 0;
        outPrimitive.layerNameId = layerNameId;
        outPrimitive.layerTypeId = layerTypeId;
        outPrimitive.preview = false;
        outPrimitive.pathWidth = 0;
        outPrimitive.pathPoints.clear();
        outPrimitive.polygonVertices = {
            WorldPoint{anchorX, anchorY},
            WorldPoint{currentX, anchorY},
            WorldPoint{currentX, currentY},
            WorldPoint{anchorX, currentY}
        };
        return true;
    }

    if (activeTool == "path") {
        outPrimitive.objectId = 0;
        outPrimitive.layerNameId = layerNameId;
        outPrimitive.layerTypeId = layerTypeId;
        outPrimitive.preview = false;
        outPrimitive.pathWidth = 40;
        outPrimitive.pathPoints = {
            WorldPoint{anchorX, anchorY},
            WorldPoint{currentX, anchorY},
            WorldPoint{currentX, currentY}
        };
        const DrawnPath path{layerNameId, layerTypeId, outPrimitive.pathWidth, outPrimitive.pathPoints};
        outPrimitive.polygonVertices = polygonForPath(path);
        return true;
    }

    return false;
}

bool LayoutEditPreviewModel::tryBuildCommittedObject(const QString& activeTool,
                                                     const SceneRenderPrimitive& primitive,
                                                     std::shared_ptr<LayoutObjectModel>& outObject) {
    if (activeTool == "rect") {
        if (primitive.polygonVertices.size() != 4) {
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

    if (activeTool == "path") {
        if (primitive.pathPoints.size() < 2 || primitive.pathWidth <= 0) {
            return false;
        }

        DrawnPath path{primitive.layerNameId, primitive.layerTypeId, primitive.pathWidth, primitive.pathPoints};
        outObject = std::make_shared<PathObjectModel>(path);
        return true;
    }

    return false;
}
