#include "EditorSessionController.h"

#include <algorithm>

EditorSession* EditorSessionController::sessionById(const int editorId) {
    auto it = m_sessions.find(editorId);
    return it == m_sessions.end() ? nullptr : &it.value();
}

const EditorSession* EditorSessionController::sessionById(const int editorId) const {
    auto it = m_sessions.constFind(editorId);
    return it == m_sessions.constEnd() ? nullptr : &it.value();
}

EditorSession* EditorSessionController::activeSession() {
    return sessionById(m_activeEditorId);
}

const EditorSession* EditorSessionController::activeSession() const {
    return sessionById(m_activeEditorId);
}

EditorSession* EditorSessionController::effectiveSession() {
    if (m_commandEditorId > 0) {
        if (EditorSession* session = sessionById(m_commandEditorId)) {
            return session;
        }
    }
    return activeSession();
}

int EditorSessionController::createSession(LayoutEditorWindow* window) {
    EditorSession session;
    session.id = m_nextEditorId++;
    session.window = window;

    const int editorId = session.id;
    m_sessions.insert(editorId, session);
    return editorId;
}

void EditorSessionController::removeSession(const int editorId) {
    const bool removedActive = editorId == m_activeEditorId;
    m_sessions.remove(editorId);
    if (removedActive) {
        m_activeEditorId = m_sessions.isEmpty() ? 0 : m_sessions.constBegin().key();
    }
}

void EditorSessionController::setActiveEditor(const int editorId) {
    if (sessionById(editorId)) {
        m_activeEditorId = editorId;
    }
}

int EditorSessionController::activeEditorId() const {
    return m_activeEditorId;
}

int EditorSessionController::commandEditorId() const {
    return m_commandEditorId;
}

void EditorSessionController::setCommandEditorId(const int editorId) {
    m_commandEditorId = editorId;
}

QHash<int, EditorSession>& EditorSessionController::sessions() {
    return m_sessions;
}

const QHash<int, EditorSession>& EditorSessionController::sessions() const {
    return m_sessions;
}

void EditorSessionController::initializeSessionLayers(EditorSession& session,
                                                      const QVector<LayerDefinition>& sharedLayers) const {
    session.layers = sharedLayers;
    if (session.layers.isEmpty()) {
        session.activeLayerName.clear();
        session.activeLayerType.clear();
        return;
    }

    const bool hasExisting = std::any_of(session.layers.cbegin(), session.layers.cend(),
                                         [&session](const LayerDefinition& layer) {
                                             return layer.name.compare(session.activeLayerName, Qt::CaseInsensitive) == 0
                                                    && layer.type.compare(session.activeLayerType, Qt::CaseInsensitive) == 0;
                                         });
    if (!hasExisting) {
        session.activeLayerName = session.layers[0].name;
        session.activeLayerType = session.layers[0].type;
    }
}

void EditorSessionController::applySessionToWindow(EditorSession& session) const {
    if (!session.window) {
        return;
    }

    session.window->setLayers(session.layers);
    session.window->onActiveLayerChanged(session.activeLayerName, session.activeLayerType);
    session.window->onToolChanged(session.activeTool);
    session.window->onViewChanged(session.zoom, session.panX, session.panY, session.gridSize);
    session.window->onEditPreviewChanged(session.editInProgress, session.editPreview);
}
