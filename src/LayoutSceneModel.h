#pragma once

#include <QVector>
#include <QString>
#include <functional>
#include <memory>

#include "LayoutGeometry.h"

// Base scene object. Additional object kinds (paths/instances/text) can
// implement this interface and be inserted into a scene node.
class LayoutObjectModel {
public:
    LayoutObjectModel();
    virtual ~LayoutObjectModel() = default;

    quint64 objectId() const;

    virtual bool containsPoint(qint64 x, qint64 y) const = 0;
    virtual const DrawnRectangle* asRectangle() const { return nullptr; }
    virtual void appendOutlineSegments(QVector<WorldLineSegment>& outSegments) const = 0;
    virtual void appendRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const = 0;

private:
    quint64 m_objectId{0};
};

class RectangleObjectModel final : public LayoutObjectModel {
public:
    explicit RectangleObjectModel(const DrawnRectangle& rectangle);

    bool containsPoint(qint64 x, qint64 y) const override;
    const DrawnRectangle* asRectangle() const override;
    void appendOutlineSegments(QVector<WorldLineSegment>& outSegments) const override;
    void appendRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const override;

private:
    DrawnRectangle m_rectangle;
};


namespace LayoutEditPreviewModel {
bool tryBuildPreviewPrimitive(const QString& activeTool,
                              quint32 layerNameId,
                              quint32 layerTypeId,
                              qint64 anchorX,
                              qint64 anchorY,
                              qint64 currentX,
                              qint64 currentY,
                              SceneRenderPrimitive& outPrimitive);
bool tryBuildCommittedRectangle(const QString& activeTool,
                                quint32 layerNameId,
                                quint32 layerTypeId,
                                qint64 anchorX,
                                qint64 anchorY,
                                qint64 currentX,
                                qint64 currentY,
                                DrawnRectangle& outRectangle);
}

// Hierarchical container for objects and child scene nodes.
class LayoutSceneNode {
public:
    void addObject(std::shared_ptr<LayoutObjectModel> object);
    void addChild(std::shared_ptr<LayoutSceneNode> child);

    void collectRectangles(QVector<const DrawnRectangle*>& outRectangles) const;
    void collectRenderPrimitives(QVector<SceneRenderPrimitive>& outPrimitives) const;
    void collectObjects(QVector<const LayoutObjectModel*>& outObjects) const;
    QVector<quint64> matchingObjectIdsAt(qint64 x,
                                         qint64 y,
                                         const std::function<bool(const LayoutObjectModel&)>& predicate) const;
    bool collectOutlineSegmentsByObjectId(quint64 objectId, QVector<WorldLineSegment>& outSegments) const;
    const LayoutObjectModel* findObjectById(quint64 objectId) const;
    bool removeObjectById(quint64 objectId);
private:
    bool collectOutlineSegmentsByObjectIdRecursive(quint64 objectId,
                                                   QVector<WorldLineSegment>& outSegments) const;
    const LayoutObjectModel* findObjectByIdRecursive(quint64 objectId) const;
    bool removeObjectByIdRecursive(quint64 objectId);

    QVector<std::shared_ptr<LayoutObjectModel>> m_objects;
    QVector<std::shared_ptr<LayoutSceneNode>> m_children;
};
