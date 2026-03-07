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
    primitive.polygonVertices = {
        WorldPoint{minX, minY},
        WorldPoint{maxX, minY},
        WorldPoint{maxX, maxY},
        WorldPoint{minX, maxY}
    };
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

bool LayoutSceneNode::removeRectangleAt(int rectangleIndex) {
    int mutableIndex = rectangleIndex;
    return removeRectangleAtRecursive(mutableIndex);
}

bool LayoutSceneNode::removeRectangleAtRecursive(int& rectangleIndex) {
    for (int i = 0; i < m_objects.size(); ++i) {
        if (!m_objects[i]->asRectangle()) {
            continue;
        }

        if (rectangleIndex == 0) {
            m_objects.removeAt(i);
            return true;
        }
        --rectangleIndex;
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (child->removeRectangleAtRecursive(rectangleIndex)) {
            return true;
        }
    }

    return false;
}
