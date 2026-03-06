#pragma once

#include <QVector>
#include <functional>
#include <memory>

#include "LayoutGeometry.h"

// Base scene object. Additional object kinds (paths/instances/text) can
// implement this interface and be inserted into a scene node.
class LayoutObjectModel {
public:
    virtual ~LayoutObjectModel() = default;

    virtual bool containsPoint(qint64 x, qint64 y) const = 0;
    virtual const DrawnRectangle* asRectangle() const { return nullptr; }
};

class RectangleObjectModel final : public LayoutObjectModel {
public:
    explicit RectangleObjectModel(const DrawnRectangle& rectangle);

    bool containsPoint(qint64 x, qint64 y) const override;
    const DrawnRectangle* asRectangle() const override;

private:
    DrawnRectangle m_rectangle;
};

// Hierarchical container for objects and child scene nodes.
class LayoutSceneNode {
public:
    void addObject(std::shared_ptr<LayoutObjectModel> object);
    void addChild(std::shared_ptr<LayoutSceneNode> child);

    void collectRectangles(QVector<const DrawnRectangle*>& outRectangles) const;
    void collectObjects(QVector<const LayoutObjectModel*>& outObjects) const;
    QVector<int> matchingObjectIndicesAt(qint64 x,
                                         qint64 y,
                                         const std::function<bool(const LayoutObjectModel&)>& predicate) const;
    bool removeRectangleAt(int rectangleIndex);

private:
    bool removeRectangleAtRecursive(int& rectangleIndex);

    QVector<std::shared_ptr<LayoutObjectModel>> m_objects;
    QVector<std::shared_ptr<LayoutSceneNode>> m_children;
};
