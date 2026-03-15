#pragma once

#include <QSet>
#include <QWidget>
#include <functional>

#include "LayoutSceneModel.h"

namespace SelectionPropertiesDialog {

using DialogSelectionChangedCallback = std::function<void(const QSet<quint64>&)>;

void show(QWidget* parent,
          const LayoutSceneNode* rootCell,
          const QSet<quint64>& selectedObjectIds,
          DialogSelectionChangedCallback onSelectionChanged);

void refreshIfOpen(QWidget* parent,
                   const LayoutSceneNode* rootCell,
                   const QSet<quint64>& selectedObjectIds,
                   DialogSelectionChangedCallback onSelectionChanged);

}
