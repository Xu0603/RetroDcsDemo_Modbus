#pragma once

#include <QIcon>

class QApplication;
class QColor;
class QLabel;
class QString;

void applyRetroStyle(QApplication &app);
QIcon pixelIcon(const QColor &color, int kind);
QLabel *sunkenLabel(const QString &text, int minWidth = 100);
