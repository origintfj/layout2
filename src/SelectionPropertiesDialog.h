#pragma once

#include <QSet>
#include <QWidget>

#include "LayoutSceneModel.h"

namespace SelectionPropertiesDialog {

void show(QWidget* parent,
          const LayoutSceneNode* rootCell,
          const QSet<quint64>& selectedObjectIds);

}
