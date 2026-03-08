#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "LayerManager.h"
#include "LayoutEditorWindow.h"

struct EditorSession {
    int id{0};
    LayoutEditorWindow* window{nullptr};
    QVector<LayerDefinition> layers;
    QString activeLayerName;
    QString activeLayerType;
    QString activeTool{"none"};
    bool editInProgress{false};
    SceneRenderPrimitive editPreview{};
    qint64 editAnchorX{0};
    qint64 editAnchorY{0};
    double zoom{1.0};
    double panX{0.0};
    double panY{0.0};
    double gridSize{40.0};
};

class EditorSessionController {
public:
    EditorSession* sessionById(int editorId);
    const EditorSession* sessionById(int editorId) const;

    EditorSession* activeSession();
    const EditorSession* activeSession() const;
    EditorSession* effectiveSession();

    int createSession(LayoutEditorWindow* window);
    void removeSession(int editorId);
    void setActiveEditor(int editorId);

    int activeEditorId() const;
    int commandEditorId() const;
    void setCommandEditorId(int editorId);

    QHash<int, EditorSession>& sessions();
    const QHash<int, EditorSession>& sessions() const;

    void initializeSessionLayers(EditorSession& session, const QVector<LayerDefinition>& sharedLayers) const;
    void applySessionToWindow(EditorSession& session) const;

private:
    QHash<int, EditorSession> m_sessions;
    int m_nextEditorId{1};
    int m_activeEditorId{0};
    int m_commandEditorId{0};
};
