#include "LayoutSceneModel.h"

#include <algorithm>
#include <utility>

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

QVector<int> LayoutSceneNode::matchingObjectIndicesAt(
    qint64 x,
    qint64 y,
    const std::function<bool(const LayoutObjectModel&)>& predicate) const {
    QVector<int> matches;
    QVector<const LayoutObjectModel*> objects;
    collectObjects(objects);

    for (int i = objects.size() - 1; i >= 0; --i) {
        const LayoutObjectModel* object = objects[i];
        if (!object || !predicate(*object)) {
            continue;
        }

        if (object->containsPoint(x, y)) {
            matches.push_back(i);
        }
    }

    return matches;
}

bool LayoutSceneNode::collectRectangleOutlineSegments(int rectangleIndex,
                                                      QVector<WorldLineSegment>& outSegments) const {
    int mutableIndex = rectangleIndex;
    return collectRectangleOutlineSegmentsRecursive(mutableIndex, outSegments);
}

bool LayoutSceneNode::collectRectangleOutlineSegmentsRecursive(
    int& rectangleIndex,
    QVector<WorldLineSegment>& outSegments) const {
    for (int i = 0; i < m_objects.size(); ++i) {
        if (!m_objects[i]->asRectangle()) {
            continue;
        }

        if (rectangleIndex == 0) {
            m_objects[i]->appendOutlineSegments(outSegments);
            return true;
        }
        --rectangleIndex;
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        if (child->collectRectangleOutlineSegmentsRecursive(rectangleIndex, outSegments)) {
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
