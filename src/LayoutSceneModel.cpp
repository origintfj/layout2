#include "LayoutSceneModel.h"

#include <algorithm>

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

void LayoutSceneNode::addObject(std::shared_ptr<LayoutObjectModel> object) {
    m_objects.push_back(std::move(object));
}

void LayoutSceneNode::addChild(std::shared_ptr<LayoutSceneNode> child) {
    m_children.push_back(std::move(child));
}

void LayoutSceneNode::collectRectangles(QVector<const DrawnRectangle*>& outRectangles) const {
    for (const std::shared_ptr<LayoutObjectModel>& object : m_objects) {
        if (const DrawnRectangle* rectangle = object->asRectangle()) {
            outRectangles.push_back(rectangle);
        }
    }

    for (const std::shared_ptr<LayoutSceneNode>& child : m_children) {
        child->collectRectangles(outRectangles);
    }
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
